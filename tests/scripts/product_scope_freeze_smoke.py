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


def copy_gate_inputs(source_root: Path, target_root: Path) -> None:
    paths = [
        "config/product-scope-freeze.json",
        "docs/rfcs/0012-structured-workflow-execution-ux.zh.md",
        "docs/rfcs/index.yml",
        "include/ahfl/compiler/backends/driver.hpp",
        "src/tooling/cli/command_catalog.hpp",
        "src/tooling/cli/command_registry.cpp",
    ]
    for relative in paths:
        source = source_root / relative
        require(source.exists(), f"missing gate input: {relative}")
        target = target_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def add_enum_value(path: Path, sentinel: str, value: str) -> None:
    text = path.read_text(encoding="utf-8")
    require(sentinel in text, f"missing mutation sentinel in {path}")
    path.write_text(text.replace(sentinel, f"    {value},\n{sentinel}", 1), encoding="utf-8")


def remove_enum_value(path: Path, value: str) -> None:
    text = path.read_text(encoding="utf-8")
    needle = f"    {value},\n"
    require(needle in text, f"missing enum value {value} in {path}")
    path.write_text(text.replace(needle, "", 1), encoding="utf-8")


def main() -> int:
    require(len(sys.argv) == 3, "usage: product_scope_freeze_smoke.py <checker> <repo-root>")
    checker = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve()
    require(checker.exists(), f"scope-freeze checker does not exist: {checker}")

    baseline = json.loads(
        (source_root / "config/product-scope-freeze.json").read_text(encoding="utf-8")
    )
    require(baseline["schema"] == "ahfl.product-scope-freeze.v1", "unexpected baseline schema")

    with tempfile.TemporaryDirectory(prefix="ahfl-scope-freeze-") as temp_dir:
        root = Path(temp_dir)
        copy_gate_inputs(source_root, root)

        result = run_checker(checker, root)
        require(result.returncode == 0, f"baseline must pass:\n{result.stderr}")

        command_header = root / "src/tooling/cli/command_catalog.hpp"
        add_enum_value(command_header, "    _Count, // sentinel — must be last", "EmitNewSurface")
        result = run_checker(checker, root)
        require(result.returncode != 0, "new CommandKind must fail the freeze gate")
        require("EmitNewSurface" in result.stderr, "new command failure must name the addition")
        shutil.copy2(source_root / "src/tooling/cli/command_catalog.hpp", command_header)

        backend_header = root / "include/ahfl/compiler/backends/driver.hpp"
        add_enum_value(backend_header, "};", "InfraNewSurface")
        result = run_checker(checker, root)
        require(result.returncode != 0, "new BackendKind must fail the freeze gate")
        require("InfraNewSurface" in result.stderr, "new backend failure must name the addition")
        shutil.copy2(source_root / "include/ahfl/compiler/backends/driver.hpp", backend_header)

        command_registry = root / "src/tooling/cli/command_registry.cpp"
        registry_text = command_registry.read_text(encoding="utf-8")
        registry_sentinel = "constexpr CommandSpec kCommandSpecs[] = {\n"
        require(registry_sentinel in registry_text, "missing command registry mutation sentinel")
        command_registry.write_text(
            registry_text.replace(
                registry_sentinel,
                registry_sentinel
                + '    emit_command(CommandKind::EmitIr, "emit-new-surface", '
                + '"new-surface", "new-surface"),\n',
                1,
            ),
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "new emitted artifact must fail the freeze gate")
        require("new-surface" in result.stderr, "artifact failure must name the addition")
        shutil.copy2(source_root / "src/tooling/cli/command_registry.cpp", command_registry)

        provider_def = (
            root / "src/tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
        )
        provider_def.parent.mkdir(parents=True, exist_ok=True)
        provider_def.write_text(
            "AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(\n"
            + "    NewSurface,\n"
            + "    Placeholder,\n"
            + "    build_placeholder,\n"
            + "    print_placeholder,\n"
            + '    "new-surface",\n'
            + "    Internal,\n"
            + "    999,\n"
            + "    0,\n"
            + "    ({}))\n",
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "new provider artifact must fail the freeze gate")
        require("NewSurface" in result.stderr, "provider artifact failure must name the addition")
        provider_def.unlink()

        remove_enum_value(command_header, "EmitTerraform")
        result = run_checker(checker, root)
        require(result.returncode == 0, f"surface deletion must remain allowed:\n{result.stderr}")

        rfc = root / "docs/rfcs/0012-structured-workflow-execution-ux.zh.md"
        rfc.write_text(
            rfc.read_text(encoding="utf-8").replace('status: "accepted"', 'status: "draft"', 1),
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "freeze gate must require accepted RFC 0012")
        require("accepted" in result.stderr, "RFC status failure must explain accepted requirement")

    print("product scope freeze smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
