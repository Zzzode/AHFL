#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
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


def write_fake_curl(fake_bin_dir: Path) -> None:
    fake_bin_dir.mkdir()
    fake_curl_path = fake_bin_dir / "curl"
    fake_curl_path.write_text(
        """#!/usr/bin/env python3
import json
import os
import sys

config = sys.stdin.read()
with open(os.environ["AHFL_RUNTIME_BINDING_CURL_CONFIG"], "a", encoding="utf-8") as handle:
    handle.write("\\n--- request ---\\n")
    handle.write(config)

mode = os.environ["AHFL_RUNTIME_BINDING_MODE"]
payload = {
    "_type": "smoke::Response",
    "value": os.environ.get("AHFL_RUNTIME_BINDING_VALUE", "bound-hello"),
}
body = json.dumps(payload)
status_code = "200"

if mode == "http_retry_failure":
    body = "retry failure"
    status_code = "503"
elif mode == "http_malformed_json":
    body = "{not-json"
elif mode == "http_schema_mismatch":
    body = json.dumps({"_type": "smoke::UnexpectedResponse", "value": "wrong"})

if mode.startswith("grpc_"):
    header_path = None
    for line in config.splitlines():
        if line.startswith('dump-header = "'):
            header_path = line[len('dump-header = "'):-1]
    if header_path is None:
        raise SystemExit("missing dump-header in gRPC curl config")
    if mode == "grpc_trailer_failure":
        with open(header_path, "w", encoding="utf-8") as handle:
            handle.write("HTTP/2 200\\r\\n")
            handle.write("content-type: application/json\\r\\n")
            handle.write("\\r\\n")
            handle.write("grpc-status: 7\\r\\n")
            handle.write("grpc-message: permission%20denied\\r\\n")
            handle.write("\\r\\n")
    elif mode == "grpc_metadata_failure":
        with open(header_path, "w", encoding="utf-8") as handle:
            handle.write("HTTP/2 200\\r\\n")
            handle.write("content-type: application/json\\r\\n")
            handle.write("grpc-status: 7\\r\\n")
            handle.write("grpc-message: metadata%20denied\\r\\n")
            handle.write("\\r\\n")
    elif mode == "grpc_deadline_retry":
        with open(header_path, "w", encoding="utf-8") as handle:
            handle.write("HTTP/2 200\\r\\n")
            handle.write("content-type: application/json\\r\\n")
            handle.write("grpc-status: 4\\r\\n")
            handle.write("grpc-message: deadline%20from%20metadata\\r\\n")
            handle.write("\\r\\n")
    else:
        with open(header_path, "w", encoding="utf-8") as handle:
            handle.write("HTTP/2 200\\r\\n")
            handle.write("content-type: application/json\\r\\n")
            handle.write("\\r\\n")
    if mode == "grpc_malformed_json":
        body = "{not-json"
    elif mode == "grpc_schema_mismatch":
        body = json.dumps({"_type": "smoke::UnexpectedResponse", "value": "wrong"})

sys.stdout.write(body + "\\n" + status_code + "\\n")
""",
        encoding="utf-8",
    )
    fake_curl_path.chmod(0o755)


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
    bindings_path: Path, *, auth: dict | None = None, max_retries: int = 0
) -> None:
    binding = {
        "capability": "smoke::Echo",
        "transport": "http",
        "url": "http://capability.example.test/echo",
        "method": "POST",
        "headers": {"X-AHFL-Test": "runtime-binding"},
        "timeout_ms": 1000,
        "retry": retry_config(max_retries),
    }
    if auth is not None:
        binding["auth"] = auth
    bindings_path.write_text(
        json.dumps(
            {
                "schema": "ahfl.runtime_capability_bindings.v0",
                "bindings": [binding],
            }
        ),
        encoding="utf-8",
    )


def write_grpc_bindings(
    bindings_path: Path,
    *,
    auth: dict | None = None,
    max_retries: int = 0,
    timeout_ms: int = 1000,
) -> None:
    binding = {
        "capability": "smoke::Echo",
        "transport": "grpc_json_transcoding",
        "endpoint": "http://grpc-capability.example.test:50051",
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
                "schema": "ahfl.runtime_capability_bindings.v0",
                "bindings": [binding],
            }
        ),
        encoding="utf-8",
    )


def run_ahflc(
    ahflc: str,
    run_dir: Path,
    source_path: Path,
    bindings_path: Path,
    mode: str,
    value: str = "bound-hello",
):
    config_path = run_dir / "llm_config.json"
    curl_capture_path = run_dir / "curl_config.txt"
    fake_bin_dir = run_dir / "fake-curl-bin"
    if fake_bin_dir.exists():
        shutil.rmtree(fake_bin_dir)
    if curl_capture_path.exists():
        curl_capture_path.unlink()
    write_fake_curl(fake_bin_dir)
    write_llm_config(config_path)

    env = os.environ.copy()
    env["PATH"] = f"{fake_bin_dir}{os.pathsep}{env.get('PATH', '')}"
    env["AHFL_RUNTIME_BINDING_CURL_CONFIG"] = str(curl_capture_path)
    env["AHFL_RUNTIME_BINDING_LLM_KEY"] = "dummy-llm-key"
    env["AHFL_RUNTIME_BINDING_MODE"] = mode
    env["AHFL_RUNTIME_BINDING_VALUE"] = value

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
        timeout=20,
    ), curl_capture_path


