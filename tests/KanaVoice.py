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

plugin = Path(sys.argv.pop(1)).resolve()
runtime = json.loads((plugin / "runtime.json").read_text(encoding="utf-8-sig"))
sys.path[:0] = [str(plugin), runtime["modules"]]
sys.dont_write_bytecode = True
from kana_reading import kana
from kana_worker import synthesize


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


unittest.main()
