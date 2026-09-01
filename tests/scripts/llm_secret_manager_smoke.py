#!/usr/bin/env python3
import base64
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


class QuietHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return


class SmokeHTTPServer(ThreadingHTTPServer):
    daemon_threads = True


def send_json(handler, status, payload):
    body = json.dumps(payload).encode("utf-8")
    try:
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(body)))
        handler.end_headers()
        handler.wfile.write(body)
    except (BrokenPipeError, ConnectionResetError, OSError):
        return


def start_server(handler):
    server = SmokeHTTPServer(("127.0.0.1", 0), handler)
    server.request_count = 0
    server.auth_headers = []
    server.paths = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def stop_servers(*servers):
    for server in servers:
        server.shutdown()
        server.server_close()


def request_path(raw_path):
    return unquote(urlsplit(raw_path).path)


def write_smoke_source(path):
    path.write_text(
        """module smoke;

struct Request {
    value: String;
}

struct Context {
    value: String = "pending";
}

struct Response {
    value: String;
}

capability Echo(request: Request) -> Response;

agent EchoAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for EchoAgent {
    state Init {
        let reply = Echo(Request { value: input.value });
        ctx.value = reply.value;
        goto Done;
    }

    state Done {
        return Response { value: ctx.value };
    }
}

workflow SmokeWorkflow {
    input: Request;
    output: Response;

    node first: EchoAgent(input);

    return: Response { value: first.value };
}
""",
        encoding="utf-8",
    )


def make_llm_handler(expected_api_key, response_value):
    class LLMHandler(QuietHandler):
        def do_POST(self):
            self.server.request_count += 1
            self.server.paths.append(request_path(self.path))
            auth_header = self.headers.get("Authorization", "")
            self.server.auth_headers.append(auth_header)
            length = int(self.headers.get("Content-Length", "0"))
            self.rfile.read(length)

            if request_path(self.path) != "/v1/chat/completions":
                send_json(self, 404, {"error": "unknown path"})
                return
            if auth_header != f"Bearer {expected_api_key}":
                send_json(self, 401, {"error": "unexpected api key"})
                return

            send_json(
                self,
                200,
                {
                    "choices": [
                        {
                            "message": {
                                "content": json.dumps({"value": response_value}),
                            }
                        }
                    ]
                },
            )

    return LLMHandler


def make_cloud_secret_handler(mode, expected_token, secret_value):
    class CloudSecretHandler(QuietHandler):
        def do_GET(self):
            self.server.request_count += 1
            self.server.paths.append(request_path(self.path))
            auth_header = self.headers.get("Authorization", "")
            self.server.auth_headers.append(auth_header)

            if mode == "timeout":
                time.sleep(2.5)

            if auth_header != f"Bearer {expected_token}":
                send_json(self, 401, {"error": "unauthorized"})
                return
            if mode == "not_found":
                send_json(self, 404, {"error": "not found"})
                return

            expected_path = (
                "/v1/projects/agent-prod/secrets/llm/api-key/versions/7:access"
            )
            if request_path(self.path) != expected_path:
                send_json(self, 404, {"error": "unknown secret"})
                return

            encoded = base64.b64encode(secret_value.encode("utf-8")).decode("ascii")
            send_json(self, 200, {"payload": {"data": encoded}})

    return CloudSecretHandler


def make_vault_secret_handler(mode, expected_token, secret_value):
    class VaultSecretHandler(QuietHandler):
        def do_GET(self):
            self.server.request_count += 1
            self.server.paths.append(request_path(self.path))
            auth_header = self.headers.get("X-Vault-Token", "")
            self.server.auth_headers.append(auth_header)

            if mode == "timeout":
                time.sleep(2.5)

            if auth_header != expected_token:
                send_json(self, 401, {"errors": ["unauthorized"]})
                return
            if mode == "not_found":
                send_json(self, 404, {"errors": ["not found"]})
                return

            if request_path(self.path) != "/v1/kv/data/llm/api-key":
                send_json(self, 404, {"errors": ["unknown secret"]})
                return

            send_json(self, 200, {"data": {"data": {"value": secret_value}}})

    return VaultSecretHandler


