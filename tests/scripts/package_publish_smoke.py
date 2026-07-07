#!/usr/bin/env python3
import json
import hashlib
import http.server
import os
import pathlib
import re
import shutil
import socketserver
import subprocess
import sys
import threading


SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")


def run_command(args, env=None):
    result = subprocess.run(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env
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
    if result.stderr:
        raise AssertionError(f"unexpected stderr for {' '.join(map(str, args))}: {result.stderr}")
    return result.stdout


def run_failing_command(args, env=None):
    result = subprocess.run(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env
    )
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


def cache_file_name(key):
    out = []
    for byte in key.encode("utf-8"):
        ch = chr(byte)
        if ch.isalnum() or ch in "-_.":
            out.append(ch)
        else:
            out.append(f"_{byte:02x}")
    return "".join(out) + ".json"


def sha256_digest(text):
    return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def public_api_snapshot(package, version, entries):
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
        separators=(",", ":"),
        sort_keys=True,
    )


def seed_previous_release_cache(cache_dir, package, version, snapshot):
    digest = sha256_digest(snapshot)
    registry_entry = {
        "format_version": "ahfl.registry.index.v1",
        "registry_id": "default",
        "package": package,
        "version": version,
        "yanked": False,
        "source_archive_sha256": "sha256:" + ("a" * 64),
        "manifest_sha256": "sha256:" + ("b" * 64),
        "public_api_sha256": digest,
        "dependencies": [{"name": "std", "source": "sysroot"}],
    }
    package_index = {
        "format_version": "ahfl.registry.package_index.v1",
        "package": package,
        "versions": [registry_entry],
    }
    write_text(
        cache_dir / cache_file_name(f"registry-package-index:{package}"),
        json.dumps(package_index, separators=(",", ":"), sort_keys=True),
    )
    write_text(cache_dir / cache_file_name(f"public-api-snapshot:{digest}"), snapshot)
    return digest


def write_publish_fixture(package_dir, version="0.2.0"):
    (package_dir / "src").mkdir(parents=True, exist_ok=True)
    (package_dir / "ahfl.toml").write_text(
        f"""manifest_version = 2

[package]
name = "publish-demo"
version = "{version}"
edition = "2026"
kind = "library"

[module]
prefix = "publish_demo"
root = "src"

[exports]
modules = ["main"]

[targets.lib]
kind = "library"
entry = "src/main.ahfl"

[dependencies]
std = {{ source = "sysroot" }}
""",
        encoding="utf-8",
    )
    (package_dir / "src/main.ahfl").write_text(
        """module publish_demo::main;

pub struct ScoreRequest {
    amount: Int;
}

pub fn score(req: ScoreRequest) -> Int effect Pure decreases 0 {
    return req.amount;
}
""",
        encoding="utf-8",
    )


