#!/usr/bin/env python3
"""Generate BETA-08 evidence for clean CLI/sysroot and platform VSIX installs."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from ahfl_source_revision import compute_source_revision


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def require_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def parse_last_json_object(output: str) -> dict[str, object]:
    start = output.find("{")
    if start < 0:
        raise RuntimeError(f"install smoke did not emit JSON:\n{output}")
    return json.loads(output[start:])


def host_target() -> str:
    uname_s = subprocess.run(
        ["uname", "-s"], check=True, capture_output=True, text=True
    ).stdout.strip().lower()
    uname_m = subprocess.run(
        ["uname", "-m"], check=True, capture_output=True, text=True
    ).stdout.strip()
    mapping = {
        ("darwin", "arm64"): "darwin-arm64",
        ("darwin", "x86_64"): "darwin-x64",
        ("linux", "aarch64"): "linux-arm64",
        ("linux", "arm64"): "linux-arm64",
        ("linux", "x86_64"): "linux-x64",
        ("linux", "amd64"): "linux-x64",
    }
    target = mapping.get((uname_s, uname_m))
    if target is None:
        raise RuntimeError(f"unsupported host target: {uname_s}-{uname_m}")
    return target


def main() -> int:
    args = parse_args()
    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    output = args.out.resolve()

    install = run(["python3", "tests/scripts/install_prefix_smoke.py", str(repo)], repo)
    require_success(install, "clean prefix install smoke")
    install_result = parse_last_json_object(install.stdout)
    if install_result.get("schema") != "ahfl.install-prefix-smoke.v1":
        raise RuntimeError("unexpected install prefix result schema")

    migration = run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "--output-on-failure",
            "-R",
            "^(ahfl\\.package_graph\\.core_all|ahfl\\.runtime\\.workflow_recovery_all)$",
        ],
        repo,
    )
    require_success(migration, "version and schema migration smoke")

    packaged = run(["bash", "scripts/package-vscode-vsix-release.sh"], repo)
    require_success(packaged, "platform VSIX packaging")
    target = host_target()
    extension_version = run(
        ["node", "-p", "require('./tools/vscode/package.json').version"], repo
    )
    require_success(extension_version, "extension version lookup")
    vsix = (
        repo
        / "tools"
        / "vscode"
        / "dist"
        / f"ahfl-language-{extension_version.stdout.strip()}-{target}.vsix"
    )
    if not vsix.is_file():
        raise RuntimeError(f"platform VSIX not found: {vsix}")

    inventory = run(["pnpm", "run", "test:package-inventory"], repo / "tools/vscode")
    require_success(inventory, "VSIX package inventory")
    install_vsix = run(
        ["pnpm", "run", "test:vsix-install"], repo / "tools/vscode"
    )
    require_success(install_vsix, "VSIX isolated install")
    installed_match = re.search(r"Installed ([^ ]+@[^ ]+) from", install_vsix.stdout)
    if installed_match is None:
        raise RuntimeError("VSIX install smoke did not report installed extension")

    value: dict[str, object] = {
        "schema": "ahfl.beta-evidence.install-smoke.v1",
        "status": "passed",
        "criterion": "BETA-08",
        "source_revision": compute_source_revision(repo),
        "clean_prefix": {
            "cli": install_result["cli"],
            "lsp": install_result["lsp"],
            "sysroot": install_result["sysroot"],
            "version": install_result["version"],
            "reference_workflow_checked": install_result["reference_workflow_checked"],
        },
        "vsix": {
            "path": vsix.relative_to(repo).as_posix(),
            "target": target,
            "extension": installed_match.group(1),
            "bundled_server": True,
            "bundled_sysroot": True,
            "isolated_install": True,
        },
        "schema_compatibility": {
            "lockfile_v1": "accepted",
            "unknown_lockfile_version": "rejected",
            "workflow_recovery_v1": "accepted",
            "unknown_recovery_schema": "rejected",
            "legacy_recovery_shape": "rejected",
        },
        "commands": [
            "python3 tests/scripts/install_prefix_smoke.py .",
            "scripts/package-vscode-vsix-release.sh",
            "pnpm run test:package-inventory",
            "pnpm run test:vsix-install",
            (
                f"ctest --test-dir {build} --output-on-failure -R "
                "'^(ahfl\\.package_graph\\.core_all|"
                "ahfl\\.runtime\\.workflow_recovery_all)$'"
            ),
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    print("beta install evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
