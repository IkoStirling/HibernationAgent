"""Separate-process checks for the read-only upgrade history audit."""
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ArchiveAudit(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="archive-audit-", dir=ROOT / "artifacts")
        self.folder = Path(self.scratch.name)
        self.database, self.manifest = self.folder / "records.sqlite", self.folder / "baseline.json"
        with closing(sqlite3.connect(self.database)) as database:
            with database:
                database.execute("CREATE TABLE ay_record_versions(scope TEXT,id TEXT,revision INTEGER,schema_version INTEGER,payload TEXT,source TEXT,correlation TEXT,occurred_ms INTEGER)")
                database.execute("INSERT INTO ay_record_versions VALUES ('private','hidden identity',1,1,'HV1:encrypted-fixture','private source','private correlation',123)")

    def tearDown(self):
        self.scratch.cleanup()

    def run_audit(self, mode, *extra):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/ArchiveAudit.py"), mode, str(self.database), str(self.manifest), *extra], capture_output=True, text=True)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertEqual(result.returncode == 0, report["completed"])
        self.assertNotIn("private source", result.stdout)
        return report

    def alter(self, sql):
        with closing(sqlite3.connect(self.database)) as database:
            with database:
                database.execute(sql)

    def test_snapshot_contains_only_fingerprints_and_preserves_database(self):
        original = self.database.read_bytes()
        self.assertEqual(self.run_audit("snapshot")["snapshot_rows"], 1)
        encoded = self.manifest.read_text()
        for hidden in ("private", "hidden identity", "encrypted-fixture", str(self.database)):
            self.assertNotIn(hidden, encoded)
        self.assertEqual(self.run_audit("compare")["preserved_rows"], 1)
        self.assertEqual(self.database.read_bytes(), original)

    def test_additional_versions_are_allowed_and_new_codec_is_required(self):
        self.run_audit("snapshot")
        self.assertEqual(self.run_audit("compare", "--require-new-format")["error"], "new_envelope_not_observed")
        self.alter("INSERT INTO ay_record_versions VALUES ('private','hidden identity',2,1,'HV2:new-encrypted-fixture','source',NULL,124)")
        self.assertEqual(self.run_audit("compare", "--require-new-format")["additional_rows"], 1)

    def test_ciphertext_change_fails(self):
        self.run_audit("snapshot")
        self.alter("UPDATE ay_record_versions SET payload='HV1:changed'")
        self.assertEqual(self.run_audit("compare")["error"], "historical_record_changed_or_missing")

    def test_provenance_change_fails(self):
        self.run_audit("snapshot")
        self.alter("UPDATE ay_record_versions SET source='changed'")
        self.assertFalse(self.run_audit("compare")["completed"])

    def test_missing_history_fails(self):
        self.run_audit("snapshot")
        self.alter("INSERT INTO ay_record_versions SELECT scope,id,2,schema_version,payload,source,correlation,occurred_ms FROM ay_record_versions")
        self.alter("DELETE FROM ay_record_versions WHERE revision=1")
        self.assertEqual(self.run_audit("compare")["error"], "historical_record_changed_or_missing")

    def test_plain_payload_and_overwrite_are_rejected(self):
        self.run_audit("snapshot")
        baseline = self.manifest.read_bytes()
        self.assertEqual(self.run_audit("snapshot")["error"], "baseline_already_exists")
        self.assertEqual(self.manifest.read_bytes(), baseline)
        self.alter("UPDATE ay_record_versions SET payload='plaintext'")
        self.assertEqual(self.run_audit("compare")["error"], "unrecognized_encrypted_payload")

    def test_missing_database_is_never_created(self):
        self.database.unlink()
        self.assertEqual(self.run_audit("snapshot")["error"], "archive_audit_unavailable")
        self.assertFalse(self.database.exists())

    def test_malformed_baseline_fails_without_exposing_content(self):
        self.manifest.write_text('["private source"]', encoding="utf-8")
        self.assertEqual(self.run_audit("compare")["error"], "invalid_baseline")
        self.manifest.write_text('{"version":1,"records":{"x":"private source"}}', encoding="utf-8")
        self.assertEqual(self.run_audit("compare")["error"], "invalid_baseline")


if __name__ == "__main__":
    unittest.main()
