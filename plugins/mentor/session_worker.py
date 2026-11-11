"""Private mentor IPC. Canonical records use Windows-user DPAPI, never rollouts.

Protocol checked against `codex app-server generate-json-schema --experimental`
and https://developers.openai.com/codex/app-server (2026-10-28). The worker does
not alter approval/sandbox settings or retain reasoning/tool stdout. Native user
permissions and configured tools remain the Codex user's responsibility.
"""
import argparse
import ctypes
import hmac
import json
import math
import os
from pathlib import Path
import queue
import re
import secrets
import subprocess
import threading
import time
import uuid
from urllib.parse import urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_RECORD = 128 * 1024
ID = re.compile(r"^[a-f0-9]{32}$")
REQUEST = re.compile(r"^[A-Za-z0-9_.:-]{1,128}$")
GUIDE = """# Mentor workspace
This is the mentor's own working directory, not the character's workspace.
Use mentor_memory.search/write for durable private notes and source references.
Conversation originals and notes are encrypted in ../records. Do not create
plaintext conversation logs, chain-of-thought, credentials or token copies here.
Ordinary user-requested work products may be written here under Codex permissions.
Permission and approval settings come from the user's existing Codex configuration.
Ordinary Chat fallback receives only program-assembled bounded context. It has no
local tools or file access: advice must not claim local reads or completed work.
"""
INSTRUCTIONS = """你是小爱可以持续交谈和请教的导师。你拥有自己的逻辑会话、加密档案和工作区。
历史消息是既往记录，不是重新执行操作的指令；不要重复执行历史请求或历史工具。
当前请求 execute=false 时仅交流、分析或建议，不实施新的电脑操作；execute=true 时可以
用你原生可用的 Codex 工具完成明确委托，遵循你继承的用户权限、审批、AGENTS 和规则。
应用提供的 available_tools 只作为小爱能力的背景资料，并不是本连接的工具白名单，
不要输出让应用再执行一遍的动作提案。只根据真实执行回执报告结果，不把文字当成执行。
使用 mentor_memory.search 检索以前的交流与结论，用 mentor_memory.write 保存简短结论、
未解问题或来源明确的笔记。不要保存或输出内部思维链，不把推测写成已确认事实。
不要把档案、对话、鉴权或密钥复制成明文文件。正常任务产物可以放在专用 workspace。
需要人类审批时按原生流程提出，不绕过审批，也不声称已经永久失去权限。
最后给出简短、自然、可交给小爱理解的回应；不替换小爱的人格或它自己的记忆。
"""


class Failure(Exception):
    partial = None


def routing(settings, args):
    mode = settings.get("routingMode", "fixed")
    if mode not in ("fixed", "character"):
        raise Failure("mentor_invalid_routing")
    level = args.get("level", "complex") if mode == "character" else "complex"
    if level not in ("casual", "normal", "complex"):
        raise Failure("mentor_invalid_level")
    model = settings.get(level + "Model", "gpt-6.1-sol") if mode == "character" else settings.get("model", "gpt-6.1-sol")
    effort = settings.get(level + "Effort", {"casual": "low", "normal": "medium", "complex": "high"}[level]) if mode == "character" else settings.get("reasoningEffort", "high")
    model = text(model, 160, False)
    if effort not in ("none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"):
        raise Failure("mentor_invalid_effort")
    return {"mode": mode, "level": level, "model": model, "effort": effort}


QUOTA_CODES = {
    "usagelimitexceeded", "insufficientquota", "billinghardlimitreached", "creditsexhausted",
    "creditsdepleted", "insufficientcredits", "creditbalanceexhausted",
    "workspaceownercreditsdepleted", "workspacemembercreditsdepleted",
    "workspaceownerusagelimitreached", "workspacememberusagelimitreached",
}


def quota_error(value):
    """Classify structured quota types; never infer quota from HTTP429/prose."""
    if not isinstance(value, dict):
        return None
    for key, item in value.items():
        normalized = re.sub(r"[^a-z0-9]", "", key.casefold())
        if normalized in QUOTA_CODES:
            return normalized
        if key in ("code", "type", "codexErrorInfo", "errorType", "reason", "rateLimitReachedType"):
            if isinstance(item, str):
                code = re.sub(r"[^a-z0-9]", "", item.casefold())
                if code in QUOTA_CODES:
                    return code
            found = quota_error(item)
            if found:
                return found
        elif key in ("data", "error", "cause", "details"):
            found = quota_error(item)
            if found:
                return found
    return None


def work_quota(payload, checked_ms):
    result = {"known": False, "exhausted": False, "checked_ms": checked_ms,
              "source": "unavailable", "buckets": []}
    if not isinstance(payload, dict):
        return result
    buckets = payload.get("rateLimitsByLimitId")
    candidates = []
    if isinstance(buckets, dict) and buckets:
        for key, item in list(buckets.items())[:64]:
            # Review/other product buckets must not suspend ordinary mentor work.
            if isinstance(key, str) and key.casefold() == "codex" and isinstance(item, dict):
                candidates.append((key.casefold(), item))
    else:
        legacy = payload.get("rateLimits")
        if isinstance(legacy, dict) and legacy.get("limitId") in (None, "codex"):
            candidates.append(("codex", legacy))
    for key, item in candidates:
        view = {"limit_id": key[:96]}
        found = False
        for slot in ("primary", "secondary"):
            window = item.get(slot)
            used = window.get("usedPercent") if isinstance(window, dict) else None
            if type(used) not in (int, float) or not math.isfinite(used) or used < 0:
                continue
            found = True
            normalized = {"used_percent": min(used, 100)}
            reset = window.get("resetsAt")
            minutes = window.get("windowDurationMins")
            if type(reset) is int and 0 <= reset <= 253402300799:
                normalized["reset_at"] = reset
            if type(minutes) is int and 0 < minutes <= 5256000:
                normalized["window_minutes"] = minutes
            view[slot] = normalized
            if used >= 100:
                result.update(exhausted=True, reason="usage_window_exhausted")
        kind = quota_error({"rateLimitReachedType": item.get("rateLimitReachedType")})
        if kind or item.get("spendControlReached") is True:
            found = True
            result.update(exhausted=True, reason=kind or "spend_control_reached")
        if found:
            result["known"] = True
            result["source"] = "account_rate_limits"
            result["buckets"].append(view)
    return result


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def now():
    return int(time.time() * 1000)


