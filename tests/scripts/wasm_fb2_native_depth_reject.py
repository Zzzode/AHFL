#!/usr/bin/env python3
"""RFC 0026 FB-2 fix-forward: native-stack RESOURCE gate (compile reject).

The depth lattice SEALS a recursion group whose static depth is under the
Core-layer ceiling (1,000,000) but above the engine-safe native wasm call-stack
budget (kFnRecursionNativeStackDepthMax = 1024). Such a plan is structurally
bounded — the Core verifier accepts it — yet it would trap a real engine with
native stack exhaustion, so the wasm backend must fail closed with
wasm.RESOURCE_EXHAUSTED and write no artifact. The condensation-DAG depth
weighting must count every fn on the longest path regardless of declaration
order. This test is engine-independent (no node needed): the gate is the
compile rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = "wasm.RESOURCE_EXHAUSTED"
MARKER = "exceeds the engine-safe limit"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        return fail("usage: wasm_fb2_native_depth_reject.py <producer> <source>...")
    producer = Path(argv[1])
    sources = [Path(p) for p in argv[2:]]
    if not producer.is_file() or any(not s.is_file() for s in sources):
        return fail("missing producer or a native-depth fixture")

    for source in sources:
        with tempfile.TemporaryDirectory(prefix="ahfl-fb2-depth-") as td:
            wasm = Path(td) / "must_not_exist.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode == 0:
                return fail(
                    f"{source.name}: an over-deep recursion plan compiled; "
                    "expected wasm.RESOURCE_EXHAUSTED")
            combined = run.stdout + run.stderr
            if EXPECTED not in combined or MARKER not in combined:
                return fail(
                    f"{source.name}: rejected but not with the native-depth "
                    f"RESOURCE diagnostic; got: {combined.strip()}")
            if wasm.exists():
                return fail(
                    f"{source.name}: a wasm artifact was written despite the reject")

    print("FB-2 native-depth RESOURCE compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
