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
        payload = b'{"error":"provider unavailable"}'
        self.send_response(500)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class UnauthorizedHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = b'{"error":"unauthorized"}'
        self.send_response(401)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class InterruptedStreamingHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        body = 'data: {"choices":[{"delta":{"content":"{\\"value\\":\\"partial"}}]}\n'
        payload = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        return


class InvalidToolArgsHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": None,
                            "tool_calls": [
                                {
                                    "id": "call_invalid_args",
                                    "type": "function",
                                    "function": {
                                        "name": "ahfl_smoke__Echo",
                                        "arguments": "not-json",
                                    },
                                }
                            ],
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


class UnknownToolHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": None,
                            "tool_calls": [
                                {
                                    "id": "call_unknown_tool",
                                    "type": "function",
                                    "function": {
                                        "name": "ahfl_unknown_tool",
                                        "arguments": '{"value":"hello"}',
                                    },
                                }
                            ],
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


class CatalogInvalidToolArgsHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": None,
                            "tool_calls": [
                                {
                                    "id": "call_catalog_invalid_args",
                                    "type": "function",
                                    "function": {
                                        "name": "lookup_context",
                                        "arguments": "not-json",
                                    },
                                }
                            ],
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


class CatalogErrorToolHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": None,
                            "tool_calls": [
                                {
                                    "id": "call_catalog_error",
                                    "type": "function",
                                    "function": {
                                        "name": "catalog_failure",
                                        "arguments": '{"query":"hello"}',
                                    },
                                }
                            ],
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


class CatalogTimeoutToolHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": None,
                            "tool_calls": [
                                {
                                    "id": "call_catalog_timeout",
                                    "type": "function",
                                    "function": {
                                        "name": "catalog_timeout",
                                        "arguments": '{"query":"hello"}',
                                    },
                                }
                            ],
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


