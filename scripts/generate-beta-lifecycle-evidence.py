#!/usr/bin/env python3
"""Generate BETA-04 lifecycle evidence from runtime tests and event taxonomy."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from ahfl_source_revision import compute_source_revision


REQUIRED_LIFECYCLES = [
    "success",
    "failure",
    "dependency_skip",
    "retry",
    "fallback",
    "cancellation",
    "budget_rejection",
    "capability_timeout",
    "process_interruption",
    "checkpoint",
    "resume",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def require_source_markers(path: Path, markers: dict[str, list[str]]) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    results: dict[str, str] = {}
    for lifecycle, required in markers.items():
        missing = [marker for marker in required if marker not in text]
        if missing:
            raise RuntimeError(
                f"{lifecycle} lifecycle lacks source markers in {path}: {missing}"
            )
        results[lifecycle] = "passed"
    return results


def main() -> int:
    args = parse_args()
    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    output = args.out.resolve()

    tests = run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "--output-on-failure",
            "-R",
            "^ahfl\\.runtime\\.(workflow_runtime_all|execution_event_all|execution_projection_all)$",
        ],
        repo,
    )
    if tests.returncode != 0:
        raise RuntimeError(tests.stdout + tests.stderr)

    workflow_test = repo / "tests/unit/runtime/engine/workflow_runtime.cpp"
    matrix = require_source_markers(
        workflow_test,
        {
            "success": ["test_single_node_workflow", "NodeReportStatus::Completed"],
            "failure": ["test_node_failure_propagation", "NodeReportStatus::Failed"],
            "dependency_skip": [
                "test_node_failure_propagation",
                "NodeReportStatus::Skipped",
            ],
            "retry": [
                "test_retry_and_fallback_emit_paired_attempt_events",
                "CapabilityRetryScheduled",
            ],
            "fallback": [
                "test_retry_and_fallback_emit_paired_attempt_events",
                "ProviderDegraded",
            ],
            "cancellation": [
                "test_cancellation_and_interruption_terminalize_scheduled_nodes",
                "RunTerminalStatus::Cancelled",
            ],
            "budget_rejection": [
                "test_budget_rejection_is_classified_in_terminal_events",
                "CapabilityFailureKind::BudgetRejected",
                "NodeFailureKind::BudgetRejected",
            ],
            "capability_timeout": [
                "test_node_input_capability_failure_fails_workflow_with_diagnostic",
                "CapabilityCallStatus::Timeout",
            ],
            "process_interruption": [
                "test_cancellation_and_interruption_terminalize_scheduled_nodes",
                "RunTerminalStatus::Interrupted",
            ],
            "checkpoint": [
                "test_checkpoint_and_resume_events_share_run_identity",
                "CheckpointSaved",
            ],
            "resume": [
                "test_checkpoint_and_resume_events_share_run_identity",
                "RunResumed",
            ],
        },
    )
    if sorted(matrix) != sorted(REQUIRED_LIFECYCLES):
        raise RuntimeError("lifecycle matrix does not cover the accepted RFC 0012 taxonomy")

    validator = (repo / "src/runtime/engine/execution_event.cpp").read_text(encoding="utf-8")
    for invariant in (
        "MissingTerminal",
        "DuplicateTerminal",
        "TerminalWithoutStart",
    ):
        if invariant not in validator:
            raise RuntimeError(f"terminal validator does not enforce {invariant}")

    value = {
        "schema": "ahfl.beta-evidence.lifecycle-matrix.v1",
        "status": "passed",
        "criterion": "BETA-04",
        "source_revision": compute_source_revision(repo),
        "lifecycles": matrix,
        "terminal_invariants": [
            "missing_terminal",
            "duplicate_terminal",
            "terminal_without_start",
        ],
        "test_command": (
            "ctest --test-dir "
            + str(build)
            + " --output-on-failure -R "
            + "'^ahfl\\.runtime\\.(workflow_runtime_all|execution_event_all|execution_projection_all)$'"
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("beta lifecycle evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
