#!/usr/bin/env python3
"""RFC 0026 FB-4 fix-forward: Int64 in-fn capability ABI reject.

The capability import has the FIXED functype (i32,i32)->(i32,i32,i32). An
unbounded Int is Int64 in the P6 layout, so an Int -> Int capability called
from an outlined fn used to plan/emit an invalid module (local.get of an i64
where the import expects i32) with exit 0 and no diagnostic. The in-fn
capability planner now rejects the non-i32-word scalar at plan time with
wasm.UNSUPPORTED_CAPABILITY_FRAME and writes no module.

Engine-independent (no Node needed): the gate is the compile rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = "wasm.UNSUPPORTED_CAPABILITY_FRAME"
MARKER = "capability ABI"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_fb4_int64_capability_abi_reject.py <producer> <source>")
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing producer or int64-capability-abi fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb4-int64-") as td:
        wasm = Path(td) / "must_not_exist.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode == 0:
            return fail("an Int64 in-fn capability compiled; expected "
                        "wasm.UNSUPPORTED_CAPABILITY_FRAME")
        combined = run.stdout + run.stderr
        if EXPECTED not in combined or MARKER not in combined:
            return fail(
                "rejected but not with the fixed-capability-ABI "
                f"UNSUPPORTED_CAPABILITY_FRAME diagnostic; got: {combined.strip()}")
        if wasm.exists():
            return fail("a wasm artifact was written despite the reject")

    print("FB-4 Int64 in-fn capability ABI compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
