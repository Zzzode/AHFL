#!/usr/bin/env python3
"""Real (non-stub) local LLM run evidence — KR4.5 / RFC 0012 stabilized.

Every OTHER LLM test in this repo (llm_provider_runtime_smoke.py,
llm_failure_matrix_smoke.py) drives an in-process mock HTTP server that returns
canned JSON. That proves AHFL's provider plumbing but is NOT a real model run.

This harness drives a GENUINE local inference engine end to end:

  * a real llama.cpp `llama-server` performing autoregressive transformer
    inference over a GGUF model (the committed random-weight tiny-llama fixture;
    weights are untrained but every token is produced by a real forward pass +
    sampler, not a lookup table), and
  * a thin protocol-translation proxy that relays AHFL's OpenAI-compatible
    `/chat/completions` request to llama.cpp's `/completion` endpoint under a
    GBNF grammar constraint, then reshapes the model's real output into the
    OpenAI chat envelope AHFL's response parser consumes.

The proxy performs NO inference: it forwards the prompt to the model and returns
whatever the model generates. The evidence is therefore a real LLM run through
AHFL's production HttpClient / response parser / event projection path.

This test is OPTIONAL and env-gated at the CMake layer: it only registers when a
`llama-server` binary is discovered. Run it directly with:

    python3 real_llm_run_evidence.py <ahflc> <work-dir> <model.gguf> <llama-server>
"""
from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# Both servers bind OS-assigned ephemeral ports so parallel ctest workers (and
# stale llama-server processes from earlier runs) cannot collide on fixed
# ports. The backend port is read back from llama-server's startup log; the
# proxy port from the bound socket.
LISTENING_PORT_RE = re.compile(rb"127\.0\.0\.1:(\d+)")

# Bounded GBNF grammar: forces {"value":"<1..8 lowercase>"} so the random-weight
# model's raw bytes always form complete, parseable ASCII JSON that AHFL's
# response parser accepts. The characters inside [a-z] are still chosen by real
# autoregressive sampling over the model's logits.
RESPONSE_GRAMMAR = r'root ::= "{\"value\":\"" [a-z]{1,8} "\"}"'

SOURCE = """module realllm;

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

workflow RealLlmWorkflow {
    input: Request;
    output: Response;

    node only: EchoAgent(input);

    return: Response { value: only.value };
}
"""


def log(msg: str) -> None:
    print(f"[real-llm-evidence] {msg}", flush=True)


def wait_health(port: int, timeout: float = 40.0) -> None:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(
                f"http://127.0.0.1:{port}/health", timeout=2
            ) as r:
                if r.status == 200:
                    return
        except Exception as exc:  # noqa: BLE001
            last = str(exc)
        time.sleep(0.5)
    raise RuntimeError(f"llama-server on :{port} never became healthy: {last}")


def read_backend_port(
    log_path: Path, proc: "subprocess.Popen[bytes]", timeout: float = 20.0
) -> int | None:
    """Read the OS-assigned port from llama-server's startup log.

    Returns None if the process exits or no listening line appears in time
    (e.g. an older llama-server that does not support --port 0).
    """
    deadline = time.monotonic() + timeout
    with open(log_path, "rb") as log:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return None
            line = log.readline()
            if not line:
                time.sleep(0.1)
                continue
            if b"listening" in line:
                match = LISTENING_PORT_RE.search(line)
                if match and int(match.group(1)) > 0:
                    return int(match.group(1))
    return None


