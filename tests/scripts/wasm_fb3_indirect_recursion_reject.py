#!/usr/bin/env python3
"""RFC 0026 FB-3b fix-forward: call_indirect-cycle compile-reject lane.

A recursion cycle that closes ONLY through a closure ARGUMENT (an indirect
call_indirect edge) used to be invisible to the FB-2 direct-call lattice: the
static CoreCallExpr graph was acyclic, so the verifier and the wasm backend
accepted the module and a real engine trapped at run time with native stack
exhaustion (or crossed the 64 KiB heap page). The closure points-to analysis
now feeds every possible call_indirect target into the SAME SCC/rank lattice
that seals direct recursion; with no rank threaded through the closure and
strictly decremented against a static bound, the module must fail closed at
COMPILE TIME with core.verify.FN_RECURSION_UNBOUNDED and write no wasm. Engine
-independent (no Node needed): the gate is the compile rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = "core.verify.FN_RECURSION_UNBOUNDED"
MARKER = "rank parameter progressed by a positive constant"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        return fail("usage: wasm_fb3_indirect_recursion_reject.py <producer> <source>...")
    producer = Path(argv[1])
    sources = [Path(p) for p in argv[2:]]
    if not producer.is_file() or any(not s.is_file() for s in sources):
        return fail("missing producer or an indirect-recursion fixture")

    for source in sources:
        with tempfile.TemporaryDirectory(prefix="ahfl-fb3-indirect-") as td:
            wasm = Path(td) / "must_not_exist.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode == 0:
                return fail(
                    f"{source.name}: a closure-mediated recursion cycle compiled; "
                    "expected core.verify.FN_RECURSION_UNBOUNDED")
            combined = run.stdout + run.stderr
            if EXPECTED not in combined or MARKER not in combined:
                return fail(
                    f"{source.name}: rejected but not with the indirect-cycle "
                    f"FN_RECURSION_UNBOUNDED diagnostic; got: {combined.strip()}")
            if wasm.exists():
                return fail(
                    f"{source.name}: a wasm artifact was written despite the reject")

    print("FB-3b call_indirect-cycle compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
