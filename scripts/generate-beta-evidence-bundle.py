#!/usr/bin/env python3
"""Regenerate the complete AHFL beta evidence bundle and require readiness."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from ahfl_source_revision import compute_source_revision


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    return parser.parse_args()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def run_checked(
    args: list[str],
    cwd: Path,
    label: str,
    env: dict[str, str] | None = None,
) -> None:
    result = subprocess.run(
        args,
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
        env=env,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} failed with {result.returncode}\n"
            f"command: {' '.join(args)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )


def validate_output_paths(repo: Path, output_dir: Path) -> None:
    contract = load_json(repo / "config/beta-gate.json")
    criteria = contract.get("criteria")
    if not isinstance(criteria, list):
        raise RuntimeError("beta gate criteria must be an array")
    expected_parent = output_dir.resolve()
    for criterion in criteria:
        if not isinstance(criterion, dict):
            raise RuntimeError("beta gate criterion must be an object")
        evidence = criterion.get("evidence")
        if not isinstance(evidence, list) or len(evidence) != 1:
            raise RuntimeError(
                f"{criterion.get('id', '<missing>')} must define exactly one evidence file"
            )
        relative = evidence[0].get("path") if isinstance(evidence[0], dict) else None
        if not isinstance(relative, str) or not relative:
            raise RuntimeError(
                f"{criterion.get('id', '<missing>')} evidence path must not be empty"
            )
        parent = (repo / relative).resolve().parent
        if parent != expected_parent:
            raise RuntimeError(
                f"{criterion.get('id', '<missing>')} evidence must live in "
                f"{output_dir.relative_to(repo)}"
            )


def generator_command(
    repo: Path,
    script: str,
    build: Path,
    output: Path,
    source_revision: str,
) -> list[str]:
    return [
        sys.executable,
        str(repo / "scripts" / script),
        "--repo-root",
        str(repo),
        "--build-dir",
        str(build),
        "--out",
        str(output),
        "--source-revision",
        source_revision,
    ]


def main() -> int:
    args = parse_args()
    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    output_dir = repo / "build/release-evidence/beta"

    # Compute the source revision exactly once and hand it to every generator
    # and the gate checker. Computing it independently in each subprocess lets
    # parallel test activity change the dirty-tree digest mid-run, which makes
    # the checker mark freshly generated evidence as stale.
    try:
        source_revision = compute_source_revision(repo)
    except RuntimeError as error:
        print(f"beta evidence bundle source revision error: {error}", file=sys.stderr)
        return 1

    validate_output_paths(repo, output_dir)
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    try:
        run_checked(
            [
                "ctest",
                f"-j{os.cpu_count() or 1}",
                "--test-dir",
                str(build),
                "--output-on-failure",
                "-R",
                "^ahflc\\.run\\.profile_and_output_contract\\.smoke$",
            ],
            repo,
            "run profiles evidence tests",
            env={**os.environ, "AHFL_SOURCE_REVISION": source_revision},
        )
        run_checked(
            [
                sys.executable,
                str(repo / "scripts/generate-beta-runtime-evidence.py"),
                "--repo-root",
                str(repo),
                "--build-dir",
                str(build),
                "--out-dir",
                str(output_dir),
                "--source-revision",
                source_revision,
            ],
            repo,
            "runtime evidence generation",
        )
        generators = (
            (
                "generate-beta-lifecycle-evidence.py",
                "lifecycle-matrix.json",
                "lifecycle evidence generation",
            ),
            (
                "generate-beta-formatter-evidence.py",
                "formatter-idempotence.json",
                "formatter evidence generation",
            ),
            (
                "generate-beta-stdlib-container-evidence.py",
                "stdlib-container-migration.json",
                "stdlib container evidence generation",
            ),
            (
                "generate-beta-install-evidence.py",
                "install-smoke.json",
                "install evidence generation",
            ),
        )
        for script, filename, label in generators:
            run_checked(
                generator_command(
                    repo, script, build, output_dir / filename, source_revision
                ),
                repo,
                label,
            )
        run_checked(
            [
                sys.executable,
                str(repo / "scripts/check-readme-capabilities.py"),
                "--root",
                str(repo),
                "--out",
                str(output_dir / "readme-capabilities.json"),
                "--source-revision",
                source_revision,
            ],
            repo,
            "README capability evidence generation",
        )
    except RuntimeError as error:
        print(f"beta evidence bundle error: {error}", file=sys.stderr)
        return 1

    checker = subprocess.run(
        [
            sys.executable,
            str(repo / "scripts/check-beta-gate.py"),
            "--root",
            str(repo),
            "--require-ready",
            "--source-revision",
            source_revision,
        ],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    if checker.stdout:
        print(checker.stdout, end="")
    if checker.stderr:
        print(checker.stderr, end="", file=sys.stderr)
    return checker.returncode


if __name__ == "__main__":
    raise SystemExit(main())
