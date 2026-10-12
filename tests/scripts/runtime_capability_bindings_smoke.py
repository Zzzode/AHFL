#!/usr/bin/env python3
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


SOURCE = """module smoke;

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
"""


# ---------------------------------------------------------------------------
# Loopback HTTP/1.1 server (for HTTP capability binding tests)
# ---------------------------------------------------------------------------

class _Http1Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        self.server.request_count += 1
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)

        mode = self.server.mode
        if mode == "http_success":
            body = json.dumps({"_type": "smoke::Response", "value": self.server.value})
            self._send(200, body, "application/json")
        elif mode == "http_retry_failure":
            self._send(503, "retry failure", "text/plain")
        elif mode == "http_malformed_json":
            self._send(200, "{not-json", "application/json")
        elif mode == "http_schema_mismatch":
            body = json.dumps({"_type": "smoke::UnexpectedResponse", "value": "wrong"})
            self._send(200, body, "application/json")
        else:
            self._send(500, "unknown mode", "text/plain")

    def _send(self, status, body, content_type):
        body_bytes = body.encode("utf-8") if isinstance(body, str) else body
        try:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body_bytes)))
            self.end_headers()
            self.wfile.write(body_bytes)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass

    def log_message(self, fmt, *args):
        return


class LoopbackHttp1Server:
    """HTTP/1.1 loopback server that returns a canned response per mode."""

    def __init__(self, mode, value="bound-hello"):
        self.mode = mode
        self.value = value
        self._server = ThreadingHTTPServer(("127.0.0.1", 0), _Http1Handler)
        self._server.mode = mode
        self._server.value = value
        self._server.request_count = 0
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    @property
    def request_count(self):
        return self._server.request_count

    def stop(self):
        self._server.shutdown()
        self._server.server_close()


# ---------------------------------------------------------------------------
# Loopback HTTP/2 server (for gRPC JSON transcoding tests)
# ---------------------------------------------------------------------------

def _hpack_encode_int(value, prefix_bits):
    max_prefix = (1 << prefix_bits) - 1
    if value < max_prefix:
        return bytes([value])
    result = bytes([max_prefix])
    value -= max_prefix
    while value >= 128:
        result += bytes([(value % 128) + 128])
        value //= 128
    result += bytes([value])
    return result


def _hpack_encode(headers):
    result = b""
    for name, value in headers:
        if name == ":status" and value == "200":
            result += bytes([0x88])  # indexed: :status: 200
        else:
            result += bytes([0x00])  # literal without indexing, new name
            result += _hpack_encode_int(len(name), 7)
            result += name.encode("utf-8")
            result += _hpack_encode_int(len(value), 7)
            result += value.encode("utf-8")
    return result


