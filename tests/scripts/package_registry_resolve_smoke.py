#!/usr/bin/env python3
import json
import os
import pathlib
import shutil
import subprocess
import sys


def run_command(args, env=None):
    result = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    if result.returncode != 0:
        raise AssertionError(
            "command failed\n"
            + "args: "
            + " ".join(map(str, args))
            + "\nstdout:\n"
            + result.stdout
            + "\nstderr:\n"
            + result.stderr
        )
    return result.stdout


def cache_file_name(key):
    out = []
    for byte in key.encode("utf-8"):
        ch = chr(byte)
        if ch.isalnum() or ch in "-_.":
            out.append(ch)
        else:
            out.append(f"_{byte:02x}")
    return "".join(out) + ".json"


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_risk_model(package_dir):
    write_text(
        package_dir / "ahfl.toml",
        """manifest_version = 2

[package]
name = "risk-model"
version = "2.1.3"
edition = "2026"
kind = "library"

[module]
prefix = "risk_model"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/main.ahfl"

[dependencies]
std = { source = "sysroot" }
""",
    )
    write_text(package_dir / "src/main.ahfl", "module risk_model::main;\n")


def write_app(package_dir):
    write_text(
        package_dir / "ahfl.toml",
        """manifest_version = 2

[package]
name = "registry-app"
version = "0.1.0"
edition = "2026"
kind = "library"

[module]
prefix = "registry_app"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/main.ahfl"

[dependencies]
std = { source = "sysroot" }
risk-model = { source = "registry", registry = "default", version = "^2.0.0" }
""",
    )
    write_text(package_dir / "src/main.ahfl", "module registry_app::main;\n")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: package_registry_resolve_smoke.py <ahflc> <repo-root> <work-dir>")

    ahflc = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(sys.argv[2])
    work_dir = pathlib.Path(sys.argv[3])
    if work_dir.exists():
        shutil.rmtree(work_dir)

    risk_dir = work_dir / "risk-model"
    app_dir = work_dir / "registry-app"
    out_dir = work_dir / "publish"
    cache_dir = work_dir / "cache"
    out_dir.mkdir(parents=True)
    cache_dir.mkdir(parents=True)
    write_risk_model(risk_dir)
    write_app(app_dir)

    run_command(
        [
            ahflc,
            "package",
            "publish",
            "--dry-run",
            "--manifest",
            risk_dir / "ahfl.toml",
            "--registry",
            "default",
            "--sysroot",
            repo,
            "--out",
            out_dir,
        ]
    )

    stem = "risk-model-2.1.3"
    registry_index_path = out_dir / f"{stem}.registry-index.json"
    archive_manifest_path = out_dir / f"{stem}.source-archive.json"
    archive_payload_path = out_dir / f"{stem}.source-archive.payload"
    registry_index = json.loads(registry_index_path.read_text(encoding="utf-8"))
    archive_manifest = json.loads(archive_manifest_path.read_text(encoding="utf-8"))
    digest = registry_index["source_archive_sha256"]

    package_index = {
        "format_version": "ahfl.registry.package_index.v1",
        "package": "risk-model",
        "versions": [registry_index],
    }
    (cache_dir / cache_file_name("registry-package-index:risk-model")).write_text(
        json.dumps(package_index, separators=(",", ":")),
        encoding="utf-8",
    )
    (cache_dir / cache_file_name("registry-index:risk-model:2.1.3")).write_text(
        json.dumps(registry_index, separators=(",", ":")),
        encoding="utf-8",
    )
    (cache_dir / cache_file_name(f"source-archive-manifest:{digest}")).write_text(
        json.dumps(archive_manifest, separators=(",", ":")),
        encoding="utf-8",
    )
    (cache_dir / cache_file_name(f"source-archive-payload:{digest}")).write_bytes(
        archive_payload_path.read_bytes()
    )

    env = dict(os.environ)
    env["AHFL_PACKAGE_CACHE"] = str(cache_dir)
    env["AHFL_REGISTRY_URL"] = "http://127.0.0.1:9"
    graph_output = run_command(
        [
            ahflc,
            "dump",
            "package-graph",
            "--manifest",
            app_dir / "ahfl.toml",
            "--sysroot",
            repo,
        ],
        env=env,
    )
    graph = json.loads(graph_output)
    packages = {package["name"]: package for package in graph["packages"]}
    if packages["risk-model"]["source"] != "registry":
        raise AssertionError(f"risk-model did not resolve as registry package: {packages}")
    if packages["risk-model"]["registry"]["source_archive_sha256"] != digest:
        raise AssertionError("registry package digest was not preserved in package graph")

    edges = [
        edge
        for edge in graph["dependencies"]
        if edge["dependency"] == "risk-model" and edge["source"] == "registry"
    ]
    if len(edges) != 1:
        raise AssertionError(f"missing registry dependency edge: {graph['dependencies']}")
    if edges[0]["selected_version"] != "2.1.3":
        raise AssertionError(f"unexpected selected registry version: {edges[0]}")


if __name__ == "__main__":
    main()
