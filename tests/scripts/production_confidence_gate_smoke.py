#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(checker: Path, root: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), "--root", str(root), *extra],
        check=False,
        capture_output=True,
        text=True,
    )


def initialize_repository(root: Path) -> str:
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    (root / "tracked.txt").write_text("baseline\n", encoding="utf-8")
    template = root / "empty-template"
    template.mkdir()
    commands = (
        ["git", "init", "-q", f"--template={template}"],
        ["git", "config", "user.name", "AHFL Test"],
        ["git", "config", "user.email", "ahfl-test@example.invalid"],
        ["git", "add", "."],
        ["git", "commit", "--no-verify", "-qm", "test: initialize production confidence fixture"],
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
    (root / "config").mkdir(parents=True)
    (root / "config/production-confidence-gate.json").write_text(
        json.dumps(
            {
                "schema": "ahfl.production-confidence-gate.v1",
                "evidence": {
                    "path": "build/release-evidence/production-confidence/hour-scale-soak.json",
                    "schema": "ahfl.production-confidence-soak.v1",
                },
                "minimum_duration_seconds": 3600,
                "minimum_iterations": 100,
                "maximum_last_quartile_growth_ratio": 0.20,
            }
        )
        + "\n",
        encoding="utf-8",
    )


def metric() -> dict[str, object]:
    return {
        "samples": 100,
        "minimum_bytes": 1000,
        "maximum_bytes": 1200,
        "mean_bytes": 1100.0,
        "slope_bytes_per_iteration": 1.0,
        "first_quartile_mean_bytes": 1000.0,
        "last_quartile_mean_bytes": 1100.0,
    }


def evidence(revision: str) -> dict[str, object]:
    return {
        "schema": "ahfl.production-confidence-soak.v1",
        "status": "passed",
        "source_revision": revision,
        "kind": "hour-scale",
        "process_model": "single-long-lived-worker",
        "reference_workflow": "examples/execution-demo",
        "duration_seconds": 3600.5,
        "minimum_duration_seconds": 3600,
        "iterations": 100,
        "minimum_iterations": 100,
        "stable_event_count": 25,
        "provider_request_count": 100,
        "throughput_runs_per_second": 100 / 3600.5,
        "latency_seconds": {"minimum": 0.1, "maximum": 0.3, "mean": 0.2},
        "peak_rss": metric(),
        "allocator_in_use": metric(),
        "allocator_reserved": metric(),
    }


def main() -> int:
    require(
        len(sys.argv) == 2,
        "usage: production_confidence_gate_smoke.py <checker>",
    )
    checker = Path(sys.argv[1]).resolve()
    require(checker.is_file(), f"production confidence checker missing: {checker}")

    with tempfile.TemporaryDirectory(prefix="ahfl-production-confidence-") as temp_dir:
        root = Path(temp_dir)
        write_contract(root)
        revision = initialize_repository(root)

        result = run(checker, root)
        require(result.returncode == 0, result.stderr)
        report = json.loads(result.stdout)
        require(report["status"] == "missing_evidence", "missing evidence status lost")
        require(
            run(checker, root, "--require-ready").returncode != 0,
            "ready gate accepted missing evidence",
        )

        path = root / "build/release-evidence/production-confidence/hour-scale-soak.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(evidence(revision)) + "\n", encoding="utf-8")
        result = run(checker, root, "--require-ready")
        require(result.returncode == 0, result.stdout + result.stderr)
        require(json.loads(result.stdout)["status"] == "ready", "valid evidence not ready")

        mutations = (
            ("short duration", lambda value: value.update(duration_seconds=3599)),
            ("few iterations", lambda value: value.update(iterations=99)),
            ("wrong request count", lambda value: value.update(provider_request_count=99)),
            ("event count zero", lambda value: value.update(stable_event_count=0)),
            (
                "wrong process model",
                lambda value: value.update(process_model="one-process-per-iteration"),
            ),
            (
                "rss growth",
                lambda value: value["peak_rss"].update(last_quartile_mean_bytes=1300.0),
            ),
            (
                "allocator growth",
                lambda value: value["allocator_in_use"].update(
                    last_quartile_mean_bytes=1300.0
                ),
            ),
        )
        for label, mutate in mutations:
            value = evidence(revision)
            mutate(value)
            path.write_text(json.dumps(value) + "\n", encoding="utf-8")
            result = run(checker, root, "--require-ready")
            require(result.returncode != 0, f"gate accepted {label}")
            require(json.loads(result.stdout)["status"] == "failed", f"{label} not failed")

        path.write_text(json.dumps(evidence(revision)) + "\n", encoding="utf-8")
        (root / "tracked.txt").write_text("drift\n", encoding="utf-8")
        result = run(checker, root, "--require-ready")
        require(result.returncode != 0, "gate accepted stale evidence")
        require(
            any("stale" in failure for failure in json.loads(result.stdout)["failures"]),
            "stale evidence failure not explained",
        )

    print("production confidence gate smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
