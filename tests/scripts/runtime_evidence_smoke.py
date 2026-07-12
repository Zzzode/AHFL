#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_PROJECTIONS = {
    "human",
    "json",
    "jsonl",
    "quiet",
    "replay",
    "audit",
    "scheduler",
    "checkpoint",
    "otel",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(
        len(sys.argv) == 4,
        "usage: runtime_evidence_smoke.py <generator> <repo-root> <build-dir>",
    )
    generator = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    build_dir = Path(sys.argv[3]).resolve()

    with tempfile.TemporaryDirectory(prefix="ahfl-runtime-evidence-") as temp_dir:
        out_dir = Path(temp_dir)
        result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--repo-root",
                str(repo),
                "--build-dir",
                str(build_dir),
                "--out-dir",
                str(out_dir),
            ],
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
        )
        require(
            result.returncode == 0,
            f"runtime evidence generation failed:\n{result.stdout}\n{result.stderr}",
        )

        value = json.loads((out_dir / "event-projections.json").read_text())
        require(value["status"] == "passed", "runtime projection evidence did not pass")
        require(
            set(value["projections"]) == EXPECTED_PROJECTIONS,
            "runtime evidence does not cover every canonical event projection",
        )
        require(
            value["scheduler_projection"] == "ExecutionSchedulerProjection",
            "scheduler projection identity missing",
        )
        require(
            value["checkpoint_projection"] == "ExecutionCheckpointProjection",
            "checkpoint projection identity missing",
        )
        require(
            value["otel_projection"] == "ExecutionOtelTrace",
            "OTel projection identity missing",
        )
        require(
            value["recovery_snapshot"] == "WorkflowRecoverySnapshot",
            "recovery materialization identity missing",
        )
        require(
            set(value["canonical_provider_facts"])
            == {
                "usage",
                "cache_outcome",
                "provider_degradation",
                "policy_notices",
                "ranged_diagnostics",
            },
            "provider runtime facts are not fully event-native",
        )
        required_tests = set(value["projection_tests"])
        require(
            {
                "ahfl.runtime.execution_projection_all",
                "ahfl.runtime.execution_otel_all",
                "ahfl.runtime.workflow_recovery_all",
                "ahflc.run.llm_provider_runtime.smoke",
            }
            <= required_tests,
            "projection evidence omits scheduler/checkpoint, OTel, or recovery tests",
        )

    print("runtime evidence smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