class LoopbackHttp2Server:
    """Minimal HTTP/2 cleartext (h2c) server for gRPC JSON transcoding tests.

    Handles one request per connection, returns a canned response with
    configurable headers, body, and trailers.
    """

    def __init__(self, mode, value="grpc-bound-hello"):
        self.mode = mode
        self.value = value
        self.request_count = 0
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind(("127.0.0.1", 0))
        self._server_socket.listen(5)
        self.port = self._server_socket.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self):
        while True:
            try:
                conn, _ = self._server_socket.accept()
            except OSError:
                break
            try:
                self._handle_connection(conn)
            except Exception:
                pass
            finally:
                try:
                    conn.close()
                except Exception:
                    pass

    def _read_full(self, conn, length):
        data = b""
        while len(data) < length:
            try:
                chunk = conn.recv(length - len(data))
            except OSError:
                break
            if not chunk:
                break
            data += chunk
        return data

    def _send_frame(self, conn, frame_type, flags, stream_id, payload):
        header = bytes([
            (len(payload) >> 16) & 0xFF,
            (len(payload) >> 8) & 0xFF,
            len(payload) & 0xFF,
            frame_type,
            flags,
            (stream_id >> 24) & 0xFF,
            (stream_id >> 16) & 0xFF,
            (stream_id >> 8) & 0xFF,
            stream_id & 0xFF,
        ])
        try:
            conn.sendall(header + payload)
        except OSError:
            pass

    def _handle_connection(self, conn):
        # Read the HTTP/2 client connection preface (24 bytes).
        preface = self._read_full(conn, 24)
        if len(preface) < 24:
            return

        # HTTP/2 keep-alive: handle multiple requests on the same connection.
        # The gRPC retry logic retries on the same HTTP/2 connection (stream
        # multiplexing), so closing after one request breaks the retry test.
        # A bounded keep-alive timeout prevents hanging when the client is
        # done; EOF (recv returns 0) exits immediately on client disconnect.
        conn.settimeout(5.0)
        while self._handle_one_request(conn):
            pass

    def _handle_one_request(self, conn) -> bool:
        """Handle one HTTP/2 request. Returns False to close the connection."""
        # Read frames until END_STREAM on the request.
        request_complete = False
        request_stream = 0
        while not request_complete:
            frame_header = self._read_full(conn, 9)
            if len(frame_header) < 9:
                return False
            length = (frame_header[0] << 16) | (frame_header[1] << 8) | frame_header[2]
            frame_type = frame_header[3]
            flags = frame_header[4]
            stream_id = int.from_bytes(frame_header[5:9], "big") & 0x7FFFFFFF
            if length > 0:
                self._read_full(conn, length)

            if frame_type == 0x4 and not (flags & 0x1):  # SETTINGS (not ACK)
                self._send_frame(conn, 0x4, 0x0, 0, b"")  # SETTINGS (empty)
                self._send_frame(conn, 0x4, 0x1, 0, b"")  # SETTINGS ACK
            elif frame_type == 0x7:  # GOAWAY: client is closing the connection
                return False
            elif frame_type == 0x1:  # HEADERS
                request_stream = stream_id
                if flags & 0x1:  # END_STREAM
                    request_complete = True
            elif frame_type == 0x0:  # DATA
                if flags & 0x1:  # END_STREAM
                    request_complete = True

        self.request_count += 1

        # Build response based on mode.
        if self.mode == "grpc_success":
            body = json.dumps({"_type": "smoke::Response", "value": self.value})
            response_headers = [(":status", "200"), ("content-type", "application/json")]
            trailers = []
        elif self.mode == "grpc_trailer_failure":
            body = json.dumps({"_type": "smoke::Response", "value": self.value})
            response_headers = [(":status", "200"), ("content-type", "application/json")]
            trailers = [("grpc-status", "7"), ("grpc-message", "permission%20denied")]
        elif self.mode == "grpc_metadata_failure":
            body = json.dumps({"_type": "smoke::Response", "value": self.value})
            response_headers = [
                (":status", "200"),
                ("content-type", "application/json"),
                ("grpc-status", "7"),
                ("grpc-message", "metadata%20denied"),
            ]
            trailers = []
        elif self.mode == "grpc_deadline_retry":
            body = json.dumps({"_type": "smoke::Response", "value": self.value})
            response_headers = [
                (":status", "200"),
                ("content-type", "application/json"),
                ("grpc-status", "4"),
                ("grpc-message", "deadline%20from%20metadata"),
            ]
            trailers = []
        elif self.mode == "grpc_malformed_json":
            body = "{not-json"
            response_headers = [(":status", "200"), ("content-type", "application/json")]
            trailers = []
        elif self.mode == "grpc_schema_mismatch":
            body = json.dumps({"_type": "smoke::UnexpectedResponse", "value": "wrong"})
            response_headers = [(":status", "200"), ("content-type", "application/json")]
            trailers = []
        else:
            body = "{}"
            response_headers = [(":status", "200"), ("content-type", "application/json")]
            trailers = []

        # Send response HEADERS (END_HEADERS, no END_STREAM).
        hpack = _hpack_encode(response_headers)
        self._send_frame(conn, 0x1, 0x4, request_stream, hpack)

        # Send DATA (body, no END_STREAM — trailers follow).
        if body:
            self._send_frame(conn, 0x0, 0x0, request_stream, body.encode("utf-8"))

        # Send trailer HEADERS (END_HEADERS | END_STREAM).
        trailer_hpack = _hpack_encode(trailers)
        self._send_frame(conn, 0x1, 0x5, request_stream, trailer_hpack)

        return True  # keep-alive

    def stop(self):
        try:
            self._server_socket.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------

