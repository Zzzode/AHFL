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
    events_path,
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
        "--output-format",
        "jsonl",
        "--verbosity",
        "trace",
    ]
    if capability_mocks_path is not None:
        command.extend(["--capability-mocks", str(capability_mocks_path)])
    if tool_catalog_path is not None:
        command.extend(["--tool-catalog", str(tool_catalog_path)])
    command.append(str(source_path))
    result = subprocess.run(
        command,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
    )
    events_path.write_text(result.stdout, encoding="utf-8")
    return result


def load_events(path):
    if not path.exists():
        raise AssertionError(f"missing canonical execution event stream: {path}")
    text = path.read_text(encoding="utf-8")
    if "failure-matrix-secret" in text:
        raise AssertionError("secret value leaked into canonical execution events")
    events = [json.loads(line) for line in text.splitlines() if line]
    if any(event.get("schema") != "ahfl.run-event" for event in events):
        raise AssertionError(f"unexpected execution event schema: {events!r}")
    if [event.get("event_id") for event in events] != list(range(len(events))):
        raise AssertionError(f"execution event ids are not contiguous: {events!r}")
    return events


def assert_terminal_execution(path, expected_status):
    events = load_events(path)
    if not events:
        raise AssertionError("canonical execution event stream is empty")
    types = [event.get("type") for event in events]
    for required in ("run_started", "workflow_started", "capability_started", "run_completed"):
        if required not in types:
            raise AssertionError(f"execution event stream lacks {required}: {events!r}")
    if events[-1].get("type") != "run_completed":
        raise AssertionError(f"run terminal event is not last: {events!r}")
    if events[-1].get("payload", {}).get("status") != expected_status:
        raise AssertionError(
            f"run status is not {expected_status!r}: {events[-1]!r}"
        )
    terminal_capabilities = types.count("capability_completed") + types.count(
        "capability_failed"
    )
    if terminal_capabilities != types.count("capability_started"):
        raise AssertionError(f"capability terminal invariant failed: {events!r}")
    if expected_status == "failed":
        materialized = [
            event.get("payload", {}).get("diagnostic")
            for event in events
            if event.get("type") in ("capability_failed", "node_failed", "workflow_failed")
            and isinstance(event.get("payload", {}).get("diagnostic"), dict)
        ]
        if not materialized:
            raise AssertionError(f"failed run has no materialized diagnostic: {events!r}")
        if not any(
            diagnostic.get("message")
            and diagnostic.get("code")
            and isinstance(diagnostic.get("range"), dict)
            for diagnostic in materialized
        ):
            raise AssertionError(
                f"failed run diagnostic lacks code, message, or source range: {materialized!r}"
            )
    return events


