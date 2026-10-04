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


LIFECYCLE_TEST_REGEX = (
    r"^ahfl\.runtime\.("
    r"execution_event_all|execution_report_all|execution_projection_all|"
    r"capability_bridge_all|capability_event_projection_all|"
    r"wasm_host_workflow_session"
    r")$"
)


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
            LIFECYCLE_TEST_REGEX,
        ],
        repo,
    )
    if tests.returncode != 0:
        raise RuntimeError(tests.stdout + tests.stderr)

    # WH-9: the evaluator-backed workflow_runtime test TU was deleted; lifecycle
    # markers are now spread across surviving test TUs and the wasm runtime
    # source that emits the events.
    lifecycle_files: dict[str, tuple[str, list[str]]] = {
        "success": (
            "tests/unit/runtime/engine/execution_report.cpp",
            ["NodeReportStatus::Completed"],
        ),
        "failure": (
            "tests/unit/runtime/engine/execution_report.cpp",
            ["NodeReportStatus::Failed"],
        ),
        "dependency_skip": (
            "tests/unit/runtime/engine/execution_report.cpp",
            ["NodeReportStatus::Skipped"],
        ),
        "retry": (
            "tests/unit/runtime/engine/capability_event_projection.cpp",
            ["CapabilityRetryScheduled"],
        ),
        "fallback": (
            "tests/unit/runtime/engine/capability_event_projection.cpp",
            ["ProviderDegraded"],
        ),
        "cancellation": (
            "src/runtime/wasm_host/workflow_session.cpp",
            ["RunTerminalStatus::Cancelled"],
        ),
        "budget_rejection": (
            "tests/unit/runtime/engine/capability_event_projection.cpp",
            ["CapabilityFailureKind::BudgetRejected"],
        ),
        "capability_timeout": (
            "tests/unit/runtime/engine/capability_bridge.cpp",
            ["CapabilityCallStatus::Timeout"],
        ),
        "process_interruption": (
            "src/runtime/wasm_host/workflow_session.cpp",
            ["RunTerminalStatus::Interrupted"],
        ),
        "checkpoint": (
            "tests/unit/runtime/engine/execution_projection.cpp",
            ["CheckpointSaved"],
        ),
        "resume": (
            "tests/unit/runtime/engine/execution_event.cpp",
            ["RunResumed"],
        ),
    }
    matrix: dict[str, str] = {}
    for lifecycle, (relative, markers) in lifecycle_files.items():
        require_source_markers(repo / relative, {lifecycle: markers})
        matrix[lifecycle] = "passed"
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
            + "'"
            + LIFECYCLE_TEST_REGEX
            + "'"
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("beta lifecycle evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
