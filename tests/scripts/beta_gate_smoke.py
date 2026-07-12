#!/usr/bin/env python3

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_CRITERIA = {f"BETA-{index:02d}" for index in range(1, 11)}


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
        ["git", "add", "config/beta-gate.json", ".gitignore", "tracked.txt"],
        ["git", "commit", "--no-verify", "-qm", "test: initialize beta gate fixture"],
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


def main() -> int:
    require(len(sys.argv) == 3, "usage: beta_gate_smoke.py <checker> <repo-root>")
    checker = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve()
    require(checker.exists(), f"beta gate checker does not exist: {checker}")

    with tempfile.TemporaryDirectory(prefix="ahfl-beta-gate-") as temp_dir:
        root = Path(temp_dir)
        (root / "config").mkdir(parents=True)
        shutil.copy2(source_root / "config/beta-gate.json", root / "config/beta-gate.json")
        revision = initialize_repository(root)

        result = run_checker(checker, root)
        require(result.returncode == 0, f"contract inspection must succeed:\n{result.stderr}")
        report = json.loads(result.stdout)
        require(report["schema"] == "ahfl.beta-gate-report.v1", "unexpected report schema")
        require(report["status"] == "not_ready", "missing evidence must report not_ready")
        require(
            {criterion["id"] for criterion in report["criteria"]} == EXPECTED_CRITERIA,
            "report must cover all ten beta criteria",
        )
        require(
            all(criterion["status"] == "missing_evidence" for criterion in report["criteria"]),
            "empty evidence root must not claim passed criteria",
        )

        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "--require-ready must reject missing evidence")

        config = json.loads((root / "config/beta-gate.json").read_text(encoding="utf-8"))
        for criterion in config["criteria"]:
            for evidence in criterion["evidence"]:
                evidence_path = root / evidence["path"]
                evidence_path.parent.mkdir(parents=True, exist_ok=True)
                evidence_path.write_text(
                    json.dumps(
                        {
                            "schema": evidence["schema"],
                            "status": "passed",
                            "criterion": criterion["id"],
                            "source_revision": revision,
                        }
                    )
                    + "\n",
                    encoding="utf-8",
                )

        result = run_checker(checker, root, "--require-ready")
        require(result.returncode == 0, f"complete valid evidence must pass:\n{result.stderr}")
        report = json.loads(result.stdout)
        require(report["status"] == "ready", "complete evidence must report ready")
        require(
            all(criterion["status"] == "passed" for criterion in report["criteria"]),
            "ready report must contain only passed criteria",
        )

        second_evidence = root / config["criteria"][1]["evidence"][0]["path"]
        mismatched = json.loads(second_evidence.read_text(encoding="utf-8"))
        mismatched["source_revision"] = "different-test-revision"
        second_evidence.write_text(json.dumps(mismatched) + "\n", encoding="utf-8")
        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "mixed evidence revisions must fail")
        report = json.loads(result.stdout)
        require(report["status"] == "not_ready", "revision drift must report not_ready")
        require(
            report["criteria"][1]["status"] == "failed",
            "revision drift must identify the mismatched criterion",
        )
        mismatched["source_revision"] = revision
        second_evidence.write_text(json.dumps(mismatched) + "\n", encoding="utf-8")

        (root / "tracked.txt").write_text("drift\n", encoding="utf-8")
        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "stale evidence must fail after source drift")
        report = json.loads(result.stdout)
        require(report["status"] == "not_ready", "source drift must report not_ready")
        require(
            report["source_revision"] == revision,
            "report must retain the evidence bundle revision",
        )
        require(
            report["current_source_revision"] != revision,
            "report must expose the current dirty source revision",
        )
        (root / "tracked.txt").write_text("baseline\n", encoding="utf-8")

        first_evidence = root / config["criteria"][0]["evidence"][0]["path"]
        bad = json.loads(first_evidence.read_text(encoding="utf-8"))
        bad["schema"] = "wrong.schema"
        first_evidence.write_text(json.dumps(bad) + "\n", encoding="utf-8")
        result = run_checker(checker, root, "--require-ready")
        require(result.returncode != 0, "wrong evidence schema must fail")
        report = json.loads(result.stdout)
        require(report["criteria"][0]["status"] == "failed", "schema drift must report failed")

        config["criteria"] = config["criteria"][:-1]
        (root / "config/beta-gate.json").write_text(
            json.dumps(config, indent=2) + "\n", encoding="utf-8"
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "missing beta criterion must invalidate the contract")
        require("BETA-10" in result.stderr, "contract error must name the missing criterion")

    print("beta gate smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