def run_auth_failure(ahflc, work_dir, source_path):
    server = start_server(UnauthorizedHandler)
    try:
        config_path = work_dir / "auth_failure_config.json"
        events_path = work_dir / "auth_failure_events.jsonl"
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
        result = run_ahflc(ahflc, source_path, config_path, events_path)
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("authentication failure run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "status=401" not in combined_output:
        raise AssertionError(f"missing HTTP 401 authentication diagnostic:\n{combined_output}")

    events = assert_terminal_execution(events_path, "failed")
    if not any(event.get("type") == "capability_failed" for event in events):
        raise AssertionError(f"auth failure lacks capability_failed event: {events!r}")


def run_fallback_exhausted(ahflc, work_dir, source_path):
    primary = start_server(FailingHandler)
    backup = start_server(FailingHandler)
    try:
        config_path = work_dir / "fallback_exhausted_config.json"
        events_path = work_dir / "fallback_exhausted_events.jsonl"
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
        result = run_ahflc(ahflc, source_path, config_path, events_path)
    finally:
        stop_servers(primary, backup)

    if result.returncode == 0:
        raise AssertionError("fallback exhausted run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "LLM provider fallback exhausted" not in combined_output:
        raise AssertionError(f"missing fallback exhausted diagnostic:\n{combined_output}")

    events = assert_terminal_execution(events_path, "failed")
    types = [event.get("type") for event in events]
    if types.count("capability_failed") != types.count("capability_started"):
        raise AssertionError(f"fallback exhaustion lacks paired capability failure: {events!r}")


def run_stream_interrupted(ahflc, work_dir, source_path):
    server = start_server(InterruptedStreamingHandler)
    try:
        config_path = work_dir / "stream_interrupted_config.json"
        events_path = work_dir / "stream_interrupted_events.jsonl"
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
        result = run_ahflc(ahflc, source_path, config_path, events_path)
    finally:
        stop_servers(server)

    if result.returncode == 0:
        raise AssertionError("interrupted streaming run unexpectedly succeeded")
    combined_output = result.stdout + result.stderr
    if "incomplete LLM streaming response: missing [DONE]" not in combined_output:
        raise AssertionError(f"missing interrupted streaming diagnostic:\n{combined_output}")

    assert_terminal_execution(events_path, "failed")


def run_tool_catalog_success(ahflc, work_dir, source_path):
    ToolCatalogSuccessHandler.request_bodies = []
    server = start_server(ToolCatalogSuccessHandler)
    try:
        config_path = work_dir / "tool_catalog_success_config.json"
        tool_catalog_path = work_dir / "tool_catalog_success_catalog.json"
        events_path = work_dir / "tool_catalog_success_events.jsonl"
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
            events_path,
            tool_catalog_path=tool_catalog_path,
        )
    finally:
        stop_servers(server)

    if result.returncode != 0:
        raise AssertionError(
            "tool catalog run unexpectedly failed:\n" + result.stdout + result.stderr
        )
    if len(ToolCatalogSuccessHandler.request_bodies) != 2:
        raise AssertionError(
            "tool catalog run should perform exactly two provider requests"
        )

    events = assert_terminal_execution(events_path, "completed")
    completed = [event for event in events if event.get("type") == "capability_completed"]
    if len(completed) != 1:
        raise AssertionError(f"tool catalog run lacks capability completion: {events!r}")
    if completed[0].get("payload", {}).get("output_value_id") is None:
        raise AssertionError(
            f"tool catalog completion does not reference its result value: {completed[0]!r}"
        )


def run_tool_catalog_invalid_schema(ahflc, work_dir, source_path):
    config_path = work_dir / "tool_catalog_invalid_schema_config.json"
    tool_catalog_path = work_dir / "tool_catalog_invalid_schema_catalog.json"
    events_path = work_dir / "tool_catalog_invalid_schema_events.jsonl"
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
        events_path,
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
        events_path = work_dir / "tool_catalog_invalid_args_events.jsonl"
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
            events_path,
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
    assert_terminal_execution(events_path, "failed")


def run_tool_catalog_unknown_tool(ahflc, work_dir, source_path):
    server = start_server(UnknownToolHandler)
    try:
        config_path = work_dir / "tool_catalog_unknown_tool_config.json"
        tool_catalog_path = work_dir / "tool_catalog_unknown_tool_catalog.json"
        events_path = work_dir / "tool_catalog_unknown_tool_events.jsonl"
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
            events_path,
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
    assert_terminal_execution(events_path, "failed")


def run_tool_catalog_error(ahflc, work_dir, source_path):
    server = start_server(CatalogErrorToolHandler)
    try:
        config_path = work_dir / "tool_catalog_error_config.json"
        tool_catalog_path = work_dir / "tool_catalog_error_catalog.json"
        events_path = work_dir / "tool_catalog_error_events.jsonl"
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
            events_path,
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
    assert_terminal_execution(events_path, "failed")


def run_tool_catalog_timeout(ahflc, work_dir, source_path):
    server = start_server(CatalogTimeoutToolHandler)
    try:
        config_path = work_dir / "tool_catalog_timeout_config.json"
        tool_catalog_path = work_dir / "tool_catalog_timeout_catalog.json"
        events_path = work_dir / "tool_catalog_timeout_events.jsonl"
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
            events_path,
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
    assert_terminal_execution(events_path, "failed")


def run_tool_invalid_args(ahflc, work_dir, source_path):
    server = start_server(InvalidToolArgsHandler)
    try:
        config_path = work_dir / "tool_invalid_args_config.json"
        mocks_path = work_dir / "tool_invalid_args_mocks.json"
        events_path = work_dir / "tool_invalid_args_events.jsonl"
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
            ahflc, source_path, config_path, events_path, mocks_path
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

    assert_terminal_execution(events_path, "failed")


def run_unknown_tool(ahflc, work_dir, source_path):
    server = start_server(UnknownToolHandler)
    try:
        config_path = work_dir / "unknown_tool_config.json"
        mocks_path = work_dir / "unknown_tool_mocks.json"
        events_path = work_dir / "unknown_tool_events.jsonl"
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
            ahflc, source_path, config_path, events_path, mocks_path
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

    assert_terminal_execution(events_path, "failed")


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
