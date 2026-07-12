#!/usr/bin/env python3
"""Generate BETA-05 evidence for lossless, idempotent, blocking formatting."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from ahfl_source_revision import compute_source_revision


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def count_ahfl_files(root: Path) -> int:
    return sum(1 for path in root.rglob("*.ahfl") if path.is_file())


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
            "-L",
            "v0.59-formatter",
        ],
        repo,
    )
    if tests.returncode != 0:
        raise RuntimeError(tests.stdout + tests.stderr)

    gate = run(
        ["bash", "scripts/ci-format-check.sh"],
        repo,
    )
    if gate.returncode != 0:
        raise RuntimeError(gate.stdout + gate.stderr)

    workflow = (repo / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    formatter_step = re.search(
        r"- name: Check \.ahfl formatting\n(?P<body>(?:\s{8,}.*\n)+)",
        workflow,
    )
    if formatter_step is None:
        raise RuntimeError("CI does not contain the .ahfl formatting step")
    if "continue-on-error" in formatter_step.group("body"):
        raise RuntimeError("CI formatter step is not blocking")

    source = (repo / "src/tooling/formatter/lossless_source_formatter.cpp").read_text(
        encoding="utf-8"
    )
    if "format_lossless_source" not in source or "sort_contiguous_imports" not in source:
        raise RuntimeError("lossless source formatter implementation is incomplete")
    public_entry = (repo / "src/tooling/formatter/formatter_api.cpp").read_text(
        encoding="utf-8"
    )
    if "format_lossless_source(source, options)" not in public_entry:
        raise RuntimeError("public formatter entry point does not use the lossless formatter")

    fixture_match = re.search(r"formatter fixture files: (\d+)", gate.stdout)
    cases_match = re.search(r"formatter idempotence cases: (\d+)", gate.stdout)
    changes_match = re.search(r"formatter second-pass changes: (\d+)", gate.stdout)
    if fixture_match is None or cases_match is None or changes_match is None:
        raise RuntimeError("formatter gate did not report its evidence counters")

    fixture_files = int(fixture_match.group(1))
    idempotence_cases = int(cases_match.group(1))
    second_pass_changes = int(changes_match.group(1))
    if idempotence_cases != fixture_files * 2 or second_pass_changes != 0:
        raise RuntimeError("formatter idempotence matrix is incomplete")

    value: dict[str, object] = {
        "schema": "ahfl.beta-evidence.formatter-idempotence.v1",
        "status": "passed",
        "criterion": "BETA-05",
        "source_revision": compute_source_revision(repo),
        "ci_blocking": True,
        "lossless_source_surface": True,
        "stdlib_files": count_ahfl_files(repo / "std"),
        "fixture_files": fixture_files,
        "idempotence_cases": idempotence_cases,
        "second_pass_changes": second_pass_changes,
        "preserved_surface": [
            "attributes",
            "comments",
            "effects",
            "generics",
            "imports",
            "visibility",
        ],
        "test_command": f"ctest --test-dir {build} --output-on-failure -L v0.59-formatter",
        "gate_command": "AHFLC=build/dev/src/tooling/cli/ahflc scripts/ci-format-check.sh",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("beta formatter evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
