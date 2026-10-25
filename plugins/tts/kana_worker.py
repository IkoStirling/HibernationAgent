"""One bounded local synthesis job. Only a DPAPI request path is passed in argv."""
import contextlib
import struct
import ctypes
from ctypes import wintypes
import io
import json
import math
from pathlib import Path
import re
import sys
import wave


def unprotect(path):
    sealed = Path(path).read_bytes()
    if not 1 <= len(sealed) <= 65536:
        raise ValueError("request_limit")
    class Blob(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]
    buffer = (ctypes.c_ubyte * len(sealed)).from_buffer_copy(sealed)
    source, plain = Blob(len(sealed), buffer), Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    crypt.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                                        ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    crypt.CryptUnprotectData.restype = wintypes.BOOL
    if not crypt.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(plain)):
        raise ValueError("request_unavailable")
    try:
        return json.loads(ctypes.string_at(plain.data, plain.size))
    finally:
        local_free = ctypes.WinDLL("kernel32").LocalFree
        local_free.argtypes = [ctypes.c_void_p]
        local_free.restype = ctypes.c_void_p
        local_free(plain.data)


def synthesize(request):
    import numpy as np
    import pyopenjtalk
    from kana_reading import kana
    text = request.get("text")
    speed, maximum = request.get("speed", 1.0), request.get("max_seconds", 60)
    if request.get("version") != 1 or not isinstance(text, str) or not 1 <= len(text) <= 6000:
        raise ValueError("request_shape")
    if type(speed) not in (int, float) or not math.isfinite(speed) or not 0.5 <= speed <= 2:
        raise ValueError("request_speed")
    if type(maximum) not in (int, float) or not math.isfinite(maximum) or not 1 <= maximum <= 120:
        raise ValueError("request_duration")
    # Bound every synthesis chunk and the accumulated PCM, even for a long reply.
    # Re-anchor at short clauses, so estimates within one clause cannot drift
    # into following sentences. Cue times come from actual synthesized PCM.
    chunks = re.finditer(r"[^。！？!?，,、；;\n]{1,24}[。！？!?，,、；;\n]?|[。！？!?，,、；;\n]", text)
    parts, samples, rate, cues = [], 0, 24000, [[0.0, 0]]
    for match in chunks:
        chunk = match.group()
        reading = kana(chunk)
        if not reading.strip(" 、。！？\n"):
            continue
        audio, source_rate = pyopenjtalk.tts(reading, speed=1.15 * speed, half_tone=1.5,
                                     use_vanilla=True, predict_nani=False)
        if source_rate != 48000:
            raise ValueError("unsupported_sample_rate")
        even = len(audio) // 2 * 2
        audio = (audio[:even:2] + audio[1:even:2]) * 0.5
        if not np.isfinite(audio).all():
            raise ValueError("invalid_samples")
        remaining = int(maximum * rate) - samples
        if remaining <= 0:
            break
        start_seconds = samples / rate
        full_seconds = len(audio) / rate
        weights = [max(0.15, len(kana(char))) for char in chunk]
        total_weight, weight = sum(weights), 0
        for index, value in enumerate(weights):
            weight += value
            cues.append([start_seconds + full_seconds * weight / total_weight, match.start() + index + 1])
        parts.append(audio[:remaining])
        samples += len(parts[-1])
        if samples >= int(maximum * rate):
            break
    if not parts:
        raise ValueError("no_readable_text")
    audio = np.concatenate(parts)
    peak = float(np.max(np.abs(audio)))
    if peak <= 0:
        raise ValueError("empty_audio")
    # Same normal audition tone; speed never changes the pitch parameter.
    pcm = np.clip(audio * min(0.75, 0.82 * 32767 / peak), -32768, 32767).astype("<i2")
    out = io.BytesIO()
    with wave.open(out, "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(pcm.tobytes())
    payload = json.dumps({"version": 1, "characters": len(text), "cues": cues}, separators=(",", ":")).encode("utf-8")
    extra = b"aytm" + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) % 2 else b"")
    data = bytearray(out.getvalue() + extra)
    struct.pack_into("<I", data, 4, len(data) - 8)
    return bytes(data)


def main():
    sys.dont_write_bytecode = True
    # Optional packages may print notices at import; stdout is a WAV-only protocol.
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        runtime = json.loads((Path(__file__).parent / "runtime.json").read_text(encoding="utf-8-sig"))
        sys.path.insert(0, runtime["modules"])
        if sys.argv[1:] == ["--check"]:
            import numpy, pypinyin, pyopenjtalk
            return 0
        if len(sys.argv) != 2:
            return 2
        audio = synthesize(unprotect(sys.argv[1]))
    sys.stdout.buffer.write(audio)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception:
        # Never echo dialogue, paths, credentials or package tracebacks.
        raise SystemExit(3)
