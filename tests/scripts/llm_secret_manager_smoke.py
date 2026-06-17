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


def run_ahflc(ahflc, source_path, config_path, observability_path, env):
    return subprocess.run(
        [
            ahflc,
            "run",
            "--workflow",
            "smoke::SmokeWorkflow",
            "--input",
            '{"_type":"smoke::Request","value":"hello"}',
            "--llm-config",
            str(config_path),
            "--llm-observability",
            str(observability_path),
            str(source_path),
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
    )


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
        raise AssertionError(f"missing LLM observability artifact: {path}")
    text = path.read_text(encoding="utf-8")
    assert_secret_free_text(text, forbidden_values, "LLM observability artifact")
    artifact = json.loads(text)
    if artifact.get("schema") != "ahfl.llm_provider_observability.v0":
        raise AssertionError(f"unexpected schema: {artifact.get('schema')!r}")
    if artifact.get("secret_free") is not True:
        raise AssertionError("artifact is not marked secret_free")
    if not expect_secret_refresh:
        return

    lifecycle_events = artifact.get("secret_lifecycle_events", [])
    lifecycle_kinds = [event.get("kind") for event in lifecycle_events]
    if "refresh" not in lifecycle_kinds or "resolve" not in lifecycle_kinds:
        raise AssertionError(f"missing refresh/resolve lifecycle events: {artifact}")
    if artifact.get("secret_lifecycle_event_count") != len(lifecycle_events):
        raise AssertionError("secret_lifecycle_event_count does not match lifecycle events")
    if expected_provider_prefix is not None:
        prefixes = {event.get("provider_prefix") for event in lifecycle_events}
        if expected_provider_prefix not in prefixes:
            raise AssertionError(
                f"missing provider prefix {expected_provider_prefix!r} in {lifecycle_events!r}"
            )
    for event in lifecycle_events:
        if "key_fingerprint" not in event:
            raise AssertionError(f"lifecycle event missing fingerprint: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"lifecycle event not marked secret_free: {event!r}")


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


if __name__ == "__main__":
    main()
