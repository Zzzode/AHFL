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


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def main() -> int:
    require(
        len(sys.argv) == 2,
        "usage: install_prefix_smoke.py <repo-root>",
    )
    repo = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory(prefix="ahfl-install-smoke-") as temp_dir:
        temp = Path(temp_dir)
        build = temp / "build"
        prefix = temp / "prefix"
        configure = run(
            [
                "cmake",
                "-S",
                str(repo),
                "-B",
                str(build),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DAHFL_INSTALL=ON",
                "-DAHFL_WARNINGS_AS_ERRORS=ON",
            ],
            repo,
        )
        require(
            configure.returncode == 0,
            "clean install configure failed:\n" + configure.stdout + configure.stderr,
        )
        build_result = run(
            ["cmake", "--build", str(build), "--target", "ahflc", "ahfl-lsp", "-j8"],
            repo,
        )
        require(
            build_result.returncode == 0,
            "clean install build failed:\n" + build_result.stdout + build_result.stderr,
        )
        installed = run(
            ["cmake", "--install", str(build), "--prefix", str(prefix)],
            repo,
        )
        require(
            installed.returncode == 0,
            "clean prefix install failed:\n" + installed.stdout + installed.stderr,
        )

        ahflc = prefix / "bin" / "ahflc"
        lsp = prefix / "bin" / "ahfl-lsp"
        sysroot = prefix / "share" / "ahfl"
        require(ahflc.is_file(), f"installed CLI missing: {ahflc}")
        require(lsp.is_file(), f"installed LSP missing: {lsp}")
        require((sysroot / "std" / "ahfl.toml").is_file(), "installed sysroot manifest missing")
        require((sysroot / "std" / "prelude.ahfl").is_file(), "installed prelude missing")

        help_result = run([str(ahflc), "--help"], temp)
        require(help_result.returncode == 0, f"installed CLI help failed: {help_result.stderr}")
        require("ahflc" in help_result.stdout, f"installed CLI help mismatch: {help_result.stdout}")
        version_config = prefix / "lib" / "cmake" / "AHFL" / "AHFLConfigVersion.cmake"
        require(version_config.is_file(), "installed CMake package version file missing")
        require("0.59.0" in version_config.read_text(), "installed package version mismatch")

        checked = run(
            [
                str(ahflc),
                "check",
                "--manifest",
                str(repo / "examples" / "execution-demo" / "ahfl.toml"),
                "--target",
                "workflow",
                "--sysroot",
                str(sysroot),
            ],
            temp,
        )
        require(
            checked.returncode == 0,
            "installed CLI/sysroot check failed:\n" + checked.stdout + checked.stderr,
        )
        require("ok: checked" in checked.stdout, "installed CLI did not report a successful check")

        inventory = sorted(
            path.relative_to(prefix).as_posix()
            for path in prefix.rglob("*")
            if path.is_file()
        )
        result_path = temp / "install-prefix-result.json"
        result_path.write_text(
            json.dumps(
                {
                    "schema": "ahfl.install-prefix-smoke.v1",
                    "version": "0.59.0",
                    "inventory": inventory,
                    "cli": "bin/ahflc",
                    "lsp": "bin/ahfl-lsp",
                    "sysroot": "share/ahfl/std/ahfl.toml",
                    "reference_workflow_checked": True,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n"
        )
        print(result_path.read_text(), end="")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
