#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS 1 fix-forward) aggregate-payload fail-closed lane.

Two fixtures route an AGGREGATE (Pair) payload through the expression-match
arm-copy machinery, which can carry only a single-word scalar (Bool/Int):

  * p6_unwrap_aggregate_reject.ahfl        — `unwrap(Option::Some(pair))`,
                                             rejected at Core lowering
                                             (core.UNLOWERED_EXPRESSION);
  * p6_match_aggregate_payload_reject.ahfl — a source `match` that binds the
                                             aggregate payload, rejected at
                                             wasm planning
                                             (wasm.UNSUPPORTED_ORCHESTRATION,
                                             the typed backstop).

Pre-fix BOTH producers emitted a well-formed module: the arm latch copied the
payload SLOT's own address instead of the child Pair's address word, so a
later `p.y` read the wrong scratch and the wasm computed-goto branched Low
while the native evaluator branched High (silent wrong code, no trap). This
lane asserts the compile now fails closed and writes NO wasm artifact.

Engine-independent (no Node needed): the gate is the rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

CASES = (
    ("p6_unwrap_aggregate_reject.ahfl", "core.UNLOWERED_EXPRESSION", "unwrap payload"),
    ("p6_match_aggregate_payload_reject.ahfl",
     "wasm.UNSUPPORTED_ORCHESTRATION", "aggregate enum payload slot"),
)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_p6_aggregate_payload_reject.py <p6-producer>")
    producer = Path(argv[0])
    if not producer.is_file():
        return fail("missing P6 producer")
    fixtures = Path(__file__).resolve().parent.parent / "golden" / "wasm"

    for stem, expected_code, marker in CASES:
        source = fixtures / stem
        if not source.is_file():
            return fail(f"missing negative fixture {stem}")
        with tempfile.TemporaryDirectory(prefix="ahfl-p6-agg-payload-") as td:
            wasm = Path(td) / "must_not_exist.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode == 0:
                return fail(
                    f"{stem}: an aggregate payload compiled; expected fail-closed "
                    f"rejection {expected_code}")
            combined = run.stdout + run.stderr
            if expected_code not in combined or marker not in combined:
                return fail(
                    f"{stem}: rejected but not with {expected_code} / {marker!r}; "
                    f"got: {combined.strip()}")
            if wasm.exists():
                return fail(f"{stem}: a wasm artifact was written despite the reject")

    print("P6 aggregate enum payload fail-closed compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
