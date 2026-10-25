"""Read-only ciphertext history audit. Does not load a key or decrypt memories."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import sqlite3
import sys


class AuditError(Exception):
    pass


def scan(database):
    database = Path(database).resolve(strict=True)
    connection = sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)
    fingerprints, formats = {}, {"HV1": 0, "HV2": 0}
    try:
        connection.execute("PRAGMA query_only=ON")
        connection.execute("BEGIN")
        for row in connection.execute(
            "SELECT scope,id,revision,schema_version,payload,source,correlation,occurred_ms "
            "FROM ay_record_versions ORDER BY scope,id,revision"
        ):
            if not isinstance(row[4], str) or row[4][:4] not in ("HV1:", "HV2:"):
                raise AuditError("unrecognized_encrypted_payload")
            identity = hashlib.sha256(json.dumps(row[:3], ensure_ascii=False, separators=(",", ":")).encode()).hexdigest()
            digest = hashlib.sha256(json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode()).hexdigest()
            if identity in fingerprints:
                raise AuditError("duplicate_record_identity")
            fingerprints[identity] = digest
            formats[row[4][:3]] += 1
    finally:
        connection.close()
    if not fingerprints:
        raise AuditError("empty_archive")
    return fingerprints, formats


def execute(mode, database, manifest, require_new_format=False):
    fingerprints, formats = scan(database)
    manifest = Path(manifest)
    if mode == "snapshot":
        record = {"version": 1, "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  "records": fingerprints, "formats": formats}
        # Never overwrite an earlier baseline, even when a caller repeats a command.
        with manifest.open("x", encoding="utf-8") as output:
            json.dump(record, output, separators=(",", ":"))
        return {"completed": True, "snapshot_rows": len(fingerprints), "formats": formats}
    original = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(original, dict) or original.get("version") != 1 or not isinstance(original.get("records"), dict) or not original["records"]:
        raise AuditError("invalid_baseline")
    before = original["records"]
    if any(not isinstance(key, str) or len(key) != 64 or not isinstance(value, str) or len(value) != 64 for key, value in before.items()):
        raise AuditError("invalid_baseline")
    missing = sum(identity not in fingerprints for identity in before)
    changed = sum(identity in fingerprints and fingerprints[identity] != digest for identity, digest in before.items())
    if missing or changed:
        raise AuditError("historical_record_changed_or_missing")
    if require_new_format:
        previous_formats = original.get("formats")
        if not isinstance(previous_formats, dict) or not isinstance(previous_formats.get("HV2"), int) or previous_formats["HV2"] < 0:
            raise AuditError("invalid_baseline")
        if formats["HV2"] <= previous_formats["HV2"]:
            raise AuditError("new_envelope_not_observed")
    return {"completed": True, "preserved_rows": len(before), "additional_rows": len(fingerprints) - len(before), "formats": formats}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("snapshot", "compare"))
    parser.add_argument("database", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--require-new-format", action="store_true")
    args = parser.parse_args()
    try:
        result = execute(args.mode, args.database, args.manifest, args.require_new_format)
    except AuditError as issue:
        print(json.dumps({"completed": False, "error": str(issue)}))
        return 1
    except FileExistsError:
        print(json.dumps({"completed": False, "error": "baseline_already_exists"}))
        return 1
    except (OSError, sqlite3.Error, ValueError, TypeError, KeyError):
        # File paths, SQL values, payloads and underlying exceptions stay private.
        print(json.dumps({"completed": False, "error": "archive_audit_unavailable"}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
