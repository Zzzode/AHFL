#!/usr/bin/env python3
"""Generate BETA-06 evidence for nominal stdlib container migration."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from ahfl_source_revision import compute_source_revision


CANONICAL_STDLIB_TYPES = [
    "std::collections::List",
    "std::collections::Map",
    "std::collections::Set",
    "std::option::Option",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def require_absent(path: Path, patterns: list[str], label: str) -> None:
    text = path.read_text(encoding="utf-8")
    matches = [pattern for pattern in patterns if re.search(pattern, text)]
    if matches:
        raise RuntimeError(f"{label} remains in {path}: {matches}")


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
            "-R",
            (
                "^(ahfl\\.semantics\\.(typed_hir_all|type_resolver_all)|"
                "ahfl\\.evaluator\\.eval_all|ahfl\\.executor\\.exec_all|"
                "ahfl\\.runtime\\.(value_json_all|response_schema_validator|"
                "capability_bridge_all)|ahflc\\.check\\.stdlib_api_smoke)$"
            ),
        ],
        repo,
    )
    if tests.returncode != 0:
        raise RuntimeError(tests.stdout + tests.stderr)

    types_header = repo / "include/ahfl/compiler/semantics/types.hpp"
    require_absent(
        types_header,
        [
            r"TypeKind::(?:Optional|List|Set|Map)",
            r"struct (?:Optional|List|Set|Map)T\b",
        ],
        "legacy semantic container type",
    )
    types_text = types_header.read_text(encoding="utf-8")
    if "Container types (Option / List / Set / Map) are encoded through nominal" not in types_text:
        raise RuntimeError("semantic type model does not declare nominal container representation")

    value_header = repo / "src/runtime/evaluator/value.hpp"
    require_absent(
        value_header,
        [
            r"struct OptionalValue\b",
            r"\bOptionalValue\b",
            r"\bassociated\b",
            r"make_optional_(?:some|none)",
        ],
        "legacy runtime option representation",
    )
    value_text = value_header.read_text(encoding="utf-8")
    for marker in (
        'value->enum_name == "std::option::Option"',
        'value->variant == "Some"',
        'value->variant == "None"',
    ):
        if marker not in value_text:
            raise RuntimeError(f"nominal runtime Option accessor lacks marker: {marker}")

    frontend_header = repo / "include/ahfl/compiler/frontend/frontend.hpp"
    require_absent(frontend_header, [r"enable_desugaring"], "container migration flag")
    frontend_build = (
        repo / "src/compiler/syntax/frontend/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    if "desugar.cpp" in frontend_build:
        raise RuntimeError("legacy container desugar pass remains in the production build")

    schema_test = (
        repo / "tests/unit/runtime/engine/response_schema_validator.cpp"
    ).read_text(encoding="utf-8")
    for marker in ("optional.raw_none_rejected", "optional.raw_inner_rejected"):
        if marker not in schema_test:
            raise RuntimeError(f"schema regression does not reject fallback shape: {marker}")

    container_view = (
        repo / "src/compiler/semantics/std_container_types.hpp"
    ).read_text(encoding="utf-8")
    for canonical_name in CANONICAL_STDLIB_TYPES:
        if canonical_name not in container_view:
            raise RuntimeError(f"missing canonical stdlib container identity: {canonical_name}")

    value: dict[str, object] = {
        "schema": "ahfl.beta-evidence.stdlib-container-migration.v1",
        "status": "passed",
        "criterion": "BETA-06",
        "source_revision": compute_source_revision(repo),
        "semantic_type_representation": "nominal_generics",
        "runtime_option_representation": "nominal_enum",
        "legacy_type_kinds": [],
        "legacy_runtime_variants": [],
        "migration_flags": [],
        "stdlib_types": CANONICAL_STDLIB_TYPES,
        "raw_optional_fallback_rejected": True,
        "production_desugar_pass_removed": True,
        "test_command": (
            f"ctest --test-dir {build} --output-on-failure -R "
            "'^(ahfl\\.semantics\\.(typed_hir_all|type_resolver_all)|"
            "ahfl\\.evaluator\\.eval_all|ahfl\\.executor\\.exec_all|"
            "ahfl\\.runtime\\.(value_json_all|response_schema_validator|capability_bridge_all)|"
            "ahflc\\.check\\.stdlib_api_smoke)$'"
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("beta stdlib container evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