class LocalRegistry:
    def __init__(self):
        self.requests = []
        self.published_entry = None
        self.httpd = None
        self.thread = None
        self.base_url = ""

    def __enter__(self):
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                return

            def _read_json(self):
                length = int(self.headers.get("Content-Length", "0"))
                payload = self.rfile.read(length).decode("utf-8")
                return payload, json.loads(payload)

            def _write_json(self, status, payload):
                body = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_PUT(self):
                payload, body = self._read_json()
                outer.requests.append(("PUT", self.path, payload))
                if self.path != "/v1/packages/publish-demo/versions/0.2.0/publish":
                    self._write_json(404, {"error": "not found"})
                    return
                if body.get("format_version") != "ahfl.registry.publish_request.v1":
                    self._write_json(400, {"error": "bad schema"})
                    return
                entry = body.get("registry_index", {})
                if not body.get("source_archive_payload") or not body.get("public_api_snapshot"):
                    self._write_json(400, {"error": "missing artifacts"})
                    return
                outer.published_entry = dict(entry)
                self._write_json(200, entry)

            def do_POST(self):
                payload, body = self._read_json()
                outer.requests.append(("POST", self.path, payload))
                if self.path != "/v1/packages/publish-demo/versions/0.2.0/yank":
                    self._write_json(404, {"error": "not found"})
                    return
                if body.get("format_version") != "ahfl.registry.yank_request.v1":
                    self._write_json(400, {"error": "bad schema"})
                    return
                if body.get("reason") != "bad release":
                    self._write_json(400, {"error": "missing reason"})
                    return
                if outer.published_entry is None:
                    self._write_json(409, {"error": "not published"})
                    return
                entry = dict(outer.published_entry)
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

    def __exit__(self, exc_type, exc, tb):
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
        if self.thread is not None:
            self.thread.join(timeout=5)


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: package_publish_smoke.py <ahflc> <repo-root> <work-dir>")

    ahflc = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(sys.argv[2])
    work_dir = pathlib.Path(sys.argv[3])
    if work_dir.exists():
        shutil.rmtree(work_dir)
    package_dir = work_dir / "publish-demo"
    out_dir = work_dir / "dist"
    out_dir.mkdir(parents=True)
    write_publish_fixture(package_dir)

    manifest = package_dir / "ahfl.toml"
    output = run_command(
        [
            ahflc,
            "package",
            "publish",
            "--dry-run",
            "--manifest",
            manifest,
            "--registry",
            "default",
            "--sysroot",
            repo,
            "--out",
            out_dir,
        ]
    )
    for fragment in [
        "publish-dry-run: pass",
        "registry: default",
        "upload: skipped",
        "source-archive-sha256:",
        "public-api-sha256:",
    ]:
        if fragment not in output:
            raise AssertionError(f"missing publish dry-run output fragment {fragment!r}:\n{output}")

    stem = "publish-demo-0.2.0"
    archive_manifest = out_dir / f"{stem}.source-archive.json"
    archive_payload = out_dir / f"{stem}.source-archive.payload"
    public_api = out_dir / f"{stem}.public-api.json"
    registry_index = out_dir / f"{stem}.registry-index.json"
    dry_run = out_dir / f"{stem}.publish-dry-run.json"
    for artifact in [archive_manifest, archive_payload, public_api, registry_index, dry_run]:
        if not artifact.is_file():
            raise AssertionError(f"missing publish dry-run artifact: {artifact}")

    archive = json.loads(archive_manifest.read_text(encoding="utf-8"))
    registry = json.loads(registry_index.read_text(encoding="utf-8"))
    evidence = json.loads(dry_run.read_text(encoding="utf-8"))
    api_snapshot = json.loads(public_api.read_text(encoding="utf-8"))

    if registry.get("format_version") != "ahfl.registry.index.v1":
        raise AssertionError(f"unexpected registry index schema: {registry}")
    if registry.get("package") != "publish-demo" or registry.get("version") != "0.2.0":
        raise AssertionError(f"unexpected registry coordinate: {registry}")
    if registry.get("registry_id") != "default":
        raise AssertionError(f"unexpected registry id: {registry}")
    if registry.get("dependencies") != [{"name": "std", "source": "sysroot"}]:
        raise AssertionError(f"unexpected registry dependencies: {registry}")

    assert_sha256(registry["source_archive_sha256"], "registry.source_archive_sha256")
    assert_sha256(registry["manifest_sha256"], "registry.manifest_sha256")
    assert_sha256(registry["public_api_sha256"], "registry.public_api_sha256")
    if registry["source_archive_sha256"] != archive["archive_sha256"]:
        raise AssertionError("registry source archive digest does not match archive manifest")
    if registry["manifest_sha256"] != archive["manifest_sha256"]:
        raise AssertionError("registry manifest digest does not match archive manifest")
    if evidence["public_api_sha256"] != registry["public_api_sha256"]:
        raise AssertionError("dry-run public API digest does not match registry index")
    if evidence["upload_performed"] is not False:
        raise AssertionError(f"dry-run evidence claims upload happened: {evidence}")
    if evidence["semver_gate"] != "not-run":
        raise AssertionError(f"unexpected semver gate status: {evidence}")
    if api_snapshot.get("package", {}).get("name") != "publish-demo":
        raise AssertionError(f"unexpected public API snapshot package: {api_snapshot}")

    upload_out = work_dir / "upload-dist"
    with LocalRegistry() as local_registry:
        upload_env = dict(os.environ)
        upload_env["AHFL_REGISTRY_URL"] = local_registry.base_url
        upload_output = run_command(
            [
                ahflc,
                "package",
                "publish",
                "--manifest",
                manifest,
                "--registry",
                "default",
                "--sysroot",
                repo,
                "--out",
                upload_out,
            ],
            env=upload_env,
        )
        for fragment in ["publish: pass", "registry: default", "upload: performed"]:
            if fragment not in upload_output:
                raise AssertionError(f"missing publish upload output fragment {fragment!r}:\n{upload_output}")
        upload_evidence = json.loads(
            (upload_out / "publish-demo-0.2.0.publish.json").read_text(encoding="utf-8")
        )
        if upload_evidence["format_version"] != "ahfl.publish.v1":
            raise AssertionError(f"unexpected publish evidence schema: {upload_evidence}")
        if upload_evidence["upload_performed"] is not True:
            raise AssertionError(f"publish evidence did not record upload: {upload_evidence}")
        if not any(method == "PUT" and path.endswith("/publish") for method, path, _ in local_registry.requests):
            raise AssertionError(f"publish request was not sent: {local_registry.requests}")

        yank_output = run_command(
            [
                ahflc,
                "package",
                "yank",
                "publish-demo@0.2.0",
                "--registry",
                "default",
                "--reason",
                "bad release",
            ],
            env=upload_env,
        )
        for fragment in [
            "package-yank: pass",
            "registry: default",
            "package: publish-demo",
            "version: 0.2.0",
            "yanked: true",
        ]:
            if fragment not in yank_output:
                raise AssertionError(f"missing package yank output fragment {fragment!r}:\n{yank_output}")
        if not any(method == "POST" and path.endswith("/yank") for method, path, _ in local_registry.requests):
            raise AssertionError(f"yank request was not sent: {local_registry.requests}")

    pass_cache = work_dir / "semver-pass-cache"
    pass_cache.mkdir()
    previous_snapshot = public_api_snapshot("publish-demo", "0.1.0", [])
    seed_previous_release_cache(pass_cache, "publish-demo", "0.1.0", previous_snapshot)
    pass_out = work_dir / "semver-pass-dist"
    pass_env = dict(os.environ)
    pass_env["AHFL_PACKAGE_CACHE"] = str(pass_cache)
    pass_env["AHFL_REGISTRY_URL"] = "http://127.0.0.1:9"
    semver_pass_output = run_command(
        [
            ahflc,
            "package",
            "publish",
            "--dry-run",
            "--manifest",
            manifest,
            "--registry",
            "default",
            "--sysroot",
            repo,
            "--out",
            pass_out,
            "--semver-gate",
            "--from",
            "0.1.0",
        ],
        env=pass_env,
    )
    if "semver-gate: pass" not in semver_pass_output:
        raise AssertionError(f"missing SemVer pass output:\n{semver_pass_output}")
    pass_evidence = json.loads(
        (pass_out / "publish-demo-0.2.0.publish-dry-run.json").read_text(encoding="utf-8")
    )
    if pass_evidence["semver_gate"] != "pass":
        raise AssertionError(f"unexpected SemVer pass evidence: {pass_evidence}")
    if pass_evidence["semver_from"] != "0.1.0" or pass_evidence["semver_to"] != "0.2.0":
        raise AssertionError(f"unexpected SemVer versions in evidence: {pass_evidence}")
    previous_artifact = pathlib.Path(pass_evidence["artifacts"]["previous_public_api_snapshot"])
    if not previous_artifact.is_file():
        raise AssertionError(f"missing previous public API artifact: {previous_artifact}")

    fail_cache = work_dir / "semver-fail-cache"
    fail_cache.mkdir()
    removed_entry = {
        "api_id": "symbol:function:publish_demo::main::legacy",
        "entry_kind": "symbol",
        "symbol_kind": "function",
        "namespace": "consts",
        "local_name": "legacy",
        "canonical_name": "publish_demo::main::legacy",
        "module": "publish_demo::main",
        "source": "src/main.ahfl",
        "range": {"begin_offset": 0, "end_offset": 0},
        "symbol_id": 1,
        "alias_id": None,
        "target_symbol_id": None,
        "target_canonical_name": "",
        "signature_text": "pub fn legacy() -> Int effect Pure decreases 0",
        "signature": {"kind": "function"},
    }
    failing_previous_snapshot = public_api_snapshot("publish-demo", "1.0.0", [removed_entry])
    seed_previous_release_cache(fail_cache, "publish-demo", "1.0.0", failing_previous_snapshot)
    write_publish_fixture(package_dir, version="1.0.1")
    fail_out = work_dir / "semver-fail-dist"
    fail_env = dict(os.environ)
    fail_env["AHFL_PACKAGE_CACHE"] = str(fail_cache)
    fail_env["AHFL_REGISTRY_URL"] = "http://127.0.0.1:9"
    semver_fail = run_failing_command(
        [
            ahflc,
            "package",
            "publish",
            "--dry-run",
            "--manifest",
            manifest,
            "--registry",
            "default",
            "--sysroot",
            repo,
            "--out",
            fail_out,
            "--semver-gate",
            "--from",
            "1.0.0",
        ],
        env=fail_env,
    )
    if "semver-gate: fail" not in semver_fail or "requires a major version bump" not in semver_fail:
        raise AssertionError(f"missing SemVer fail diagnostic:\n{semver_fail}")

    missing_out = run_failing_command(
        [
            ahflc,
            "package",
            "publish",
            "--manifest",
            manifest,
            "--registry",
            "default",
        ]
    )
    if "package publish requires --out <dir>" not in missing_out:
        raise AssertionError(f"missing publish --out diagnostic:\n{missing_out}")

    bad_yank_coordinate = run_failing_command(
        [
            ahflc,
            "package",
            "yank",
            "PublishDemo@0.2.0",
            "--registry",
            "default",
        ]
    )
    if "package yank requires exactly one <package>@<version> coordinate" not in bad_yank_coordinate:
        raise AssertionError(f"missing bad yank coordinate diagnostic:\n{bad_yank_coordinate}")

    wrong_command = run_failing_command(
        [
            ahflc,
            "check",
            "--registry",
            "default",
            manifest,
        ]
    )
    if "--registry is only supported with package publish or package yank" not in wrong_command:
        raise AssertionError(f"missing --registry misuse diagnostic:\n{wrong_command}")


if __name__ == "__main__":
    main()
