"""Synthetic IPC fixture only; installed into an isolated test plugin directory."""
import argparse
import json
import os
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path)
parser.add_argument("--ready", type=Path)
parser.add_argument("--instance")
args = parser.parse_args()
auth = os.environ["HIBERNATION_STYLE_AUTH"]
args.root.mkdir(parents=True, exist_ok=True)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        if self.headers.get("Authorization") != "Bearer " + auth:
            self.send_error(403)
            return
        value = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with (args.root / "fixture-requests.jsonl").open("a", encoding="utf-8") as out:
            out.write(json.dumps({"path": self.path, "args": value["args"]}, ensure_ascii=False) + "\n")
        if self.path == "/preview":
            text = value["args"]["text"]
            text = "还要3分钟。" if "2分钟" in text else text.replace("，不敢瞎说", "")
            result = {"text": text, "changed": True, "adapter_id": "style-fixture", "meaning_review_required": True}
        else:
            result = {"samples": 1, "adapters": [{"id": "style-fixture", "samples": 1, "rank": 8}],
                      "job": {"state": "idle"}, "dependencies": {"torch": True, "transformers": True, "peft": True, "safetensors": True}}
        data = json.dumps({"ok": True, "result": result}, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
args.ready.write_text(json.dumps({"port": server.server_port, "instance": args.instance, "pid": os.getpid()}), encoding="utf-8")
server.serve_forever()