def assert_success(result, curl_capture_path: Path, expected_value: str, expected_config_fragments):
    if result.returncode != 0:
        raise AssertionError(
            f"ahflc run with capability bindings failed with {result.returncode}\\n"
            f"stdout:\\n{result.stdout}\\nstderr:\\n{result.stderr}"
        )
    if expected_value not in result.stdout:
        raise AssertionError(f"workflow output did not use bound capability:\\n{result.stdout}")
    if not curl_capture_path.exists():
        raise AssertionError("fake curl was not invoked by runtime capability binding")

    curl_config = curl_capture_path.read_text(encoding="utf-8")
    for fragment in expected_config_fragments:
        if fragment not in curl_config:
            raise AssertionError(f"curl config missing {fragment!r}:\\n{curl_config}")
    if "llm-should-not-be-called" in curl_config:
        raise AssertionError(f"runtime binding unexpectedly called LLM endpoint:\\n{curl_config}")


def request_count(curl_capture_path: Path) -> int:
    if not curl_capture_path.exists():
        return 0
    return curl_capture_path.read_text(encoding="utf-8").count("--- request ---")


def assert_failure(
    result,
    curl_capture_path: Path,
    expected_diagnostic_fragment: str,
    *,
    expect_curl_invoked: bool = True,
    expected_request_count: int | None = None,
):
    if result.returncode == 0:
        raise AssertionError(
            f"ahflc run with failing capability binding unexpectedly succeeded\\n"
            f"stdout:\\n{result.stdout}\\nstderr:\\n{result.stderr}"
        )
    combined_output = result.stdout + result.stderr
    if expected_diagnostic_fragment not in combined_output:
        raise AssertionError(
            f"failing capability binding missing diagnostic {expected_diagnostic_fragment!r}\\n"
            f"stdout:\\n{result.stdout}\\nstderr:\\n{result.stderr}"
        )
    if expect_curl_invoked and not curl_capture_path.exists():
        raise AssertionError("fake curl was not invoked by failing runtime capability binding")
    if not expect_curl_invoked and curl_capture_path.exists():
        raise AssertionError(
            f"fake curl should not be invoked for fail-closed binding:\\n"
            f"{curl_capture_path.read_text(encoding='utf-8')}"
        )
    if expected_request_count is not None:
        actual_count = request_count(curl_capture_path)
        if actual_count != expected_request_count:
            raise AssertionError(
                f"expected {expected_request_count} curl requests, got {actual_count}\\n"
                f"stdout:\\n{result.stdout}\\nstderr:\\n{result.stderr}"
            )


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: runtime_capability_bindings_smoke.py <ahflc> <work-dir>")

    ahflc = sys.argv[1]
    work_dir = Path(sys.argv[2])
    run_dir = work_dir / f"run-{os.getpid()}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True)

    source_path = run_dir / "smoke.ahfl"
    source_path.write_text(SOURCE, encoding="utf-8")

    http_bindings_path = run_dir / "http_runtime_bindings.json"
    write_http_bindings(http_bindings_path)
    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, http_bindings_path, "http_success", "bound-hello"
    )
    assert_success(
        result,
        curl_capture_path,
        "bound-hello",
        [
            'url = "http://capability.example.test/echo"',
            'request = "POST"',
            'header = "X-AHFL-Test: runtime-binding"',
        ],
    )

    http_auth_bindings_path = run_dir / "http_auth_runtime_bindings.json"
    write_http_bindings(
        http_auth_bindings_path,
        auth={"scheme": "bearer", "token_key": "AHFL_RUNTIME_BINDING_MISSING_TOKEN"},
    )
    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, http_auth_bindings_path, "http_success"
    )
    assert_failure(
        result,
        curl_capture_path,
        "bearer token secret not found",
        expect_curl_invoked=False,
    )

    http_retry_bindings_path = run_dir / "http_retry_runtime_bindings.json"
    write_http_bindings(http_retry_bindings_path, max_retries=1)
    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, http_retry_bindings_path, "http_retry_failure"
    )
    assert_failure(
        result,
        curl_capture_path,
        "retry_exhausted",
        expected_request_count=2,
    )

    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, http_bindings_path, "http_malformed_json"
    )
    assert_failure(result, curl_capture_path, "invalid wire JSON response body")

    grpc_bindings_path = run_dir / "grpc_runtime_bindings.json"
    write_grpc_bindings(grpc_bindings_path)
    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, grpc_bindings_path, "grpc_success", "grpc-bound-hello"
    )
    assert_success(
        result,
        curl_capture_path,
        "grpc-bound-hello",
        [
            'url = "http://grpc-capability.example.test:50051/smoke.EchoService/Echo"',
            'request = "POST"',
            "http2-prior-knowledge",
            'header = "TE: trailers"',
        ],
    )

    result, curl_capture_path = run_ahflc(
        ahflc,
        run_dir,
        source_path,
        grpc_bindings_path,
        "grpc_metadata_failure",
        "should-not-complete",
    )
    assert_failure(result, curl_capture_path, "metadata denied")

    result, curl_capture_path = run_ahflc(
        ahflc,
        run_dir,
        source_path,
        grpc_bindings_path,
        "grpc_trailer_failure",
        "should-not-complete",
    )
    assert_failure(result, curl_capture_path, "permission denied")

    grpc_retry_bindings_path = run_dir / "grpc_retry_runtime_bindings.json"
    write_grpc_bindings(grpc_retry_bindings_path, max_retries=1)
    result, curl_capture_path = run_ahflc(
        ahflc,
        run_dir,
        source_path,
        grpc_retry_bindings_path,
        "grpc_deadline_retry",
        "should-not-complete",
    )
    assert_failure(
        result,
        curl_capture_path,
        "retry_exhausted",
        expected_request_count=2,
    )

    result, curl_capture_path = run_ahflc(
        ahflc,
        run_dir,
        source_path,
        grpc_bindings_path,
        "grpc_schema_mismatch",
        "should-not-complete",
    )
    assert_failure(result, curl_capture_path, "response schema validation failed")


if __name__ == "__main__":
    main()
