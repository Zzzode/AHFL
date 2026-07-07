#!/usr/bin/env python3
import pathlib
import shutil
import subprocess
import sys


def run_command(args, expect_success=True):
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if expect_success and result.returncode != 0:
        raise AssertionError(
            "command failed\n"
            + "args: "
            + " ".join(map(str, args))
            + "\nstdout:\n"
            + result.stdout
            + "\nstderr:\n"
            + result.stderr
        )
    if not expect_success and result.returncode == 0:
        raise AssertionError(
            "command unexpectedly succeeded\n"
            + "args: "
            + " ".join(map(str, args))
            + "\nstdout:\n"
            + result.stdout
            + "\nstderr:\n"
            + result.stderr
        )
    return result


def assert_contains(path, expected):
    content = path.read_text(encoding="utf-8")
    if expected not in content:
        raise AssertionError(f"{path} missing {expected!r}:\n{content}")
    return content


def check_manifest_package(ahflc, repo, manifest):
    run_command([ahflc, "check", "--manifest", manifest, "--sysroot", repo])


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: init_single_file_smoke.py <ahflc> <repo-root> <work-dir>")

    ahflc = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(sys.argv[2])
    work_dir = pathlib.Path(sys.argv[3])
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)

    created_dir = work_dir / "Created App"
    created_source = created_dir / "main.ahfl"
    run_command([ahflc, "init", "--single-file", created_source])
    created_manifest = created_dir / "ahfl.toml"
    assert_contains(created_source, "module created_app::main;")
    assert_contains(created_manifest, 'name = "created-app"')
    assert_contains(created_manifest, 'prefix = "created_app"')
    assert_contains(created_manifest, 'modules = ["main"]')
    assert_contains(created_manifest, 'std = { source = "sysroot" }')
    check_manifest_package(ahflc, repo, created_manifest)

    existing_dir = work_dir / "existing"
    existing_dir.mkdir(parents=True, exist_ok=True)
    existing_source = existing_dir / "scratch.ahfl"
    existing_source.write_text("struct Payload {\n    value: String;\n}\n", encoding="utf-8")
    run_command([ahflc, "init", "--single-file", existing_source])
    existing_manifest = existing_dir / "ahfl.toml"
    content = assert_contains(existing_source, "module existing::scratch;")
    if not content.startswith("module existing::scratch;\n\nstruct Payload"):
        raise AssertionError(f"source was not initialized by prepending module declaration:\n{content}")
    check_manifest_package(ahflc, repo, existing_manifest)

    module_dir = work_dir / "declared"
    module_dir.mkdir(parents=True, exist_ok=True)
    module_source = module_dir / "entry.ahfl"
    module_source.write_text("module custom_prefix::entry;\n\nstruct Ready {}\n", encoding="utf-8")
    run_command([ahflc, "init", "--single-file", module_source])
    module_manifest = module_dir / "ahfl.toml"
    assert_contains(module_manifest, 'name = "declared"')
    assert_contains(module_manifest, 'prefix = "custom_prefix"')
    assert_contains(module_manifest, 'modules = ["entry"]')
    check_manifest_package(ahflc, repo, module_manifest)

    duplicate = run_command([ahflc, "init", "--single-file", module_source], expect_success=False)
    if duplicate.returncode != 2:
        raise AssertionError(f"expected usage error for existing manifest, got {duplicate.returncode}")
    if "manifest already exists" not in duplicate.stderr:
        raise AssertionError(f"missing existing manifest diagnostic:\n{duplicate.stderr}")


if __name__ == "__main__":
    main()