def text(value, limit, empty=True):
    if not isinstance(value, str) or "\0" in value or len(value.encode("utf-8")) > limit or (not empty and not value.strip()):
        raise Failure("mentor_invalid_request")
    return value


def protect(value, decrypt=False):
    if os.name != "nt":
        raise Failure("mentor_windows_dpapi_required")

    class Blob(ctypes.Structure):
        _fields_ = [("size", ctypes.c_uint32), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    raw = ctypes.create_string_buffer(value)
    source = Blob(len(value), ctypes.cast(raw, ctypes.POINTER(ctypes.c_ubyte)))
    output = Blob()
    fn = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    fn.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                   ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Blob)]
    try:
        if not fn(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(output)):
            raise Failure("mentor_encrypted_storage_unavailable")
        try:
            return ctypes.string_at(output.data, output.size)
        finally:
            ctypes.memset(output.data, 0, output.size)
            kernel.LocalFree(output.data)
    finally:
        ctypes.memset(raw, 0, len(raw))


def safe_path(path):
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink() or (hasattr(parent, "is_junction") and parent.is_junction()):
            raise Failure("mentor_path_link_rejected")
        if os.name == "nt" and parent.exists():
            attrs = ctypes.windll.kernel32.GetFileAttributesW(str(parent))
            if attrs != -1 and attrs & 0x400:
                raise Failure("mentor_path_link_rejected")
    return path


def atomic(path, data):
    safe_path(path)
    temporary = path.with_name(path.name + "." + secrets.token_hex(8) + ".pending")
    safe_path(temporary)
    with temporary.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


class Store:
    def __init__(self, root):
        self.root = safe_path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.records = safe_path(self.root / "records")
        self.records.mkdir(exist_ok=True)
        self.workspace = safe_path(self.root / "workspace")
        self.workspace.mkdir(exist_ok=True)
        self.state_path = self.root / "state.dpapi"
        self.mutex = threading.RLock()
        if self.state_path.exists():
            self.state = self.read(self.state_path, 1024 * 1024)
            if (set(self.state) != {"format", "session_id", "records", "created_ms", "generation"}
                    or self.state["format"] != "hibernation.mentor-session.v1"
                    or not ID.fullmatch(self.state["session_id"])
                    or not isinstance(self.state["records"], list) or len(self.state["records"]) > 10000
                    or len(set(self.state["records"])) != len(self.state["records"])
                    or any(not isinstance(x, str) or not ID.fullmatch(x) for x in self.state["records"])):
                raise Failure("mentor_state_invalid")
        else:
            # Existing encrypted records cannot silently become a new session.
            if any(self.records.iterdir()):
                raise Failure("mentor_state_missing")
            self.state = {"format": "hibernation.mentor-session.v1", "session_id": uuid.uuid4().hex,
                          "records": [], "created_ms": now(), "generation": 0}
            self.save()
        guide = self.workspace / "MENTOR_MEMORY.md"
        if not guide.exists():
            atomic(guide, GUIDE.encode("utf-8"))
        for key in self.state["records"]:
            if not safe_path(self.records / (key + ".dpapi")).is_file():
                raise Failure("mentor_record_missing")
        self.note_count = sum(1 for record in self.all() if record["kind"] == "note")

    def read(self, path, maximum):
        safe_path(path)
        if not path.is_file() or path.stat().st_size > maximum + 8192:
            raise Failure("mentor_state_invalid")
        data = protect(path.read_bytes(), True)
        if len(data) > maximum:
            raise Failure("mentor_state_invalid")
        return json.loads(data)

    def save(self):
        atomic(self.state_path, protect(canonical(self.state)))

    def all(self):
        with self.mutex:
            result = []
            for key in self.state["records"]:
                value = self.read(self.records / (key + ".dpapi"), MAX_RECORD)
                if (set(value) != {"format", "id", "session_id", "kind", "created_ms", "request_id", "data"}
                        or value["format"] != "hibernation.mentor-record.v1" or value["id"] != key
                        or value["session_id"] != self.state["session_id"]
                        or value["kind"] not in ("user", "assistant", "note", "operation", "interruption", "forward")
                        or not isinstance(value["data"], dict)):
                    raise Failure("mentor_record_invalid")
                result.append(value)
            return result

    def public_history(self, query="", limit=4):
        query = text(query, 200, True).casefold()
        if type(limit) is not int or not 1 <= limit <= 8:
            raise Failure("mentor_history_arguments")
        rows = []
        for record in self.all():
            # Only replies already produced for the character. Private notes,
            # native operations and tool stdout are never exposed here.
            if record["kind"] != "assistant":
                continue
            summary = record["data"].get("summary", "")
            if not isinstance(summary, str) or not summary or query and query not in summary.casefold():
                continue
            rows.append({"id": record["id"], "request_id": record["request_id"],
                         "created_ms": record["created_ms"], "summary": summary[:2400]})
        return {"items": rows[-limit:], "total_matches": len(rows),
                "scope": "previous_public_mentor_replies", "model_called": False}

    def append(self, kind, data, request_id):
        with self.mutex:
            if len(self.state["records"]) >= 10000:
                raise Failure("mentor_archive_limit")
            key = uuid.uuid4().hex
            record = {"format": "hibernation.mentor-record.v1", "id": key,
                      "session_id": self.state["session_id"], "kind": kind,
                      "created_ms": now(), "request_id": request_id, "data": data}
            encoded = canonical(record)
            if len(encoded) > MAX_RECORD:
                raise Failure("mentor_record_limit")
            atomic(self.records / (key + ".dpapi"), protect(encoded))
            self.state["records"].append(key)
            self.save()
            if kind == "note":
                self.note_count += 1
            return key

    def search(self, query, limit):
        query = text(query, 512, False).casefold()
        if type(limit) is not int or not 1 <= limit <= 12:
            raise Failure("mentor_memory_arguments")
        hits = []
        for record in reversed(self.all()):
            if record["kind"] not in ("user", "assistant", "note"):
                continue
            body = record["data"].get("text", record["data"].get("summary", record["data"].get("goal", "")))
            if query in body.casefold():
                hits.append({"id": record["id"], "kind": record["kind"], "text": body[:2000],
                             "request_id": record["request_id"], "created_ms": record["created_ms"]})
            if len(hits) == limit:
                break
        return {"matches": hits, "sources_preserved": True}


