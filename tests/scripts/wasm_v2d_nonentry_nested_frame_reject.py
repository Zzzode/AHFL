#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-D fix-forward fail-closed gate.

A workflow whose SECOND packaged computed-final node reads a NESTED struct
field off its scheduler-materialized INPUT frame
(v2d_nonentry_nested_frame_reject.ahfl) must NOT be packaged. The in-module
scheduler materializes a non-entry I_k INLINE (every child struct/enum at a
fixed offset, no child-address slots), while a relocated handler that crosses
an aggregate edge emits an i32.load expecting the module pointer-tree form.
Only the host-packed ENTRY frame is rewritten into pointer-tree form this
rung, so the build must fail closed:
  * exit non-zero;
  * stderr carries wasm.UNSUPPORTED_WORKFLOW_FRAME;
  * no wasm artifact is written.

Positive control: a flat-field computed-final workflow
(runtime/enum_variant_e2e.ahfl, one packaged agent whose computed final reads
only top-level Int/String slots) still emits with exactly one packaged
instance, proving the gate did not sweep the V2-D computed-final lane.

Engine-independent (no Node needed): the gate is the reject plus the positive
emission.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

REJECT_FIXTURE = "v2d_nonentry_nested_frame_reject.ahfl"
POSITIVE_FIXTURE = "enum_variant_e2e.ahfl"
EXPECTED_CODE = "wasm.UNSUPPORTED_WORKFLOW_FRAME"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail(
            "usage: wasm_v2d_nonentry_nested_frame_reject.py <producer>"
        )
    producer = Path(argv[0])
    if not producer.is_file():
        return fail("missing workflow producer")
    wasm_fixtures = Path(__file__).resolve().parent.parent / "golden" / "wasm"
    runtime_fixtures = (
        Path(__file__).resolve().parent.parent / "golden" / "runtime"
    )

    reject_source = wasm_fixtures / REJECT_FIXTURE
    if not reject_source.is_file():
        return fail(f"missing negative fixture {REJECT_FIXTURE}")
    positive_source = runtime_fixtures / POSITIVE_FIXTURE
    if not positive_source.is_file():
        return fail(f"missing positive fixture {POSITIVE_FIXTURE}")

    with tempfile.TemporaryDirectory(prefix="ahfl-v2d-nonentry-") as td:
        scratch = Path(td)

        rejected = scratch / "must_not_exist.wasm"
        run = subprocess.run(
            [str(producer), str(reject_source), str(rejected)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode == 0:
            return fail(
                "a non-entry computed-final node with a nested aggregate INPUT "
                "projection emitted a workflow module (its inline I_k would be "
                "dereferenced as a pointer-tree child address)"
            )
        if EXPECTED_CODE not in run.stderr:
            return fail(
                f"expected {EXPECTED_CODE} rejection, got stderr: {run.stderr!r}"
            )
        if rejected.exists():
            return fail("the rejected workflow must not write a wasm artifact")

        # Positive control: a flat-field computed-final workflow still emits.
        positive = scratch / "positive.wasm"
        pos_run = subprocess.run(
            [str(producer), str(positive_source), str(positive)],
            capture_output=True, text=True, timeout=60,
        )
        if pos_run.returncode != 0:
            return fail(
                "a flat-field computed-final workflow stopped emitting: "
                f"{pos_run.stderr!r}"
            )
        if "packaged_instances=1" not in pos_run.stdout:
            return fail(
                "the positive workflow must package exactly one instance, "
                f"got stdout: {pos_run.stdout!r}"
            )
        if not positive.is_file():
            return fail("the positive workflow wrote no wasm artifact")

    print("V2-D non-entry inline nested-frame fail-closed lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
