#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-D fix-forward fail-closed gate.

A workflow node whose packaged agent's ONLY computed content is a SCALAR
computed-goto preamble feeding an opaque capability final
(v2d_computed_goto_preamble_reject.ahfl) must NOT be packaged on the opaque
workflow-runner lane: that runner is a static GotoAction walk, so the
ComputedGotoAction state becomes its terminal and the emitted module completes
the node through the identity arm WITHOUT EVER INVOKING THE CAPABILITY
(the ahfl_cap import is present but dead, and run2 forges an
un-capability-called result). Pre-fix the producer emitted a well-formed,
byte-gate-passing module.

Until V2-D computed-runner module emission lands the producer must fail
closed:
  * exit non-zero;
  * stderr carries wasm.UNSUPPORTED_WORKFLOW_FRAME;
  * no wasm artifact is written.

Positive control: the baseline bare-goto capability workflow
(e3_capability_workflow.ahfl, Start -> Done{return Echo(input)}) still emits
with exactly one ahfl_cap import, so the fail-closed predicate did not sweep
the opaque capability lane.

Engine-independent (no Node needed): the gate is the reject plus the
baseline import count.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

REJECT_FIXTURE = "v2d_computed_goto_preamble_reject.ahfl"
BASELINE_FIXTURE = "e3_capability_workflow.ahfl"
EXPECTED_CODE = "wasm.UNSUPPORTED_WORKFLOW_FRAME"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_v2d_computed_goto_preamble_reject.py <producer>")
    producer = Path(argv[0])
    if not producer.is_file():
        return fail("missing capability-workflow producer")
    fixtures = Path(__file__).resolve().parent.parent / "golden" / "wasm"

    reject_source = fixtures / REJECT_FIXTURE
    if not reject_source.is_file():
        return fail(f"missing negative fixture {REJECT_FIXTURE}")
    baseline_source = fixtures / BASELINE_FIXTURE
    if not baseline_source.is_file():
        return fail(f"missing baseline fixture {BASELINE_FIXTURE}")

    with tempfile.TemporaryDirectory(prefix="ahfl-v2d-preamble-") as td:
        scratch = Path(td)

        rejected = scratch / "must_not_exist.wasm"
        run = subprocess.run(
            [str(producer), str(reject_source), str(rejected)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode == 0:
            return fail(
                "a scalar computed-goto preamble feeding a capability final "
                "emitted a workflow module (opaque runner would forge an "
                "un-capability-called result)"
            )
        if EXPECTED_CODE not in run.stderr:
            return fail(
                f"expected {EXPECTED_CODE} rejection, got stderr: {run.stderr!r}"
            )
        if rejected.exists():
            return fail("the rejected workflow must not write a wasm artifact")

        # Positive control: the bare-goto opaque capability workflow still
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

    print("V2-D scalar computed-goto preamble fail-closed compile-reject lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
