"""Run with the prepared plugin directory as argv[1]; no conversation archive."""
import io
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest
import wave
import time
import secrets
import urllib.request
import urllib.error

plugin = Path(sys.argv.pop(1)).resolve()
runtime = json.loads((plugin / "runtime.json").read_text(encoding="utf-8-sig"))
sys.path[:0] = [str(plugin), runtime["modules"]]
sys.dont_write_bytecode = True
from kana_reading import kana
from kana_worker import synthesize, protect, unprotect


class KanaVoiceTests(unittest.TestCase):
    def test_chinese_pronunciation_is_transliterated_without_translation(self):
        self.assertEqual(kana("你好，小爱。"), "ニーハオ、シャオアイ。")
        self.assertEqual(kana("银行"), "インハン")
        self.assertEqual(kana("音乐"), "インユェ")
        self.assertEqual(kana("重庆"), "チョンチン")

    def test_mixed_text_keeps_letters_numbers_and_japanese_kana(self):
        self.assertEqual(kana("AI"), "エーアイ")
        self.assertEqual(kana("12"), "イアル")
        self.assertEqual(kana("カタカナ"), "カタカナ")
        self.assertEqual(kana("🙂"), "")
        self.assertEqual(kana("你好？"), "ニーハオ？")

    def test_bad_requests_fail_before_synthesis(self):
        valid = dict(version=1, text="你好", speed=1, max_seconds=1)
        for field, value in (("version", 2), ("text", ""), ("text", "你" * 6001),
                             ("speed", True), ("speed", 0), ("speed", float("nan")),
                             ("max_seconds", 0), ("max_seconds", float("inf"))):
            with self.subTest(field=field, value=str(value)[:20]), self.assertRaises(ValueError):
                synthesize(valid | {field: value})

    def test_real_voice_has_nonzero_pcm_and_respects_synthesis_limit(self):
        output = synthesize(dict(version=1, text="你好，我正在测试新的声音。" * 20, speed=1, max_seconds=1))
        with wave.open(io.BytesIO(output), "rb") as stream:
            self.assertEqual(stream.getframerate(), 24000)
            self.assertEqual(stream.getnchannels(), 1)
            self.assertEqual(stream.getsampwidth(), 2)
            self.assertEqual(stream.getnframes(), 24000)
            self.assertNotEqual(stream.readframes(24000), b"\0" * 48000)
        at = output.index(b"aytm")
        size = struct.unpack_from("<I", output, at + 4)[0]
        timing = json.loads(output[at + 8:at + 8 + size])
        self.assertEqual(timing["version"], 1)
        self.assertEqual(timing["cues"][0], [0, 0])
        self.assertTrue(all(a[0] <= b[0] and a[1] <= b[1] for a, b in zip(timing["cues"], timing["cues"][1:])))

    def test_worker_failure_does_not_echo_private_input(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "invalid.dpapi"
            path.write_bytes(b"private-input-marker")
            result = subprocess.run([runtime["pythonExecutable"], "-B", str(plugin / "kana_worker.py"), str(path)],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(result.stderr, b"")

    def test_prewarmed_worker_reuses_voice_and_checks_requests(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bootstrap, ready = root / "boot.dpapi", root / "ready.dpapi"
            token = secrets.token_hex(40)
            protect(bootstrap, dict(version=1, token=token, ready=str(ready)))
            process = subprocess.Popen([runtime["pythonExecutable"], "-B", str(plugin / "kana_worker.py"),
                                        "--serve", str(bootstrap)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 15
                while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(ready.exists(), "worker must announce readiness after warmup")
                endpoint = "http://127.0.0.1:%d/synthesize" % unprotect(ready)["port"]
                def call(request, bearer=token):
                    req = urllib.request.Request(endpoint, json.dumps(request).encode("utf-8"),
                        headers={"Authorization": "Bearer " + bearer, "Content-Type": "application/json"})
                    # Local voice traffic must not go through a system proxy.
                    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=15) as response:
                        return response.read()
                request = dict(version=1, text="你好，今天的天气很好。", speed=1, max_seconds=5)
                for _ in range(2):
                    self.assertEqual(call(request), synthesize(request))
                    self.assertIsNone(process.poll())
                for invalid in (request | dict(speed=99), [], request | dict(text="")):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        call(invalid)
                    self.assertEqual(error.exception.code, 400)
                with self.assertRaises(urllib.error.HTTPError) as error:
                    call(request, "invalid-token")
                self.assertEqual(error.exception.code, 403)
                self.assertEqual(call(request | dict(text="🙂")), b"")
                self.assertTrue(call(request).startswith(b"RIFF"))
                self.assertEqual(sorted(p.name for p in root.iterdir()), ["boot.dpapi", "ready.dpapi"])
                for file in root.iterdir():
                    self.assertNotIn(request["text"].encode("utf-8"), file.read_bytes())
                    self.assertNotIn(token.encode("ascii"), file.read_bytes())
            finally:
                process.terminate()
                stdout, stderr = process.communicate(timeout=5)
                self.assertEqual(stdout, b"")
                self.assertEqual(stderr, b"")


unittest.main()
