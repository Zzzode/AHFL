#!/usr/bin/env python3
"""Pass-level trace export gate.

Runs `ahflc emit ir -O --pass-trace-export <tmp> <fixture>` and validates the
emitted JSON document:

  1. Well-formed JSON.
  2. schema == "ahfl.pass_trace.v1" and optimized == true.
  3. total_ms is a number and passes is a non-empty list.
  4. Each pass record carries name/duration_ms/modified/iteration of the right
     type.
  5. The recorded passes include at least the transformation passes that the
     default optimization pipeline runs under -O.

Stdout must NOT contain the JSON (it is written to the file only), so the file
is the single source of truth.

Usage:
    pass_trace_gate.py <ahflc> <fixture> <out_json>
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

REQUIRED_PASSES = {
    "dead-state-elimination",
    "workflow-simplification",
    "expr-canonicalization",
    "temporal-simplification",
}


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <ahflc> <fixture> <out_json>", file=sys.stderr)
        return 2

    ahflc, fixture, out_json = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    out_json.parent.mkdir(parents=True, exist_ok=True)
    if out_json.exists():
        out_json.unlink()

    proc = subprocess.run(
        [ahflc, "emit", "ir", "-O", "--pass-trace-export", str(out_json), fixture],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print(f"ahflc exited {proc.returncode}\nstderr:\n{proc.stderr}", file=sys.stderr)
        return 1

    if "ahfl.pass_trace" in proc.stdout:
        print("stdout was polluted with pass trace JSON", file=sys.stderr)
        return 1

    if not out_json.exists():
        print(f"pass trace file was not written: {out_json}", file=sys.stderr)
        return 1

    try:
        doc = json.loads(out_json.read_text())
    except json.JSONDecodeError as exc:
        print(f"pass trace is not valid JSON: {exc}", file=sys.stderr)
        return 1

    if doc.get("schema") != "ahfl.pass_trace.v1":
        print(f"unexpected schema: {doc.get('schema')!r}", file=sys.stderr)
        return 1
    if doc.get("optimized") is not True:
        print(f"expected optimized==true, got {doc.get('optimized')!r}", file=sys.stderr)
        return 1
    if not isinstance(doc.get("total_ms"), (int, float)):
        print(f"total_ms must be a number, got {doc.get('total_ms')!r}", file=sys.stderr)
        return 1

    passes = doc.get("passes")
    if not isinstance(passes, list) or not passes:
        print("passes must be a non-empty list", file=sys.stderr)
        return 1

    names = set()
    for record in passes:
        for key, expected in (
            ("name", str),
            ("duration_ms", (int, float)),
            ("modified", bool),
            ("iteration", int),
        ):
            if not isinstance(record.get(key), expected):
                print(f"pass record field {key} has wrong type: {record!r}", file=sys.stderr)
                return 1
        names.add(record["name"])

    missing = REQUIRED_PASSES - names
    if missing:
        print(f"pass trace is missing expected passes: {sorted(missing)}", file=sys.stderr)
        return 1

    print(f"pass trace OK: {len(passes)} records, passes={sorted(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
