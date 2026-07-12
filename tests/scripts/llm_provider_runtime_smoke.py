#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class CountingServer(HTTPServer):
    request_count = 0


class QuietHandler(BaseHTTPRequestHandler):
    def read_request(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        self.server.request_count += 1

    def log_message(self, fmt: str, *args: object) -> None:
        return


class FailureHandler(QuietHandler):
    def do_POST(self) -> None:
        self.read_request()
        payload = b'{"error":"primary unavailable"}'
        self.send_response(500)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


class StreamingHandler(QuietHandler):
    def do_POST(self) -> None:
        self.read_request()
        payload = (
            'data: {"choices":[{"delta":{"content":"{\\"value\\":\\"fallback"}}]}\n'
            'data: {"choices":[{"delta":{"content":"\\"}"}}]}\n'
            "data: [DONE]\n"
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


class CacheHandler(QuietHandler):
    def do_POST(self) -> None:
        self.read_request()
        payload = json.dumps(
            {
                "choices": [{"message": {"content": '{"value":"cached"}'}}],
                "usage": {
                    "prompt_tokens": 16,
                    "completion_tokens": 4,
                    "total_tokens": 20,
                },
            }
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


class UsageExceededHandler(QuietHandler):
    def do_POST(self) -> None:
        self.read_request()
        payload = json.dumps(
            {
                "choices": [{"message": {"content": '{"value":"over-budget"}'}}],
                "usage": {
                    "prompt_tokens": 4000,
                    "completion_tokens": 97,
                    "total_tokens": 4097,
                },
            }
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def start_server(handler: type[BaseHTTPRequestHandler]) -> CountingServer:
    server = CountingServer(("127.0.0.1", 0), handler)
    server.request_count = 0
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def stop_servers(*servers: CountingServer) -> None:
    for server in servers:
        server.shutdown()
        server.server_close()


def write_source(path: Path) -> None:
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


def run_ahflc(
    ahflc: Path,
    source: Path,
    config: Path,
    events_path: Path,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["AHFL_TEST_LLM_PROVIDER_RUNTIME_KEY"] = "provider-runtime-secret"
    result = subprocess.run(
        [
            str(ahflc),
            "run",
            "--workflow",
            "smoke::SmokeWorkflow",
            "--input",
            '{"_type":"smoke::Request","value":"hello"}',
            "--llm-config",
            str(config),
            "--output-format",
            "jsonl",
            "--verbosity",
            "trace",
            str(source),
        ],
        env=env,
        check=False,
        capture_output=True,
        text=True,
        timeout=30,
    )
    events_path.write_text(result.stdout, encoding="utf-8")
    return result


def load_events(path: Path) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8")
    require("provider-runtime-secret" not in text, "canonical events leaked the API key")
    events = [json.loads(line) for line in text.splitlines() if line]
    require(events, "canonical event stream is empty")
    require(
        all(event.get("schema") == "ahfl.run-event" for event in events),
        "canonical event schema mismatch",
    )
    require(
        [event.get("event_id") for event in events] == list(range(len(events))),
        "canonical event IDs are not contiguous",
    )
    require(events[-1].get("type") == "run_completed", "run terminal event is not last")
    return events


def terminal_status(events: list[dict[str, object]]) -> object:
    return events[-1].get("payload", {}).get("status")


def capability_events(
    events: list[dict[str, object]], event_type: str
) -> list[dict[str, object]]:
    return [event for event in events if event.get("type") == event_type]


def run_fallback_stream_case(ahflc: Path, source: Path, work: Path) -> None:
    primary = start_server(FailureHandler)
    fallback = start_server(StreamingHandler)
    try:
        config = work / "fallback-stream.json"
        config.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{primary.server_port}/v1",
                    "model": "primary",
                    "api_key_secret": "AHFL_TEST_LLM_PROVIDER_RUNTIME_KEY",
                    "stream": True,
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                    "fallback_providers": [
                        {
                            "name": "backup",
                            "endpoint": f"http://127.0.0.1:{fallback.server_port}/v1",
                            "model": "backup",
                            "api_key_secret": "AHFL_TEST_LLM_PROVIDER_RUNTIME_KEY",
                            "priority": 10,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(ahflc, source, config, work / "fallback-stream.jsonl")
    finally:
        stop_servers(primary, fallback)

    require(result.returncode == 0, result.stdout + result.stderr)
    events = load_events(work / "fallback-stream.jsonl")
    require(terminal_status(events) == "completed", "fallback stream run did not complete")
    require(
        len(capability_events(events, "provider_degraded")) == 1,
        "fallback selection did not enter canonical events",
    )
    completed = capability_events(events, "capability_completed")
    require(len(completed) == 2, "fallback stream did not complete two capability calls")
    require(
        [event["payload"]["cache_hit"] for event in completed] == [False, True],
        "fallback stream cache hit sequence mismatch",
    )
    require(primary.request_count == 1, "fallback stream repeated the primary request")
    require(fallback.request_count == 1, "fallback stream repeated the fallback request")


def run_persistent_cache_case(ahflc: Path, source: Path, work: Path) -> None:
    server = start_server(CacheHandler)
    try:
        snapshot = work / "response-cache.json"
        config = work / "persistent-cache.json"
        config.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "cache",
                    "api_key_secret": "AHFL_TEST_LLM_PROVIDER_RUNTIME_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": True,
                    "response_cache_max_entries": 8,
                    "response_cache_ttl_seconds": 60,
                    "response_cache_path": str(snapshot),
                }
            ),
            encoding="utf-8",
        )
        first = run_ahflc(ahflc, source, config, work / "cache-first.jsonl")
        second = run_ahflc(ahflc, source, config, work / "cache-second.jsonl")
    finally:
        stop_servers(server)

    require(first.returncode == 0, first.stdout + first.stderr)
    require(second.returncode == 0, second.stdout + second.stderr)
    first_events = load_events(work / "cache-first.jsonl")
    second_events = load_events(work / "cache-second.jsonl")
    require(terminal_status(first_events) == "completed", "first cache run failed")
    require(terminal_status(second_events) == "completed", "second cache run failed")
    require(server.request_count == 1, "persistent cache repeated an external request")
    require(
        [event["payload"]["cache_hit"] for event in capability_events(first_events, "capability_completed")]
        == [False, True],
        "first cache run hit sequence mismatch",
    )
    require(
        [event["payload"]["cache_hit"] for event in capability_events(second_events, "capability_completed")]
        == [True, True],
        "persistent cache was not reused across processes",
    )
    snapshot_text = snapshot.read_text(encoding="utf-8")
    require("ahfl.llm_response_cache.v0" in snapshot_text, "cache snapshot schema missing")
    require("provider-runtime-secret" not in snapshot_text, "cache snapshot leaked API key")
    require("hello" not in snapshot_text, "cache snapshot leaked prompt input")


def run_budget_case(
    ahflc: Path,
    source: Path,
    work: Path,
    policy: str,
    expected_status: str,
) -> None:
    server = start_server(UsageExceededHandler)
    try:
        config = work / f"budget-{policy}.json"
        config.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": f"budget-{policy}",
                    "api_key_secret": "AHFL_TEST_LLM_PROVIDER_RUNTIME_KEY",
                    "max_retries": 0,
                    "token_budget_policy": policy,
                    "response_cache_enabled": False,
                }
            ),
            encoding="utf-8",
        )
        result = run_ahflc(ahflc, source, config, work / f"budget-{policy}.jsonl")
    finally:
        stop_servers(server)

    events = load_events(work / f"budget-{policy}.jsonl")
    require(
        terminal_status(events) == expected_status,
        f"{policy} budget policy produced terminal status {terminal_status(events)!r}",
    )
    require(
        "runtime.LLM_TOKEN_BUDGET_EXCEEDED" in result.stderr,
        f"{policy} budget policy lacks stable diagnostic code",
    )
    if policy == "warn":
        require(result.returncode == 0, result.stdout + result.stderr)
        require(
            len(capability_events(events, "capability_completed")) == 2,
            "warn budget policy did not preserve capability success",
        )
    else:
        require(result.returncode != 0, "fail budget policy unexpectedly succeeded")
        failed = capability_events(events, "capability_failed")
        require(failed, "fail budget policy lacks capability_failed")
        materialized = [
            event["payload"].get("diagnostic")
            for event in events
            if event.get("type") in ("node_failed", "workflow_failed")
        ]
        require(
            any(
                isinstance(diagnostic, dict)
                and diagnostic.get("code")
                and diagnostic.get("message")
                and isinstance(diagnostic.get("range"), dict)
                for diagnostic in materialized
            ),
            "fail budget policy lacks actionable ranged diagnostic",
        )


def main() -> int:
    require(
        len(sys.argv) == 3,
        "usage: llm_provider_runtime_smoke.py <ahflc> <work-dir>",
    )
    ahflc = Path(sys.argv[1]).resolve()
    work = Path(sys.argv[2]).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    source = work / "smoke.ahfl"
    write_source(source)

    run_fallback_stream_case(ahflc, source, work)
    run_persistent_cache_case(ahflc, source, work)
    run_budget_case(ahflc, source, work, "warn", "completed")
    run_budget_case(ahflc, source, work, "fail", "failed")

    print("LLM provider runtime smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
