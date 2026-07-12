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
        "usage: stdlib_container_evidence_smoke.py <generator> <repo-root> <build-dir>",
    )
    generator = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    build = Path(sys.argv[3]).resolve()
    require(generator.exists(), f"stdlib container evidence generator does not exist: {generator}")

    with tempfile.TemporaryDirectory(prefix="ahfl-container-evidence-") as temp_dir:
        evidence = Path(temp_dir) / "stdlib-container-migration.json"
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
            "stdlib container evidence generation must pass:\n"
            + result.stdout
            + result.stderr,
        )
        value = json.loads(evidence.read_text(encoding="utf-8"))
        require(
            value["schema"] == "ahfl.beta-evidence.stdlib-container-migration.v1",
            "unexpected stdlib container evidence schema",
        )
        require(value["criterion"] == "BETA-06", "evidence must target BETA-06")
        require(value["status"] == "passed", "stdlib container evidence must pass")
        require(value["semantic_type_representation"] == "nominal_generics", "types must be nominal")
        require(value["runtime_option_representation"] == "nominal_enum", "Option must be nominal")
        require(value["legacy_type_kinds"] == [], "legacy container TypeKind values must be absent")
        require(value["legacy_runtime_variants"] == [], "legacy runtime variants must be absent")
        require(value["migration_flags"] == [], "migration feature flags must be absent")
        require(
            value["stdlib_types"]
            == [
                "std::collections::List",
                "std::collections::Map",
                "std::collections::Set",
                "std::option::Option",
            ],
            "evidence must enumerate canonical stdlib types",
        )

    print("stdlib container evidence smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