def write_llm_config(config_path: Path) -> None:
    config_path.write_text(
        json.dumps(
            {
                "endpoint": "http://llm-should-not-be-called.invalid/v1",
                "model": "binding-smoke-model",
                "api_key_secret": "AHFL_RUNTIME_BINDING_LLM_KEY",
                "max_retries": 0,
                "response_cache_enabled": False,
            }
        ),
        encoding="utf-8",
    )


def retry_config(max_retries: int = 0) -> dict:
    return {
        "max_retries": max_retries,
        "initial_delay_ms": 0,
        "backoff_multiplier": 1.0,
    }


def write_http_bindings(
    bindings_path: Path,
    url: str,
    *,
    auth: dict | None = None,
    max_retries: int = 0,
    response_format: str | None = None,
    circuit_breaker: dict | None = None,
) -> None:
    binding = {
        "capability": "smoke::Echo",
        "transport": "http",
        "url": url,
        "method": "POST",
        "headers": {"X-AHFL-Test": "runtime-binding"},
        "timeout_ms": 5000,
        "retry": retry_config(max_retries),
    }
    if response_format is not None:
        binding["response_format"] = response_format
    if circuit_breaker is not None:
        binding["circuit_breaker"] = circuit_breaker
    if auth is not None:
        binding["auth"] = auth
    bindings_path.write_text(
        json.dumps(
            {
                "format": "ahfl.runtime_capability_bindings",
                "bindings": [binding],
            }
        ),
        encoding="utf-8",
    )


def write_grpc_bindings(
    bindings_path: Path,
    endpoint: str,
    *,
    auth: dict | None = None,
    max_retries: int = 0,
    timeout_ms: int = 5000,
) -> None:
    binding = {
        "capability": "smoke::Echo",
        "transport": "grpc_json_transcoding",
        "endpoint": endpoint,
        "service": "smoke.EchoService",
        "method": "Echo",
        "timeout_ms": timeout_ms,
        "retry": retry_config(max_retries),
    }
    if auth is not None:
        binding["auth"] = auth
    bindings_path.write_text(
        json.dumps(
            {
                "format": "ahfl.runtime_capability_bindings",
                "bindings": [binding],
            }
        ),
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# ahflc runner
# ---------------------------------------------------------------------------

def run_ahflc(
    ahflc: str,
    run_dir: Path,
    source_path: Path,
    bindings_path: Path,
):
    config_path = run_dir / "llm_config.json"
    write_llm_config(config_path)

    env = os.environ.copy()
    env["AHFL_RUNTIME_BINDING_LLM_KEY"] = "dummy-llm-key"

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
            "--capability-bindings",
            str(bindings_path),
            str(source_path),
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )


# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