def reserve_free_port() -> int:
    """Reserve a free port via a temporary socket (fallback path)."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def start_backend(
    llama_server: Path, model: Path, work: Path
) -> "tuple[subprocess.Popen[bytes], int]":
    """Start llama-server on an OS-assigned port.

    Primary path: --port 0 and parse the real port from the startup log.
    Fallback (llama-server without --port 0 support): reserve a free port
    via a temporary socket and pass it explicitly.
    """
    log_path = work / "llama-backend.log"

    def launch(port: int) -> "subprocess.Popen[bytes]":
        return subprocess.Popen(
            [
                str(llama_server), "-m", str(model), "--host", "127.0.0.1",
                "--port", str(port), "-c", "2048", "--parallel", "1",
                "--no-warmup",
            ],
            stdout=open(log_path, "wb"),
            stderr=subprocess.STDOUT,
        )

    backend = launch(0)
    backend_port = read_backend_port(log_path, backend)
    if backend_port is None:
        backend.terminate()
        try:
            backend.wait(timeout=10)
        except subprocess.TimeoutExpired:
            backend.kill()
        backend_port = reserve_free_port()
        log(f"llama-server did not report an ephemeral port; reserved :{backend_port}")
        backend = launch(backend_port)
    return backend, backend_port


def make_proxy(backend_port: int):
    class Proxy(BaseHTTPRequestHandler):
        backend_calls = 0

        def log_message(self, *args: object) -> None:
            return

        def do_POST(self) -> None:  # noqa: N802
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                req = json.loads(raw)
            except json.JSONDecodeError:
                req = {}
            parts = [
                f"{m.get('role','user')}: {m.get('content','')}"
                for m in req.get("messages", [])
            ]
            prompt = "\n".join(parts) + "\nassistant:"

            backend_req = json.dumps(
                {
                    "prompt": prompt,
                    "n_predict": 24,
                    "temperature": 0.0,
                    "grammar": RESPONSE_GRAMMAR,
                    "cache_prompt": False,
                }
            ).encode()
            Proxy.backend_calls += 1
            with urllib.request.urlopen(
                urllib.request.Request(
                    f"http://127.0.0.1:{backend_port}/completion",
                    data=backend_req,
                    headers={"Content-Type": "application/json"},
                ),
                timeout=30,
            ) as r:
                model_out = json.loads(r.read())

            content = model_out.get("content", "")  # REAL model-generated JSON
            prompt_tokens = int(model_out.get("tokens_evaluated", 0))
            completion_tokens = int(model_out.get("tokens_predicted", 0))
            envelope = {
                "id": "real-llm-run",
                "object": "chat.completion",
                "model": req.get("model", "tiny-llama"),
                "choices": [
                    {
                        "index": 0,
                        "message": {"role": "assistant", "content": content},
                        "finish_reason": "stop",
                    }
                ],
                "usage": {
                    "prompt_tokens": prompt_tokens,
                    "completion_tokens": completion_tokens,
                    "total_tokens": prompt_tokens + completion_tokens,
                },
            }
            body = json.dumps(envelope).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Proxy


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: real_llm_run_evidence.py <ahflc> <work-dir> <model.gguf> "
            "<llama-server>",
            file=sys.stderr,
        )
        return 2
    ahflc = Path(sys.argv[1]).resolve()
    work = Path(sys.argv[2]).resolve()
    model = Path(sys.argv[3]).resolve()
    llama_server = Path(sys.argv[4]).resolve()

    if not model.exists():
        print(f"model fixture {model} missing", file=sys.stderr)
        return 2
    if not llama_server.exists():
        print(f"llama-server {llama_server} missing", file=sys.stderr)
        return 2

    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    source = work / "realllm.ahfl"
    source.write_text(SOURCE, encoding="utf-8")

    backend, backend_port = start_backend(llama_server, model, work)
    proxy_cls = make_proxy(backend_port)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), proxy_cls)
    proxy_port = proxy.server_address[1]
    proxy_thread = threading.Thread(target=proxy.serve_forever, daemon=True)
    result: subprocess.CompletedProcess[str] | None = None
    try:
        wait_health(backend_port)
        log(f"llama.cpp backend healthy on :{backend_port}")
        proxy_thread.start()
        log(f"translation proxy on :{proxy_port}")

        config = work / "real-llm.json"
        config.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{proxy_port}/v1",
                    "model": "tiny-llama-real",
                    "api_key_secret": "AHFL_REAL_LLM_KEY",
                    "max_retries": 0,
                    "response_cache_enabled": False,
                    "temperature": 0.0,
                }
            ),
            encoding="utf-8",
        )

        env = os.environ.copy()
        env["AHFL_REAL_LLM_KEY"] = "local-llama-cpp"
        events_path = work / "real-llm.jsonl"
        result = subprocess.run(
            [
                str(ahflc), "run", "--workflow", "realllm::RealLlmWorkflow",
                "--input", '{"_type":"realllm::Request","value":"hello"}',
                "--llm-config", str(config), "--output-format", "jsonl",
                "--verbosity", "trace", str(source),
            ],
            env=env, check=False, capture_output=True, text=True, timeout=90,
        )
        events_path.write_text(result.stdout, encoding="utf-8")
        (work / "ahflc.stderr").write_text(result.stderr, encoding="utf-8")
    finally:
        proxy.shutdown()
        backend.terminate()
        try:
            backend.wait(timeout=10)
        except subprocess.TimeoutExpired:
            backend.kill()

    assert result is not None
    if result.returncode != 0:
        print("ahflc run failed:\n" + result.stdout + result.stderr, file=sys.stderr)
        return 1
    events = [json.loads(l) for l in events_path.read_text().splitlines() if l]
    if not events:
        print("empty event stream", file=sys.stderr)
        return 1
    status = events[-1].get("payload", {}).get("status")
    completed = [e for e in events if e.get("type") == "capability_completed"]
    backend_calls = proxy_cls.backend_calls
    if status != "completed":
        print(f"terminal status {status!r} != completed", file=sys.stderr)
        return 1
    if not completed:
        print("no capability_completed events", file=sys.stderr)
        return 1
    if backend_calls < 1:
        print("proxy never called the real backend", file=sys.stderr)
        return 1

    summary = {
        "format": "ahfl.real-llm-run-evidence",
        "engine": "llama.cpp llama-server",
        "model_file": str(model),
        "inference": "autoregressive transformer forward pass (random-weight GGUF)",
        "stub": False,
        "backend_calls": backend_calls,
        "terminal_status": status,
        "capability_completed_count": len(completed),
        "event_count": len(events),
    }
    (work / "evidence-summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    log(
        f"terminal={status} backend_calls={backend_calls} "
        f"events={len(events)} completed={len(completed)}"
    )
    print("REAL LLM run evidence passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
