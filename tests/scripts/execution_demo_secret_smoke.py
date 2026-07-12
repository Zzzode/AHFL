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


def run_checker(checker: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), "--root", str(root)],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    require(
        len(sys.argv) == 3,
        "usage: execution_demo_secret_smoke.py <checker> <repo-root>",
    )
    checker = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve()
    require(checker.exists(), f"execution demo secret checker does not exist: {checker}")

    with tempfile.TemporaryDirectory(prefix="ahfl-execution-demo-secret-") as temp_dir:
        root = Path(temp_dir)
        demo = root / "examples/execution-demo"
        demo.mkdir(parents=True)
        for relative in (
            "examples/execution-demo/README.md",
            "examples/execution-demo/llm_config.example.json",
        ):
            source = source_root / relative
            require(source.exists(), f"missing execution demo contract file: {relative}")
            shutil.copy2(source, demo / source.name)

        result = run_checker(checker, root)
        require(result.returncode == 0, f"secure demo baseline must pass:\n{result.stderr}")

        config_path = demo / "llm_config.example.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["api_key"] = "inline-secret-must-fail"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        result = run_checker(checker, root)
        require(result.returncode != 0, "inline api_key must fail")
        require("api_key" in result.stderr, "inline key failure must name api_key")

        shutil.copy2(
            source_root / "examples/execution-demo/llm_config.example.json", config_path
        )
        readme_path = demo / "README.md"
        readme_path.write_text(
            readme_path.read_text(encoding="utf-8")
            + "\nCopy ANTHROPIC_AUTH_TOKEN into the JSON file.\n",
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "token-copy guidance must fail")
        require("token" in result.stderr.lower(), "README failure must identify token guidance")

        shutil.copy2(source_root / "examples/execution-demo/README.md", readme_path)
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["api_key_secret"] = "plain-handle-without-provider"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        result = run_checker(checker, root)
        require(result.returncode != 0, "unqualified example secret handle must fail")
        require("env:" in result.stderr, "secret handle failure must require explicit env prefix")

    print("execution demo secret smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
