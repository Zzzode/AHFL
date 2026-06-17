#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


class FailingHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = b'{"error":"primary unavailable"}'
        self.send_response(500)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class StreamingHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        body = (
            'data: {"choices":[{"delta":{"content":"{\\"value\\":\\"stub"}}]}\n'
            'data: {"choices":[{"delta":{"content":"\\"}"}}]}\n'
            "data: [DONE]\n"
        )
        payload = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class UsageHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": '{"value":"usage"}',
                        }
                    }
                ],
                "usage": {
                    "prompt_tokens": 16,
                    "completion_tokens": 4,
                    "total_tokens": 20,
                },
            }
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class UsageExceededHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": '{"value":"usage-exceeded"}',
                        }
                    }
                ],
                "usage": {
                    "prompt_tokens": 4000,
                    "completion_tokens": 97,
                    "total_tokens": 4097,
                },
            }
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class PersistentCacheHandler(BaseHTTPRequestHandler):
    request_count = 0

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        type(self).request_count += 1
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": '{"value":"persistent-cache"}',
                        }
                    }
                ]
            }
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


def start_server(handler):
    server = HTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def stop_servers(*servers):
    for server in servers:
        server.shutdown()
        server.server_close()


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
    node second: EchoAgent(input) after [first];

    return: Response { value: second.value };
}
""",
        encoding="utf-8",
    )


def assert_contains(values, expected, label):
    if expected not in values:
        raise AssertionError(f"{label} missing {expected!r}: {values!r}")


def assert_degradation_summary(artifact, *, outcome, status, selected_provider, degraded):
    summary = artifact.get("provider_degradation_summary")
    if not isinstance(summary, dict):
        raise AssertionError(f"missing provider_degradation_summary: {artifact}")
    if summary.get("schema") != "ahfl.llm_provider_degradation_summary.v0":
        raise AssertionError(f"unexpected degradation summary schema: {summary}")
    if summary.get("secret_free") is not True:
        raise AssertionError(f"degradation summary is not secret-free: {summary}")
    if summary.get("outcome") != outcome:
        raise AssertionError(f"unexpected degradation outcome: {summary}")
    if summary.get("status") != status:
        raise AssertionError(f"unexpected degradation status: {summary}")
    if summary.get("selected_provider") != selected_provider:
        raise AssertionError(f"unexpected selected provider: {summary}")
    degraded_providers = summary.get("degraded_providers")
    if not isinstance(degraded_providers, list):
        raise AssertionError(f"degraded_providers is not a list: {summary}")
    for provider in degraded:
        assert_contains(degraded_providers, provider, "degraded providers")
    if summary.get("degraded_provider_count") != len(degraded_providers):
        raise AssertionError(f"degraded provider count mismatch: {summary}")
    if summary.get("total_attempts", 0) < len(degraded):
        raise AssertionError(f"total_attempts does not cover degraded providers: {summary}")


def assert_secret_free_artifact(path, secret):
    if not path.exists():
        raise AssertionError(f"LLM observability artifact was not created: {path}")
    artifact_text = path.read_text(encoding="utf-8")
    artifact = json.loads(artifact_text)
    if artifact.get("schema") != "ahfl.llm_provider_observability.v0":
        raise AssertionError(f"unexpected schema: {artifact.get('schema')!r}")
    if artifact.get("secret_free") is not True:
        raise AssertionError("artifact is not marked secret_free")
    if secret in artifact_text:
        raise AssertionError("secret value leaked into LLM observability artifact")
    return artifact


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: llm_observability_smoke.py <ahflc> <work-dir>")

    ahflc = sys.argv[1]
    work_dir = Path(sys.argv[2])
    work_dir.mkdir(parents=True, exist_ok=True)
    run_dir = work_dir / f"run-{os.getpid()}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir()

    primary = start_server(FailingHandler)
    fallback = start_server(StreamingHandler)
    try:
        source_path = run_dir / "smoke.ahfl"
        config_path = run_dir / "llm_config.json"
        observability_path = run_dir / "llm_observability.json"
        write_smoke_source(source_path)

        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{primary.server_port}/v1",
                    "model": "primary-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "stream": True,
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                    "fallback_providers": [
                        {
                            "name": "backup",
                            "endpoint": f"http://127.0.0.1:{fallback.server_port}/v1",
                            "model": "backup-model",
                            "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                            "priority": 10,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["AHFL_TEST_LLM_OBSERVABILITY_KEY"] = "test-secret-value"
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
    finally:
        stop_servers(primary, fallback)

    if result.returncode != 0:
        raise AssertionError(
            f"ahflc run failed with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    artifact = assert_secret_free_artifact(observability_path, "test-secret-value")

    cache_kinds = [event.get("kind") for event in artifact.get("cache_events", [])]
    health_kinds = [event.get("kind") for event in artifact.get("provider_health_events", [])]
    stream_kinds = [event.get("kind") for event in artifact.get("streaming_events", [])]

    assert_contains(cache_kinds, "miss", "cache events")
    assert_contains(cache_kinds, "write", "cache events")
    assert_contains(cache_kinds, "hit", "cache events")
    assert_contains(health_kinds, "provider_degraded", "provider health events")
    assert_contains(health_kinds, "fallback_selected", "provider health events")
    assert_contains(stream_kinds, "chunk", "streaming events")
    assert_contains(stream_kinds, "completed", "streaming events")
    assert_degradation_summary(
        artifact,
        outcome="fallback_selected",
        status="recovered",
        selected_provider="backup",
        degraded=["primary"],
    )

    if artifact.get("cache_event_count") != len(cache_kinds):
        raise AssertionError("cache_event_count does not match cache_events")
    if artifact.get("provider_health_event_count") != len(health_kinds):
        raise AssertionError("provider_health_event_count does not match provider_health_events")
    if artifact.get("streaming_event_count") != len(stream_kinds):
        raise AssertionError("streaming_event_count does not match streaming_events")

    if "LLM Provider Observability" not in result.stdout:
        raise AssertionError(f"stdout missing human-readable observability summary: {result.stdout!r}")

    disabled_cache_server = start_server(StreamingHandler)
    try:
        disabled_cache_config_path = run_dir / "llm_config_cache_disabled.json"
        disabled_cache_observability_path = run_dir / "llm_observability_cache_disabled.json"
        disabled_cache_config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{disabled_cache_server.server_port}/v1",
                    "model": "cache-disabled-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "stream": True,
                    "max_retries": 0,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )
        disabled_cache_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(disabled_cache_config_path),
                "--llm-observability",
                str(disabled_cache_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(disabled_cache_server)

    if disabled_cache_result.returncode != 0:
        raise AssertionError(
            "ahflc run with disabled response cache failed "
            f"with {disabled_cache_result.returncode}\nstdout:\n"
            f"{disabled_cache_result.stdout}\nstderr:\n{disabled_cache_result.stderr}"
        )

    disabled_cache_artifact = assert_secret_free_artifact(
        disabled_cache_observability_path, "test-secret-value"
    )
    if disabled_cache_artifact.get("cache_event_count") != 0:
        raise AssertionError(f"cache disabled run emitted cache events: {disabled_cache_artifact}")
    if disabled_cache_artifact.get("cache_events") != []:
        raise AssertionError(f"cache disabled run emitted cache events: {disabled_cache_artifact}")

    disabled_stream_kinds = [
        event.get("kind") for event in disabled_cache_artifact.get("streaming_events", [])
    ]
    assert_contains(disabled_stream_kinds, "chunk", "cache disabled streaming events")
    assert_contains(disabled_stream_kinds, "completed", "cache disabled streaming events")

    PersistentCacheHandler.request_count = 0
    persistent_cache_server = start_server(PersistentCacheHandler)
    try:
        persistent_cache_path = run_dir / "llm_response_cache.json"
        persistent_cache_config_path = run_dir / "llm_config_persistent_cache.json"
        persistent_cache_first_observability_path = (
            run_dir / "llm_observability_persistent_cache_first.json"
        )
        persistent_cache_second_observability_path = (
            run_dir / "llm_observability_persistent_cache_second.json"
        )
        persistent_cache_config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{persistent_cache_server.server_port}/v1",
                    "model": "persistent-cache-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                    "response_cache_path": str(persistent_cache_path),
                }
            ),
            encoding="utf-8",
        )
        persistent_first_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(persistent_cache_config_path),
                "--llm-observability",
                str(persistent_cache_first_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
        persistent_second_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(persistent_cache_config_path),
                "--llm-observability",
                str(persistent_cache_second_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(persistent_cache_server)

    if persistent_first_result.returncode != 0:
        raise AssertionError(
            "first ahflc run with persistent response cache failed "
            f"with {persistent_first_result.returncode}\nstdout:\n"
            f"{persistent_first_result.stdout}\nstderr:\n{persistent_first_result.stderr}"
        )
    if PersistentCacheHandler.request_count != 1:
        raise AssertionError(
            "first persistent response cache run did not issue exactly one HTTP request: "
            f"{PersistentCacheHandler.request_count}"
        )
    if persistent_second_result.returncode != 0:
        raise AssertionError(
            "second ahflc run with persistent response cache failed "
            f"with {persistent_second_result.returncode}\nstdout:\n"
            f"{persistent_second_result.stdout}\nstderr:\n{persistent_second_result.stderr}"
        )
    if PersistentCacheHandler.request_count != 1:
        raise AssertionError(
            "persistent response cache did not prevent a cross-process HTTP request: "
            f"{PersistentCacheHandler.request_count}"
        )

    persistent_first_artifact = assert_secret_free_artifact(
        persistent_cache_first_observability_path, "test-secret-value"
    )
    persistent_second_artifact = assert_secret_free_artifact(
        persistent_cache_second_observability_path, "test-secret-value"
    )
    first_cache_events = persistent_first_artifact.get("cache_events", [])
    first_cache_kinds = [event.get("kind") for event in first_cache_events]
    assert_contains(first_cache_kinds, "miss", "persistent cache first events")
    assert_contains(first_cache_kinds, "write", "persistent cache first events")
    assert_contains(first_cache_kinds, "hit", "persistent cache first events")
    assert_contains(first_cache_kinds, "snapshot_persisted", "persistent cache first events")
    for event in first_cache_events:
        if event.get("persistent") is not True:
            raise AssertionError(f"first persistent cache event not marked persistent: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"first persistent cache event is not secret-free: {event!r}")
        if event.get("kind") == "snapshot_persisted" and event.get("snapshot_entry_count") != 1:
            raise AssertionError(f"unexpected persisted snapshot entry count: {event!r}")

    second_cache_events = persistent_second_artifact.get("cache_events", [])
    second_cache_kinds = [event.get("kind") for event in second_cache_events]
    assert_contains(second_cache_kinds, "snapshot_loaded", "persistent cache second events")
    if second_cache_kinds.count("hit") != 2:
        raise AssertionError(f"expected two persistent cache hits: {second_cache_events!r}")
    if "miss" in second_cache_kinds or "write" in second_cache_kinds:
        raise AssertionError(f"second persistent cache run should not miss or write: {second_cache_events!r}")
    for event in second_cache_events:
        if event.get("persistent") is not True:
            raise AssertionError(f"second persistent cache event not marked persistent: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"second persistent cache event is not secret-free: {event!r}")
        if event.get("kind") == "snapshot_loaded" and event.get("snapshot_entry_count") != 1:
            raise AssertionError(f"unexpected loaded snapshot entry count: {event!r}")

    persistent_cache_text = persistent_cache_path.read_text(encoding="utf-8")
    if "ahfl.llm_response_cache.v0" not in persistent_cache_text:
        raise AssertionError("persistent response cache snapshot is missing schema marker")
    if "test-secret-value" in persistent_cache_text:
        raise AssertionError("secret value leaked into persistent response cache snapshot")
    if "hello" in persistent_cache_text:
        raise AssertionError("prompt input leaked into persistent response cache snapshot")

    usage_server = start_server(UsageHandler)
    try:
        usage_config_path = run_dir / "llm_config_usage.json"
        usage_observability_path = run_dir / "llm_observability_usage.json"
        usage_config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{usage_server.server_port}/v1",
                    "model": "usage-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "max_retries": 0,
                    "token_budget_policy": "warn",
                    "prompt_token_cost_per_million": 0.25,
                    "completion_token_cost_per_million": 1.25,
                    "max_workflow_total_tokens": 35,
                    "max_node_total_tokens": 35,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )
        usage_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(usage_config_path),
                "--llm-observability",
                str(usage_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(usage_server)

    if usage_result.returncode != 0:
        raise AssertionError(
            "ahflc run with token usage accounting failed "
            f"with {usage_result.returncode}\nstdout:\n"
            f"{usage_result.stdout}\nstderr:\n{usage_result.stderr}"
        )

    usage_artifact = assert_secret_free_artifact(
        usage_observability_path, "test-secret-value"
    )
    usage_events = usage_artifact.get("token_usage_events", [])
    budget_events = usage_artifact.get("token_budget_events", [])
    if usage_artifact.get("token_usage_event_count") != len(usage_events):
        raise AssertionError("token_usage_event_count does not match token_usage_events")
    if usage_artifact.get("token_budget_event_count") != len(budget_events):
        raise AssertionError("token_budget_event_count does not match token_budget_events")
    if len(usage_events) != 2:
        raise AssertionError(f"expected token usage for two provider calls: {usage_events!r}")
    if len(budget_events) != 8:
        raise AssertionError(
            f"expected prompt, usage, workflow and node budget events for two calls: {budget_events!r}"
        )
    budget_kinds = [event.get("kind") for event in budget_events]
    if budget_kinds.count("prompt_accepted") != 2:
        raise AssertionError(f"missing prompt budget accepted events: {budget_events!r}")
    if budget_kinds.count("usage_within_budget") != 2:
        raise AssertionError(f"missing usage budget within events: {budget_events!r}")
    if budget_kinds.count("workflow_usage_within_budget") != 1:
        raise AssertionError(f"missing workflow cumulative within event: {budget_events!r}")
    if budget_kinds.count("workflow_usage_exceeded_budget") != 1:
        raise AssertionError(f"missing workflow cumulative exceeded event: {budget_events!r}")
    if budget_kinds.count("node_usage_within_budget") != 2:
        raise AssertionError(f"missing node cumulative within events: {budget_events!r}")
    for event in budget_events:
        if event.get("secret_free") is not True:
            raise AssertionError(f"budget event is not secret-free: {event!r}")
        if event.get("max_total_tokens", 0) <= 0:
            raise AssertionError(f"budget event did not include max_total_tokens: {event!r}")
        if event.get("workflow") != "smoke::SmokeWorkflow":
            raise AssertionError(f"budget event did not include workflow context: {event!r}")
        if event.get("kind") in (
            "prompt_accepted",
            "usage_within_budget",
            "workflow_usage_within_budget",
            "workflow_usage_exceeded_budget",
            "node_usage_within_budget",
        ):
            if event.get("workflow_node") not in ("first", "second"):
                raise AssertionError(f"budget event did not include node context: {event!r}")
            if event.get("agent") != "smoke::EchoAgent":
                raise AssertionError(f"budget event did not include agent context: {event!r}")
            if event.get("state") != "Init":
                raise AssertionError(f"budget event did not include state context: {event!r}")
    workflow_exceeded_events = [
        event for event in budget_events if event.get("kind") == "workflow_usage_exceeded_budget"
    ]
    if workflow_exceeded_events[0].get("cumulative_workflow_tokens") != 40:
        raise AssertionError(f"unexpected cumulative workflow tokens: {workflow_exceeded_events!r}")
    if workflow_exceeded_events[0].get("max_workflow_total_tokens") != 35:
        raise AssertionError(f"unexpected workflow budget max: {workflow_exceeded_events!r}")
    if workflow_exceeded_events[0].get("policy") != "warn":
        raise AssertionError(f"workflow cumulative budget policy not projected: {workflow_exceeded_events!r}")
    for event in usage_events:
        if event.get("provider") != "primary":
            raise AssertionError(f"unexpected token usage provider: {event!r}")
        if event.get("model") != "usage-model":
            raise AssertionError(f"unexpected token usage model: {event!r}")
        if event.get("prompt_tokens") != 16:
            raise AssertionError(f"unexpected prompt token count: {event!r}")
        if event.get("completion_tokens") != 4:
            raise AssertionError(f"unexpected completion token count: {event!r}")
        if event.get("total_tokens") != 20:
            raise AssertionError(f"unexpected total token count: {event!r}")
        if event.get("cost_estimated") is not True:
            raise AssertionError(f"usage cost was not marked estimated: {event!r}")
        if event.get("total_cost_usd", 0) <= 0:
            raise AssertionError(f"usage cost was not recorded: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"usage event is not secret-free: {event!r}")
        if event.get("workflow") != "smoke::SmokeWorkflow":
            raise AssertionError(f"usage event did not include workflow context: {event!r}")
        if event.get("workflow_node") not in ("first", "second"):
            raise AssertionError(f"usage event did not include node context: {event!r}")
        if event.get("agent") != "smoke::EchoAgent" or event.get("state") != "Init":
            raise AssertionError(f"usage event did not include agent/state context: {event!r}")
    if "Token Usage Events" not in usage_result.stdout:
        raise AssertionError(
            f"stdout missing token usage observability summary: {usage_result.stdout!r}"
        )
    if "Token Budget Events" not in usage_result.stdout:
        raise AssertionError(
            f"stdout missing token budget observability summary: {usage_result.stdout!r}"
        )

    usage_exceeded_server = start_server(UsageExceededHandler)
    try:
        usage_exceeded_config_path = run_dir / "llm_config_usage_exceeded.json"
        usage_exceeded_observability_path = (
            run_dir / "llm_observability_usage_exceeded.json"
        )
        usage_exceeded_config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{usage_exceeded_server.server_port}/v1",
                    "model": "usage-exceeded-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )
        usage_exceeded_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(usage_exceeded_config_path),
                "--llm-observability",
                str(usage_exceeded_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(usage_exceeded_server)

    if usage_exceeded_result.returncode == 0:
        raise AssertionError(
            "ahflc run with token usage over max_total_tokens unexpectedly succeeded\n"
            f"stdout:\n{usage_exceeded_result.stdout}\nstderr:\n{usage_exceeded_result.stderr}"
        )
    if "runtime.LLM_TOKEN_BUDGET_EXCEEDED" not in usage_exceeded_result.stderr:
        raise AssertionError(
            "usage budget policy diagnostic did not include stable code: "
            f"{usage_exceeded_result.stderr!r}"
        )
    if "reported total_tokens=4097 while max_total_tokens=4096" not in usage_exceeded_result.stderr:
        raise AssertionError(
            "usage budget policy diagnostic did not include related token details: "
            f"{usage_exceeded_result.stderr!r}"
        )

    usage_exceeded_artifact = assert_secret_free_artifact(
        usage_exceeded_observability_path, "test-secret-value"
    )
    exceeded_budget_events = usage_exceeded_artifact.get("token_budget_events", [])
    exceeded_kinds = [event.get("kind") for event in exceeded_budget_events]
    if exceeded_kinds.count("usage_exceeded_budget") != 2:
        raise AssertionError(
            f"expected usage_exceeded_budget events for two calls: {exceeded_budget_events!r}"
        )
    for event in exceeded_budget_events:
        if event.get("kind") != "usage_exceeded_budget":
            continue
        if event.get("diagnostic_code") != "runtime.LLM_TOKEN_BUDGET_EXCEEDED":
            raise AssertionError(f"missing usage budget diagnostic code: {event!r}")
        if event.get("capability") != "smoke::Echo":
            raise AssertionError(f"unexpected usage budget capability: {event!r}")
        if event.get("total_tokens") != 4097:
            raise AssertionError(f"unexpected usage budget total tokens: {event!r}")
        if event.get("max_total_tokens") != 4096:
            raise AssertionError(f"unexpected usage budget max total: {event!r}")
        if event.get("policy") != "fail":
            raise AssertionError(f"unexpected fail budget policy: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"usage budget exceeded event is not secret-free: {event!r}")

    usage_warn_server = start_server(UsageExceededHandler)
    try:
        usage_warn_config_path = run_dir / "llm_config_usage_warn.json"
        usage_warn_observability_path = run_dir / "llm_observability_usage_warn.json"
        usage_warn_config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{usage_warn_server.server_port}/v1",
                    "model": "usage-warn-model",
                    "api_key_secret": "AHFL_TEST_LLM_OBSERVABILITY_KEY",
                    "max_retries": 0,
                    "token_budget_policy": "warn",
                    "prompt_token_cost_per_million": 1000.0,
                    "completion_token_cost_per_million": 1000.0,
                    "max_total_cost_usd": 0.001,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )
        usage_warn_result = subprocess.run(
            [
                ahflc,
                "run",
                "--workflow",
                "smoke::SmokeWorkflow",
                "--input",
                '{"_type":"smoke::Request","value":"hello"}',
                "--llm-config",
                str(usage_warn_config_path),
                "--llm-observability",
                str(usage_warn_observability_path),
                str(source_path),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
    finally:
        stop_servers(usage_warn_server)

    if usage_warn_result.returncode != 0:
        raise AssertionError(
            "ahflc run with token usage warning policy failed unexpectedly\n"
            f"stdout:\n{usage_warn_result.stdout}\nstderr:\n{usage_warn_result.stderr}"
        )
    if "warning [runtime.LLM_TOKEN_BUDGET_EXCEEDED]" not in usage_warn_result.stderr:
        raise AssertionError(
            "usage warning policy did not emit warning diagnostic: "
            f"{usage_warn_result.stderr!r}"
        )
    if "runtime.LLM_TOKEN_BUDGET_EXCEEDED" not in usage_warn_result.stderr:
        raise AssertionError(
            "usage warning diagnostic did not include token budget code: "
            f"{usage_warn_result.stderr!r}"
        )
    if "runtime.LLM_COST_BUDGET_EXCEEDED" not in usage_warn_result.stderr:
        raise AssertionError(
            "usage warning diagnostic did not include cost budget code: "
            f"{usage_warn_result.stderr!r}"
        )

    usage_warn_artifact = assert_secret_free_artifact(
        usage_warn_observability_path, "test-secret-value"
    )
    warn_budget_events = usage_warn_artifact.get("token_budget_events", [])
    warn_kinds = [event.get("kind") for event in warn_budget_events]
    if warn_kinds.count("usage_exceeded_budget") != 2:
        raise AssertionError(
            f"expected usage warning budget events for two calls: {warn_budget_events!r}"
        )
    if warn_kinds.count("cost_exceeded_budget") != 2:
        raise AssertionError(
            f"expected cost warning budget events for two calls: {warn_budget_events!r}"
        )
    for event in warn_budget_events:
        if event.get("kind") not in ("usage_exceeded_budget", "cost_exceeded_budget"):
            continue
        if event.get("policy") != "warn":
            raise AssertionError(f"warn policy was not projected to artifact: {event!r}")
        if event.get("kind") == "cost_exceeded_budget":
            if event.get("diagnostic_code") != "runtime.LLM_COST_BUDGET_EXCEEDED":
                raise AssertionError(f"missing cost budget diagnostic code: {event!r}")
            if event.get("total_cost_usd", 0) <= event.get("max_total_cost_usd", 0):
                raise AssertionError(f"cost budget event did not exceed limit: {event!r}")
        if event.get("secret_free") is not True:
            raise AssertionError(f"warning budget event is not secret-free: {event!r}")


if __name__ == "__main__":
    main()
