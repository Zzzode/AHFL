#!/usr/bin/env python3
"""RFC 0026 dual-track elimination: computed-goto preamble capability final.

A workflow node whose packaged agent's ONLY computed content is a SCALAR
computed-goto preamble feeding a capability final
(computed_goto_preamble_reject.ahfl) is now packaged on the P6 bridge
runner lane. The P6 bridge runner is a full state-machine interpreter, so the
ComputedGotoAction state dispatches to the capability final and the bridge
import is invoked. Pre-elimination the opaque workflow runner (a static
GotoAction walk) would have treated the ComputedGotoAction state as its
terminal and completed the node through the identity arm WITHOUT EVER
INVOKING THE CAPABILITY, so the producer fail-closed rejected this shape.
With the opaque lane gone, the emission succeeds.

Positive control: the baseline bare-goto capability workflow
(e3_capability_workflow.ahfl, Start -> Done{return Echo(input)}) still emits
with exactly one ahfl_cap import.

Engine-independent (no Node needed): the gate is the successful emission plus
the baseline import count.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

FORMERLY_REJECT_FIXTURE = "computed_goto_preamble_reject.ahfl"
BASELINE_FIXTURE = "e3_capability_workflow.ahfl"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_computed_goto_preamble_reject.py <producer>")
    producer = Path(argv[0])
    if not producer.is_file():
        return fail("missing capability-workflow producer")
    fixtures = Path(__file__).resolve().parent.parent / "golden" / "wasm"

    source = fixtures / FORMERLY_REJECT_FIXTURE
    if not source.is_file():
        return fail(f"missing fixture {FORMERLY_REJECT_FIXTURE}")
    baseline_source = fixtures / BASELINE_FIXTURE
    if not baseline_source.is_file():
        return fail(f"missing baseline fixture {BASELINE_FIXTURE}")

    with tempfile.TemporaryDirectory(prefix="ahfl-v2d-preamble-") as td:
        scratch = Path(td)

        # The computed-goto preamble feeding a capability final now emits
        # successfully on the P6 bridge runner lane.
        emitted = scratch / "computed_goto_preamble.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(emitted)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode != 0:
            return fail(
                "a scalar computed-goto preamble feeding a capability final "
                f"stopped emitting: {run.stderr!r}"
            )
        if not emitted.is_file():
            return fail("the computed-goto preamble workflow wrote no wasm artifact")

        # Positive control: the bare-goto capability workflow still
        # emits with exactly one capability import.
        baseline = scratch / "baseline.wasm"
        base_run = subprocess.run(
            [str(producer), str(baseline_source), str(baseline)],
            capture_output=True, text=True, timeout=60,
        )
        if base_run.returncode != 0:
            return fail(
                "the baseline bare-goto capability workflow stopped emitting: "
                f"{base_run.stderr!r}"
            )
        if "import_count=1" not in base_run.stdout:
            return fail(
                "the baseline capability workflow must report import_count=1, "
                f"got stdout: {base_run.stdout!r}"
            )
        if not baseline.is_file():
            return fail("the baseline capability workflow wrote no wasm artifact")

    print("workflow-frame scalar computed-goto preamble P6-bridge emission lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
