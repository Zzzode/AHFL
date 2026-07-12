#!/usr/bin/env python3
"""Enforce the RFC 0012 beta product-surface freeze."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


DEFAULT_ROOT = Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=DEFAULT_ROOT,
        help="repository root to inspect",
    )
    return parser.parse_args()


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise ValueError(f"missing product-scope input: {relative}")
    return path.read_text(encoding="utf-8")


def enum_values(text: str, enum_name: str) -> set[str]:
    match = re.search(rf"enum class {re.escape(enum_name)}\s*\{{(.*?)\n\}};", text, re.DOTALL)
    if match is None:
        raise ValueError(f"could not locate enum class {enum_name}")
    return {
        value
        for value in re.findall(
            r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*,", match.group(1), re.MULTILINE
        )
        if value != "_Count"
    }


def emitted_artifact_ids(text: str) -> set[str]:
    return set(
        re.findall(
            r"emit_command\s*\(\s*CommandKind::[A-Za-z_][A-Za-z0-9_]*"
            r'\s*,\s*"[^"]+"\s*,\s*"([^"]+)"',
            text,
            re.DOTALL,
        )
    )


def provider_artifacts(text: str) -> tuple[set[str], set[str]]:
    marker = "AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT("
    starts = [match.start() for match in re.finditer(re.escape(marker), text)]
    kinds: set[str] = set()
    artifact_ids: set[str] = set()
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(text)
        block = text[start + len(marker) : end]
        kind_match = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*,", block)
        id_match = re.search(r'^\s*"([^"]+)"\s*,\s*$', block, re.MULTILINE)
        if kind_match is None or id_match is None:
            continue
        kinds.add(kind_match.group(1))
        artifact_ids.add(id_match.group(1))
    if not kinds:
        raise ValueError("could not parse provider artifact catalog")
    return kinds, artifact_ids


def require_rfc_contract(root: Path) -> list[str]:
    failures: list[str] = []
    rfc = read(root, "docs/rfcs/0012-structured-workflow-execution-ux.zh.md")
    index = read(root, "docs/rfcs/index.yml")

    if not re.search(r'^status:\s*"accepted"\s*$', rfc, re.MULTILINE):
        failures.append("RFC 0012 must remain accepted while the beta scope freeze is active")
    if "https://github.com/Zzzode/AHFL/issues/16" not in rfc:
        failures.append("RFC 0012 must retain tracking issue #16")
    if "### Reference Workflow" not in rfc or "`examples/execution-demo`" not in rfc:
        failures.append("RFC 0012 must define examples/execution-demo as the reference workflow")
    if not re.search(
        r'rfc:\s*"0012".*?status:\s*"accepted"',
        index,
        re.DOTALL,
    ):
        failures.append("RFC registry must record RFC 0012 as accepted")
    return failures


def report_additions(
    label: str, current: set[str], allowed: set[str], failures: list[str]
) -> None:
    additions = sorted(current - allowed)
    if additions:
        failures.append(f"new {label} forbidden by RFC 0012 scope freeze: {', '.join(additions)}")


def main() -> int:
    root = parse_args().root.resolve()
    failures: list[str] = []
    try:
        baseline_path = root / "config/product-scope-freeze.json"
        if not baseline_path.is_file():
            raise ValueError("missing product-scope input: config/product-scope-freeze.json")
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
        if baseline.get("schema") != "ahfl.product-scope-freeze.v1":
            raise ValueError("product scope baseline schema must be ahfl.product-scope-freeze.v1")

        command_kinds = enum_values(
            read(root, "src/tooling/cli/command_catalog.hpp"), "CommandKind"
        )
        backend_kinds = enum_values(
            read(root, "include/ahfl/compiler/backends/driver.hpp"), "BackendKind"
        )
        artifact_ids = emitted_artifact_ids(
            read(root, "src/tooling/cli/command_registry.cpp")
        )
        provider_catalog = (
            root
            / "src/tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
        )
        if provider_catalog.is_file():
            provider_kinds, provider_ids = provider_artifacts(
                provider_catalog.read_text(encoding="utf-8")
            )
        else:
            provider_kinds, provider_ids = set(), set()

        catalogs = baseline.get("catalogs")
        if not isinstance(catalogs, dict):
            raise ValueError("product scope baseline catalogs must be an object")

        report_additions(
            "CommandKind",
            command_kinds,
            set(catalogs.get("command_kinds", [])),
            failures,
        )
        report_additions(
            "BackendKind",
            backend_kinds,
            set(catalogs.get("backend_kinds", [])),
            failures,
        )
        report_additions(
            "emitted artifact",
            artifact_ids,
            set(catalogs.get("emitted_artifact_ids", [])),
            failures,
        )
        report_additions(
            "ProviderArtifactKind",
            provider_kinds,
            set(catalogs.get("provider_artifact_kinds", [])),
            failures,
        )
        report_additions(
            "provider artifact id",
            provider_ids,
            set(catalogs.get("provider_artifact_ids", [])),
            failures,
        )
        failures.extend(require_rfc_contract(root))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        failures.append(str(error))

    if failures:
        print("product scope freeze check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("product scope freeze check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