class ToolCatalogSuccessHandler(BaseHTTPRequestHandler):
    request_bodies = []

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        try:
            request_body = json.loads(body)
        except json.JSONDecodeError:
            request_body = {}
        self.__class__.request_bodies.append(request_body)

        if len(self.__class__.request_bodies) == 1:
            tool_names = [
                tool.get("function", {}).get("name")
                for tool in request_body.get("tools", [])
            ]
            if "lookup_context" not in tool_names:
                self.send_response(400)
                payload = b'{"error":"missing lookup_context tool"}'
            else:
                self.send_response(200)
                payload = json.dumps(
                    {
                        "choices": [
                            {
                                "message": {
                                    "content": None,
                                    "tool_calls": [
                                        {
                                            "id": "call_lookup_context",
                                            "type": "function",
                                            "function": {
                                                "name": "lookup_context",
                                                "arguments": '{"query":"hello"}',
                                            },
                                        }
                                    ],
                                }
                            }
                        ]
                    }
                ).encode("utf-8")
        else:
            tool_messages = [
                message.get("content", "")
                for message in request_body.get("messages", [])
                if message.get("role") == "tool"
            ]
            if not any("catalog-context" in content for content in tool_messages):
                self.send_response(400)
                payload = b'{"error":"missing catalog tool result"}'
            else:
                self.send_response(200)
                payload = json.dumps(
                    {
                        "choices": [
                            {
                                "message": {
                                    "content": json.dumps(
                                        {"value": "catalog-success"}
                                    )
                                }
                            }
                        ]
                    }
                ).encode("utf-8")

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

    return: Response { value: first.value };
}
""",
        encoding="utf-8",
    )


def write_mock_tools(path):
    path.write_text(
        json.dumps(
            {
                "format_version": "ahfl.capability-mocks.v0.6",
                "mocks": [
                    {
                        "capability_name": "smoke::Echo",
                        "result_fixture": "fixture.echo.ok",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )


def write_tool_catalog(path, tools=None):
    if tools is None:
        tools = [
            {
                "name": "lookup_context",
                "description": "Return deterministic smoke-test context",
                "parameters": {
                    "type": "object",
                    "properties": {"query": {"type": "string"}},
                    "required": ["query"],
                    "additionalProperties": False,
                },
                "result": {"value": "catalog-context"},
            }
        ]
    path.write_text(
        json.dumps(
            {
                "schema": "ahfl.llm_tool_catalog.v0",
                "tools": tools,
            }
        ),
        encoding="utf-8",
    )


def write_invalid_tool_catalog(path):
    path.write_text(
        json.dumps(
            {
                "schema": "ahfl.llm_tool_catalog.invalid",
                "tools": [],
            }
        ),
        encoding="utf-8",
    )


def run_ahflc(
    ahflc,
    source_path,
    config_path,
    observability_path,
    capability_mocks_path=None,
    tool_catalog_path=None,
):
    env = os.environ.copy()
    env["AHFL_TEST_LLM_FAILURE_MATRIX_KEY"] = "failure-matrix-secret"
    command = [
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
    ]
    if capability_mocks_path is not None:
        command.extend(["--capability-mocks", str(capability_mocks_path)])
    if tool_catalog_path is not None:
        command.extend(["--tool-catalog", str(tool_catalog_path)])
    command.append(str(source_path))
    return subprocess.run(
        command,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
    )


def load_artifact(path):
    if not path.exists():
        raise AssertionError(f"missing observability artifact: {path}")
    text = path.read_text(encoding="utf-8")
    if "failure-matrix-secret" in text:
        raise AssertionError("secret value leaked into LLM observability artifact")
    artifact = json.loads(text)
    if artifact.get("schema") != "ahfl.llm_provider_observability.v0":
        raise AssertionError(f"unexpected schema: {artifact.get('schema')!r}")
    if artifact.get("secret_free") is not True:
        raise AssertionError("artifact is not marked secret_free")
    return artifact


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
    if summary.get("fallback_exhausted") != (outcome == "fallback_exhausted"):
        raise AssertionError(f"fallback_exhausted mismatch: {summary}")
    if summary.get("total_attempts", 0) < len(degraded):
        raise AssertionError(f"total_attempts does not cover degraded providers: {summary}")


def run_auth_failure(ahflc, work_dir, source_path):
    server = start_server(UnauthorizedHandler)
    try:
        config_path = work_dir / "auth_failure_config.json"
        observability_path = work_dir / "auth_failure_observability.json"
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "auth-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(ahflc, source_path, config_path, observability_path)
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("authentication failure run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "status=401" not in combined_output:
        raise AssertionError(f"missing HTTP 401 authentication diagnostic:\n{combined_output}")

    artifact = load_artifact(observability_path)
    health_kinds = [event.get("kind") for event in artifact.get("provider_health_events", [])]
    cache_kinds = [event.get("kind") for event in artifact.get("cache_events", [])]
    assert_contains(cache_kinds, "miss", "auth failure cache events")
    assert_contains(health_kinds, "provider_degraded", "auth failure health events")
    assert_contains(health_kinds, "fallback_exhausted", "auth failure health events")

    degraded = [
        event.get("provider")
        for event in artifact.get("provider_health_events", [])
        if event.get("kind") == "provider_degraded"
    ]
    assert_contains(degraded, "primary", "degraded providers")
    assert_degradation_summary(
        artifact,
        outcome="fallback_exhausted",
        status="exhausted",
        selected_provider="",
        degraded=["primary"],
    )


def run_fallback_exhausted(ahflc, work_dir, source_path):
    primary = start_server(FailingHandler)
    backup = start_server(FailingHandler)
    try:
        config_path = work_dir / "fallback_exhausted_config.json"
        observability_path = work_dir / "fallback_exhausted_observability.json"
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{primary.server_port}/v1",
                    "model": "primary-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                    "fallback_providers": [
                        {
                            "name": "backup",
                            "endpoint": f"http://127.0.0.1:{backup.server_port}/v1",
                            "model": "backup-model",
                            "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                            "priority": 10,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(ahflc, source_path, config_path, observability_path)
    finally:
        stop_servers(primary, backup)

    if result.returncode == 0:
        raise AssertionError("fallback exhausted run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "LLM provider fallback exhausted" not in combined_output:
        raise AssertionError(f"missing fallback exhausted diagnostic:\n{combined_output}")

    artifact = load_artifact(observability_path)
    health_kinds = [event.get("kind") for event in artifact.get("provider_health_events", [])]
    cache_kinds = [event.get("kind") for event in artifact.get("cache_events", [])]
    assert_contains(cache_kinds, "miss", "fallback exhausted cache events")
    assert_contains(health_kinds, "provider_degraded", "fallback exhausted health events")
    assert_contains(health_kinds, "fallback_exhausted", "fallback exhausted health events")

    degraded = [
        event.get("provider")
        for event in artifact.get("provider_health_events", [])
        if event.get("kind") == "provider_degraded"
    ]
    assert_contains(degraded, "primary", "degraded providers")
    assert_contains(degraded, "backup", "degraded providers")
    assert_degradation_summary(
        artifact,
        outcome="fallback_exhausted",
        status="exhausted",
        selected_provider="",
        degraded=["primary", "backup"],
    )


def run_stream_interrupted(ahflc, work_dir, source_path):
    server = start_server(InterruptedStreamingHandler)
    try:
        config_path = work_dir / "stream_interrupted_config.json"
        observability_path = work_dir / "stream_interrupted_observability.json"
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "stream-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "stream": True,
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(ahflc, source_path, config_path, observability_path)
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("interrupted streaming run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "incomplete LLM streaming response: missing [DONE]" not in combined_output:
        raise AssertionError(f"missing interrupted streaming diagnostic:\n{combined_output}")

    artifact = load_artifact(observability_path)
    cache_kinds = [event.get("kind") for event in artifact.get("cache_events", [])]
    stream_kinds = [event.get("kind") for event in artifact.get("streaming_events", [])]
    assert_contains(cache_kinds, "miss", "interrupted streaming cache events")
    assert_contains(stream_kinds, "chunk", "interrupted streaming events")
    assert_contains(stream_kinds, "interrupted", "interrupted streaming events")


def run_tool_catalog_success(ahflc, work_dir, source_path):
    ToolCatalogSuccessHandler.request_bodies = []
    server = start_server(ToolCatalogSuccessHandler)
    try:
        config_path = work_dir / "tool_catalog_success_config.json"
        tool_catalog_path = work_dir / "tool_catalog_success_catalog.json"
        observability_path = work_dir / "tool_catalog_success_observability.json"
        write_tool_catalog(tool_catalog_path)
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-catalog-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc,
            source_path,
            config_path,
            observability_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode != 0:
        raise AssertionError(
            "tool catalog run unexpectedly failed:\n" + result.stdout + result.stderr
        )
    if "catalog-success" not in result.stdout + result.stderr:
        raise AssertionError(
            "tool catalog run did not return catalog-success:\n"
            + result.stdout
            + result.stderr
        )
    if len(ToolCatalogSuccessHandler.request_bodies) != 2:
        raise AssertionError(
            "tool catalog run should perform exactly two provider requests"
        )

    artifact = load_artifact(observability_path)
    if artifact.get("cache_event_count") != 0:
        raise AssertionError("tool catalog success should not emit cache events")


def assert_tool_failure_artifact(observability_path, label):
    artifact = load_artifact(observability_path)
    if artifact.get("cache_event_count") != 0:
        raise AssertionError(f"{label} should not emit cache events")
    if artifact.get("streaming_event_count") != 0:
        raise AssertionError(f"{label} should not emit streaming events")


def run_tool_catalog_invalid_schema(ahflc, work_dir, source_path):
    config_path = work_dir / "tool_catalog_invalid_schema_config.json"
    tool_catalog_path = work_dir / "tool_catalog_invalid_schema_catalog.json"
    observability_path = work_dir / "tool_catalog_invalid_schema_observability.json"
    write_invalid_tool_catalog(tool_catalog_path)
    config_path.write_text(
        json.dumps(
            {
                "endpoint": "http://127.0.0.1:9/v1",
                "model": "tool-catalog-model",
                "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                "max_retries": 0,
                "max_tool_rounds": 2,
            }
        ),
        encoding="utf-8",
    )
    result = run_ahflc(
        ahflc,
        source_path,
        config_path,
        observability_path,
        tool_catalog_path=tool_catalog_path,
    )

    if result.returncode == 0:
        raise AssertionError("invalid tool catalog schema run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = "unsupported LLM tool catalog schema 'ahfl.llm_tool_catalog.invalid'"
    if expected not in combined_output:
        raise AssertionError(f"missing invalid tool catalog diagnostic:\n{combined_output}")


def run_tool_catalog_invalid_args(ahflc, work_dir, source_path):
    server = start_server(CatalogInvalidToolArgsHandler)
    try:
        config_path = work_dir / "tool_catalog_invalid_args_config.json"
        tool_catalog_path = work_dir / "tool_catalog_invalid_args_catalog.json"
        observability_path = work_dir / "tool_catalog_invalid_args_observability.json"
        write_tool_catalog(tool_catalog_path)
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-catalog-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc,
            source_path,
            config_path,
            observability_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("catalog invalid tool arguments run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = "tool call failed for 'lookup_context': invalid tool arguments JSON"
    if expected not in combined_output:
        raise AssertionError(
            f"missing catalog invalid tool arguments diagnostic:\n{combined_output}"
        )
    assert_tool_failure_artifact(observability_path, "catalog invalid args")


def run_tool_catalog_unknown_tool(ahflc, work_dir, source_path):
    server = start_server(UnknownToolHandler)
    try:
        config_path = work_dir / "tool_catalog_unknown_tool_config.json"
        tool_catalog_path = work_dir / "tool_catalog_unknown_tool_catalog.json"
        observability_path = work_dir / "tool_catalog_unknown_tool_observability.json"
        write_tool_catalog(tool_catalog_path)
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-catalog-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc,
            source_path,
            config_path,
            observability_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("catalog unknown tool run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = (
        "tool call failed for 'ahfl_unknown_tool': "
        "capability not found: ahfl_unknown_tool"
    )
    if expected not in combined_output:
        raise AssertionError(f"missing catalog unknown tool diagnostic:\n{combined_output}")
    assert_tool_failure_artifact(observability_path, "catalog unknown tool")


def run_tool_catalog_error(ahflc, work_dir, source_path):
    server = start_server(CatalogErrorToolHandler)
    try:
        config_path = work_dir / "tool_catalog_error_config.json"
        tool_catalog_path = work_dir / "tool_catalog_error_catalog.json"
        observability_path = work_dir / "tool_catalog_error_observability.json"
        write_tool_catalog(
            tool_catalog_path,
            [
                {
                    "name": "catalog_failure",
                    "description": "Return deterministic catalog failure",
                    "parameters": {"type": "object", "additionalProperties": True},
                    "failure": {
                        "kind": "error",
                        "message": "catalog tool failure",
                    },
                }
            ],
        )
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-catalog-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc,
            source_path,
            config_path,
            observability_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("catalog error tool run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = "tool call failed for 'catalog_failure': catalog tool failure"
    if expected not in combined_output:
        raise AssertionError(f"missing catalog error tool diagnostic:\n{combined_output}")
    assert_tool_failure_artifact(observability_path, "catalog error tool")


def run_tool_catalog_timeout(ahflc, work_dir, source_path):
    server = start_server(CatalogTimeoutToolHandler)
    try:
        config_path = work_dir / "tool_catalog_timeout_config.json"
        tool_catalog_path = work_dir / "tool_catalog_timeout_catalog.json"
        observability_path = work_dir / "tool_catalog_timeout_observability.json"
        write_tool_catalog(
            tool_catalog_path,
            [
                {
                    "name": "catalog_timeout",
                    "description": "Return deterministic catalog timeout",
                    "parameters": {"type": "object", "additionalProperties": True},
                    "failure": {
                        "kind": "timeout",
                        "timeout_ms": 25,
                    },
                }
            ],
        )
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-catalog-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc,
            source_path,
            config_path,
            observability_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("catalog timeout tool run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = "tool call failed for 'catalog_timeout': tool timed out after 25ms"
    if expected not in combined_output:
        raise AssertionError(f"missing catalog timeout tool diagnostic:\n{combined_output}")
    assert_tool_failure_artifact(observability_path, "catalog timeout tool")


def run_tool_invalid_args(ahflc, work_dir, source_path):
    server = start_server(InvalidToolArgsHandler)
    try:
        config_path = work_dir / "tool_invalid_args_config.json"
        mocks_path = work_dir / "tool_invalid_args_mocks.json"
        observability_path = work_dir / "tool_invalid_args_observability.json"
        write_mock_tools(mocks_path)
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc, source_path, config_path, observability_path, mocks_path
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("invalid tool arguments run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = "tool call failed for 'ahfl_smoke__Echo': invalid tool arguments JSON"
    if expected not in combined_output:
        raise AssertionError(
            f"missing invalid tool arguments diagnostic:\n{combined_output}"
        )

    artifact = load_artifact(observability_path)
    if artifact.get("cache_event_count") != 0:
        raise AssertionError("tool invalid args should not emit cache events")
    if artifact.get("streaming_event_count") != 0:
        raise AssertionError("tool invalid args should not emit streaming events")


def run_unknown_tool(ahflc, work_dir, source_path):
    server = start_server(UnknownToolHandler)
    try:
        config_path = work_dir / "unknown_tool_config.json"
        mocks_path = work_dir / "unknown_tool_mocks.json"
        observability_path = work_dir / "unknown_tool_observability.json"
        write_mock_tools(mocks_path)
        config_path.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "tool-model",
                    "api_key_secret": "AHFL_TEST_LLM_FAILURE_MATRIX_KEY",
                    "max_retries": 0,
                    "max_tool_rounds": 2,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(
            ahflc, source_path, config_path, observability_path, mocks_path
        )
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("unknown tool run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    expected = (
        "tool call failed for 'ahfl_unknown_tool': "
        "capability not found: ahfl_unknown_tool"
    )
    if expected not in combined_output:
        raise AssertionError(f"missing unknown tool diagnostic:\n{combined_output}")

    artifact = load_artifact(observability_path)
    if artifact.get("cache_event_count") != 0:
        raise AssertionError("unknown tool should not emit cache events")
    if artifact.get("streaming_event_count") != 0:
        raise AssertionError("unknown tool should not emit streaming events")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: llm_failure_matrix_smoke.py <ahflc> <work-dir>")

    ahflc = sys.argv[1]
    work_dir = Path(sys.argv[2])
    work_dir.mkdir(parents=True, exist_ok=True)
    run_dir = work_dir / f"run-{os.getpid()}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir()

    source_path = run_dir / "smoke.ahfl"
    write_smoke_source(source_path)

    run_auth_failure(ahflc, run_dir, source_path)
    run_fallback_exhausted(ahflc, run_dir, source_path)
    run_stream_interrupted(ahflc, run_dir, source_path)
    run_tool_catalog_success(ahflc, run_dir, source_path)
    run_tool_catalog_invalid_schema(ahflc, run_dir, source_path)
    run_tool_catalog_invalid_args(ahflc, run_dir, source_path)
    run_tool_catalog_unknown_tool(ahflc, run_dir, source_path)
    run_tool_catalog_error(ahflc, run_dir, source_path)
    run_tool_catalog_timeout(ahflc, run_dir, source_path)
    run_tool_invalid_args(ahflc, run_dir, source_path)
    run_unknown_tool(ahflc, run_dir, source_path)


if __name__ == "__main__":
    main()
