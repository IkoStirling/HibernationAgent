"""Independent process + actual encrypted storage checks. No network or real model."""
import json
import pathlib
import sqlite3
import subprocess
import sys
import uuid

exe = pathlib.Path(sys.argv[1]).resolve()
scratch = pathlib.Path(sys.argv[2]).resolve() / ("process-" + uuid.uuid4().hex)
scratch.mkdir(parents=True)
checks = []


def check(value, name):
    if not value:
        raise AssertionError(name)
    checks.append(name)


def run(root, events=None, extra=(), expected=0):
    args = [str(exe), "--root", str(root)]
    if events is not None:
        script = scratch / (uuid.uuid4().hex + ".json")
        script.write_text(json.dumps(events, ensure_ascii=False), encoding="utf-8")
        args += ["--script", str(script)]
    result = subprocess.run(args + list(extra), capture_output=True, encoding="utf-8", timeout=20,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode != expected:
        raise AssertionError(f"unexpected process exit {result.returncode}: {result.stderr[:500]}")
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    return rows, result


def event(name, kind, **data):
    return dict(event_id=name, type=kind, **data)


def observe(name="apple", salience=0.4):
    return event("see-" + name, "observe", object=dict(id=name, revision=1, x=0.8, salience=salience, ttl_ms=60000))


def metric(rows, name):
    return rows[-1]["view"]["metrics"][name]


normal = scratch / "normal"
rows, _ = run(normal)
check(metric(rows, "consumed") == 1, "default demo reaches actual simulated consumption")
check(metric(rows, "goals_interrupted") == 1, "conversation interrupts without erasing prior movement")
check(metric(rows, "goals_completed") == 1, "one resumed intention completes once")
before = rows[-1]["view"]
status, _ = run(normal, extra=("--status",))
check(status[-1]["view"] == before, "status does not cancel execution or advance offline time")

with sqlite3.connect(normal / "archive/records.sqlite") as db:
    payloads = [row[0] for row in db.execute("SELECT payload FROM ay_record_versions")]
    check(len(payloads) >= 16, "inputs, results and state versions retained in append-only ledger")
    check(all(str(p).startswith("HV2:") for p in payloads), "every stored payload uses existing authenticated encryption")
    check(all("先停一下" not in str(p) for p in payloads), "synthetic conversation absent from raw database payloads")

pending = scratch / "pending"
first = [event("seed", "seed", energy=0.8, hunger=0.8, x=0.1), observe(),
         event("some-movement", "advance", ms=1000, executor="automatic")]
rows, _ = run(pending, first)
pending_position = rows[-1]["view"]["position_x"]
check(rows[-1]["view"]["operation"] is not None, "process can persist an incomplete operation")
rows, _ = run(pending, [event("after-restart", "advance", ms=30000, executor="automatic")])
check(metric(rows, "goals_unknown") == 1 and metric(rows, "consumed") == 0, "restart marks unconfirmed execution unknown and never replays")
check(rows[-1]["view"]["position_x"] == pending_position, "restart preserves last observed position")
rows, _ = run(pending, first[1:2])
check(rows[0]["result"]["duplicate"] and metric(rows, "consumed") == 0, "event dedup survives process restart")

comparison = [event("seed", "seed", energy=0.8, hunger=0.8, x=0.1), observe("apple", 0.4),
              observe("pear", 0.5), observe("berry", 0.6)]
steady, _ = run(scratch / "steady", comparison)
reactive, _ = run(scratch / "reactive", comparison, ("--attention-off",))
check(metric(steady, "focus_switches") < metric(reactive, "focus_switches"), "controlled ablation reduces attention switching")
check(metric(steady, "model_calls") < metric(reactive, "model_calls"), "controlled ablation reduces mock planning calls")

arbitrary = scratch / "not-an-experiment"
arbitrary.mkdir()
original = arbitrary / "keep.txt"
original.write_text("unchanged", encoding="utf-8")
_, denied = run(arbitrary, [], expected=1)
check("not_a_prototype_root" in denied.stderr and original.read_text() == "unchanged", "arbitrary existing roots refused before initialization")
check(not (arbitrary / "archive").exists() and not (arbitrary / "runtime").exists(), "refusal creates no archives or keys")

bad = scratch / "invalid"
rows, _ = run(bad, [event("bad", "pause", value="true")], expected=2)
check(not rows[0]["result"]["ok"] and metric(rows, "actions_started") == 0, "malformed input has no partial action")

report = dict(completed=True, checks=checks, scratch=str(scratch), real_model_calls=0,
              comparison=dict(attention=steady[-1]["view"]["metrics"], immediate=reactive[-1]["view"]["metrics"]))
(scratch / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(report, ensure_ascii=False))