def write_config(
    path,
    endpoint,
    provider_kind,
    secret_address,
    token_env,
    api_key_secret,
    refresh_secrets_before_use=False,
):
    provider = {
        "kind": provider_kind,
        "prefix": provider_kind,
        "address": secret_address,
        "token_env": token_env,
        "timeout_seconds": 1,
    }
    if provider_kind == "cloud":
        provider["project"] = "agent-prod"
        provider["version"] = "7"
    else:
        provider["mount_path"] = "kv"

    path.write_text(
        json.dumps(
            {
                "endpoint": endpoint,
                "model": f"{provider_kind}-secret-model",
                "api_key_secret": api_key_secret,
                "refresh_secrets_before_use": refresh_secrets_before_use,
                "max_retries": 0,
                "response_cache_enabled": False,
                "secret_providers": [provider],
            }
        ),
        encoding="utf-8",
    )


def run_ahflc(ahflc, source_path, config_path, events_path, env):
    result = subprocess.run(
        [
            ahflc,
            "run",
            "--workflow",
            "smoke::SmokeWorkflow",
            "--input",
            '{"_type":"smoke::Request","value":"hello"}',
            "--llm-config",
            str(config_path),
            "--output-format",
            "jsonl",
            "--verbosity",
            "trace",
            str(source_path),
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
    )
    events_path.write_text(result.stdout, encoding="utf-8")
    return result


def assert_secret_free_text(text, forbidden_values, label):
    for value in forbidden_values:
        if value and value in text:
            raise AssertionError(f"{label} leaked secret value {value!r}")


def assert_success_artifact(
    path,
    forbidden_values,
    expect_secret_refresh=False,
    expected_provider_prefix=None,
):
    if not path.exists():
        raise AssertionError(f"missing canonical execution event stream: {path}")
    text = path.read_text(encoding="utf-8")
    assert_secret_free_text(text, forbidden_values, "canonical execution event stream")
    events = [json.loads(line) for line in text.splitlines() if line]
    if not events:
        raise AssertionError("canonical execution event stream is empty")
    if any(event.get("schema") != "ahfl.run-event" for event in events):
        raise AssertionError(f"unexpected execution event schema: {events!r}")
    if [event.get("event_id") for event in events] != list(range(len(events))):
        raise AssertionError(f"execution event ids are not contiguous: {events!r}")
    if events[-1].get("type") != "run_completed":
        raise AssertionError(f"execution event stream lacks terminal run event: {events!r}")
    if events[-1].get("payload", {}).get("status") != "completed":
        raise AssertionError(f"secret-backed run did not complete: {events[-1]!r}")


def run_secret_case(ahflc, run_dir, source_path, provider_kind, mode):
    token_env = f"AHFL_TEST_{provider_kind.upper()}_SECRET_TOKEN"
    expected_token = f"{provider_kind}-secret-token"
    configured_token = "wrong-token" if mode == "auth_failure" else expected_token
    api_key = f"{provider_kind}-resolved-api-key"
    secret_key = "missing/api-key" if mode == "not_found" else "llm/api-key"
    api_key_secret = f"{provider_kind}:{secret_key}"
    expect_success = mode == "success"

    if provider_kind == "cloud":
        secret_handler = make_cloud_secret_handler(
            "not_found" if mode == "not_found" else mode,
            expected_token,
            api_key,
        )
    else:
        secret_handler = make_vault_secret_handler(
            "not_found" if mode == "not_found" else mode,
            expected_token,
            api_key,
        )

    secret_server = start_server(secret_handler)
    llm_server = start_server(make_llm_handler(api_key, f"{provider_kind}-{mode}"))
    try:
        config_path = run_dir / f"{provider_kind}_{mode}_config.json"
        observability_path = run_dir / f"{provider_kind}_{mode}_observability.json"
        write_config(
            config_path,
            f"http://127.0.0.1:{llm_server.server_port}/v1",
            provider_kind,
            f"http://127.0.0.1:{secret_server.server_port}",
            token_env,
            api_key_secret,
            refresh_secrets_before_use=expect_success,
        )

        env = os.environ.copy()
        env[token_env] = configured_token
        result = run_ahflc(ahflc, source_path, config_path, observability_path, env)
    finally:
        stop_servers(secret_server, llm_server)

    forbidden_values = [expected_token, configured_token, api_key]
    combined_output = result.stdout + result.stderr
    assert_secret_free_text(combined_output, forbidden_values, "ahflc output")

    if expect_success:
        if result.returncode != 0:
            raise AssertionError(
                f"{provider_kind} secret success failed with {result.returncode}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )
        if secret_server.request_count != 1:
            raise AssertionError(
                f"{provider_kind} secret success expected one secret request: "
                f"{secret_server.request_count}, paths={secret_server.paths!r}"
            )
        if llm_server.request_count != 1:
            raise AssertionError(
                f"{provider_kind} secret success expected one LLM request: "
                f"{llm_server.request_count}, paths={llm_server.paths!r}"
            )
        if llm_server.auth_headers != [f"Bearer {api_key}"]:
            raise AssertionError(
                f"{provider_kind} LLM request did not use resolved secret: "
                f"{llm_server.auth_headers!r}"
            )
        assert_success_artifact(
            observability_path,
            forbidden_values + [api_key_secret],
            expect_secret_refresh=True,
            expected_provider_prefix=provider_kind,
        )
        return

    if result.returncode == 0:
        raise AssertionError(f"{provider_kind} {mode} unexpectedly succeeded")
    expected = f"failed to resolve LLM api_key_secret '{api_key_secret}'"
    if expected not in combined_output:
        raise AssertionError(
            f"{provider_kind} {mode} missing secret resolution diagnostic:\n"
            f"{combined_output}"
        )
    if secret_server.request_count != 1:
        raise AssertionError(
            f"{provider_kind} {mode} expected one secret request: "
            f"{secret_server.request_count}, paths={secret_server.paths!r}"
        )
    if llm_server.request_count != 0:
        raise AssertionError(
            f"{provider_kind} {mode} called LLM after secret resolution failed"
        )
    if observability_path.exists():
        text = observability_path.read_text(encoding="utf-8")
        assert_secret_free_text(text, forbidden_values, "failure observability artifact")


def run_oauth2_token_secret_case(ahflc, run_dir, source_path):
    access_token = "oauth2-access-token-value"
    llm_server = start_server(make_llm_handler(access_token, "oauth2-success"))
    try:
        config_path = run_dir / "oauth2_token_secret_config.json"
        observability_path = run_dir / "oauth2_token_secret_observability.json"
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{llm_server.server_port}/v1",
                    "model": "oauth2-secret-model",
                    "auth_scheme": "oauth2_client_credentials",
                    "oauth2_token_secret": "AHFL_TEST_LLM_OAUTH2_TOKEN",
                    "max_retries": 0,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["AHFL_TEST_LLM_OAUTH2_TOKEN"] = access_token
        result = run_ahflc(ahflc, source_path, config_path, observability_path, env)
    finally:
        stop_servers(llm_server)

    forbidden_values = [access_token]
    combined_output = result.stdout + result.stderr
    assert_secret_free_text(combined_output, forbidden_values, "ahflc output")

    if result.returncode != 0:
        raise AssertionError(
            f"oauth2 token secret case failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if llm_server.request_count != 1:
        raise AssertionError(
            f"oauth2 token secret expected one LLM request: "
            f"{llm_server.request_count}, paths={llm_server.paths!r}"
        )
    if llm_server.auth_headers != [f"Bearer {access_token}"]:
        raise AssertionError(
            f"oauth2 token secret did not use resolved bearer token: "
            f"{llm_server.auth_headers!r}"
        )
    assert_success_artifact(observability_path, forbidden_values)


def run_mtls_curl_config_case(ahflc, run_dir, source_path):
    fake_bin_dir = run_dir / "fake-curl-bin"
    fake_bin_dir.mkdir()
    captured_config_path = run_dir / "mtls_curl_config.txt"
    fake_curl_path = fake_bin_dir / "curl"
    fake_curl_path.write_text(
        """#!/usr/bin/env python3
import json
import os
import sys

config = sys.stdin.read()
capture_path = os.environ["AHFL_FAKE_CURL_CONFIG"]
with open(capture_path, "w", encoding="utf-8") as handle:
    handle.write(config)

payload = {
    "choices": [
        {
            "message": {
                "content": json.dumps({"value": "mtls-success"}),
            }
        }
    ]
}
sys.stdout.write(json.dumps(payload) + "\\n200\\n")
""",
        encoding="utf-8",
    )
    fake_curl_path.chmod(0o755)

    config_path = run_dir / "mtls_config.json"
    observability_path = run_dir / "mtls_observability.json"
    cert_path = run_dir / "client.pem"
    key_path = run_dir / "client-key.pem"
    ca_path = run_dir / "ca.pem"
    cert_path.write_text("fake client cert\n", encoding="utf-8")
    key_path.write_text("fake client key\n", encoding="utf-8")
    ca_path.write_text("fake ca cert\n", encoding="utf-8")
    config_path.write_text(
        json.dumps(
            {
                "endpoint": "https://mtls.example.test/v1",
                "model": "mtls-model",
                "auth_scheme": "mtls",
                "mtls_client_cert_path": str(cert_path),
                "mtls_client_key_path": str(key_path),
                "mtls_ca_cert_path": str(ca_path),
                "mtls_verify_tls": False,
                "max_retries": 0,
                "response_cache_enabled": False,
            }
        ),
        encoding="utf-8",
    )

    env = os.environ.copy()
    env["PATH"] = f"{fake_bin_dir}{os.pathsep}{env.get('PATH', '')}"
    env["AHFL_FAKE_CURL_CONFIG"] = str(captured_config_path)
    result = run_ahflc(ahflc, source_path, config_path, observability_path, env)

    if result.returncode != 0:
        raise AssertionError(
            f"mTLS curl config case failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not captured_config_path.exists():
        raise AssertionError("fake curl did not capture curl config")
    captured_config = captured_config_path.read_text(encoding="utf-8")
    expected_fragments = [
        f'cert = "{cert_path}"',
        f'key = "{key_path}"',
        f'cacert = "{ca_path}"',
        "insecure",
        'url = "https://mtls.example.test/v1/chat/completions"',
    ]
    for fragment in expected_fragments:
        if fragment not in captured_config:
            raise AssertionError(f"mTLS curl config missing {fragment!r}:\n{captured_config}")
    if "Authorization:" in captured_config or "x-api-key:" in captured_config:
        raise AssertionError(f"mTLS curl config should not include API auth header:\n{captured_config}")
    assert_success_artifact(observability_path, [])


# RFC 0026 C2b G4b: two-phase CLI admission. `ahflc run` first runs a PURE
# Phase A (parse the --capability-bindings descriptor, project each capability's
# declared response type into a verified wire schema, and exact-decode --input)
# BEFORE Phase B ever touches the network (build_llm_secret_manager /
# resolve_llm_credentials authenticate to Vault etc.). The two cases below prove
# that a Phase A failure aborts with ZERO secret-provider and ZERO LLM traffic:
# both the local Vault server and the LLM server must observe request_count == 0.
#
# The complementary evidence — that the capability transport itself issues zero
# requests on a fail-closed admission — lives in
# runtime_capability_bindings_smoke.py, which proves that when a CLI Phase-A
# TextPlain configuration is rejected the capability transport makes zero
# requests (its fake-curl 0-request assertions). The factory-minted poison
# binding's fine-grained properties (retry=0, CB-off, attempts==1, zero
# transport) are pinned separately by the capability_bridge unit test. These
# layers are intentionally complementary: this file proves 0 secret/LLM network,
# that file proves 0 capability transport network.
_A1_PACKAGE_MANIFEST = """manifest_version = 1

[package]
name = "g4b-two-phase-admission"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "smoke"
root = "."

[targets.workflow]
kind = "handoff"
entry = "smoke::main::SmokeWorkflow"
exports = [
  { kind = "workflow", name = "smoke::main::SmokeWorkflow" },
  { kind = "agent", name = "smoke::main::EchoAgent" },
]

[dependencies]
std = { source = "sysroot" }
"""

# A legal, fully compilable producer. `BadMap` returns std::collections::Map with
# a non-String key (`Map<Int, Int>`), which is a legal AHFL type but NOT
# projectable to a wire schema (core.wire.UNSUPPORTED_MAP_KEY). The workflow never
# calls BadMap; the descriptor merely binds it, so admission fails purely on the
# projection, proving "legal producer + independent wire-projection failure".
_A1_PACKAGE_SOURCE = """module smoke::main;

import std::collections as collections;

pub struct Request {
    value: String;
}

pub struct Context {
    value: String = "pending";
}

pub struct Response {
    value: String;
}

pub capability Echo(request: Request) -> Response;
pub capability BadMap(n: Int) -> collections::Map<Int, Int>(4);

pub agent EchoAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for EchoAgent {
    state Init {
        let reply = Echo(Request { value: input.value });
        ctx.value = reply.value;
        goto Done;
    }

    state Done {
        return Response { value: ctx.value };
    }
}

pub workflow SmokeWorkflow {
    input: Request;
    output: Response;

    node first: EchoAgent(input);

    return: Response { value: first.value };
}
"""


def sysroot_root():
    # The std sysroot (std/ahfl.toml) lives at the repo root; this script is at
    # <repo>/tests/scripts/llm_secret_manager_smoke.py.
    repo_root = Path(__file__).resolve().parents[2]
    std_manifest = repo_root / "std" / "ahfl.toml"
    if not std_manifest.is_file():
        raise AssertionError(f"std sysroot manifest not found at {std_manifest}")
    return repo_root


def write_a1_package(pkg_dir):
    pkg_dir.mkdir(parents=True, exist_ok=True)
    (pkg_dir / "ahfl.toml").write_text(_A1_PACKAGE_MANIFEST, encoding="utf-8")
    (pkg_dir / "main.ahfl").write_text(_A1_PACKAGE_SOURCE, encoding="utf-8")


def run_two_phase_admission_case(
    ahflc,
    run_dir,
    label,
    *,
    bindings,
    input_json,
    forbidden_payload_tokens=(),
    resume_pending_result_json=None,
):
    # Drive `ahflc run` on the A1 package with a live Vault-backed LLM config, so
    # a run that reaches Phase B WOULD authenticate to Vault and call the LLM. The
    # case asserts that a Phase A admission failure keeps BOTH counters at 0.
    #
    # `forbidden_payload_tokens` are unique markers embedded in the user-controlled
    # request / descriptor / raw pending payload; a Phase A diagnostic must never
    # echo them, so they are added to the no-echo forbidden set alongside the
    # Vault/LLM secrets.
    pkg_dir = run_dir / f"two_phase_{label}_pkg"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    write_a1_package(pkg_dir)

    expected_token = "vault-secret-token"
    api_key = "vault-resolved-api-key"
    vault_server = start_server(
        make_vault_secret_handler("success", expected_token, api_key)
    )
    llm_server = start_server(make_llm_handler(api_key, "should-not-run"))
    try:
        config_path = run_dir / f"two_phase_{label}_config.json"
        write_config(
            config_path,
            f"http://127.0.0.1:{llm_server.server_port}/v1",
            "vault",
            f"http://127.0.0.1:{vault_server.server_port}",
            "AHFL_TEST_VAULT_SECRET_TOKEN",
            "vault:llm/api-key",
            refresh_secrets_before_use=True,
        )

        args = [
            ahflc,
            "run",
            "--manifest",
            str(pkg_dir / "ahfl.toml"),
            "--target",
            "workflow",
            "--sysroot",
            str(sysroot_root()),
            "--workflow",
            "smoke::main::SmokeWorkflow",
            "--input",
            input_json,
            "--llm-config",
            str(config_path),
        ]
        if bindings is not None:
            bindings_path = run_dir / f"two_phase_{label}_bindings.json"
            bindings_path.write_text(json.dumps(bindings), encoding="utf-8")
            args += ["--capability-bindings", str(bindings_path)]
        if resume_pending_result_json is not None:
            args += ["--resume-pending-result", resume_pending_result_json]

        env = os.environ.copy()
        env["AHFL_TEST_VAULT_SECRET_TOKEN"] = expected_token
        result = subprocess.run(
            args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(vault_server, llm_server)

    combined_output = result.stdout + result.stderr
    # No secret material may leak into diagnostics — Vault token, resolved API
    # key, and any unique user-payload / descriptor markers.
    forbidden = [expected_token, api_key, *forbidden_payload_tokens]
    assert_secret_free_text(combined_output, forbidden, f"{label} output")
    return result, combined_output, vault_server, llm_server


def run_a1_non_projectable_binding_case(ahflc, run_dir):
    # A1: the descriptor binds `smoke::main::BadMap`, whose Map<Int, Int> response
    # is not projectable to a wire schema. Phase A migration rejects it before any
    # secret/network work — Vault and LLM counters stay 0.
    bindings = {
        "schema": "ahfl.runtime_capability_bindings.v0",
        "bindings": [
            {
                "capability": "smoke::main::BadMap",
                "transport": "http",
                # The URL path carries a unique marker so the no-echo assertion
                # proves the projection diagnostic never reflects descriptor
                # payload back to the user.
                "url": "http://capability.example.test/G4B_A1_URL_SECRET",
                "method": "POST",
                "timeout_ms": 1000,
                "retry": {
                    "max_retries": 0,
                    "initial_delay_ms": 0,
                    "backoff_multiplier": 1.0,
                },
            }
        ],
    }
    result, combined_output, vault_server, llm_server = run_two_phase_admission_case(
        ahflc,
        run_dir,
        "a1_non_projectable",
        bindings=bindings,
        input_json='{"_type":"smoke::main::Request","value":"hello"}',
        forbidden_payload_tokens=["G4B_A1_URL_SECRET"],
    )
    if result.returncode == 0:
        raise AssertionError(
            "A1 non-projectable binding unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    expected = "core.wire.UNSUPPORTED_MAP_KEY"
    if expected not in combined_output:
        raise AssertionError(
            f"A1 missing projection diagnostic {expected!r}:\n{combined_output}"
        )
    if "not projectable to a wire schema" not in combined_output:
        raise AssertionError(
            f"A1 missing 'not projectable to a wire schema' phrasing:\n{combined_output}"
        )
    if vault_server.request_count != 0:
        raise AssertionError(
            f"A1 admission failure called Vault before Phase A completed: "
            f"{vault_server.request_count}, paths={vault_server.paths!r}"
        )
    if llm_server.request_count != 0:
        raise AssertionError(
            f"A1 admission failure called LLM before Phase A completed: "
            f"{llm_server.request_count}, paths={llm_server.paths!r}"
        )


def run_a2_resolved_input_mismatch_case(ahflc, run_dir):
    # A2: no descriptor at all; the resolved SmokeWorkflow rejects a hostile
    # --input during Phase A exact decode. The input is a wrong-kind OBJECT where
    # Request.value is String, carrying a unique token in the payload so the
    # no-echo assertion proves the exact-decode diagnostic never reflects the user
    # payload back. No secret/network work runs — Vault and LLM counters stay 0.
    input_token = "G4B_A2_INPUT_SECRET"
    result, combined_output, vault_server, llm_server = run_two_phase_admission_case(
        ahflc,
        run_dir,
        "a2_input_mismatch",
        bindings=None,
        input_json=(
            '{"_type":"smoke::main::Request",'
            '"value":{"_type":"hostile::Payload","secret":"' + input_token + '"}}'
        ),
        forbidden_payload_tokens=[input_token],
    )
    if result.returncode == 0:
        raise AssertionError(
            "A2 hostile --input unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    expected = "does not match workflow input schema"
    if expected not in combined_output:
        raise AssertionError(
            f"A2 missing input-schema diagnostic {expected!r}:\n{combined_output}"
        )
    if "wire-codec: expected string" not in combined_output:
        raise AssertionError(
            f"A2 missing exact-decode diagnostic 'wire-codec: expected string':\n"
            f"{combined_output}"
        )
    if vault_server.request_count != 0:
        raise AssertionError(
            f"A2 input mismatch called Vault before Phase A completed: "
            f"{vault_server.request_count}, paths={vault_server.paths!r}"
        )
    if llm_server.request_count != 0:
        raise AssertionError(
            f"A2 input mismatch called LLM before Phase A completed: "
            f"{llm_server.request_count}, paths={llm_server.paths!r}"
        )


def run_a3_malformed_pending_case(ahflc, run_dir):
    # A3: a malformed --resume-pending-result is rejected by the Phase A.3 syntax
    # admission, which runs BEFORE Phase B secrets/network. Because no descriptor is
    # supplied (A1 skipped) and the input fixture is valid (A2 passes), this case's
    # expected failure is the A3 pending-syntax admission — Vault and LLM counters
    # must stay 0. (Env/input steps remain theoretically fallible; this case simply
    # does not trip them.) A distinctive token in the malformed payload must not
    # leak into the diagnostic.
    pending_token = "G4C_A3_PENDING_SECRET"
    result, combined_output, vault_server, llm_server = run_two_phase_admission_case(
        ahflc,
        run_dir,
        "a3_malformed_pending",
        bindings=None,
        input_json='{"_type":"smoke::main::Request","value":"hello"}',
        forbidden_payload_tokens=[pending_token],
        resume_pending_result_json='{"secret":"' + pending_token + '",',  # malformed JSON
    )
    if result.returncode == 0:
        raise AssertionError(
            "A3 malformed --resume-pending-result unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if "resume-pending-result is not valid JSON" not in combined_output:
        raise AssertionError(
            f"A3 missing targeted admission diagnostic:\n{combined_output}"
        )
    if vault_server.request_count != 0:
        raise AssertionError(
            f"A3 malformed pending called Vault before Phase A completed: "
            f"{vault_server.request_count}, paths={vault_server.paths!r}"
        )
    if llm_server.request_count != 0:
        raise AssertionError(
            f"A3 malformed pending called LLM before Phase A completed: "
            f"{llm_server.request_count}, paths={llm_server.paths!r}"
        )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: llm_secret_manager_smoke.py <ahflc> <work-dir>")

    ahflc = sys.argv[1]
    work_dir = Path(sys.argv[2])
    work_dir.mkdir(parents=True, exist_ok=True)
    run_dir = work_dir / f"run-{os.getpid()}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir()

    source_path = run_dir / "smoke.ahfl"
    write_smoke_source(source_path)

    for provider_kind in ("vault", "cloud"):
        for mode in ("success", "not_found", "auth_failure", "timeout"):
            run_secret_case(ahflc, run_dir, source_path, provider_kind, mode)
    run_oauth2_token_secret_case(ahflc, run_dir, source_path)
    run_mtls_curl_config_case(ahflc, run_dir, source_path)
    # RFC 0026 C2b G4b: two-phase admission fails BEFORE any secret/LLM network.
    run_a1_non_projectable_binding_case(ahflc, run_dir)
    run_a2_resolved_input_mismatch_case(ahflc, run_dir)
    # RFC 0026 C2b G4c: a malformed --resume-pending-result fails at Phase A.3
    # syntax admission, before any secret/LLM network.
    run_a3_malformed_pending_case(ahflc, run_dir)


if __name__ == "__main__":
    main()
