#!/usr/bin/env python3
import argparse
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import socketserver
import subprocess
import sys
import threading
from typing import Any


SCHEMA = "ahfl.release_evidence_archive.v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate AHFL release evidence archive")
    parser.add_argument("--ahflc", required=True, help="Path to the ahflc executable")
    parser.add_argument("--repo-root", required=True, help="Repository root")
    parser.add_argument("--out-dir", required=True, help="Output directory for evidence artifacts")
    parser.add_argument(
        "--timestamp",
        default="1970-01-01T00:00:00Z",
        help="UTC ISO 8601 timestamp recorded in the manifest",
    )
    return parser.parse_args()


def rel(repo: pathlib.Path, path: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def command_template(repo: pathlib.Path, ahflc: pathlib.Path, args: list[str]) -> list[str]:
    rendered = ["${ahflc}"]
    repo_root = repo.resolve()
    for arg in args:
        value = pathlib.Path(arg)
        if value.is_absolute():
            try:
                rendered.append("${repo}/" + value.resolve().relative_to(repo_root).as_posix())
                continue
            except ValueError:
                pass
        rendered.append(arg)
    return rendered


def normalize_repo_paths(value: Any, repo: pathlib.Path) -> Any:
    if isinstance(value, dict):
        return {key: normalize_repo_paths(item, repo) for key, item in value.items()}
    if isinstance(value, list):
        return [normalize_repo_paths(item, repo) for item in value]
    if isinstance(value, str):
        repo_text = repo.resolve().as_posix()
        if value == repo_text:
            return "${repo}"
        if value.startswith(repo_text + "/"):
            return "${repo}/" + value[len(repo_text) + 1 :]
    return value


def normalize_release_evidence_paths(value: Any, repo: pathlib.Path) -> Any:
    value = normalize_repo_paths(value, repo)
    if isinstance(value, dict):
        return {key: normalize_release_evidence_paths(item, repo) for key, item in value.items()}
    if isinstance(value, list):
        return [normalize_release_evidence_paths(item, repo) for item in value]
    if isinstance(value, str) and value.startswith("/"):
        path = pathlib.PurePosixPath(value)
        if "ahfl-registry-materialize-" in value and len(path.parts) >= 2:
            return "${registry-materialized}/" + "/".join(path.parts[-2:])
        return "${external-path}/" + path.name
    return value


def write_text(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: pathlib.Path, value: Any) -> None:
    write_text(path, json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")


def digest_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(65536), b""):
            digest.update(chunk)
    return "sha256:" + digest.hexdigest()


def run(
    args: list[str], cwd: pathlib.Path, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256_text(text: str) -> str:
    return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def public_api_snapshot(package: str, version: str, entries: list[dict[str, Any]]) -> str:
    return json.dumps(
        {
            "schema": "ahfl.public_api.v1",
            "package": {
                "name": package,
                "version": version,
                "module_prefix": package.replace("-", "_"),
                "manifest": "ahfl.toml",
            },
            "entries": entries,
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def registry_entry(
    package: str,
    version: str,
    public_api_sha256: str,
    *,
    yanked: bool = False,
) -> dict[str, Any]:
    return {
        "format_version": "ahfl.registry.index.v1",
        "registry_id": "default",
        "package": package,
        "version": version,
        "yanked": yanked,
        "source_archive_sha256": "sha256:" + ("a" * 64),
        "manifest_sha256": "sha256:" + ("b" * 64),
        "public_api_sha256": public_api_sha256,
        "dependencies": [{"name": "std", "source": "sysroot"}],
    }


class LocalRegistry:
    def __init__(self, previous_snapshots: dict[tuple[str, str], str]):
        self.previous_snapshots = previous_snapshots
        self.requests: list[dict[str, Any]] = []
        self.published: dict[tuple[str, str], dict[str, Any]] = {}
        self.published_artifacts: dict[tuple[str, str], dict[str, Any]] = {}
        self.httpd: http.server.HTTPServer | None = None
        self.thread: threading.Thread | None = None
        self.base_url = ""

    def __enter__(self) -> "LocalRegistry":
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                return

            def _read_json(self) -> tuple[str, dict[str, Any]]:
                length = int(self.headers.get("Content-Length", "0"))
                payload = self.rfile.read(length).decode("utf-8")
                return payload, json.loads(payload)

            def _write_json(self, status: int, payload: dict[str, Any]) -> None:
                body = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def _write_text(self, status: int, text: str) -> None:
                body = text.encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self) -> None:
                outer.requests.append({"method": "GET", "path": self.path})
                parts = self.path.strip("/").split("/")
                if len(parts) == 4 and parts[0] == "v1" and parts[1] == "packages" and parts[3] == "registry-index":
                    package = parts[2]
                    versions = [
                        registry_entry(pkg, version, sha256_text(snapshot))
                        for (pkg, version), snapshot in outer.previous_snapshots.items()
                        if pkg == package
                    ]
                    versions.extend(
                        entry
                        for (pkg, _), entry in outer.published.items()
                        if pkg == package
                    )
                    if not versions:
                        self._write_json(404, {"error": "not found"})
                        return
                    self._write_json(
                        200,
                        {
                            "format_version": "ahfl.registry.package_index.v1",
                            "package": package,
                            "versions": versions,
                        },
                    )
                    return
                if (
                    len(parts) == 6
                    and parts[0] == "v1"
                    and parts[1] == "packages"
                    and parts[3] == "versions"
                    and parts[5] == "registry-index"
                ):
                    entry = outer.published.get((parts[2], parts[4]))
                    if entry is None:
                        self._write_json(404, {"error": "not found"})
                        return
                    self._write_json(200, entry)
                    return
                if (
                    len(parts) == 6
                    and parts[0] == "v1"
                    and parts[1] == "packages"
                    and parts[3] == "versions"
                    and parts[5] == "source-archive"
                ):
                    artifacts = outer.published_artifacts.get((parts[2], parts[4]))
                    if artifacts is None:
                        self._write_json(404, {"error": "not found"})
                        return
                    self._write_json(200, artifacts["source_archive"])
                    return
                if (
                    len(parts) == 6
                    and parts[0] == "v1"
                    and parts[1] == "packages"
                    and parts[3] == "versions"
                    and parts[5] == "source-archive.payload"
                ):
                    artifacts = outer.published_artifacts.get((parts[2], parts[4]))
                    if artifacts is None:
                        self._write_json(404, {"error": "not found"})
                        return
                    self._write_text(200, artifacts["source_archive_payload"])
                    return
                if (
                    len(parts) == 6
                    and parts[0] == "v1"
                    and parts[1] == "packages"
                    and parts[3] == "versions"
                    and parts[5] == "public-api"
                ):
                    snapshot = outer.previous_snapshots.get((parts[2], parts[4]))
                    if snapshot is None:
                        artifacts = outer.published_artifacts.get((parts[2], parts[4]))
                        if artifacts is not None:
                            snapshot = artifacts["public_api_snapshot"]
                    if snapshot is None:
                        self._write_json(404, {"error": "not found"})
                        return
                    self._write_text(200, snapshot)
                    return
                self._write_json(404, {"error": "not found"})

            def do_PUT(self) -> None:
                payload, body = self._read_json()
                outer.requests.append({"method": "PUT", "path": self.path, "body": body})
                parts = self.path.strip("/").split("/")
                if (
                    len(parts) != 6
                    or parts[0] != "v1"
                    or parts[1] != "packages"
                    or parts[3] != "versions"
                    or parts[5] != "publish"
                    or body.get("format_version") != "ahfl.registry.publish_request.v1"
                ):
                    self._write_json(404, {"error": "not found"})
                    return
                entry = body.get("registry_index", {})
                require(body.get("source_archive_payload"), "publish request missing source payload")
                require(body.get("public_api_snapshot"), "publish request missing public API snapshot")
                outer.published[(parts[2], parts[4])] = dict(entry)
                outer.published_artifacts[(parts[2], parts[4])] = {
                    "source_archive": body.get("source_archive", {}),
                    "source_archive_payload": body.get("source_archive_payload", ""),
                    "public_api_snapshot": body.get("public_api_snapshot", ""),
                }
                self._write_json(200, entry)

            def do_POST(self) -> None:
                payload, body = self._read_json()
                outer.requests.append({"method": "POST", "path": self.path, "body": body})
                parts = self.path.strip("/").split("/")
                if (
                    len(parts) != 6
                    or parts[0] != "v1"
                    or parts[1] != "packages"
                    or parts[3] != "versions"
                    or parts[5] != "yank"
                    or body.get("format_version") != "ahfl.registry.yank_request.v1"
                ):
                    self._write_json(404, {"error": "not found"})
                    return
                entry = dict(outer.published.get((parts[2], parts[4]), {}))
                if not entry:
                    self._write_json(409, {"error": "not published"})
                    return
                entry["yanked"] = True
                self._write_json(200, entry)

        class ThreadingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
            daemon_threads = True
            allow_reuse_address = True

        self.httpd = ThreadingServer(("127.0.0.1", 0), Handler)
        host, port = self.httpd.server_address
        self.base_url = f"http://{host}:{port}/v1"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
        if self.thread is not None:
            self.thread.join(timeout=5)


class ArchiveBuilder:
    def __init__(self, repo: pathlib.Path, ahflc: pathlib.Path, out_dir: pathlib.Path, timestamp: str):
        self.repo = repo.resolve()
        self.ahflc = ahflc.resolve()
        self.out_dir = out_dir.resolve()
        self.timestamp = timestamp
        self.items: list[dict[str, Any]] = []

    def artifact(self, name: str) -> pathlib.Path:
        return self.out_dir / "artifacts" / name

    def add_item(
        self,
        *,
        evidence_id: str,
        covers: list[str],
        evidence_type: str,
        artifacts: list[pathlib.Path],
        command: list[str] | None = None,
        status: str = "passed",
        summary: str = "",
    ) -> None:
        self.items.append(
            {
                "id": evidence_id,
                "type": evidence_type,
                "covers": covers,
                "status": status,
                "summary": summary,
                "command": command,
                "artifacts": [
                    {
                        "path": rel(self.out_dir, artifact),
                        "digest": digest_file(artifact),
                    }
                    for artifact in artifacts
                ],
            }
        )

    def command_item(
        self,
        *,
        evidence_id: str,
        covers: list[str],
        args: list[str],
        artifact_name: str,
        parse_json: bool,
        validate,
        summary: str,
    ) -> Any:
        result = run([str(self.ahflc), *args], self.repo)
        require(
            result.returncode == 0,
            f"{evidence_id} failed with exit code {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
        require(result.stderr == "", f"{evidence_id} emitted stderr:\n{result.stderr}")
        artifact = self.artifact(artifact_name)
        if parse_json:
            payload = normalize_repo_paths(json.loads(result.stdout), self.repo)
            validate(payload)
            write_json(artifact, payload)
            value = payload
        else:
            validate(result.stdout)
            write_text(artifact, result.stdout)
            value = result.stdout
        self.add_item(
            evidence_id=evidence_id,
            covers=covers,
            evidence_type="command-output",
            artifacts=[artifact],
            command=command_template(self.repo, self.ahflc, args),
            summary=summary,
        )
        return value

    def public_api_evidence(self) -> None:
        manifest = self.repo / "tests/integration/package_graph_manifest/ahfl.toml"
        snapshot = self.command_item(
            evidence_id="rfc0009.public_api.snapshot.non_std_package",
            covers=["RFC0009"],
            args=["emit", "public-api", "--manifest", str(manifest), "--sysroot", str(self.repo)],
            artifact_name="public-api/refund-audit.public-api.json",
            parse_json=True,
            validate=validate_public_api_snapshot,
            summary="Generated public API snapshot for a non-std package.",
        )
        docs = self.command_item(
            evidence_id="rfc0009.public_api.docs.non_std_package",
            covers=["RFC0009"],
            args=["emit", "public-api-docs", "--manifest", str(manifest), "--sysroot", str(self.repo)],
            artifact_name="public-api/refund-audit.public-api.md",
            parse_json=False,
            validate=validate_public_api_docs,
            summary="Generated public API Markdown docs from visibility facts.",
        )
        require(docs, "public API docs evidence must not be empty")

        old_path = self.artifact("public-api/refund-audit.old.public-api.json")
        new_path = self.artifact("public-api/refund-audit.new.public-api.json")
        write_json(old_path, snapshot)
        write_json(new_path, snapshot)
        diff_result = run(
            [str(self.ahflc), "emit", "public-api-diff", str(old_path), str(new_path)], self.repo
        )
        require(diff_result.returncode == 0, f"public API diff failed:\n{diff_result.stderr}")
        require(diff_result.stderr == "", f"public API diff emitted stderr:\n{diff_result.stderr}")
        require("added: 0" in diff_result.stdout, diff_result.stdout)
        require("removed: 0" in diff_result.stdout, diff_result.stdout)
        require("changed: 0" in diff_result.stdout, diff_result.stdout)
        diff_path = self.artifact("public-api/refund-audit.public-api.diff.txt")
        write_text(diff_path, diff_result.stdout)
        self.add_item(
            evidence_id="rfc0009.public_api.diff.baseline",
            covers=["RFC0009"],
            evidence_type="command-output",
            artifacts=[old_path, new_path, diff_path],
            command=[
                "${ahflc}",
                "emit",
                "public-api-diff",
                rel(self.out_dir, old_path),
                rel(self.out_dir, new_path),
            ],
            summary="Compared two release-facing public API snapshots.",
        )

    def write_registry_fixture(self, package: str, version: str) -> pathlib.Path:
        package_root = self.out_dir / "fixtures" / package
        module_prefix = package.replace("-", "_")
        write_text(
            package_root / "ahfl.toml",
            f"""manifest_version = 2

[package]
name = "{package}"
version = "{version}"
edition = "2026"
kind = "library"

[module]
prefix = "{module_prefix}"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/main.ahfl"

[dependencies]
std = {{ source = "sysroot" }}
""",
        )
        write_text(
            package_root / "src/main.ahfl",
            f"""module {module_prefix}::main;

pub struct ScoreRequest {{
    amount: Int;
}}

pub fn score(req: ScoreRequest) -> Int effect Pure decreases 0 {{
    return req.amount;
}}
""",
        )
        return package_root / "ahfl.toml"

    def write_registry_consumer_fixture(
        self, package: str, version: str, dependency: str, requirement: str
    ) -> pathlib.Path:
        package_root = self.out_dir / "fixtures" / package
        module_prefix = package.replace("-", "_")
        write_text(
            package_root / "ahfl.toml",
            f"""manifest_version = 2

[package]
name = "{package}"
version = "{version}"
edition = "2026"
kind = "library"

[module]
prefix = "{module_prefix}"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/main.ahfl"

[dependencies]
std = {{ source = "sysroot" }}
{dependency} = {{ source = "registry", registry = "default", version = "{requirement}" }}
""",
        )
        write_text(
            package_root / "src/main.ahfl",
            f"""module {module_prefix}::main;

pub fn marker() -> Int effect Pure decreases 0 {{
    return 1;
}}
""",
        )
        return package_root / "ahfl.toml"

    def registry_publish_evidence(self) -> None:
        previous_release_demo = public_api_snapshot("release-demo", "0.1.0", [])
        removed_entry = {
            "api_id": "symbol:function:release_breaking::main::legacy",
            "entry_kind": "symbol",
            "symbol_kind": "function",
            "namespace": "consts",
            "local_name": "legacy",
            "canonical_name": "release_breaking::main::legacy",
            "module": "release_breaking::main",
            "source": "src/main.ahfl",
            "range": {"begin_offset": 0, "end_offset": 0},
            "symbol_id": 1,
            "alias_id": None,
            "target_symbol_id": None,
            "target_canonical_name": "",
            "signature_text": "pub fn legacy() -> Int effect Pure decreases 0",
            "signature": {"kind": "function"},
        }
        previous_breaking = public_api_snapshot("release-breaking", "1.0.0", [removed_entry])
        release_manifest = self.write_registry_fixture("release-demo", "0.2.0")
        breaking_manifest = self.write_registry_fixture("release-breaking", "1.0.1")

        previous_snapshots = {
            ("release-demo", "0.1.0"): previous_release_demo,
            ("release-breaking", "1.0.0"): previous_breaking,
        }
        with LocalRegistry(previous_snapshots) as registry:
            env = dict(os.environ)
            env["AHFL_REGISTRY_URL"] = registry.base_url

            dry_run_out = self.artifact("registry/release-demo-dry-run")
            dry_run_args = [
                "package",
                "publish",
                "--dry-run",
                "--manifest",
                str(release_manifest),
                "--registry",
                "default",
                "--sysroot",
                str(self.repo),
                "--out",
                str(dry_run_out),
                "--semver-gate",
                "--from",
                "0.1.0",
            ]
            dry_run = run([str(self.ahflc), *dry_run_args], self.repo, env=env)
            require(dry_run.returncode == 0, f"registry dry-run failed:\n{dry_run.stderr}")
            require(dry_run.stderr == "", f"registry dry-run emitted stderr:\n{dry_run.stderr}")
            require("semver-gate: pass" in dry_run.stdout, dry_run.stdout)
            dry_run_stdout = self.artifact("registry/release-demo.publish-dry-run.stdout.txt")
            write_text(dry_run_stdout, dry_run.stdout)
            dry_run_evidence = dry_run_out / "release-demo-0.2.0.publish-dry-run.json"
            dry_run_payload = json.loads(dry_run_evidence.read_text(encoding="utf-8"))
            require(dry_run_payload.get("semver_gate") == "pass", str(dry_run_payload))
            require(dry_run_payload.get("upload_performed") is False, str(dry_run_payload))
            self.add_item(
                evidence_id="rfc0010.registry_publish.dry_run_semver_gate",
                covers=["RFC0010"],
                evidence_type="command-artifacts",
                artifacts=[
                    dry_run_stdout,
                    dry_run_out / "release-demo-0.2.0.source-archive.json",
                    dry_run_out / "release-demo-0.2.0.source-archive.payload",
                    dry_run_out / "release-demo-0.2.0.public-api.json",
                    dry_run_out / "release-demo-0.2.0.registry-index.json",
                    dry_run_out / "release-demo-0.1.0.previous-public-api.json",
                    dry_run_evidence,
                ],
                command=command_template(self.repo, self.ahflc, dry_run_args),
                summary="Ran publish dry-run with previous-release SemVer gate against a local fixture registry.",
            )

            upload_out = self.artifact("registry/release-demo-upload")
            upload_args = [
                "package",
                "publish",
                "--manifest",
                str(release_manifest),
                "--registry",
                "default",
                "--sysroot",
                str(self.repo),
                "--out",
                str(upload_out),
            ]
            upload = run([str(self.ahflc), *upload_args], self.repo, env=env)
            require(upload.returncode == 0, f"registry upload failed:\n{upload.stderr}")
            require(upload.stderr == "", f"registry upload emitted stderr:\n{upload.stderr}")
            require("upload: performed" in upload.stdout, upload.stdout)
            upload_stdout = self.artifact("registry/release-demo.publish.stdout.txt")
            write_text(upload_stdout, upload.stdout)
            upload_evidence = upload_out / "release-demo-0.2.0.publish.json"
            upload_payload = json.loads(upload_evidence.read_text(encoding="utf-8"))
            require(upload_payload.get("upload_performed") is True, str(upload_payload))
            self.add_item(
                evidence_id="rfc0010.registry_publish.upload",
                covers=["RFC0010"],
                evidence_type="command-artifacts",
                artifacts=[
                    upload_stdout,
                    upload_out / "release-demo-0.2.0.source-archive.json",
                    upload_out / "release-demo-0.2.0.source-archive.payload",
                    upload_out / "release-demo-0.2.0.public-api.json",
                    upload_out / "release-demo-0.2.0.registry-index.json",
                    upload_evidence,
                ],
                command=command_template(self.repo, self.ahflc, upload_args),
                summary="Uploaded a package to a local fixture registry after local publish gates passed.",
            )

            resolve_manifest = self.write_registry_consumer_fixture(
                "release-consumer", "0.1.0", "release-demo", "^0.2.0"
            )
            resolve_lockfile = resolve_manifest.parent / "ahfl.lock"
            resolve_args = [
                "registry",
                "resolve",
                "--manifest",
                str(resolve_manifest),
                "--sysroot",
                str(self.repo),
                "--lockfile",
                str(resolve_lockfile),
            ]
            resolve = run([str(self.ahflc), *resolve_args], self.repo, env=env)
            require(resolve.returncode == 0, f"registry resolve failed:\n{resolve.stderr}")
            require(resolve.stderr == "", f"registry resolve emitted stderr:\n{resolve.stderr}")
            require("registry-resolve: pass" in resolve.stdout, resolve.stdout)
            lockfile_payload = json.loads(resolve_lockfile.read_text(encoding="utf-8"))
            locked_packages = {
                package.get("name"): package for package in lockfile_payload.get("packages", [])
            }
            release_demo = locked_packages.get("release-demo", {})
            require(release_demo.get("source") == "registry", str(lockfile_payload))
            require(release_demo.get("registry_id") == "default", str(lockfile_payload))
            require(release_demo.get("version") == "0.2.0", str(lockfile_payload))
            locked_edges = [
                edge
                for edge in lockfile_payload.get("edges", [])
                if edge.get("dependency") == "release-demo" and edge.get("source") == "registry"
            ]
            require(len(locked_edges) == 1, str(lockfile_payload))
            require(locked_edges[0].get("version_requirement") == "^0.2.0", str(locked_edges[0]))
            require(locked_edges[0].get("selected_version") == "0.2.0", str(locked_edges[0]))
            resolve_stdout = self.artifact("registry/release-consumer.registry-resolve.stdout.txt")
            resolve_lockfile_artifact = self.artifact("registry/release-consumer.ahfl.lock.json")
            normalized_lockfile = normalize_release_evidence_paths(lockfile_payload, self.repo)
            normalized_text = json.dumps(normalized_lockfile, sort_keys=True)
            require("/private/" not in normalized_text, normalized_text)
            require("/tmp/" not in normalized_text, normalized_text)
            require("/Users/" not in normalized_text, normalized_text)
            write_text(resolve_stdout, resolve.stdout)
            write_json(resolve_lockfile_artifact, normalized_lockfile)
            self.add_item(
                evidence_id="rfc0010.registry_resolve.lockfile",
                covers=["RFC0010"],
                evidence_type="command-artifacts",
                artifacts=[resolve_stdout, resolve_lockfile_artifact],
                command=command_template(self.repo, self.ahflc, resolve_args),
                summary="Resolved a manifest v2 registry dependency from the local fixture registry into ahfl.lock.",
            )

            yank_args = [
                "package",
                "yank",
                "release-demo@0.2.0",
                "--registry",
                "default",
                "--reason",
                "release evidence fixture",
            ]
            yank = run([str(self.ahflc), *yank_args], self.repo, env=env)
            require(yank.returncode == 0, f"registry yank failed:\n{yank.stderr}")
            require(yank.stderr == "", f"registry yank emitted stderr:\n{yank.stderr}")
            require("yanked: true" in yank.stdout, yank.stdout)
            yank_stdout = self.artifact("registry/release-demo.yank.stdout.txt")
            write_text(yank_stdout, yank.stdout)

            breaking_out = self.artifact("registry/release-breaking-rejected")
            breaking_args = [
                "package",
                "publish",
                "--dry-run",
                "--manifest",
                str(breaking_manifest),
                "--registry",
                "default",
                "--sysroot",
                str(self.repo),
                "--out",
                str(breaking_out),
                "--semver-gate",
                "--from",
                "1.0.0",
            ]
            breaking = run([str(self.ahflc), *breaking_args], self.repo, env=env)
            require(breaking.returncode != 0, "breaking SemVer fixture unexpectedly passed")
            require("semver-gate: fail" in breaking.stdout + breaking.stderr, breaking.stdout + breaking.stderr)
            require("requires a major version bump" in breaking.stdout + breaking.stderr, breaking.stdout + breaking.stderr)
            breaking_output = self.artifact("registry/release-breaking.semver-rejection.txt")
            write_text(breaking_output, breaking.stdout + breaking.stderr)

            transcript = self.artifact("registry/local-registry-transcript.json")
            write_json(transcript, registry.requests)
            self.add_item(
                evidence_id="rfc0010.registry_yank.local_fixture",
                covers=["RFC0010"],
                evidence_type="command-artifacts",
                artifacts=[yank_stdout, transcript],
                command=command_template(self.repo, self.ahflc, yank_args),
                summary="Yanked an immutable package version through the local fixture registry.",
            )
            self.add_item(
                evidence_id="rfc0010.registry_publish.semver_rejection",
                covers=["RFC0010"],
                evidence_type="expected-failure-output",
                artifacts=[breaking_output],
                command=command_template(self.repo, self.ahflc, breaking_args),
                summary="Proved publish-time SemVer gate rejects an incompatible patch release before upload.",
            )

    def package_and_sysroot_evidence(self) -> None:
        manifest = self.repo / "tests/integration/package_graph_manifest/ahfl.toml"
        self.command_item(
            evidence_id="rfc0005.package_graph.non_std_package",
            covers=["RFC0005"],
            args=["dump", "package-graph", "--manifest", str(manifest), "--sysroot", str(self.repo)],
            artifact_name="package/package-graph.json",
            parse_json=True,
            validate=validate_package_graph,
            summary="Resolved package graph for a non-std package with path and sysroot dependencies.",
        )
        self.command_item(
            evidence_id="rfc0005.lockfile.non_std_package",
            covers=["RFC0005"],
            args=["dump", "lockfile", "--manifest", str(manifest), "--sysroot", str(self.repo)],
            artifact_name="package/ahfl.lock.json",
            parse_json=True,
            validate=validate_lockfile,
            summary="Generated v1 lockfile for package identity and dependency edges.",
        )
        self.command_item(
            evidence_id="rfc0006.user_package_with_repo_sysroot",
            covers=["RFC0005", "RFC0006"],
            args=["check", "--manifest", str(manifest), "--target", "workflow", "--sysroot", str(self.repo)],
            artifact_name="sysroot/user-package-check.txt",
            parse_json=False,
            validate=lambda text: require("ok: checked 3 source(s)" in text, text),
            summary="Checked ordinary user package with repository source sysroot.",
        )

        source_sysroot = self.repo / "tests/integration/source_sysroot_cli"
        source_sysroot_manifest = source_sysroot / "std/ahfl.toml"
        self.command_item(
            evidence_id="rfc0006.source_sysroot_corelib_development",
            covers=["RFC0006"],
            args=["check", "--manifest", str(source_sysroot_manifest), "--sysroot", str(source_sysroot)],
            artifact_name="sysroot/source-sysroot-check.txt",
            parse_json=False,
            validate=lambda text: require("ok: checked 4 source(s)" in text, text),
            summary="Checked active std package as source sysroot without duplicate package/module errors.",
        )

    def vscode_bundled_sysroot_evidence(self) -> None:
        package_script = (self.repo / "scripts/package-vscode-vsix-release.sh").read_text(
            encoding="utf-8"
        )
        workflow = (self.repo / ".github/workflows/vscode-extension.yml").read_text(encoding="utf-8")
        package_inventory = (self.repo / "tools/vscode/test/packageInventory.js").read_text(
            encoding="utf-8"
        )
        toolchain = (self.repo / "tools/vscode/src/toolchain.ts").read_text(encoding="utf-8")

        require("cmake --build --preset build-release --target ahfl-lsp" in package_script,
                "VSIX package script must stage a release ahfl-lsp")
        require("cp std/ahfl.toml" in package_script, "VSIX package script must bundle std manifest")
        require("cp std/*.ahfl" in package_script, "VSIX package script must bundle std sources")
        require("pnpm run test:package-inventory" in workflow,
                "VSIX workflow must run package inventory gate")
        require("pnpm run test:vsix-install" in workflow,
                "VSIX workflow must run install smoke")
        require("scripts/package-vscode-vsix-release.sh" in workflow,
                "VSIX workflow must use platform packaging script")
        require("'std/ahfl.toml'" in package_inventory,
                "package inventory must require bundled std manifest")
        require("'std/prelude.ahfl'" in package_inventory,
                "package inventory must require bundled std source")
        require("server/ahfl-lsp" in package_inventory,
                "package inventory must require bundled server")
        require("bundledSysroot" in toolchain, "toolchain payload must include bundledSysroot")
        require("AHFL_SYSROOT" not in toolchain, "VS Code extension must not inject AHFL_SYSROOT")

        contract = {
            "platform_workflow": ".github/workflows/vscode-extension.yml",
            "package_script": "scripts/package-vscode-vsix-release.sh",
            "inventory_gate": "tools/vscode/test/packageInventory.js",
            "toolchain_config": "tools/vscode/src/toolchain.ts",
            "required_contracts": [
                "release ahfl-lsp staged into server/ahfl-lsp",
                "std/ahfl.toml and std/*.ahfl staged into bundled sysroot",
                "Marketplace package inventory requires bundled server and std",
                "VSIX install smoke validates platform package installation",
                "toolchain initialization exposes bundledSysroot without AHFL_SYSROOT",
            ],
        }
        artifact = self.artifact("sysroot/vsix-bundled-sysroot-contract.json")
        write_json(artifact, contract)
        self.add_item(
            evidence_id="rfc0006.vsix_bundled_sysroot_contract",
            covers=["RFC0006", "RFC0007"],
            evidence_type="repository-contract",
            artifacts=[artifact],
            summary="Verified platform VSIX bundled sysroot release contract.",
        )

    def lsp_multi_root_evidence(self) -> None:
        server = (self.repo / "src/tooling/lsp/server.cpp").read_text(encoding="utf-8")
        handlers = (self.repo / "tests/unit/tooling/lsp/server_handlers.cpp").read_text(
            encoding="utf-8"
        )

        require("toolchain_profiles_from_initialization_json" in server,
                "LSP server must parse initialization toolchain profiles")
        require("\"profiles\"" in server,
                "LSP server must consume initialization toolchain profiles")
        require("WorkspaceToolchainProfile" in server,
                "LSP server must keep workspace-scoped toolchain profiles")
        require("merge_toolchain_profiles" in server,
                "LSP server must merge initialization/configuration profiles")
        require("test_cross_workspace_path_dependency_rejects_mixed_toolchain_profiles" in handlers,
                "LSP handler tests must cover cross-workspace mixed toolchain profiles")
        require("E::toolchain_profile_ambiguous" in handlers,
                "LSP handler tests must assert ambiguous profile diagnostics")
        require("profiles.workspace_profiles.push_back" in handlers,
                "LSP handler tests must build multiple workspace profiles")

        contract = {
            "server": "src/tooling/lsp/server.cpp",
            "handler_tests": "tests/unit/tooling/lsp/server_handlers.cpp",
            "required_contracts": [
                "initializationOptions.ahfl.toolchain.profiles[] is parsed",
                "workspace-scoped toolchain profiles are stored separately",
                "initialization and configuration profiles are merged deterministically",
                "cross-workspace path dependencies with incompatible profiles report E::toolchain_profile_ambiguous",
            ],
        }
        artifact = self.artifact("lsp/multi-root-toolchain-profile-contract.json")
        write_json(artifact, contract)
        self.add_item(
            evidence_id="rfc0007.lsp_multi_root_toolchain_profiles",
            covers=["RFC0006", "RFC0007"],
            evidence_type="repository-contract",
            artifacts=[artifact],
            summary="Verified LSP multi-root toolchain profile release contract.",
        )

    def write_manifest(self) -> pathlib.Path:
        self.out_dir.mkdir(parents=True, exist_ok=True)
        manifest = {
            "schema": SCHEMA,
            "generated_at": self.timestamp,
            "is_release_ready": all(item["status"] == "passed" for item in self.items),
            "total_evidence_count": len(self.items),
            "passed_evidence_count": sum(1 for item in self.items if item["status"] == "passed"),
            "failed_evidence_count": sum(1 for item in self.items if item["status"] != "passed"),
            "evidence_items": sorted(self.items, key=lambda item: item["id"]),
        }
        path = self.out_dir / "release-evidence-archive.json"
        write_json(path, manifest)
        return path


def validate_public_api_snapshot(payload: dict[str, Any]) -> None:
    require(payload.get("schema") == "ahfl.public_api.v1", "unexpected public API schema")
    package = payload.get("package", {})
    require(package.get("name") == "refund-audit", f"unexpected package: {package}")
    require(package.get("manifest") == "ahfl.toml", f"manifest is not package-relative: {package}")
    names = {entry.get("canonical_name") for entry in payload.get("entries", [])}
    require(
        names
        == {
            "refund_audit::main::RefundAuditInput",
            "refund_audit::main::RefundAuditOutput",
            "refund_audit::main::RefundAuditWorkflow",
        },
        f"unexpected public API entries: {sorted(names)}",
    )
    for entry in payload.get("entries", []):
        require(not str(entry.get("canonical_name", "")).startswith("std::"), "dependency API leaked")
        source = str(entry.get("source", ""))
        require(source == "" or not pathlib.PurePosixPath(source).is_absolute(),
                f"source path is not package-relative: {source}")


def validate_public_api_docs(text: str) -> None:
    require("# Public API: refund-audit" in text, text)
    require("RefundAuditWorkflow" in text, text)
    require("std::" not in text, text)
    require("/Users/" not in text, text)


def validate_package_graph(payload: dict[str, Any]) -> None:
    packages = {package.get("name"): package for package in payload.get("packages", [])}
    require({"std", "refund-audit", "audit-core"}.issubset(packages.keys()), str(packages))
    require(packages["std"].get("source") == "sysroot", str(packages["std"]))
    require(packages["refund-audit"].get("source") == "root", str(packages["refund-audit"]))
    require(packages["audit-core"].get("source") == "path", str(packages["audit-core"]))
    module_prefixes = {package.get("module_prefix") for package in packages.values()}
    require({"std", "refund_audit", "audit_core"}.issubset(module_prefixes), str(module_prefixes))


def validate_lockfile(payload: dict[str, Any]) -> None:
    require(payload.get("format_version") == "ahfl.lock.v1", "unexpected lockfile version")
    require(payload.get("root_package") == "refund-audit", "unexpected root package")
    packages = {package.get("name"): package for package in payload.get("packages", [])}
    require(packages.get("std", {}).get("source") == "sysroot", str(packages))
    require(packages.get("audit-core", {}).get("manifest") == "packages/audit-core/ahfl.toml",
            str(packages))
    edges = {(edge.get("dependency"), edge.get("source")) for edge in payload.get("edges", [])}
    require(("std", "sysroot") in edges, str(edges))
    require(("audit-core", "path") in edges, str(edges))


def main() -> int:
    args = parse_args()
    repo = pathlib.Path(args.repo_root)
    ahflc = pathlib.Path(args.ahflc)
    out_dir = pathlib.Path(args.out_dir)
    if out_dir.exists():
        shutil.rmtree(out_dir)

    builder = ArchiveBuilder(repo, ahflc, out_dir, args.timestamp)
    builder.package_and_sysroot_evidence()
    builder.public_api_evidence()
    builder.registry_publish_evidence()
    builder.vscode_bundled_sysroot_evidence()
    builder.lsp_multi_root_evidence()
    manifest = builder.write_manifest()
    print(manifest)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(error, file=sys.stderr)
        raise
