"""Hold one synthetic journal writer to exercise application recovery."""
import sqlite3
import sys
import time
from pathlib import Path
root = Path(sys.argv[1]).resolve()
if not (root / ".synthetic-world-journal-test").is_file():
    raise RuntimeError("synthetic_fixture_marker_required")
db = sqlite3.connect(root / "data/archive/events.sqlite", timeout=5)
try:
    db.execute("BEGIN IMMEDIATE")
    (root / "lock-ready").write_text("writer held", encoding="utf-8")
    end = time.monotonic() + 60
    while time.monotonic() < end and not (root / "release-lock").exists():
        time.sleep(0.025)
finally:
    db.rollback()
    db.close()
