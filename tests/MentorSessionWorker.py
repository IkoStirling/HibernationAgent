"""Synthetic stdio App Server + real Windows DPAPI; never calls a model."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
import uuid


def fake(plan_path):
    plan = json.loads(Path(plan_path).read_text(encoding="utf-8"))
    physical = "fixture-" + uuid.uuid4().hex
    turn = ""
    pending = None

    def send(value):
        print(json.dumps(value, ensure_ascii=False), flush=True)

    def notify(method, params):
        send({"method": method, "params": params})

    def finish(value):
        notify("item/completed", {"item": {"type": "agentMessage", "id": "answer", "phase": "final_answer", "text": value}})
        notify("turn/completed", {"turn": {"id": turn, "status": "completed"}})

    if os.environ.get("HIBERNATION_MENTOR_AUTH"):
        raise RuntimeError("IPC token leaked into native tool environment")
    for raw in sys.stdin.buffer:
        message = json.loads(raw)
        method, params = message.get("method"), message.get("params", {})
        with open(plan["capture"], "a", encoding="utf-8") as stream:
            stream.write(json.dumps(message, ensure_ascii=False) + "\n")
        identifier = message.get("id")
        if method == "initialize":
            send({"id": identifier, "result": {}})
        elif method == "account/read":
            send({"id": identifier, "result": {"account": {"type": "chatgpt"}}})
        elif method == "account/rateLimits/read":
            current = json.loads(Path(plan_path).read_text(encoding="utf-8"))
            send({"id": identifier, "result": current.get("quota", {"rateLimits": {"limitId": "codex", "primary": {"usedPercent": 10, "resetsAt": 1900000000, "windowDurationMins": 300}}})})
        elif method == "thread/start":
            assert params["ephemeral"] is True
            assert not ({"approvalPolicy", "sandbox", "permissions", "config"} & set(params))
            assert params["dynamicTools"][0]["name"] == "mentor_memory"
            send({"id": identifier, "result": {"thread": {"id": physical, "ephemeral": plan.get("ephemeral", True)}}})
        elif method == "turn/start":
            prompt = json.loads(params["input"][0]["text"])
            goal = prompt["current_request"]["goal"]
            turn = "turn-" + uuid.uuid4().hex
            if goal in ("quota_start", "rate_start"):
                send({"id": identifier, "error": {"code": -32000, "data": {"codexErrorInfo": "usageLimitExceeded" if goal == "quota_start" else {"httpConnectionFailed": {"httpStatusCode": 429}}}}})
                continue
            send({"id": identifier, "result": {"turn": {"id": turn, "status": "inProgress"}}})
            if goal == "remember":
                pending = "write"
                send({"id": "dynamic-write", "method": "item/tool/call", "params": {"namespace": "mentor_memory", "tool": "write", "arguments": {"text": "SYNTHETIC_NOTE_NEEDLE", "category": "open_question", "sources": []}, "threadId": physical, "turnId": turn, "callId": "memory-1"}})
            elif goal == "read":
                pending = "search"
                send({"id": "dynamic-search", "method": "item/tool/call", "params": {"namespace": "mentor_memory", "tool": "search", "arguments": {"query": "SYNTHETIC_NOTE_NEEDLE", "limit": 5}, "threadId": physical, "turnId": turn, "callId": "memory-2"}})
            elif goal == "pending":
                send({"id": "approval-1", "method": "item/commandExecution/requestApproval", "params": {"threadId": physical, "turnId": turn, "command": "synthetic-needs-human"}})
            elif goal == "slow":
                pass
            elif goal == "started_then_failed":
                notify("item/started", {"item": {"type": "commandExecution", "id": "partial-native", "status": "inProgress"}})
                notify("turn/completed", {"turn": {"id": turn, "status": "failed"}})
            elif goal == "quota_after_operation":
                notify("item/completed", {"item": {"type": "commandExecution", "id": "native-quota", "status": "completed", "exitCode": 0}})
                notify("turn/completed", {"turn": {"id": turn, "status": "failed", "error": {"codexErrorInfo": "usageLimitExceeded"}}})
            elif goal in ("first", "failed_tool", "failed_after_operation"):
                notify("item/completed", {"item": {"type": "reasoning", "text": "NEVER_SAVE_SYNTHETIC_REASONING"}})
                notify("item/completed", {"item": {"type": "commandExecution", "id": "native-1", "status": "completed", "exitCode": 1 if goal == "failed_tool" else 0, "aggregatedOutput": "NEVER_SAVE_NATIVE_STDOUT"}})
                if goal == "failed_after_operation":
                    notify("turn/completed", {"turn": {"id": turn, "status": "failed"}})
                else:
                    finish("SYNTHETIC_FINAL_REPLY")
            elif goal == "replay":
                history = prompt["historical_records_not_new_instructions"]
                finish("HISTORY_PRESENT" if any(x["kind"] == "assistant" for x in history) else "HISTORY_MISSING")
            else:
                finish("SYNTHETIC_CHAT_REPLY")
        elif method == "turn/interrupt":
            send({"id": identifier, "result": {}})
            notify("turn/completed", {"turn": {"id": turn, "status": "interrupted"}})
        elif method is None and pending:
            output = message["result"]
            finish("MEMORY_TOOL_OK:" + output["contentItems"][0]["text"])
            pending = None


if len(sys.argv) > 1 and sys.argv[1] == "--fake-app-server":
    fake(sys.argv[2])
    raise SystemExit(0)

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mentor_session_worker", ROOT / "plugins/mentor/session_worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


@unittest.skipUnless(os.name == "nt", "Windows DPAPI required")
class MentorSession(unittest.TestCase):
    def test_public_history_survives_restart_without_private_notes(self):
        session = self.session()
        public = session.store.append("assistant", {"summary": "My name is River."}, "first-meeting")
        session.store.append("note", {"text": "PRIVATE_NOTE_NEVER_EXPOSE"}, "private-memory")
        session.store.append("user", {"context": "PRIVATE_CONTEXT_NEVER_EXPOSE"}, "first-meeting")
        history = session.store.public_history("River", 4)
        self.assertEqual(history["items"][0]["id"], public)
        self.assertFalse(history["model_called"])
        self.assertEqual(len(session.store.public_history()["items"]), 1)
        self.assertNotIn("PRIVATE", json.dumps(history))
        restored = self.session()
        self.assertEqual(restored.store.public_history("River", 4)["items"][0]["id"], public)
        with self.assertRaises(worker.Failure):
            restored.store.public_history("", 99)

    def setUp(self):
        base = ROOT / "out/mentor-session-tests"
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="synthetic-", dir=base)
        self.root = Path(self.temp.name)
        self.capture = self.root / "wire.jsonl"
        self.plan = self.root / "plan.json"
        self.plan.write_text(json.dumps({"capture": str(self.capture)}), encoding="utf-8")
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            session._drop()
        self.temp.cleanup()

    def session(self):
        def factory(executable, workspace):
            return worker.CodexServer(executable, workspace, [sys.executable, str(Path(__file__).resolve()), "--fake-app-server", str(self.plan)])
        session = worker.Session(self.root / "vault", factory)
        self.sessions.append(session)
        return session

    def body(self, goal, request=None, settings=None, level=None):
        args = {"goal": goal}
        if level is not None:
            args["level"] = level
        return {"settings": dict({"codexExecutable": sys.executable, "timeoutSeconds": 5}, **(settings or {})),
                "args": args, "execute": True, "request_id": request or uuid.uuid4().hex}

    def ask(self, session, goal, request=None, settings=None, level=None):
        return session.ask(self.body(goal, request, settings, level))

    def wire(self, method):
        return [x for x in (json.loads(line) for line in self.capture.read_text(encoding="utf-8").splitlines()) if x.get("method") == method]

    def set_quota(self, quota):
        self.plan.write_text(json.dumps({"capture": str(self.capture), "quota": quota}), encoding="utf-8")

    def test_logical_session_physical_reuse_rebuild_replay_and_encrypted_originals(self):
        session = self.session()
        self.assertEqual(session.status()["generation"], 0)
        first = self.ask(session, "first", "first-request")
        self.assertEqual(session.status()["generation"], 1)
        self.assertTrue(first["operations_executed"])
        self.assertFalse(first["host_execution_required"])
        self.assertEqual(first["proposals"], [])
        second = self.ask(session, "chat")
        self.assertEqual(first["provider_thread_id"], second["provider_thread_id"])
        self.assertEqual(session.status()["generation"], 1)
        duplicate = self.ask(session, "first", "first-request")
        self.assertTrue(duplicate["duplicate"])
        count = session.status()["record_count"]
        rebuild = session.rebuild()
        self.assertTrue(rebuild["pending_thread_recreation"])
        self.assertEqual(session.status()["generation"], 1)
        replay = self.ask(session, "replay")
        self.assertEqual(replay["summary"], "HISTORY_PRESENT")
        self.assertNotEqual(first["provider_thread_id"], replay["provider_thread_id"])
        self.assertEqual(first["session_id"], replay["session_id"])
        self.assertEqual(session.status()["generation"], 2)
        self.assertGreater(session.status()["record_count"], count)
        restored = self.session()
        self.assertEqual(restored.status()["session_id"], first["session_id"])
        self.assertEqual(restored.status()["generation"], 2)
        records = json.dumps(restored.store.all())
        self.assertNotIn("NEVER_SAVE_SYNTHETIC_REASONING", records)
        self.assertNotIn("NEVER_SAVE_NATIVE_STDOUT", records)
        for path in (self.root / "vault").rglob("*.dpapi"):
            self.assertNotIn(b"SYNTHETIC_FINAL_REPLY", path.read_bytes())
        wire = [json.loads(line) for line in self.capture.read_text(encoding="utf-8").splitlines()]
        threads = [x["params"] for x in wire if x.get("method") == "thread/start"]
        self.assertEqual(len(threads), 2)
        self.assertTrue(all(x["ephemeral"] and "approvalPolicy" not in x and "sandbox" not in x for x in threads))
        after_restart = self.ask(restored, "replay")
        self.assertEqual(after_restart["session_id"], first["session_id"])
        self.assertEqual(restored.status()["generation"], 3)

    def test_memory_tools_are_encrypted_provenance_bound_and_searchable(self):
        session = self.session()
        self.ask(session, "remember")
        self.assertEqual(session.status()["memory_count"], 1)
        found = self.ask(session, "read")
        self.assertIn("SYNTHETIC_NOTE_NEEDLE", found["summary"])
        note = next(x for x in session.store.all() if x["kind"] == "note")
        self.assertEqual(note["data"]["category"], "open_question")
        self.assertTrue(note["data"]["sources"])
        with self.assertRaises(worker.Failure):
            session._memory({"namespace": "mentor_memory", "tool": "write", "arguments": {"text": "bad", "category": "fact", "sources": ["missing"]}}, "bad", "missing")

    def test_approval_never_accepted_and_failed_native_tool_not_claimed_success(self):
        session = self.session()
        result = self.ask(session, "pending")
        self.assertEqual(result["status"], "pending")
        self.assertTrue(result["approval_required"])
        self.assertFalse(result["operations_executed"])
        self.assertEqual(session.thread_id, "")
        wire = [json.loads(line) for line in self.capture.read_text(encoding="utf-8").splitlines()]
        self.assertFalse(any(x.get("id") == "approval-1" and "result" in x for x in wire))
        failed = self.ask(session, "failed_tool")
        self.assertFalse(failed["operations_executed"])
        self.assertTrue(failed["operations_may_have_executed"])

    def test_failure_retains_actual_native_receipts_and_blocks_reexecution(self):
        session = self.session()
        with self.assertRaises(worker.Failure) as caught:
            self.ask(session, "failed_after_operation", "partial-request")
        self.assertTrue(caught.exception.partial["operations_executed"])
        self.assertEqual(caught.exception.partial["status"], "failed")
        self.assertEqual(len(caught.exception.partial["receipts"]), 1)
        with self.assertRaisesRegex(worker.Failure, "mentor_request_incomplete"):
            self.ask(session, "failed_after_operation", "partial-request")
        with self.assertRaises(worker.Failure) as dispatched:
            self.ask(session, "started_then_failed")
        self.assertFalse(dispatched.exception.partial["operations_executed"])
        self.assertTrue(dispatched.exception.partial["operations_dispatched"])
        self.assertTrue(dispatched.exception.partial["operations_may_have_executed"])

    def test_cancel_and_mutual_exclusion(self):
        session = self.session()
        errors = []
        def run():
            try:
                self.ask(session, "slow")
            except worker.Failure as error:
                errors.append(str(error))
        thread = threading.Thread(target=run)
        thread.start()
        end = time.monotonic() + 5
        while not session.turn_id and time.monotonic() < end:
            time.sleep(.01)
        with self.assertRaisesRegex(worker.Failure, "mentor_busy"):
            self.ask(session, "chat")
        session.cancel()
        thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertIn("mentor_cancelled", errors)
        self.assertFalse(session.status()["busy"])

    def test_forward_returns_provider_acceptance_before_answer_and_never_duplicates(self):
        session = self.session()
        body = self.body("slow", "dev-slow")
        body["execute"] = False
        started = time.monotonic()
        receipt = session.forward(body)
        self.assertLess(time.monotonic() - started, 4)
        self.assertEqual(receipt["status"], "accepted")
        self.assertTrue(receipt["delivered"])
        self.assertTrue(receipt["provider_thread_id"] and receipt["turn_id"])
        self.assertFalse(receipt["automatic_execution"])
        again = session.forward(body)
        self.assertTrue(again["duplicate"])
        self.assertEqual(len(self.wire("turn/start")), 1)
        other = self.body("chat", "dev-other");other["execute"] = False
        with self.assertRaisesRegex(worker.Failure, "mentor_busy"):
            session.forward(other)
        session.cancel()
        end = time.monotonic() + 5
        while session.forward_active and time.monotonic() < end:
            time.sleep(.01)
        self.assertEqual(session.forward_status(body)["status"], "failed")
        self.assertTrue(session.forward_status(body)["delivered"])
        self.assertEqual(len(self.wire("turn/start")), 1)

    def test_forward_completion_restart_receipt_and_unaccepted_failure(self):
        session = self.session()
        body = self.body("chat", "dev-complete");body["execute"] = False
        body["args"]["context"] = "合成背景" * 900  # Beyond ordinary ask's byte cap, within the bounded relay.
        session.forward(body)
        end = time.monotonic() + 5
        while session.forward_active and time.monotonic() < end:
            time.sleep(.01)
        result = session.forward_status(body)
        self.assertEqual(result["status"], "completed")
        self.assertTrue(result["delivered"])
        self.assertEqual(result["summary"], "SYNTHETIC_CHAT_REPLY")
        prompt = json.loads(self.wire("turn/start")[0]["params"]["input"][0]["text"])
        self.assertFalse(prompt["current_request"]["execute"])
        restored = self.session()
        duplicate = restored.forward(body)
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["session_id"], result["session_id"])
        self.assertEqual(len(self.wire("turn/start")), 1)
        session.store.append("forward", {"status": "accepted", "delivered": True}, "dev-unknown")
        uncertain = self.session().forward_status({"request_id": "dev-unknown"})
        self.assertEqual(uncertain["status"], "uncertain")
        self.assertFalse(uncertain["retry_automatically"])
        bad = self.body("quota_start", "dev-rejected");bad["execute"] = False
        result = session.forward(bad)
        self.assertFalse(result["delivered"])
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["error_code"], "mentor_work_quota_exhausted")
        with self.assertRaisesRegex(worker.Failure, "mentor_invalid_request"):
            session.forward(self.body("chat", "dev-execute"))

    def test_ephemeral_requirement_and_tampered_storage_fail_closed(self):
        self.plan.write_text(json.dumps({"capture": str(self.capture), "ephemeral": False}), encoding="utf-8")
        session = self.session()
        with self.assertRaisesRegex(worker.Failure, "mentor_ephemeral_thread_required"):
            self.ask(session, "chat")
        self.assertEqual(session.status()["record_count"], 0)
        state = session.store.state_path
        data = bytearray(state.read_bytes())
        data[-1] ^= 1
        state.write_bytes(data)
        with self.assertRaises(worker.Failure):
            self.session()

    def test_defaults_fixed_level_ignored_and_character_routing_reuses_native_thread(self):
        session = self.session()
        self.ask(session, "chat", level="invalid-fixed-level-ignored")
        first = session.status()
        self.assertEqual((first["last_selected_model"], first["last_selected_effort"]), ("gpt-6.1-sol", "high"))
        settings = {"routingMode": "character", "casualModel": "synthetic-casual", "normalModel": "synthetic-normal", "complexModel": "synthetic-complex"}
        for level, effort in (("casual", "low"), ("normal", "medium"), ("complex", "high")):
            self.ask(session, "chat", settings=settings, level=level)
            current = session.status()
            self.assertEqual(current["provider_thread_id"], first["provider_thread_id"])
            self.assertEqual((current["last_selected_model"], current["last_selected_effort"]), ("synthetic-" + level, effort))
        turns = self.wire("turn/start")
        self.assertTrue(all("model" in x["params"] and "effort" in x["params"] for x in turns))
        self.assertEqual(len(self.wire("thread/start")), 1)
        self.assertEqual(len(self.wire("account/rateLimits/read")), 1)
        self.assertTrue(all("level" not in x["data"] for x in session.store.all() if x["kind"] == "user"))
        self.ask(session, "chat", settings={"model": "synthetic-fixed", "reasoningEffort": "low"})
        self.assertEqual(session.status()["last_selected_effort"], "low")

    def test_quota_preflight_caches_blocks_without_paid_turn_and_filters_other_buckets(self):
        self.set_quota({"rateLimitsByLimitId": {"other": {"primary": {"usedPercent": 100}}, "codex_other": {"primary": {"usedPercent": 100}}, "codex_review": {"primary": {"usedPercent": 100}}, "codex": {"primary": {"usedPercent": 20}}}})
        session = self.session()
        self.ask(session, "chat")
        self.assertFalse(session.status()["work_quota"]["exhausted"])
        self.set_quota({"rateLimitsByLimitId": {"codex": {"primary": {"usedPercent": 100, "resetsAt": 1900000000}}}})
        self.ask(session, "chat")  # cache still valid; no extra quota RPC
        session.quota_checked_at -= 61
        turns = len(self.wire("turn/start"))
        with self.assertRaisesRegex(worker.Failure, "mentor_work_quota_exhausted") as caught:
            self.ask(session, "chat")
        self.assertTrue(caught.exception.partial["work_quota"]["exhausted"])
        self.assertEqual(len(self.wire("turn/start")), turns)
        with self.assertRaisesRegex(worker.Failure, "mentor_work_quota_exhausted"):
            self.ask(session, "chat")
        self.assertEqual(len(self.wire("turn/start")), turns)
        self.assertEqual(session.status()["work_quota"]["buckets"][0]["primary"]["reset_at"], 1900000000)

    def test_unknown_quota_not_zero_and_typed_errors_do_not_misclassify_network429(self):
        self.set_quota({"rateLimitsByLimitId": {"unrelated": {"primary": {"usedPercent": 100}}}, "accountId": "DO_NOT_EXPOSE"})
        session = self.session()
        self.ask(session, "chat")
        self.assertFalse(session.status()["work_quota"]["known"])
        self.assertNotIn("DO_NOT_EXPOSE", json.dumps(session.status()))
        with self.assertRaisesRegex(worker.Failure, "mentor_turn_start_failed"):
            self.ask(session, "rate_start")
        self.assertFalse(session.status()["work_quota"]["exhausted"])
        with self.assertRaisesRegex(worker.Failure, "mentor_work_quota_exhausted"):
            self.ask(session, "quota_start")
        self.assertTrue(session.status()["work_quota"]["exhausted"])

    def test_quota_after_native_operation_keeps_receipts_and_cannot_web_replay(self):
        session = self.session()
        body = self.body("quota_after_operation", "quota-with-native")
        with self.assertRaisesRegex(worker.Failure, "mentor_work_quota_exhausted") as caught:
            session.ask(body)
        self.assertTrue(caught.exception.partial["operations_executed"])
        self.assertEqual(len(caught.exception.partial["receipts"]), 1)
        with self.assertRaisesRegex(worker.Failure, "mentor_chat_side_effects_require_review"):
            session.chat_prepare(body)

    def test_chat_fallback_ticket_memory_completion_and_native_reconstruction(self):
        session = self.session()
        self.ask(session, "remember")
        body = self.body("fallback chat", "chat-fallback")
        with self.assertRaisesRegex(worker.Failure, "mentor_chat_fallback_not_eligible"):
            session.chat_prepare(body)
        self.set_quota({"rateLimits": {"primary": {"usedPercent": 100}}})
        session.quota_checked_at -= 61
        with self.assertRaisesRegex(worker.Failure, "mentor_work_quota_exhausted"):
            session.ask(body)
        prepared = session.chat_prepare(body)
        self.assertLessEqual(len(prepared["prompt"].encode("utf-8")), 18 * 1024)
        self.assertIn("SYNTHETIC_NOTE_NEEDLE", prepared["prompt"])
        with self.assertRaisesRegex(worker.Failure, "mentor_busy"):
            self.ask(session, "chat")
        with self.assertRaisesRegex(worker.Failure, "mentor_chat_ticket_invalid"):
            session.chat_finish({"ticket": "invalid", "request_id": "chat-fallback", "text": "reply"})
        result = session.chat_finish(dict(prepared, text="SYNTHETIC_CHAT_WEB_REPLY", conversation_url="https://chatgpt.com/c/synthetic"))
        self.assertEqual(result["execution_mode"], "chat_web_advice")
        self.assertEqual(result["provider"], "chatgpt_web_bridge")
        self.assertTrue(result["requires_work_for_execution"])
        self.assertFalse(result["operations_executed"])
        self.assertEqual(session.thread_id, "")
        self.assertFalse(session.status()["busy"])
        duplicate = session.ask(body)
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["execution_mode"], "chat_web_advice")
        self.assertEqual(duplicate["provider"], "chatgpt_web_bridge")
        self.set_quota({"rateLimits": {"primary": {"usedPercent": 1}}})
        session.quota_checked_at -= 61
        restored = self.ask(session, "replay")
        self.assertEqual(restored["summary"], "HISTORY_PRESENT")
        self.assertIn("SYNTHETIC_CHAT_WEB_REPLY", self.wire("turn/start")[-1]["params"]["input"][0]["text"])

    def test_chat_abort_and_cancel_preserve_unfinished_user_without_fabricated_answer(self):
        session = self.session()
        self.set_quota({"rateLimits": {"primary": {"usedPercent": 100}}})
        for action in ("abort", "cancel"):
            body = self.body("unfinished", "chat-" + action)
            with self.assertRaises(worker.Failure):
                session.ask(body)
            prepared = session.chat_prepare(body)
            if action == "abort":
                session.chat_finish(prepared, True)
            else:
                session.cancel()
            self.assertFalse(session.status()["busy"])
            records = [x for x in session.store.all() if x["request_id"] == body["request_id"]]
            self.assertTrue(any(x["kind"] == "user" for x in records))
            self.assertFalse(any(x["kind"] == "assistant" for x in records))


if __name__ == "__main__":
    unittest.main()
