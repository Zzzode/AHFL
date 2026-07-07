#!/usr/bin/env python3
import json
import pathlib
import re
import shutil
import subprocess
import sys


SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")


def run_command(args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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
    if result.stderr:
        raise AssertionError(f"unexpected stderr for {' '.join(map(str, args))}: {result.stderr}")
    return result.stdout


def run_failing_command(args):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode == 0:
        raise AssertionError(
            "expected command to fail\n"
            + "args: "
            + " ".join(map(str, args))
            + "\nstdout:\n"
            + result.stdout
        )
    return result.stdout + result.stderr


def assert_sha256(value, field):
    if not SHA256_RE.match(value):
        raise AssertionError(f"{field} is not a sha256 digest: {value!r}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: package_archive_smoke.py <ahflc> <repo-root> <work-dir>")

    ahflc = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(sys.argv[2])
    work_dir = pathlib.Path(sys.argv[3])
    if work_dir.exists():
        shutil.rmtree(work_dir)
    out_dir = work_dir / "dist"
    out_dir.mkdir(parents=True)

    manifest = repo / "tests/integration/package_graph_workspace/packages/no-deps-app/ahfl.toml"
    output = run_command(
        [ahflc, "package", "archive", "--manifest", manifest, "--out", out_dir]
    )
    if "archive-sha256:" not in output or "manifest-sha256:" not in output:
        raise AssertionError(f"archive command did not print digest summary:\n{output}")

    manifest_artifact = out_dir / "no-deps-app-0.1.0.source-archive.json"
    payload_artifact = out_dir / "no-deps-app-0.1.0.source-archive.payload"
    if not manifest_artifact.is_file():
        raise AssertionError(f"missing archive manifest: {manifest_artifact}")
    if not payload_artifact.is_file():
        raise AssertionError(f"missing archive payload: {payload_artifact}")

    archive = json.loads(manifest_artifact.read_text(encoding="utf-8"))
    if archive.get("format_version") != "ahfl.source_archive.v1":
        raise AssertionError(f"unexpected archive schema: {archive}")
    if archive.get("package") != "no-deps-app":
        raise AssertionError(f"unexpected archive package: {archive}")
    assert_sha256(archive["manifest_sha256"], "manifest_sha256")
    assert_sha256(archive["archive_sha256"], "archive_sha256")

    files = {entry["path"]: entry for entry in archive["files"]}
    if set(files) != {"ahfl.toml", "src/main.ahfl"}:
        raise AssertionError(f"unexpected archived files: {sorted(files)}")
    if files["ahfl.toml"]["sha256"] != archive["manifest_sha256"]:
        raise AssertionError(f"manifest digest mismatch: {archive}")
    for path, entry in files.items():
        if entry["size_bytes"] <= 0:
            raise AssertionError(f"non-positive archived size for {path}: {entry}")
        assert_sha256(entry["sha256"], f"{path}.sha256")

    payload = payload_artifact.read_bytes()
    if not payload.startswith(b"ahfl.source_archive.v1\x00no-deps-app\x00"):
        raise AssertionError(f"unexpected archive payload prefix: {payload[:80]!r}")
    if b"\r" in payload:
        raise AssertionError("archive payload contains non-normalized CR bytes")
    forbidden_fragments = [b".git", b"ahfl.lock", b"build/", b"dist/"]
    for fragment in forbidden_fragments:
        if fragment in payload:
            raise AssertionError(f"archive payload leaked excluded fragment {fragment!r}")

    wrong_command = run_failing_command(
        [
            ahflc,
            "check",
            "--out",
            out_dir,
            manifest.parent / "src/main.ahfl",
        ]
    )
    if "--out is only supported with package archive" not in wrong_command:
        raise AssertionError(f"missing --out misuse diagnostic:\n{wrong_command}")


if __name__ == "__main__":
    main()
