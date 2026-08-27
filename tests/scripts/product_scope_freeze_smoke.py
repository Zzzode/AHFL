#!/usr/bin/env python3

from __future__ import annotations

import json
import re
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


def force_rfc_status(rfc: Path, status: str) -> None:
    """Rewrite the RFC 0012 frontmatter status line to `status`."""
    text = rfc.read_text(encoding="utf-8")
    new_text, count = re.subn(
        r'^status:\s*"[a-z]+"\s*$',
        f'status: "{status}"',
        text,
        count=1,
        flags=re.MULTILINE,
    )
    require(count == 1, "could not locate RFC 0012 status line to rewrite")
    rfc.write_text(new_text, encoding="utf-8")


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

        rfc = root / "docs/rfcs/0012-structured-workflow-execution-ux.zh.md"
        # The freeze lifts once RFC 0012 reaches `stabilized`. So that the
        # mutation-detection assertions below exercise the FROZEN behavior
        # regardless of the real RFC's current status (which may already be
        # stabilized), pin the temp copy to a freeze-active status first.
        force_rfc_status(rfc, "implemented")

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
        # The freeze is active while RFC 0012 is accepted / implementing /
        # implemented (from accepted until the beta gate closes at stabilized).
        # Flipping the (whatever) freeze-active status to one outside that window
        # (draft) must trip the gate — done status-agnostically so this test does
        # not need editing every time the RFC advances within the window.
        rfc.write_text(
            re.sub(
                r'^status:\s*"(accepted|implementing|implemented)"\s*$',
                'status: "draft"',
                rfc.read_text(encoding="utf-8"),
                count=1,
                flags=re.MULTILINE,
            ),
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        require(
            result.returncode != 0,
            "freeze gate must require accepted/implementing/implemented RFC 0012",
        )
        require(
            "accepted, implementing, or implemented" in result.stderr,
            "RFC status failure must explain the accepted/implementing/implemented requirement",
        )

        # `stabilized` closes the beta gate and LIFTS the freeze: the checker
        # must pass even when a forbidden CommandKind addition is present, since
        # the surface is intentionally no longer frozen at that point.
        force_rfc_status(rfc, "stabilized")
        add_enum_value(
            command_header, "    _Count, // sentinel — must be last", "EmitPostFreezeSurface"
        )
        result = run_checker(checker, root)
        require(
            result.returncode == 0,
            f"stabilized RFC 0012 must lift the freeze even with new surface:\n{result.stderr}",
        )
        require(
            "freeze lifted" in result.stdout,
            "stabilized pass must report the freeze as lifted",
        )

    print("product scope freeze smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
