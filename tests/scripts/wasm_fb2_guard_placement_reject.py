#!/usr/bin/env python3
"""RFC 0026 FB-2 fix-forward: guard-dominance negative lane (compile reject).

Two bounded-recursion shapes carry a structurally correct rank guard but place
the recursive edge OUTSIDE the guard's continue path: (1) an unconditional self
call before the guard, and (2) a self call inside the divergent stop (THEN)
branch. Both would pass the rank-progression + guard-shape checks yet recurse
forever at runtime (a real Node engine traps with "Maximum call stack size
exceeded"). The Core verifier must reject BOTH at compile time with
FN_RECURSION_UNBOUNDED and the producer must exit non-zero WITHOUT writing a
wasm artifact. These tests are engine-independent (no node needed): the gate is
the compile rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = "core.verify.FN_RECURSION_UNBOUNDED"
MARKER = "not dominated by its base-case guard"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        return fail("usage: wasm_fb2_guard_placement_reject.py <producer> <source>...")
    producer = Path(argv[1])
    sources = [Path(p) for p in argv[2:]]
    if not producer.is_file() or any(not s.is_file() for s in sources):
        return fail("missing producer or a guard-placement fixture")

    for source in sources:
        with tempfile.TemporaryDirectory(prefix="ahfl-fb2-guard-") as td:
            wasm = Path(td) / "must_not_exist.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode == 0:
                return fail(
                    f"{source.name}: a non-dominated recursive edge compiled; "
                    "expected FN_RECURSION_UNBOUNDED")
            combined = run.stdout + run.stderr
            if EXPECTED not in combined or MARKER not in combined:
                return fail(
                    f"{source.name}: rejected but not with the guard-dominance "
                    f"diagnostic; got: {combined.strip()}")
            if wasm.exists():
                return fail(
                    f"{source.name}: a wasm artifact was written despite the reject")

    print("FB-2 guard-dominance compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
