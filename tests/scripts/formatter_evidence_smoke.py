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


def main() -> int:
    require(
        len(sys.argv) == 4,
        "usage: formatter_evidence_smoke.py <generator> <repo-root> <build-dir>",
    )
    generator = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    build = Path(sys.argv[3]).resolve()
    require(generator.exists(), f"formatter evidence generator does not exist: {generator}")

    with tempfile.TemporaryDirectory(prefix="ahfl-formatter-evidence-") as temp_dir:
        evidence = Path(temp_dir) / "formatter-idempotence.json"
        result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--repo-root",
                str(repo),
                "--build-dir",
                str(build),
                "--out",
                str(evidence),
            ],
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
        )
        require(
            result.returncode == 0,
            "formatter evidence generation must pass:\n"
            + result.stdout
            + result.stderr,
        )
        value = json.loads(evidence.read_text(encoding="utf-8"))
        require(
            value["schema"] == "ahfl.beta-evidence.formatter-idempotence.v1",
            "unexpected formatter evidence schema",
        )
        require(value["criterion"] == "BETA-05", "evidence must target BETA-05")
        require(value["status"] == "passed", "formatter evidence must pass")
        require(value["ci_blocking"] is True, "CI formatter gate must be blocking")
        require(value["lossless_source_surface"] is True, "formatter must be lossless")
        require(value["second_pass_changes"] == 0, "second format pass must be a no-op")
        require(value["stdlib_files"] >= 15, "evidence must cover the complete std package")
        require(value["fixture_files"] >= 3, "evidence must cover formatter fixtures")
        require(
            value["preserved_surface"]
            == ["attributes", "comments", "effects", "generics", "imports", "visibility"],
            "evidence must enumerate preserved source surface",
        )

    print("formatter evidence smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
