"""Independent envelope audit for explicitly marked synthetic test archives."""
import argparse
import ctypes
import hashlib
import hmac
import json
import sqlite3
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=("snapshot", "compare", "verify"))
parser.add_argument("root", type=Path)
parser.add_argument("library", type=Path)
args = parser.parse_args()
root = args.root.resolve()
if not (root / ".synthetic-vault-test").is_file():
    raise RuntimeError("synthetic_fixture_marker_required")
db = sqlite3.connect(f"file:{(root / 'data/archive/records.sqlite').as_posix()}?mode=ro", uri=True)
rows = db.execute("SELECT scope,id,revision,schema_version,payload,source,correlation,occurred_ms FROM ay_record_versions ORDER BY scope,id,revision").fetchall()
fingerprints = {json.dumps(row[:3]): hashlib.sha256(json.dumps(row, ensure_ascii=False).encode()).hexdigest() for row in rows}
snapshot = root / "original-fingerprints.json"
if args.mode == "snapshot":
    assert rows and all(row[4].startswith("HV1:") for row in rows)
    snapshot.write_text(json.dumps(fingerprints), encoding="utf-8")
    print(json.dumps({"legacy_rows": len(rows)}))
elif args.mode == "compare":
    original = json.loads(snapshot.read_text(encoding="utf-8"))
    assert all(fingerprints.get(key) == value for key, value in original.items()), "original_history_changed"
    print(json.dumps({"preserved_rows": len(original), "current_rows": len(rows)}))
else:
    class Blob(ctypes.Structure):
        _fields_ = [("size", ctypes.c_uint32), ("data", ctypes.POINTER(ctypes.c_ubyte))]
    protected = (root / "data/runtime/memory-key.dpapi").read_bytes()
    storage = ctypes.create_string_buffer(protected)
    source = Blob(len(protected), ctypes.cast(storage, ctypes.POINTER(ctypes.c_ubyte)))
    target = Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    crypt.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Blob)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    if not crypt.CryptUnprotectData(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(target)):
        raise RuntimeError("synthetic_key_locked")
    try:
        master = ctypes.create_string_buffer(ctypes.string_at(target.data, target.size))
    finally:
        ctypes.memset(target.data, 0, target.size)
        kernel.LocalFree(target.data)
    derived = ctypes.create_string_buffer(hmac.digest(master.raw[:32], b"HibernationAgent/vault/HV2/XChaCha20-Poly1305", "sha256"))
    try:
        library = ctypes.CDLL(str(args.library.resolve()))
        assert library.sodium_init() >= 0
        counts = {"HV1": 0, "HV2": 0}
        for row in rows:
            encoded = row[4]
            version = encoded[:3]
            assert version in counts
            counts[version] += 1
            packet = bytes.fromhex(encoded[4:])
            nonce_size = 12 if version == "HV1" else 24
            function = library.crypto_aead_chacha20poly1305_decrypt_detached if version == "HV1" else library.crypto_aead_xchacha20poly1305_ietf_decrypt_detached
            function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulonglong, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulonglong, ctypes.c_void_p, ctypes.c_void_p]
            function.restype = ctypes.c_int
            ciphertext = packet[nonce_size:-16]
            output = ctypes.create_string_buffer(len(ciphertext))
            key = master if version == "HV1" else derived
            assert function(output, None, ciphertext, len(ciphertext), packet[-16:], None, 0, packet[:nonce_size], key) == 0, "independent_authentication_failed"
            try:
                bound, _ = output.raw.split(b"\n", 1)
                assert json.loads(bound) == list(row[:4]) + list(row[5:]), "metadata_binding_failed"
            finally:
                ctypes.memset(output, 0, len(output))
        assert counts["HV1"] and counts["HV2"], "mixed_history_not_exercised"
        print(json.dumps({"independently_verified": len(rows), "versions": counts}))
    finally:
        ctypes.memset(master, 0, len(master))
        ctypes.memset(derived, 0, len(derived))
