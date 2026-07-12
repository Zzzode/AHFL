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
        "usage: install_evidence_smoke.py <generator> <repo-root> <build-dir>",
    )
    generator = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    build = Path(sys.argv[3]).resolve()
    require(generator.is_file(), f"install evidence generator missing: {generator}")

    with tempfile.TemporaryDirectory(prefix="ahfl-install-evidence-") as temp_dir:
        output = Path(temp_dir) / "install-smoke.json"
        result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--repo-root",
                str(repo),
                "--build-dir",
                str(build),
                "--out",
                str(output),
            ],
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
        )
        require(
            result.returncode == 0,
            "install evidence generation failed:\n" + result.stdout + result.stderr,
        )
        value = json.loads(output.read_text())
        require(value["schema"] == "ahfl.beta-evidence.install-smoke.v1", "schema mismatch")
        require(value["criterion"] == "BETA-08", "criterion mismatch")
        require(value["status"] == "passed", "install evidence must pass")
        require(value["clean_prefix"]["reference_workflow_checked"], "CLI/sysroot not checked")
        require(value["vsix"]["isolated_install"], "VSIX was not installed in isolation")
        require(value["vsix"]["bundled_server"], "VSIX server missing")
        require(value["vsix"]["bundled_sysroot"], "VSIX sysroot missing")
        require(
            set(value["schema_compatibility"].values()) == {"accepted", "rejected"},
            "schema compatibility matrix incomplete",
        )

    print("install evidence smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
