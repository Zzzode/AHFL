#!/usr/bin/env python3
"""Enforce the secret-handle contract for the beta reference workflow."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_ROOT = Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="repository root")
    return parser.parse_args()


def main() -> int:
    root = parse_args().root.resolve()
    demo = root / "examples/execution-demo"
    config_path = demo / "llm_config.example.json"
    readme_path = demo / "README.md"
    failures: list[str] = []

    try:
        config: Any = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"invalid execution demo config template: {error}")
        config = {}

    if not isinstance(config, dict):
        failures.append("execution demo config template must be a JSON object")
        config = {}
    if "api_key" in config:
        failures.append("execution demo config template must not contain inline api_key")
    secret_handle = config.get("api_key_secret")
    if not isinstance(secret_handle, str) or not secret_handle.startswith("env:"):
        failures.append("execution demo api_key_secret must use an explicit env: provider handle")
    if secret_handle == "env:":
        failures.append("execution demo api_key_secret env handle must name an environment variable")

    try:
        readme = readme_path.read_text(encoding="utf-8")
    except OSError as error:
        failures.append(f"failed to read execution demo README: {error}")
        readme = ""

    if "llm_config.example.json" not in readme:
        failures.append("execution demo README must document llm_config.example.json")
    if "AHFL_GLM_API_KEY" not in readme:
        failures.append("execution demo README must document AHFL_GLM_API_KEY")
    if re.search(
        r"copy\b.*(?:token|api[_ -]?key|credential).*\binto\b.*(?:json|config|file)",
        readme,
        re.IGNORECASE,
    ):
        failures.append("execution demo README must not instruct users to copy a token into a file")
    if re.search(r"inline\s+[`'\"]?api_key", readme, re.IGNORECASE):
        failures.append("execution demo README must not recommend an inline api_key")

    if failures:
        print("execution demo secret check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("execution demo secret check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
