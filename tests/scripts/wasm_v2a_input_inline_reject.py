#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-A fix-forward fail-closed lane.

Two computed-final fixtures copy an aggregate value straight off the
host-packed INPUT frame:

  * v2a_input_nested_aggregate_reject.ahfl - `return Out { x: input.inner }`,
                                             a nested struct field;
  * v2a_input_payload_enum_reject.ahfl     - `return Out { r: input.e }`,
                                             a payload-bearing enum field.

The host packs the input frame with the P4-D INLINE graph (every field in
place, no child-address slots), while the module representation stores an
aggregate field as the child's i32 ADDRESS and the computed-final
materializer expands that pointer tree. Pre-fix both fixtures compiled and
Node execution dereferenced the inline input words as an address (silent
zero/garbage output, or an in-page OOB on arbitrary packed words). This
lane asserts the compile now fails closed
(wasm.UNSUPPORTED_ORCHESTRATION, naming the deferred inline-input-frame
expansion) and writes NO wasm artifact.

Engine-independent (no Node needed): the gate is the rejection.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

CASES = (
    ("v2a_input_nested_aggregate_reject.ahfl",
     "wasm.UNSUPPORTED_ORCHESTRATION", "host-packed"),
    ("v2a_input_payload_enum_reject.ahfl",
     "wasm.UNSUPPORTED_ORCHESTRATION", "host-packed"),
)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_v2a_input_inline_reject.py <p6-producer>")
    producer = Path(argv[0])
    if not producer.is_file():
        return fail("missing P6 producer")
    fixtures = Path(__file__).resolve().parent.parent / "golden" / "wasm"

    for stem, expected_code, marker in CASES:
        source = fixtures / stem
        if not source.is_file():
            return fail(f"missing negative fixture {stem}")
        with tempfile.TemporaryDirectory(prefix="ahfl-v2a-input-inline-") as td:
            wasm = Path(td) / "must_not_exist.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode == 0:
                return fail(
                    f"{stem}: an input-sourced aggregate compiled; expected fail-closed "
                    f"rejection {expected_code}")
            combined = run.stdout + run.stderr
            if expected_code not in combined or marker not in combined:
                return fail(
                    f"{stem}: rejected but not with {expected_code} / {marker!r}; "
                    f"got: {combined.strip()}")
            if "inline-input-frame expansion" not in combined:
                return fail(
                    f"{stem}: rejection does not name the deferred inline-input-frame "
                    f"expansion; got: {combined.strip()}")
            if wasm.exists():
                return fail(f"{stem}: a wasm artifact was written despite the reject")

    print("V2-A input-inline aggregate fail-closed compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
