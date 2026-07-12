#!/usr/bin/env python3

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_checker(checker: Path, root: Path, out: Path | None = None) -> subprocess.CompletedProcess[str]:
    args = [sys.executable, str(checker), "--root", str(root)]
    if out is not None:
        args.extend(["--out", str(out)])
    return subprocess.run(args, cwd=root, check=False, capture_output=True, text=True)


def initialize_repository(root: Path) -> str:
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    template = root / "empty-template"
    template.mkdir()
    commands = (
        ["git", "init", "-q", f"--template={template}"],
        ["git", "config", "user.name", "AHFL Test"],
        ["git", "config", "user.email", "ahfl-test@example.invalid"],
        ["git", "add", "."],
        ["git", "commit", "--no-verify", "-qm", "test: initialize README fixture"],
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


def copy_inputs(source: Path, target: Path) -> None:
    for relative in (
        "README.md",
        "README.zh.md",
        "config/beta-gate.json",
        "config/readme-capabilities.json",
    ):
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / relative, destination)
    gate = json.loads((source / "config/beta-gate.json").read_text())
    evidence_entries: list[tuple[Path, str, str]] = []
    for criterion in gate["criteria"]:
        if criterion["id"] == "BETA-09":
            continue
        for evidence in criterion["evidence"]:
            destination = target / evidence["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            evidence_entries.append(
                (destination, evidence["schema"], criterion["id"])
            )
    revision = initialize_repository(target)
    for destination, schema, criterion in evidence_entries:
        destination.write_text(
            json.dumps(
                {
                    "schema": schema,
                    "criterion": criterion,
                    "status": "passed",
                    "source_revision": revision,
                }
            )
            + "\n"
        )


def main() -> int:
    require(
        len(sys.argv) == 4,
        "usage: readme_capabilities_smoke.py <checker> <repo-root> <build-dir>",
    )
    checker = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    build = Path(sys.argv[3]).resolve()
    require(checker.is_file(), f"README capability checker missing: {checker}")

    with tempfile.TemporaryDirectory(prefix="ahfl-readme-capabilities-") as temp_dir:
        root = Path(temp_dir)
        copy_inputs(source, root)
        evidence = root / "build/release-evidence/beta/readme-capabilities.json"

        result = run_checker(checker, root, evidence)
        require(result.returncode == 0, f"valid READMEs must pass:\n{result.stderr}")
        value = json.loads(evidence.read_text())
        require(
            value["schema"] == "ahfl.beta-evidence.readme-capabilities.v1",
            "unexpected README evidence schema",
        )
        require(value["criterion"] == "BETA-09", "README evidence criterion mismatch")
        require(value["status"] == "passed", "valid README evidence must pass")
        require(value["covered_criteria"] == [f"BETA-{index:02d}" for index in range(1, 9)] + ["BETA-10"],
                "README evidence coverage mismatch")

        readme = root / "README.md"
        text = readme.read_text()
        readme.write_text(text.replace("<!-- beta-capability:BETA-07 ", "<!-- removed:BETA-07 ", 1))
        result = run_checker(checker, root)
        require(result.returncode != 0, "missing evidence marker must fail")
        require("BETA-07" in result.stderr, "missing marker failure must name BETA-07")
        shutil.copy2(source / "README.md", readme)

        chinese = root / "README.zh.md"
        chinese.write_text(chinese.read_text().replace("BETA-08", "BETA-88", 1))
        result = run_checker(checker, root)
        require(result.returncode != 0, "English/Chinese capability drift must fail")
        require("BETA-08" in result.stderr, "language drift failure must name BETA-08")
        shutil.copy2(source / "README.zh.md", chinese)

        readme.write_text(
            readme.read_text()
            + "\nAHFL is production-ready for native gRPC and multi-region scheduling.\n"
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "unverified production-ready claim must fail")
        require("production-ready" in result.stderr, "claim failure must identify wording")

    print("README capability smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
