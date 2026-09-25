#!/usr/bin/env python3
"""RFC 0026 FB-4 fix-forward: per-agent capability import scoping.

The fn-reachability capability import pre-pass used to seed ordered-call roots
from EVERY flow in the Core program, while build_agent_plan compiles the module
for exactly ONE target agent. A pure agent whose fns reach no capability used
to get the OTHER agent's effectful-fn capability in its own import table,
over-declaring access (least-privilege / E2 manifest drift). The seeding is now
scoped to the target flow. This regression compiles the two-agent fixture once
per agent index: PureAgent (0) must plan ZERO ahfl_cap imports, EffectAgent (1)
must still plan its single Bump import.

Engine-independent (no Node needed): the gate is the planned import list.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def compile_agent(producer: Path, source: Path, td: Path, index: int) -> tuple[int, str]:
    wasm = td / f"agent{index}.wasm"
    run = subprocess.run(
        [str(producer), str(source), str(wasm), str(index)],
        capture_output=True, text=True, timeout=60,
    )
    return run.returncode, run.stdout + run.stderr


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_fb4_cross_agent_capability_leak.py <producer> <source>")
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing producer or cross-agent capability-leak fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb4-cross-agent-") as tmp:
        td = Path(tmp)
        rc0, out0 = compile_agent(producer, source, td, 0)
        if rc0 != 0:
            return fail(f"PureAgent failed to compile cleanly: {out0.strip()}")
        first_line0 = out0.strip().splitlines()[0] if out0.strip() else ""
        if "imports=" not in first_line0:
            return fail(f"missing import report for PureAgent: {out0.strip()}")
        imports0 = first_line0.split("imports=", 1)[1].strip()
        if imports0:
            return fail(
                f"PureAgent plans capability imports it never reaches: {imports0} "
                "(expected an empty import list)")

        rc1, out1 = compile_agent(producer, source, td, 1)
        if rc1 != 0:
            return fail(f"EffectAgent failed to compile cleanly: {out1.strip()}")
        first_line1 = out1.strip().splitlines()[0] if out1.strip() else ""
        imports1 = first_line1.split("imports=", 1)[1].strip()
        if "ahfl_cap." not in imports1:
            return fail(
                f"EffectAgent lost its legitimately-reached capability import: {imports1}")

    print("FB-4 per-agent capability import scoping least-privilege lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
