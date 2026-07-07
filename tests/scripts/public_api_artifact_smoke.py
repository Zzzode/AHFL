#!/usr/bin/env python3
import copy
import json
import pathlib
import subprocess
import sys


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


def public_api_snapshot(ahflc, *args):
    output = run_command([ahflc, "emit", "public-api", *map(str, args)])
    snapshot = json.loads(output)
    if snapshot.get("schema") != "ahfl.public_api.v1":
        raise AssertionError(f"unexpected schema: {snapshot.get('schema')}")
    return snapshot


def entry_names(snapshot):
    return {entry["canonical_name"] for entry in snapshot["entries"]}


def entry_by_name(snapshot, name):
    for entry in snapshot["entries"]:
        if entry["canonical_name"] == name:
            return entry
    raise AssertionError(f"missing public API entry {name}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: public_api_artifact_smoke.py <ahflc> <repo-root> <work-dir>")

    ahflc = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(sys.argv[2])
    work_dir = pathlib.Path(sys.argv[3])
    work_dir.mkdir(parents=True, exist_ok=True)

    manifest = repo / "tests/integration/package_graph_manifest/ahfl.toml"
    manifest_snapshot = public_api_snapshot(ahflc, "--manifest", manifest, "--sysroot", repo)
    if manifest_snapshot["package"]["manifest"] != "ahfl.toml":
        raise AssertionError(f"manifest path is not package-relative: {manifest_snapshot['package']}")
    manifest_names = entry_names(manifest_snapshot)
    expected_manifest_names = {
        "refund_audit::main::RefundAuditInput",
        "refund_audit::main::RefundAuditOutput",
        "refund_audit::main::RefundAuditWorkflow",
    }
    if manifest_names != expected_manifest_names:
        raise AssertionError(f"unexpected manifest public API names: {sorted(manifest_names)}")
    if any(name.startswith("std::") for name in manifest_names):
        raise AssertionError(f"dependency API leaked into package API: {sorted(manifest_names)}")
    request_entry = entry_by_name(manifest_snapshot, "refund_audit::main::RefundAuditInput")
    if request_entry["symbol_kind"] != "struct":
        raise AssertionError(f"unexpected symbol kind: {request_entry}")
    if request_entry["source"] != "src/main.ahfl":
        raise AssertionError(f"source path is not package-relative: {request_entry}")
    if request_entry["signature"]["fields"][0]["type"] != "String":
        raise AssertionError(f"missing structured field type: {request_entry}")

    docs = run_command([ahflc, "emit", "public-api-docs", "--manifest", manifest, "--sysroot", repo])
    if "# Public API: refund-audit" not in docs or "RefundAuditWorkflow" not in docs:
        raise AssertionError(f"public API docs missing expected content:\n{docs}")
    if "std::option" in docs:
        raise AssertionError(f"dependency API leaked into docs:\n{docs}")

    workspace = repo / "tests/integration/check_ok/ahfl.workspace.toml"
    workspace_snapshot = public_api_snapshot(
        ahflc,
        "--workspace",
        workspace,
        "--package",
        "lib",
        "--sysroot",
        repo,
    )
    workspace_names = entry_names(workspace_snapshot)
    expected_workspace_subset = {
        "lib::agents::AliasAgent",
        "lib::agents::Echo",
        "lib::types::RequestAlias",
        "lib::types::ResponseAlias",
    }
    if not expected_workspace_subset.issubset(workspace_names):
        raise AssertionError(f"workspace API missing expected entries: {sorted(workspace_names)}")
    if any(name.startswith("app::") for name in workspace_names):
        raise AssertionError(f"workspace selector leaked another package: {sorted(workspace_names)}")

    old_path = work_dir / "old.public-api.json"
    new_path = work_dir / "new.public-api.json"
    old_path.write_text(json.dumps(manifest_snapshot, separators=(",", ":")), encoding="utf-8")
    new_snapshot = copy.deepcopy(manifest_snapshot)
    new_snapshot["entries"] = [
        entry
        for entry in new_snapshot["entries"]
        if entry["canonical_name"] != "refund_audit::main::RefundAuditOutput"
    ]
    changed_entry = entry_by_name(new_snapshot, "refund_audit::main::RefundAuditInput")
    changed_entry["signature_text"] += " // changed"
    changed_entry["signature"]["text"] = changed_entry["signature_text"]
    new_snapshot["entries"].append(
        {
            "api_id": "symbol:functions:refund_audit::main::NewHelper",
            "entry_kind": "symbol",
            "symbol_kind": "function",
            "namespace": "functions",
            "local_name": "NewHelper",
            "canonical_name": "refund_audit::main::NewHelper",
            "module": "refund_audit::main",
            "source": "",
            "range": {},
            "symbol_id": None,
            "alias_id": None,
            "target_symbol_id": None,
            "target_canonical_name": "",
            "signature_text": "fn NewHelper() -> Unit",
            "signature": {"kind": "function", "text": "fn NewHelper() -> Unit"},
        }
    )
    new_path.write_text(json.dumps(new_snapshot, separators=(",", ":")), encoding="utf-8")
    diff = run_command([ahflc, "emit", "public-api-diff", old_path, new_path])
    expected_diff_fragments = [
        "ahfl.public_api.diff.v1",
        "added: 1",
        "+ function refund_audit::main::NewHelper",
        "removed: 1",
        "- struct refund_audit::main::RefundAuditOutput",
        "changed: 1",
        "~ struct refund_audit::main::RefundAuditInput",
    ]
    for fragment in expected_diff_fragments:
        if fragment not in diff:
            raise AssertionError(f"missing diff fragment {fragment!r}:\n{diff}")

    wrong_command_gate = run_failing_command(
        [
            ahflc,
            "emit",
            "public-api",
            "--semver-gate",
            "--manifest",
            manifest,
            "--sysroot",
            repo,
        ]
    )
    if "--semver-gate, --from, and --to are only valid with emit public-api-diff or package publish" not in wrong_command_gate:
        raise AssertionError(f"missing wrong-command semver gate diagnostic:\n{wrong_command_gate}")

    from_without_gate = run_failing_command(
        [
            ahflc,
            "emit",
            "public-api-diff",
            "--from",
            "1.2.3",
            old_path,
            new_path,
        ]
    )
    if "--from and --to are only valid with emit public-api-diff --semver-gate" not in from_without_gate:
        raise AssertionError(f"missing --from without gate diagnostic:\n{from_without_gate}")

    failed_gate = run_failing_command(
        [
            ahflc,
            "emit",
            "public-api-diff",
            "--semver-gate",
            "--from",
            "1.2.3",
            "--to",
            "1.3.0",
            old_path,
            new_path,
        ]
    )
    expected_failed_gate_fragments = [
        "semver-gate: fail",
        "severity: breaking",
        "required-bump: major",
        "actual-bump: minor",
    ]
    for fragment in expected_failed_gate_fragments:
        if fragment not in failed_gate:
            raise AssertionError(f"missing failed semver gate fragment {fragment!r}:\n{failed_gate}")

    passed_gate = run_command(
        [
            ahflc,
            "emit",
            "public-api-diff",
            "--semver-gate",
            "--from",
            "1.2.3",
            "--to",
            "2.0.0",
            old_path,
            new_path,
        ]
    )
    expected_passed_gate_fragments = [
        "semver-gate: pass",
        "severity: breaking",
        "required-bump: major",
        "actual-bump: major",
    ]
    for fragment in expected_passed_gate_fragments:
        if fragment not in passed_gate:
            raise AssertionError(f"missing passed semver gate fragment {fragment!r}:\n{passed_gate}")


if __name__ == "__main__":
    main()