def assert_success(result, expected_value: str, server=None):
    if result.returncode != 0:
        raise AssertionError(
            f"ahflc run with capability bindings failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if expected_value not in result.stdout:
        raise AssertionError(f"workflow output did not use bound capability:\n{result.stdout}")
    if server is not None and server.request_count < 1:
        raise AssertionError("loopback server was not invoked by runtime capability binding")


def assert_failure(
    result,
    expected_diagnostic_fragment: str,
    *,
    server=None,
    expected_request_count: int | None = None,
    forbidden_fragment: str | None = None,
):
    if result.returncode == 0:
        raise AssertionError(
            f"ahflc run with failing capability binding unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    combined_output = result.stdout + result.stderr
    if expected_diagnostic_fragment not in combined_output:
        raise AssertionError(
            f"failing capability binding missing diagnostic {expected_diagnostic_fragment!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if forbidden_fragment is not None and forbidden_fragment in combined_output:
        raise AssertionError(
            f"failing capability binding diagnostic unexpectedly contained "
            f"{forbidden_fragment!r}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if server is not None:
        if expected_request_count is not None:
            if server.request_count != expected_request_count:
                raise AssertionError(
                    f"expected {expected_request_count} server requests, got {server.request_count}\n"
                    f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
                )
        elif server.request_count < 1:
            raise AssertionError("loopback server was not invoked by failing runtime capability binding")


# ---------------------------------------------------------------------------
# RFC 0026 C2b G4b: rich --input exact-decode matrix
# (unchanged from the original — capability-free workflow, no transport needed)
# ---------------------------------------------------------------------------

_RICH_PACKAGE_MANIFEST = """[package]
name = "g4b-rich-input"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "rich"
root = "."

[targets.workflow]
kind = "handoff"
entry = "rich::main::EchoWorkflow"
exports = [
  { kind = "workflow", name = "rich::main::EchoWorkflow" },
  { kind = "agent", name = "rich::main::EchoAgent" },
]

[dependencies]
std = { source = "sysroot" }
"""

_RICH_PACKAGE_SOURCE = """module rich::main;

import std::collections as collections;
import std::option as option;

pub struct RichInput {
    dec: Decimal(2);
    dur: Duration;
    nums: collections::Set<Int>(8);
    tags: collections::Map<String, Int>(8);
    opt: option::Option<Int>;
    nothing: Unit;
    flag: Float;
    count: Int;
    note: String;
}

pub struct RichOutput {
    dec: Decimal(2);
    dur: Duration;
    nums: collections::Set<Int>(8);
    tags: collections::Map<String, Int>(8);
    opt: option::Option<Int>;
    nothing: Unit;
    flag: Float;
    count: Int;
    note: String;
}

pub agent EchoAgent {
    input: RichInput;
    context: Unit;
    output: RichOutput;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for EchoAgent {
    state Init {
        goto Done;
    }

    state Done {
        return RichOutput {
            dec: input.dec,
            dur: input.dur,
            nums: input.nums,
            tags: input.tags,
            opt: input.opt,
            nothing: input.nothing,
            flag: input.flag,
            count: input.count,
            note: input.note,
        };
    }
}

pub workflow EchoWorkflow {
    input: RichInput;
    output: RichOutput;

    node first: EchoAgent(input);

    return: RichOutput {
        dec: first.dec,
        dur: first.dur,
        nums: first.nums,
        tags: first.tags,
        opt: first.opt,
        nothing: first.nothing,
        flag: first.flag,
        count: first.count,
        note: first.note,
    };
}
"""


def sysroot_root() -> Path:
    repo_root = Path(__file__).resolve().parents[2]
    if not (repo_root / "std" / "ahfl.toml").is_file():
        raise AssertionError(f"std sysroot manifest not found under {repo_root}")
    return repo_root


def write_rich_package(pkg_dir: Path) -> None:
    pkg_dir.mkdir(parents=True, exist_ok=True)
    (pkg_dir / "ahfl.toml").write_text(_RICH_PACKAGE_MANIFEST, encoding="utf-8")
    (pkg_dir / "main.ahfl").write_text(_RICH_PACKAGE_SOURCE, encoding="utf-8")


def rich_input_dict(overrides: dict) -> dict:
    payload = {
        "_type": "rich::main::RichInput",
        "dec": "1.23",
        "dur": "5s",
        "nums": [1, 2, 3],
        "tags": {"_timestamp": 7, "plain": 9},
        "opt": 42,
        "nothing": None,
        "flag": 1.0,
        "count": 5,
        "note": "ok",
    }
    payload.update(overrides)
    return payload


def rich_input_json(overrides: dict) -> str:
    return json.dumps(rich_input_dict(overrides))


def run_rich_input(ahflc: str, pkg_dir: Path, run_dir: Path, input_json: str):
    config_path = run_dir / "rich_llm_config.json"
    write_llm_config(config_path)

    env = os.environ.copy()
    env["AHFL_RUNTIME_BINDING_LLM_KEY"] = "dummy-llm-key"

    result = subprocess.run(
        [
            ahflc,
            "run",
            "--manifest",
            str(pkg_dir / "ahfl.toml"),
            "--target",
            "workflow",
            "--sysroot",
            str(sysroot_root()),
            "--workflow",
            "rich::main::EchoWorkflow",
            "--input",
            input_json,
            "--llm-config",
            str(config_path),
            "--output-format",
            "json",
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )
    return result


def assert_rich_positive(ahflc, pkg_dir, run_dir, tag, overrides):
    input_payload = rich_input_dict(overrides)
    expected = dict(input_payload)
    expected["_type"] = "rich::main::RichOutput"

    result = run_rich_input(ahflc, pkg_dir, run_dir, json.dumps(input_payload))
    if result.returncode != 0:
        raise AssertionError(
            f"rich positive [{tag}] failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"rich positive [{tag}] did not emit JSON output: {exc}\n{result.stdout}"
        )
    if document.get("run", {}).get("status") != "completed":
        raise AssertionError(
            f"rich positive [{tag}] workflow did not complete: {document.get('run')!r}"
        )
    output = document.get("result")
    if output != expected:
        raise AssertionError(
            f"rich positive [{tag}] result mismatch\n"
            f"expected: {expected!r}\n     got: {output!r}"
        )


def assert_rich_negative(ahflc, pkg_dir, run_dir, tag, overrides, expected_diagnostic):
    secret_marker = f"G4B_RICH_SECRET_{tag.upper()}"
    payload = dict(overrides)
    payload["note"] = secret_marker
    result = run_rich_input(
        ahflc, pkg_dir, run_dir, rich_input_json(payload)
    )
    if result.returncode == 0:
        raise AssertionError(
            f"rich negative [{tag}] unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    combined_output = result.stdout + result.stderr
    seam = "--input does not match workflow input schema"
    if seam not in combined_output:
        raise AssertionError(
            f"rich negative [{tag}] not attributed to the --input seam ({seam!r}):\n"
            f"{combined_output}"
        )
    if expected_diagnostic not in combined_output:
        raise AssertionError(
            f"rich negative [{tag}] missing diagnostic {expected_diagnostic!r}:\n"
            f"{combined_output}"
        )
    if secret_marker in combined_output:
        raise AssertionError(
            f"rich negative [{tag}] echoed the payload marker {secret_marker!r}:\n"
            f"{combined_output}"
        )


def run_rich_input_matrix(ahflc: str, run_dir: Path) -> None:
    pkg_dir = run_dir / "rich_input_pkg"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    write_rich_package(pkg_dir)

    assert_rich_positive(ahflc, pkg_dir, run_dir, "baseline_all_shapes", {})
    assert_rich_positive(ahflc, pkg_dir, run_dir, "option_none", {"opt": None})
    assert_rich_positive(ahflc, pkg_dir, run_dir, "float_fractional", {"flag": 1.5})

    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "high_uint",
        {"count": 18446744073709551615}, "wire-codec: expected integer",
    )
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "pos_integer_fallback",
        {"count": 18446744073709551616}, "wire-codec: expected integer",
    )
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "neg_integer_fallback",
        {"count": -9223372036854775809}, "wire-codec: expected integer",
    )
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "float_bare_int",
        {"flag": 1}, "wire-codec: expected float (no int widening)",
    )
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "unit_wrong_kind",
        {"nothing": 5}, "wire-codec: expected null for Unit",
    )


def run_structural_priority_cases(ahflc: str, run_dir: Path) -> None:
    pkg_dir = run_dir / "rich_input_pkg"
    if not pkg_dir.exists():
        write_rich_package(pkg_dir)

    config_path = run_dir / "rich_llm_config.json"
    write_llm_config(config_path)
    env = os.environ.copy()
    env["AHFL_RUNTIME_BINDING_LLM_KEY"] = "dummy-llm-key"
    missing = subprocess.run(
        [
            ahflc,
            "run",
            "--manifest",
            str(pkg_dir / "ahfl.toml"),
            "--target",
            "workflow",
            "--sysroot",
            str(sysroot_root()),
            "--workflow",
            "rich::main::NoSuchWorkflow",
            "--input",
            rich_input_json({}),
            "--llm-config",
            str(config_path),
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )
    if missing.returncode == 0:
        raise AssertionError("missing-workflow run unexpectedly succeeded")
    missing_output = missing.stdout + missing.stderr
    if "not found in program" not in missing_output:
        raise AssertionError(
            f"missing-workflow missing canonical not-found diagnostic:\n{missing_output}"
        )
    competing_diagnostics = (
        "does not match workflow input schema",
        "failed to parse runtime input JSON",
    )
    for competing in competing_diagnostics:
        if competing in missing_output:
            raise AssertionError(
                f"missing-workflow leaked competing diagnostic {competing!r}:\n{missing_output}"
            )

    malformed = subprocess.run(
        [
            ahflc,
            "run",
            "--manifest",
            str(pkg_dir / "ahfl.toml"),
            "--target",
            "workflow",
            "--sysroot",
            str(sysroot_root()),
            "--workflow",
            "rich::main::NoSuchWorkflow",
            "--input",
            "{not-json",
            "--llm-config",
            str(config_path),
        ],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )
    if malformed.returncode == 0:
        raise AssertionError("malformed --input run unexpectedly succeeded")
    malformed_output = malformed.stdout + malformed.stderr
    if "failed to parse runtime input JSON" not in malformed_output:
        raise AssertionError(
            f"malformed --input missing parse diagnostic:\n{malformed_output}"
        )
    if "not found in program" in malformed_output:
        raise AssertionError(
            f"malformed --input let not-found preempt the syntax error:\n{malformed_output}"
        )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: runtime_capability_bindings_smoke.py <ahflc> <work-dir>")

    ahflc = sys.argv[1]
    work_dir = Path(sys.argv[2])
    run_dir = work_dir / f"run-{os.getpid()}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True)
    home = run_dir / "home"
    home.mkdir(mode=0o700)
    os.environ["HOME"] = str(home.resolve())

    source_path = run_dir / "smoke.ahfl"
    source_path.write_text(SOURCE, encoding="utf-8")

    # --- HTTP capability binding tests ---

    http_server = LoopbackHttp1Server("http_success", "bound-hello")
    try:
        http_bindings_path = run_dir / "http_runtime_bindings.json"
        write_http_bindings(http_bindings_path, f"http://127.0.0.1:{http_server.port}/echo")
        result = run_ahflc(ahflc, run_dir, source_path, http_bindings_path)
        assert_success(result, "bound-hello", http_server)
    finally:
        http_server.stop()

    # HTTP auth failure: bearer token secret not resolved → fail closed, zero requests.
    http_auth_server = LoopbackHttp1Server("http_success")
    try:
        http_auth_bindings_path = run_dir / "http_auth_runtime_bindings.json"
        write_http_bindings(
            http_auth_bindings_path,
            f"http://127.0.0.1:{http_auth_server.port}/echo",
            auth={"scheme": "bearer", "token_key": "AHFL_RUNTIME_BINDING_MISSING_TOKEN"},
        )
        result = run_ahflc(ahflc, run_dir, source_path, http_auth_bindings_path)
        assert_failure(result, "bearer token secret not found", server=http_auth_server,
                       expected_request_count=0)
    finally:
        http_auth_server.stop()

    # HTTP retry failure: 503 → retry once → retry_exhausted.
    http_retry_server = LoopbackHttp1Server("http_retry_failure")
    try:
        http_retry_bindings_path = run_dir / "http_retry_runtime_bindings.json"
        write_http_bindings(http_retry_bindings_path,
                           f"http://127.0.0.1:{http_retry_server.port}/echo",
                           max_retries=1)
        result = run_ahflc(ahflc, run_dir, source_path, http_retry_bindings_path)
        assert_failure(result, "retry_exhausted", server=http_retry_server,
                       expected_request_count=2)
    finally:
        http_retry_server.stop()

    # HTTP malformed JSON body.
    http_malformed_server = LoopbackHttp1Server("http_malformed_json")
    try:
        http_malformed_bindings_path = run_dir / "http_malformed_runtime_bindings.json"
        write_http_bindings(http_malformed_bindings_path,
                           f"http://127.0.0.1:{http_malformed_server.port}/echo")
        result = run_ahflc(ahflc, run_dir, source_path, http_malformed_bindings_path)
        assert_failure(result, "invalid wire JSON response body", server=http_malformed_server)
    finally:
        http_malformed_server.stop()

    # --- gRPC JSON transcoding tests ---

    grpc_server = LoopbackHttp2Server("grpc_success", "grpc-bound-hello")
    try:
        grpc_bindings_path = run_dir / "grpc_runtime_bindings.json"
        write_grpc_bindings(grpc_bindings_path, f"http://127.0.0.1:{grpc_server.port}")
        result = run_ahflc(ahflc, run_dir, source_path, grpc_bindings_path)
        assert_success(result, "grpc-bound-hello", grpc_server)
    finally:
        grpc_server.stop()

    # gRPC metadata failure: grpc-status: 7 in response headers.
    grpc_metadata_server = LoopbackHttp2Server("grpc_metadata_failure")
    try:
        grpc_metadata_bindings_path = run_dir / "grpc_metadata_bindings.json"
        write_grpc_bindings(grpc_metadata_bindings_path,
                           f"http://127.0.0.1:{grpc_metadata_server.port}")
        result = run_ahflc(ahflc, run_dir, source_path, grpc_metadata_bindings_path)
        assert_failure(result, "metadata denied", server=grpc_metadata_server)
    finally:
        grpc_metadata_server.stop()

    # gRPC trailer failure: grpc-status: 7 in trailers.
    grpc_trailer_server = LoopbackHttp2Server("grpc_trailer_failure")
    try:
        grpc_trailer_bindings_path = run_dir / "grpc_trailer_bindings.json"
        write_grpc_bindings(grpc_trailer_bindings_path,
                           f"http://127.0.0.1:{grpc_trailer_server.port}")
        result = run_ahflc(ahflc, run_dir, source_path, grpc_trailer_bindings_path)
        assert_failure(result, "permission denied", server=grpc_trailer_server)
    finally:
        grpc_trailer_server.stop()

    # gRPC deadline retry: grpc-status: 4 → retry once → retry_exhausted.
    grpc_retry_server = LoopbackHttp2Server("grpc_deadline_retry")
    try:
        grpc_retry_bindings_path = run_dir / "grpc_retry_bindings.json"
        write_grpc_bindings(grpc_retry_bindings_path,
                           f"http://127.0.0.1:{grpc_retry_server.port}",
                           max_retries=1)
        result = run_ahflc(ahflc, run_dir, source_path, grpc_retry_bindings_path)
        assert_failure(result, "retry_exhausted", server=grpc_retry_server,
                       expected_request_count=2)
    finally:
        grpc_retry_server.stop()

    # gRPC schema mismatch: wrong _type in response.
    grpc_schema_server = LoopbackHttp2Server("grpc_schema_mismatch")
    try:
        grpc_schema_bindings_path = run_dir / "grpc_schema_bindings.json"
        write_grpc_bindings(grpc_schema_bindings_path,
                           f"http://127.0.0.1:{grpc_schema_server.port}")
        result = run_ahflc(ahflc, run_dir, source_path, grpc_schema_bindings_path)
        assert_failure(result, "response schema validation failed", server=grpc_schema_server)
    finally:
        grpc_schema_server.stop()

    # --- TextPlain Phase A admission (no server needed) ---

    textplain_bindings_path = run_dir / "http_textplain_phase_a_admission_bindings.json"
    write_http_bindings(
        textplain_bindings_path,
        "http://127.0.0.1:1/echo",  # unreachable, but admission fails before network
        max_retries=3,
        response_format="text_plain",
        circuit_breaker={"enabled": True, "failure_threshold": 1},
    )
    result = run_ahflc(ahflc, run_dir, source_path, textplain_bindings_path)
    assert_failure(
        result,
        "response schema admission failed",
        expected_request_count=0,
        forbidden_fragment="retry_exhausted",
    )

    # --- RFC 0026 C2b G4b: rich --input exact-decode matrix ---

    run_rich_input_matrix(ahflc, run_dir)
    run_structural_priority_cases(ahflc, run_dir)


if __name__ == "__main__":
    main()
