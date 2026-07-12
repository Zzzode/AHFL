#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REQUIRED_FAULTS = ("disconnect", "rate_limit", "timeout", "partial_response")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_checker(
    checker: Path, root: Path, *extra: str
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), "--root", str(root), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def initialize_repository(root: Path) -> str:
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    (root / "tracked.txt").write_text("baseline\n", encoding="utf-8")
    empty_template = root / "empty-git-template"
    empty_template.mkdir()
    commands = (
        ["git", "init", "-q", f"--template={empty_template}"],
        ["git", "config", "user.name", "AHFL Test"],
        ["git", "config", "user.email", "ahfl-test@example.invalid"],
        [
            "git",
            "add",
            "config/controlled-pilot-gate.json",
            ".gitignore",
            "tracked.txt",
        ],
        [
            "git",
            "commit",
            "--no-verify",
            "-qm",
            "test: initialize controlled-pilot gate fixture",
        ],
    )
    for command in commands:
        subprocess.run(command, cwd=root, check=True, capture_output=True, text=True)
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def write_contract(root: Path) -> None:
    config = root / "config"
    config.mkdir(parents=True)
    (config / "controlled-pilot-gate.json").write_text(
        json.dumps(
            {
                "schema": "ahfl.controlled-pilot-gate.v1",
                "reference_workflow": "examples/execution-demo",
                "evidence": {
                    "path": (
                        "build/release-evidence/pilot/"
                        "reference-workflow-production-matrix.json"
                    ),
                    "schema": "ahfl.controlled-pilot-evidence.v1",
                },
                "minimums": {
                    "soak_kind": "bounded-ci-soak",
                    "iterations": 10,
                    "duration_seconds": 10,
                },
                "required_faults": list(REQUIRED_FAULTS),
                "required_tests": {
                    "process_crash": "ahfl.reference_workflow.recovery_smoke",
                    "recovery_schema": "ahfl.runtime.workflow_recovery_all",
                    "otel_adapter": "ahfl.runtime.execution_otel_all",
                    "provider_budget": "ahflc.run.llm_provider_runtime.smoke",
                },
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def valid_evidence(revision: str) -> dict[str, object]:
    return {
        "schema": "ahfl.controlled-pilot-evidence.v1",
        "status": "passed",
        "source_revision": revision,
        "reference_workflow": "examples/execution-demo",
        "soak": {
            "kind": "bounded-ci-soak",
            "iterations": 12,
            "minimum_iterations": 10,
            "minimum_duration_seconds": 10,
            "duration_seconds": 10.5,
            "stable_event_count": 24,
            "provider_request_count": 12,
        },
        "network_faults": {
            fault: {"status": "passed", "request_count": 1}
            for fault in REQUIRED_FAULTS
        },
        "process_crash_test": "ahfl.reference_workflow.recovery_smoke",
        "recovery_schema_test": "ahfl.runtime.workflow_recovery_all",
        "recovery_schema_policy": "reject unknown and legacy schemas",
        "otel_adapter_test": "ahfl.runtime.execution_otel_all",
        "provider_budget_test": "ahflc.run.llm_provider_runtime.smoke",
    }


def write_evidence(root: Path, value: dict[str, object]) -> Path:
    path = (
        root
        / "build/release-evidence/pilot/reference-workflow-production-matrix.json"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> int:
    require(
        len(sys.argv) == 2,
        "usage: controlled_pilot_gate_smoke.py <controlled-pilot-checker>",
    )
    checker = Path(sys.argv[1]).resolve()
    require(checker.exists(), f"controlled-pilot checker does not exist: {checker}")

    with tempfile.TemporaryDirectory(prefix="ahfl-controlled-pilot-") as temp_dir:
        root = Path(temp_dir)
        write_contract(root)
        revision = initialize_repository(root)

        result = run_checker(checker, root)
        require(result.returncode == 0, f"contract inspection failed:\n{result.stderr}")
        report = json.loads(result.stdout)
        require(
            report["schema"] == "ahfl.controlled-pilot-gate-report.v1",
            "unexpected gate report schema",
        )
        require(report["status"] == "missing_evidence", "missing evidence status lost")

        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "ready gate accepted missing evidence")

        evidence_path = write_evidence(root, valid_evidence(revision))
        result = run_checker(checker, root, "--require-ready")
        require(result.returncode == 0, f"valid evidence rejected:\n{result.stderr}")
        report = json.loads(result.stdout)
        require(report["status"] == "ready", "valid evidence did not become ready")

        (root / "tracked.txt").write_text("drift\n", encoding="utf-8")
        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "stale controlled-pilot evidence must fail")
        report = json.loads(result.stdout)
        require(report["status"] == "failed", "source drift must report failed")
        require(
            any("stale" in failure for failure in report["failures"]),
            "source drift failure must explain stale evidence",
        )
        require(
            report["source_revision"] == revision,
            "report must retain the evidence revision",
        )
        require(
            report["current_source_revision"] != revision,
            "report must expose the current dirty source revision",
        )
        (root / "tracked.txt").write_text("baseline\n", encoding="utf-8")

        mutations = (
            ("wrong schema", lambda value: value.update(schema="wrong.schema")),
            ("non-passing status", lambda value: value.update(status="failed")),
            (
                "short soak",
                lambda value: value["soak"].update(iterations=9),
            ),
            (
                "unstable provider count",
                lambda value: value["soak"].update(provider_request_count=11),
            ),
            (
                "missing fault",
                lambda value: value["network_faults"].pop("partial_response"),
            ),
            (
                "failed fault",
                lambda value: value["network_faults"]["timeout"].update(status="failed"),
            ),
            (
                "wrong recovery schema policy",
                lambda value: value.update(recovery_schema_policy="migrate silently"),
            ),
            (
                "wrong OTel test",
                lambda value: value.update(otel_adapter_test="not-the-otel-test"),
            ),
            (
                "wrong provider budget test",
                lambda value: value.update(
                    provider_budget_test="not-the-provider-budget-test"
                ),
            ),
        )
        for label, mutate in mutations:
            value = valid_evidence(revision)
            mutate(value)
            evidence_path.write_text(
                json.dumps(value, indent=2) + "\n", encoding="utf-8"
            )
            result = run_checker(checker, root, "--require-ready")
            require(result.returncode != 0, f"gate accepted {label}")
            report = json.loads(result.stdout)
            require(report["status"] == "failed", f"{label} was not reported failed")
            require(report["failures"], f"{label} did not explain its failure")

    print("controlled-pilot gate smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