def memory_tools():
    def spec(name, description, properties, required):
        return {"type": "function", "name": name, "description": description,
                "inputSchema": {"type": "object", "properties": properties,
                                "required": required, "additionalProperties": False}}
    return [{"type": "namespace", "name": "mentor_memory", "description": "导师自己的加密长期记忆，不读取小爱的档案。",
             "tools": [spec("search", "检索以前的交流与笔记，返回可引用的原始记录id。",
                            {"query": {"type": "string", "maxLength": 512}, "limit": {"type": "integer", "minimum": 1, "maximum": 12}}, ["query", "limit"]),
                       spec("write", "保存简短结论、摘要或未解问题，不保存思维链。sources仅使用真实记录id。",
                            {"text": {"type": "string", "maxLength": 4000},
                             "category": {"type": "string", "enum": ["fact", "summary", "open_question"]},
                             "sources": {"type": "array", "items": {"type": "string"}, "maxItems": 16}}, ["text", "category", "sources"])]}]


class CodexServer:
    def __init__(self, executable, workspace, command=None):
        env = dict(os.environ)
        env.pop("OPENAI_API_KEY", None)
        env.pop("CODEX_API_KEY", None)
        env.pop("HIBERNATION_MENTOR_AUTH", None)
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.process = subprocess.Popen(command or [executable, "app-server", "--listen", "stdio://"],
                                        cwd=workspace, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, creationflags=flags)
        self.messages = queue.Queue(maxsize=256)
        self.send_lock = threading.Lock()
        self.serial = 0
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(1024 * 1024 + 1)
                if not line:
                    break
                if len(line) > 1024 * 1024:
                    raise Failure("mentor_event_limit")
                message = json.loads(line)
                # Never retain or queue reasoning, raw model items, or deltas.
                method = message.get("method", "")
                if method.endswith("/delta") or "reasoning" in method.lower() or method.startswith("rawResponse"):
                    continue
                if method in ("item/started", "item/completed"):
                    item = message.get("params", {}).get("item", {})
                    kind = item.get("type")
                    if kind not in ("agentMessage", "commandExecution", "fileChange", "mcpToolCall", "webSearch", "imageGeneration", "dynamicToolCall"):
                        continue
                    if kind == "agentMessage" and item.get("phase") not in (None, "final_answer", "final"):
                        continue
                    if kind != "agentMessage":
                        item = {k: item[k] for k in ("type", "id", "status", "exitCode") if k in item}
                        message = {"method": method, "params": {"item": item}}
                self.messages.put(message, timeout=2)
        except Exception:
            pass
        try:
            self.messages.put({"_closed": True}, timeout=1)
        except queue.Full:
            pass

    def send(self, method, params=None):
        with self.send_lock:
            self.serial += 1
            identifier = self.serial
            self.process.stdin.write(canonical({"id": identifier, "method": method, "params": params or {}}) + b"\n")
            self.process.stdin.flush()
            return identifier

    def respond(self, identifier, result):
        with self.send_lock:
            self.process.stdin.write(canonical({"id": identifier, "result": result}) + b"\n")
            self.process.stdin.flush()

    def notify(self, method):
        with self.send_lock:
            self.process.stdin.write(canonical({"method": method}) + b"\n")
            self.process.stdin.flush()

    def next(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise Failure("mentor_timeout")
        try:
            message = self.messages.get(timeout=min(remaining, .2))
        except queue.Empty:
            return None
        if message.get("_closed"):
            raise Failure("mentor_app_server_closed")
        return message

    def rpc(self, method, params, deadline):
        identifier = self.send(method, params)
        while True:
            message = self.next(deadline)
            if not message:
                continue
            if message.get("id") == identifier and "method" not in message:
                if "error" in message:
                    raise Failure("mentor_protocol_unsupported")
                return message.get("result", {})
            if "id" in message and "method" in message:
                raise Failure("mentor_approval_required")

    def close(self):
        try:
            self.process.stdin.close()
            self.process.wait(timeout=1)
        except Exception:
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except Exception:
                try:
                    self.process.kill()
                    self.process.wait(timeout=2)
                except Exception:
                    pass
        finally:
            self.reader.join(timeout=1)
            self.process.stdout.close()


class Session:
    def __init__(self, root, server_factory=CodexServer):
        self.store = Store(root)  # DPAPI is exercised before HTTP readiness.
        self.factory = server_factory
        self.ask_lock = threading.Lock()
        self.state_lock = threading.RLock()
        self.cancelled = threading.Event()
        self.server = None
        self.thread_id = ""
        self.turn_id = ""
        self.signature = None
        self.running = False
        self.last_issue = ""
        self.selected = routing({}, {})
        self.work_quota = work_quota(None, 0)
        self.quota_checked_at = None
        self.web_candidates = set()
        self.chat_pending = None
        self.forward_lock = threading.RLock()
        self.forward_active = set()

    def forward_status(self, body):
        request_id = text(body.get("request_id"), 128, False)
        if not REQUEST.fullmatch(request_id):
            raise Failure("mentor_invalid_request")
        records = [x for x in self.store.all() if x["request_id"] == request_id and x["kind"] == "forward"]
        if not records:
            raise Failure("mentor_forward_not_found")
        receipt = dict(records[-1]["data"])
        with self.forward_lock:
            if receipt["status"] in ("dispatching", "accepted") and request_id not in self.forward_active:
                # A worker restart cannot prove what the provider completed.
                receipt.update(status="uncertain", retry_automatically=False)
        return dict(receipt, request_id=request_id, session_id=self.store.state["session_id"])

    def forward(self, body):
        """Deliver into this same logical mentor session without waiting for its answer."""
        request_id = text(body.get("request_id"), 128, False)
        if not REQUEST.fullmatch(request_id) or body.get("execute", False) is not False:
            raise Failure("mentor_invalid_request")
        with self.forward_lock:
            try:
                return dict(self.forward_status(body), duplicate=True)
            except Failure as error:
                if str(error) != "mentor_forward_not_found":
                    raise
            if not self.ask_lock.acquire(False):
                raise Failure("mentor_busy")
            try:
                self.store.append("forward", {"status": "dispatching", "delivered": False,
                                             "automatic_execution": False}, request_id)
            except Exception:
                self.ask_lock.release()
                raise
            self.forward_active.add(request_id)
        ready = threading.Event()

        def accepted(thread_id, turn_id):
            if not thread_id or not turn_id:
                return
            self.store.append("forward", {"status": "accepted", "delivered": True,
                                         "provider_thread_id": thread_id, "turn_id": turn_id,
                                         "automatic_execution": False}, request_id)
            ready.set()

        def run():
            try:
                result = self.ask(body, _lock_acquired=True, _on_accepted=accepted)
                prior = self.forward_status(body)
                receipt = {key: prior[key] for key in ("delivered", "provider_thread_id", "turn_id") if key in prior}
                receipt.update(status="needs_confirmation" if result.get("approval_required") else "completed",
                               automatic_execution=False, summary=result["summary"])
                self.store.append("forward", receipt, request_id)
            except Exception as error:
                prior = self.forward_status(body)
                receipt = {key: prior[key] for key in ("delivered", "provider_thread_id", "turn_id") if key in prior}
                receipt.update(status="failed", automatic_execution=False,
                               error_code=str(error) if isinstance(error, Failure) else "mentor_request_failed")
                self.store.append("forward", receipt, request_id)
            finally:
                with self.forward_lock:
                    self.forward_active.discard(request_id)
                ready.set()

        threading.Thread(target=run, name="mentor-forward", daemon=True).start()
        ready.wait(2)
        return self.forward_status(body)

    def status(self):
        with self.state_lock:
            result = {"session_id": self.store.state["session_id"], "provider_thread_id": self.thread_id,
                      "running": self.running, "busy": self.running, "records": len(self.store.state["records"]),
                      "record_count": len(self.store.state["records"]), "memory_count": self.store.note_count,
                      "workspace": str(self.store.workspace), "encrypted": True,
                      "memory_scope": "independent_mentor", "permissions": "inherited_codex_user_configuration",
                      "permission_policy": "codex_user_configuration", "native_history": "ephemeral",
                      "generation": self.store.state["generation"], "last_issue": self.last_issue,
                      "rebuild_generation": self.store.state["generation"]}
            result.update(routing_mode=self.selected["mode"], last_selected_level=self.selected["level"],
                          last_selected_model=self.selected["model"], last_selected_effort=self.selected["effort"],
                          work_quota=self.work_quota, quota=self.work_quota, chat_pending=self.chat_pending is not None)
            return result

    def cancel(self):
        self.cancelled.set()
        with self.state_lock:
            if self.chat_pending:
                pending = self.chat_pending
                self.chat_pending = None
                try:
                    self.store.append("interruption", {"reason": "mentor_chat_cancelled", "turn_id": ""}, pending["request_id"])
                finally:
                    self.running = False
                    self.web_candidates.discard(pending["request_id"])
                    self.ask_lock.release()
            if self.server and self.turn_id:
                try:
                    self.server.send("turn/interrupt", {"threadId": self.thread_id, "turnId": self.turn_id})
                except Exception:
                    pass
        return {"cancel_requested": True, "running": self.running}

    def _drop(self):
        with self.state_lock:
            if self.server:
                self.server.close()
            self.server = None
            self.thread_id = ""
            self.turn_id = ""

    def rebuild(self):
        if not self.ask_lock.acquire(False):
            raise Failure("mentor_busy")
        try:
            self._drop()
            return dict(self.status(), rebuilt=True, pending_thread_recreation=True, originals_retained=True)
        finally:
            self.ask_lock.release()

    def _ensure(self, settings, deadline):
        executable = text(settings.get("codexExecutable"), 4096, False)
        if not Path(executable).is_absolute() or not Path(executable).is_file():
            raise Failure("mentor_codex_not_found")
        model = text(settings.get("model", "gpt-6.1-sol"), 160, False)
        signature = executable
        if self.server and signature == self.signature and self.server.process.poll() is None:
            return False
        self._drop()
        server = self.factory(executable, self.store.workspace)
        try:
            server.rpc("initialize", {"clientInfo": {"name": "hibernation_mentor", "title": "Hibernation Mentor", "version": "0.1.0"}, "capabilities": {"experimentalApi": True}}, deadline)
            server.notify("initialized")
            account = server.rpc("account/read", {"refreshToken": False}, deadline).get("account")
            if not isinstance(account, dict) or account.get("type") != "chatgpt":
                raise Failure("mentor_chatgpt_login_required")
            params = {"cwd": str(self.store.workspace), "ephemeral": True,
                      "developerInstructions": INSTRUCTIONS, "dynamicTools": memory_tools(),
                      "serviceName": "hibernation_mentor"}
            if model:
                params["model"] = model
            # Deliberately omit permissions, sandbox, approvalPolicy and config.
            started = server.rpc("thread/start", params, deadline).get("thread", {})
            if started.get("ephemeral") is not True or not isinstance(started.get("id"), str):
                raise Failure("mentor_ephemeral_thread_required")
            with self.store.mutex:
                self.store.state["generation"] += 1
                self.store.save()
            with self.state_lock:
                self.server, self.thread_id, self.signature = server, started["id"], signature
            return True
        except Exception:
            server.close()
            raise

    def _quota(self, deadline):
        if self.quota_checked_at is None or time.monotonic() - self.quota_checked_at >= 60:
            try:
                payload = self.server.rpc("account/rateLimits/read", {}, min(deadline, time.monotonic() + 5))
            except Failure as error:
                if str(error) not in ("mentor_protocol_unsupported", "mentor_timeout"):
                    raise
                payload = None
            self.work_quota = work_quota(payload, now())
            self.quota_checked_at = time.monotonic()
        if self.work_quota["exhausted"]:
            raise Failure("mentor_work_quota_exhausted")

    def _quota_failure(self, error):
        code = quota_error(error)
        if not code:
            return False
        self.work_quota = dict(self.work_quota, known=True, exhausted=True, source="provider_error", checked_ms=now(), reason=code)
        self.quota_checked_at = time.monotonic()
        return True

    def _memory(self, params, request_id, user_id):
        name, namespace, args = params.get("tool"), params.get("namespace"), params.get("arguments")
        if isinstance(args, str):
            args = json.loads(args)
        if namespace != "mentor_memory" or not isinstance(args, dict):
            raise Failure("mentor_memory_arguments")
        if name == "search" and set(args) == {"query", "limit"}:
            return self.store.search(args["query"], args["limit"])
        if name == "write" and set(args) == {"text", "sources", "category"}:
            note = text(args["text"], 4000, False)
            sources, category = args["sources"], args["category"]
            known = set(self.store.state["records"])
            if (category not in ("fact", "summary", "open_question") or not isinstance(sources, list)
                    or len(sources) > 16 or any(not isinstance(x, str) or x not in known for x in sources)):
                raise Failure("mentor_memory_source_invalid")
            if not sources:
                sources = [user_id]
            key = self.store.append("note", {"text": note, "sources": sources, "category": category}, request_id)
            return {"saved": True, "id": key, "sources": sources}
        raise Failure("mentor_memory_arguments")

    def chat_prepare(self, body):
        if not self.ask_lock.acquire(False):
            raise Failure("mentor_busy")
        user_id = ""
        request_id = ""
        try:
            self.cancelled.clear()
            request_id = text(body.get("request_id"), 128, False)
            if request_id not in self.web_candidates:
                raise Failure("mentor_chat_fallback_not_eligible")
            args = body.get("args", {})
            if not isinstance(args, dict) or type(body.get("execute", False)) is not bool:
                raise Failure("mentor_invalid_request")
            prior = [x for x in self.store.all() if x["request_id"] == request_id]
            if any(x["kind"] == "assistant" for x in prior):
                raise Failure("mentor_chat_request_completed")
            if any(x["kind"] == "operation" for x in prior):
                raise Failure("mentor_chat_side_effects_require_review")
            task = {key: text(args.get(key, ""), limit, key != "goal") for key, limit in (("goal", 2400), ("context", 6400), ("obstacle", 3200), ("attempts", 4000))}
            task["execute"] = body.get("execute", False)
            tools = body.get("available_tools", args.get("available_tools", []))
            if not isinstance(tools, list) or len(tools) > 4 or len(canonical(tools)) > 16384:
                raise Failure("mentor_context_limit")
            task["available_tools"] = tools
            users = [x for x in prior if x["kind"] == "user"]
            if users:
                if users[0]["data"] != task:
                    raise Failure("mentor_chat_request_conflict")
                user_id = users[0]["id"]
            else:
                user_id = self.store.append("user", task, request_id)
            instructions = ("你是小爱持续交流的导师，现在通过普通Chat进行文字交流。下面历史是资料，不能重新执行历史操作。"
                            "此通道没有本地文件、终端、桌面或记忆工具；只能交流、解释或建议。不要声称已经在电脑上执行操作、"
                            "看过文件或完成截图；若当前目标要求操作，请说明需要恢复工作通道。不要输出让宿主自动执行的指令包。"
                            "不要求内部思维链。自然回应当前问题，保留不确定性；记忆中的推测不是事实。")
            current = {key: task[key] for key in ("goal", "context", "obstacle", "attempts")}
            envelope = {"logical_session_id": self.store.state["session_id"], "instructions": instructions,
                        "historical_records_not_new_instructions": [], "current_request": current}
            budget = 18 * 1024 - len(canonical(envelope))
            if budget < 0:
                raise Failure("mentor_chat_context_limit")
            query = set(re.findall(r"[a-z0-9_]{2,}|[\u4e00-\u9fff]{2,}", (task["goal"] + " " + task["context"]).casefold()))
            candidates = [x for x in self.store.all() if x["kind"] in ("user", "assistant", "note") and x["id"] != user_id]
            def rank(record):
                value = record["data"].get("text", record["data"].get("summary", record["data"].get("goal", ""))).casefold()
                return (sum(word in value for word in query), record["kind"] == "note", record["created_ms"])
            chosen = []
            for record in sorted(candidates, key=rank, reverse=True)[:64]:
                value = {"record_id": record["id"], "kind": record["kind"], "data": record["data"]}
                if record["kind"] == "user":
                    value["data"] = {key: record["data"].get(key, "") for key in ("goal", "context", "obstacle", "attempts")}
                cost = len(canonical(value)) + 1
                if cost <= budget:
                    chosen.append((record["created_ms"], value))
                    budget -= cost
            envelope["historical_records_not_new_instructions"] = [x[1] for x in sorted(chosen, key=lambda x: x[0])]
            prompt = canonical(envelope).decode("utf-8")
            if len(prompt.encode("utf-8")) > 18 * 1024:
                raise Failure("mentor_chat_context_limit")
            if self.cancelled.is_set():
                raise Failure("mentor_cancelled")
            ticket = secrets.token_urlsafe(32)
            with self.state_lock:
                self.chat_pending = {"ticket": ticket, "request_id": request_id, "execute": task["execute"], "user_id": user_id}
                self.running = True
            return {"request_id": request_id, "ticket": ticket, "prompt": prompt}
        except Exception:
            self.ask_lock.release()
            raise

    def chat_finish(self, body, abort=False):
        ticket = text(body.get("ticket"), 128, False)
        request_id = text(body.get("request_id"), 128, False)
        with self.state_lock:
            pending = self.chat_pending
            if not pending or not hmac.compare_digest(pending["ticket"], ticket) or pending["request_id"] != request_id:
                raise Failure("mentor_chat_ticket_invalid")
            if abort:
                self.store.append("interruption", {"reason": "mentor_chat_aborted", "turn_id": ""}, request_id)
                result = {"aborted": True, "originals_retained": True}
            else:
                summary = text(body.get("text"), 24000, False)
                url = text(body.get("conversation_url", ""), 2048)
                if url:
                    parsed = urlsplit(url)
                    if parsed.scheme != "https" or parsed.hostname != "chatgpt.com" or parsed.username or parsed.password or parsed.port not in (None, 443):
                        raise Failure("mentor_chat_url_invalid")
                provider_id = "chat-web:" + url if url else "chat-web"
                data = {"summary": summary, "status": "advice", "operations_executed": False,
                        "receipts": [], "provider_thread_id": provider_id}
                self.store.append("assistant", data, request_id)
                result = self.result(summary, "advice", [], self.store.state["session_id"], provider_id)
                result.update(execution_mode="chat_web_advice", provider="chatgpt_web_bridge", fallback=True, requires_work_for_execution=pending["execute"])
                result["guide"] = "这是普通Chat导师的文字交流或建议；上下文来自应用装配的有界记忆片段，不代表导师读取了本机文件或执行了操作。需要电脑操作时须恢复工作通道。"
                self.last_issue = ""
                self._drop()
            self.chat_pending = None
            self.running = False
            self.web_candidates.discard(request_id)
            self.ask_lock.release()
            return result

    @staticmethod
    def result(summary, status, receipts, session_id, thread_id):
        native = [item for item in receipts if item["tool"] in ("codex.commandExecution", "codex.fileChange", "codex.mcpToolCall", "codex.imageGeneration")]
        operations = any(item["receipt"].get("ok") is True for item in native)
        return {"summary": summary, "status": status, "missing_capability": "", "proposals": [],
                "execution_mode": "native_mentor", "host_execution_required": False,
                "operations_executed": operations, "receipts": receipts, "session_id": session_id,
                "operations_dispatched": bool(native), "operations_may_have_executed": bool(native),
                "provider_thread_id": thread_id, "provider": "chatgpt_via_codex_app_server"}

    def ask(self, body, *, _lock_acquired=False, _on_accepted=None):
        if not _lock_acquired and not self.ask_lock.acquire(False):
            raise Failure("mentor_busy")
        user_id = ""
        request_id = ""
        receipts = []
        dispatched = {}
        try:
            self.cancelled.clear()
            with self.state_lock:
                self.running = True
                self.last_issue = ""
            settings, args = body.get("settings", {}), body.get("args", {})
            if not isinstance(settings, dict) or not isinstance(args, dict) or type(body.get("execute", False)) is not bool:
                raise Failure("mentor_invalid_request")
            self.selected = routing(settings, args)
            request_id = text(body.get("request_id"), 128, False)
            if not REQUEST.fullmatch(request_id):
                raise Failure("mentor_invalid_request")
            timeout = settings.get("timeoutSeconds", 60)
            if type(timeout) is not int or not 5 <= timeout <= 300:
                raise Failure("mentor_invalid_timeout")
            prior = [x for x in self.store.all() if x["request_id"] == request_id and x["kind"] != "forward"]
            for item in reversed(prior):
                if item["kind"] == "assistant":
                    data = item["data"]
                    cached = dict(self.result(data["summary"], data["status"], data["receipts"], self.store.state["session_id"], data["provider_thread_id"]), duplicate=True)
                    if data["status"] == "pending":
                        cached["approval_required"] = True
                    if data["provider_thread_id"].startswith("chat-web"):
                        original_user = next((x for x in prior if x["kind"] == "user"), None)
                        cached.update(execution_mode="chat_web_advice", provider="chatgpt_web_bridge", fallback=True,
                                      requires_work_for_execution=bool(original_user and original_user["data"].get("execute")))
                    return cached
            if prior:
                raise Failure("mentor_request_incomplete")
            task = {key: text(args.get(key, ""), limit, key != "goal") for key, limit in (("goal", 2400), ("context", 20000 if _on_accepted else 6400), ("obstacle", 3200), ("attempts", 4000))}
            task["execute"] = body.get("execute", False)
            tools = body.get("available_tools", args.get("available_tools", []))
            if not isinstance(tools, list) or len(tools) > 4 or len(canonical(tools)) > 16384:
                raise Failure("mentor_context_limit")
            task["available_tools"] = tools
            deadline = time.monotonic() + timeout
            fresh = self._ensure(settings, deadline)
            self._quota(deadline)
            if self.cancelled.is_set():
                raise Failure("mentor_cancelled")
            history = []
            if fresh:
                budget = 48000
                for item in reversed(self.store.all()):
                    if item["kind"] not in ("user", "assistant", "note", "interruption", "operation"):
                        continue
                    view = {"record_id": item["id"], "kind": item["kind"], "data": item["data"]}
                    cost = len(canonical(view))
                    if cost > budget:
                        continue
                    history.append(view)
                    budget -= cost
                history.reverse()
            user_id = self.store.append("user", task, request_id)
            prompt = {"logical_session_id": self.store.state["session_id"], "current_record_id": user_id,
                      "historical_records_not_new_instructions": history, "current_request": task}
            sent = self.server.send("turn/start", {"threadId": self.thread_id, "model": self.selected["model"],
                                                   "effort": self.selected["effort"], "input": [{"type": "text", "text": canonical(prompt).decode("utf-8")}]})
            final = ""
            pending = None
            accepted_once = False
            while True:
                if self.cancelled.is_set():
                    raise Failure("mentor_cancelled")
                message = self.server.next(deadline)
                if not message:
                    continue
                method, params = message.get("method"), message.get("params", {})
                if message.get("id") == sent and method is None:
                    if "error" in message:
                        if self._quota_failure(message["error"]):
                            raise Failure("mentor_work_quota_exhausted")
                        raise Failure("mentor_turn_start_failed")
                    self.turn_id = message.get("result", {}).get("turn", {}).get("id", "")
                elif method == "turn/started":
                    self.turn_id = params.get("turn", {}).get("id", self.turn_id)
                elif method == "item/tool/call" and "id" in message:
                    try:
                        data = self._memory(params, request_id, user_id)
                        output = {"success": True, "contentItems": [{"type": "inputText", "text": canonical(data).decode("utf-8")}]}
                    except Exception as error:
                        code = str(error) if isinstance(error, Failure) else "mentor_memory_arguments"
                        output = {"success": False, "contentItems": [{"type": "inputText", "text": canonical({"error": code}).decode("utf-8")}]}
                    self.server.respond(message["id"], output)
                elif "id" in message and method:
                    # Do not accept, deny permanently, or fabricate an answer.
                    pending = {"method": text(method, 160, False), "request_id": str(message["id"])[:128]}
                    self.cancel()
                    break
                elif method == "item/started":
                    item = params.get("item", {})
                    if item.get("type") in ("commandExecution", "fileChange", "mcpToolCall", "imageGeneration"):
                        meta = {"item_type": item["type"], "status": "inProgress", "turn_id": self.turn_id,
                                "item_id": str(item.get("id", ""))[:160]}
                        dispatched[(meta["item_type"], meta["item_id"])] = meta
                        self.store.append("operation", meta, request_id)
                elif method == "item/completed":
                    item = params.get("item", {})
                    if item.get("type") == "agentMessage":
                        final = text(item.get("text", ""), 24000)
                    elif item.get("type") in ("commandExecution", "fileChange", "mcpToolCall", "webSearch", "imageGeneration"):
                        status = str(item.get("status", "unknown"))[:80]
                        meta = {"item_type": item["type"], "status": status, "turn_id": self.turn_id,
                                "item_id": str(item.get("id", ""))[:160]}
                        if type(item.get("exitCode")) is int:
                            meta["exit_code"] = item["exitCode"]
                        dispatched.pop((meta["item_type"], meta["item_id"]), None)
                        self.store.append("operation", meta, request_id)
                        ok = status in ("completed", "succeeded") and item.get("exitCode", 0) == 0
                        if len(receipts) < 32:
                            receipts.append({"tool": "codex." + item["type"], "receipt": {"ok": ok, "result": meta}})
                elif method == "turn/completed":
                    status = params.get("turn", {}).get("status")
                    if status != "completed":
                        if self._quota_failure(params.get("turn", {}).get("error")):
                            raise Failure("mentor_work_quota_exhausted")
                        raise Failure("mentor_turn_" + (status if status in ("failed", "interrupted") else "incomplete"))
                    if not final.strip():
                        raise Failure("mentor_incomplete_response")
                    break
                elif method == "error" and self._quota_failure(params.get("error")):
                    raise Failure("mentor_work_quota_exhausted")
                if self.turn_id and not accepted_once and _on_accepted:
                    _on_accepted(self.thread_id, self.turn_id)
                    accepted_once = True
            if _on_accepted and not accepted_once:
                raise Failure("mentor_acceptance_unconfirmed")
            if pending:
                for meta in dispatched.values():
                    if len(receipts) < 32:
                        receipts.append({"tool": "codex." + meta["item_type"], "receipt": {"ok": False, "result": meta}})
                self.store.append("interruption", {"reason": "approval_required", "turn_id": self.turn_id,
                                                   "pending_method": pending["method"]}, request_id)
                final = "导师遇到了需要用户确认的原生审批。本连接目前未提供审批交互，未自动批准，当前任务没有继续；这不代表导师永久没有该权限。"
                result = self.result(final, "pending", receipts, self.store.state["session_id"], self.thread_id)
                result.update(approval_required=True, pending=pending)
                self._drop()
            else:
                result = self.result(final, "completed" if task["execute"] else "advice", receipts, self.store.state["session_id"], self.thread_id)
            self.store.append("assistant", {key: result[key] for key in ("summary", "status", "operations_executed", "receipts", "provider_thread_id")}, request_id)
            return result
        except Exception as error:
            reason = str(error) if isinstance(error, Failure) else "mentor_request_failed"
            self.last_issue = reason
            if reason == "mentor_work_quota_exhausted" and request_id:
                self.web_candidates.add(request_id)
                if len(self.web_candidates) > 128:
                    self.web_candidates = {request_id}
            failed_thread = self.thread_id
            for meta in dispatched.values():
                if len(receipts) < 32:
                    receipts.append({"tool": "codex." + meta["item_type"], "receipt": {"ok": False, "result": meta}})
            if user_id:
                try:
                    self.store.append("interruption", {"reason": reason, "turn_id": self.turn_id}, request_id)
                except Exception:
                    pass
                self._drop()  # Don't reuse a context after an ambiguous partial turn.
            if receipts or reason == "mentor_work_quota_exhausted":
                wrapped = Failure(reason)
                wrapped.partial = self.result("导师调用未完成；已经收到的原生操作回执保留如下，不能据此认定整项任务完成。", "failed", receipts, self.store.state["session_id"], failed_thread)
                wrapped.partial["error_code"] = reason
                wrapped.partial["work_quota"] = self.work_quota
                raise wrapped from None
            raise
        finally:
            with self.state_lock:
                self.running = False
                self.turn_id = ""
            self.ask_lock.release()


def failure(code):
    return {"ok": False, "error": {"code": code, "retryable": code in ("mentor_busy", "mentor_timeout", "mentor_app_server_closed")}}


def serve(root, ready, instance):
    token = os.environ.get("HIBERNATION_MENTOR_AUTH", "")
    if not 24 <= len(token) <= 512:
        raise Failure("mentor_ipc_token_required")
    # OS advisory lock prevents two workers from modifying one canonical session.
    root = safe_path(root)
    root.mkdir(parents=True, exist_ok=True)
    lock = safe_path(root / "worker.lock").open("a+b")
    if os.name == "nt":
        import msvcrt
        lock.seek(0)
        lock.write(b"1")
        lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    session = Session(root)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def send(self, code, value):
            data = canonical(value)
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def handle_request(self):
            if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + token):
                self.send(401, failure("mentor_unauthorized"))
                return
            try:
                if self.command == "GET":
                    if self.path != "/status":
                        raise Failure("mentor_route")
                    result = session.status()
                else:
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 0 <= size <= 96 * 1024:
                        raise Failure("mentor_request_limit")
                    body = json.loads(self.rfile.read(size) or b"{}")
                    if not isinstance(body, dict):
                        raise Failure("mentor_invalid_request")
                    if self.path == "/ask":
                        result = session.ask(body)
                    elif self.path == "/forward":
                        result = session.forward(body)
                    elif self.path == "/forward/status":
                        result = session.forward_status(body)
                    elif self.path == "/history":
                        result = session.store.public_history(body.get("query", ""), body.get("limit", 4))
                    elif self.path == "/status":
                        result = session.status()
                    elif self.path == "/cancel":
                        result = session.cancel()
                    elif self.path == "/rebuild":
                        result = session.rebuild()
                    elif self.path == "/chat/prepare":
                        result = session.chat_prepare(body)
                    elif self.path == "/chat/complete":
                        result = session.chat_finish(body)
                    elif self.path == "/chat/abort":
                        result = session.chat_finish(body, True)
                    else:
                        raise Failure("mentor_route")
                self.send(200, {"ok": True, "result": result})
            except Exception as error:
                code = str(error) if isinstance(error, Failure) else "mentor_worker_failed"
                result = failure(code)
                if isinstance(error, Failure) and error.partial is not None:
                    result["result"] = error.partial
                self.send(200, result)

        do_GET = handle_request
        do_POST = handle_request

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    ready = safe_path(ready)
    ready.parent.mkdir(parents=True, exist_ok=True)
    atomic(ready, canonical({"pid": os.getpid(), "port": server.server_port, "instance": instance}))
    try:
        server.serve_forever(poll_interval=.2)
    finally:
        session.cancel()
        session._drop()
        server.server_close()
        lock.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--ready", required=True)
    parser.add_argument("--instance", required=True)
    cli = parser.parse_args()
    try:
        serve(cli.root, cli.ready, cli.instance)
    except Exception:
        # No request content, paths, credentials or native stderr are printed.
        raise SystemExit(2)
