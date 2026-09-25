#!/usr/bin/env python3
"""RFC 0026 FB-4 fix-forward: closure-routed capability whitelist reject.

An effectful fn reached ONLY through a closure / call_indirect lane used to
evade the transitive capability-whitelist authorization: the effect fixed point
walked only direct CoreCallExpr / CoreCallStmt edges, so an agent with an EMPTY
capability whitelist could still execute a capability by routing it through an
FnT parameter. The closure-aware structural analysis (ClosurePointsTo feeding
the invocation graph) now classifies the routing fn as reaching the capability,
and the agent whitelist pass fails closed with
core.verify.FN_EFFECT_CAPABILITY_UNAUTHORIZED and writes no wasm.

Engine-independent (no Node needed): the gate is the compile rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = "core.verify.FN_EFFECT_CAPABILITY_UNAUTHORIZED"
MARKER = "whitelist"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_fb4_closure_unauthorized_reject.py <producer> <source>")
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing producer or closure-unauthorized fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb4-closure-") as td:
        wasm = Path(td) / "must_not_exist.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode == 0:
            return fail("a closure-routed unauthorized capability compiled; "
                        "expected FN_EFFECT_CAPABILITY_UNAUTHORIZED")
        combined = run.stdout + run.stderr
        if EXPECTED not in combined or MARKER not in combined:
            return fail(
                "rejected but not with the closure-route whitelist diagnostic "
                f"FN_EFFECT_CAPABILITY_UNAUTHORIZED; got: {combined.strip()}")
        if wasm.exists():
            return fail("a wasm artifact was written despite the reject")

    print("FB-4 closure-routed capability whitelist compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
