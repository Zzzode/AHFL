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
    bindings_path: Path,
    *,
    auth: dict | None = None,
    max_retries: int = 0,
    response_format: str | None = None,
    circuit_breaker: dict | None = None,
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
    if response_format is not None:
        binding["response_format"] = response_format
    if circuit_breaker is not None:
        binding["circuit_breaker"] = circuit_breaker
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
    forbidden_fragment: str | None = None,
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
    if forbidden_fragment is not None and forbidden_fragment in combined_output:
        raise AssertionError(
            f"failing capability binding diagnostic unexpectedly contained "
            f"{forbidden_fragment!r}\\nstdout:\\n{result.stdout}\\nstderr:\\n{result.stderr}"
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


# RFC 0026 C2b G4b: the rich --input matrix. `ahflc run` exact-decodes --input
# under the resolved workflow's projected wire binding (the schema-guided codec
# path: decode_json builds the canonical native Value directly) instead of the
# old schema-free value_from_json materialization + validator, which would
# false-reject variant-carrying shapes (Decimal/Duration/Set/Map/Option) and
# accept an Int where a Float is required. The cases below run a capability-free
# echo workflow so the decoded native Value flows all the way through the
# evaluator to the workflow output, and assert against the canonical
# --output-format json result (immune to CLI-arg / warning / human-renderer
# false hits). Because there is no capability binding, no transport is ever
# constructed; a sentinel fake-curl on PATH proves ZERO curl invocations.
#
# Honest evidence boundary: value_to_json serializes FloatValue(1.0) as bare `1`
# and both UnitValue and NoneValue as `null` (P0-16). So a JSON echo cannot by
# itself distinguish a FloatValue from an IntValue, nor a UnitValue from a
# NoneValue. The Float native-variant proof is therefore an accept->reject FLIP
# (1.0 completes; bare `1` is exact-rejected — the old validator accepted it),
# and the Unit proof is explicitly two-layer (CLI null positive completes here;
# the native UnitValue variant is pinned by the core codec unit matrix, not by
# this JSON output).
_RICH_PACKAGE_MANIFEST = """manifest_version = 1

[package]
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

# A capability-free echo workflow (mirrors the golden e3 identity workflow:
# capabilities: [], Init -> Done, Done returns the input). RichInput aggregates
# every lossy shape the exact codec must round-trip; RichOutput echoes them back.
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
    # The std sysroot (std/ahfl.toml) lives at the repo root; this script is at
    # <repo>/tests/scripts/runtime_capability_bindings_smoke.py.
    repo_root = Path(__file__).resolve().parents[2]
    if not (repo_root / "std" / "ahfl.toml").is_file():
        raise AssertionError(f"std sysroot manifest not found under {repo_root}")
    return repo_root


def write_rich_package(pkg_dir: Path) -> None:
    pkg_dir.mkdir(parents=True, exist_ok=True)
    (pkg_dir / "ahfl.toml").write_text(_RICH_PACKAGE_MANIFEST, encoding="utf-8")
    (pkg_dir / "main.ahfl").write_text(_RICH_PACKAGE_SOURCE, encoding="utf-8")


def rich_input_dict(overrides: dict) -> dict:
    # A fully-valid RichInput; each case overrides one field with the shape under
    # test. `note` carries a unique secret marker so negatives can assert the
    # exact-decode diagnostic never echoes the user payload.
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
    # Drive `ahflc run` on the rich echo package with a sentinel fake-curl on
    # PATH. The workflow calls no capability, so a well-behaved exact-decode run
    # must NEVER invoke curl; the curl-config capture file must not appear.
    config_path = run_dir / "rich_llm_config.json"
    curl_capture_path = run_dir / "rich_curl_config.txt"
    fake_bin_dir = run_dir / "rich-fake-curl-bin"
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
    env["AHFL_RUNTIME_BINDING_MODE"] = "http_success"
    env["AHFL_RUNTIME_BINDING_VALUE"] = "unused"

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
        timeout=20,
    )
    return result, curl_capture_path


def assert_rich_positive(ahflc, pkg_dir, run_dir, tag, overrides):
    # Positive: exact-decode succeeds, the workflow COMPLETES, and the canonical
    # --output-format json result DEEP-EQUALS the echoed input (with _type flipped
    # to RichOutput). Proves the decoded native Value flowed through the evaluator
    # to the workflow output — not just that admission did not error. The compare
    # is exact-field-set (== on the whole dict), so a missing/extra field fails.
    #
    # NOTE: Python's 1.0 == 1, so a Float field does NOT carry native-variant
    # provenance in this deep-equal; that proof is the paired bare-`1` negative.
    input_payload = rich_input_dict(overrides)
    expected = dict(input_payload)
    expected["_type"] = "rich::main::RichOutput"

    result, curl_capture_path = run_rich_input(ahflc, pkg_dir, run_dir, json.dumps(input_payload))
    if result.returncode != 0:
        raise AssertionError(
            f"rich positive [{tag}] failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if curl_capture_path.exists():
        raise AssertionError(
            f"rich positive [{tag}] invoked curl for a capability-free workflow:\n"
            f"{curl_capture_path.read_text(encoding='utf-8')}"
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
    # Negative: real CLI exact-decode gate rejects the hostile shape BEFORE any
    # transport. A unique secret marker rides in `note`; the diagnostic must hit
    # the expected codec wording, fail closed (exit != 0), invoke zero curl, and
    # never echo the payload marker anywhere in stdout+stderr.
    secret_marker = f"G4B_RICH_SECRET_{tag.upper()}"
    payload = dict(overrides)
    payload["note"] = secret_marker
    result, curl_capture_path = run_rich_input(
        ahflc, pkg_dir, run_dir, rich_input_json(payload)
    )
    if result.returncode == 0:
        raise AssertionError(
            f"rich negative [{tag}] unexpectedly succeeded\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    combined_output = result.stdout + result.stderr
    # Attribution: the failure must be at the CLI Phase-A --input seam, not merely
    # some place a codec string happens to appear.
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
    if curl_capture_path.exists():
        raise AssertionError(
            f"rich negative [{tag}] invoked curl before failing closed:\n"
            f"{curl_capture_path.read_text(encoding='utf-8')}"
        )


def run_rich_input_matrix(ahflc: str, run_dir: Path) -> None:
    pkg_dir = run_dir / "rich_input_pkg"
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    write_rich_package(pkg_dir)

    # POSITIVES — the default rich_input_dict already carries every lossy shape
    # at once (Decimal "1.23", Duration "5s", Set [1,2,3], Map with a
    # reserved-marker-LOOKING String key, Option Some(42), Unit null, Float 1.0),
    # so ONE baseline run whose full result deep-equals the echoed input proves
    # the decoder traversed and rebuilt all of them. The old schema-free validator
    # would false-reject the variant-carrying shapes, so this completed deep-equal
    # is the caller-adaptation regression proof; any single shape regressing fails
    # the whole compare (the error prints the exact expected/got dicts). Only the
    # shapes NOT expressible by the baseline get their own run.
    assert_rich_positive(ahflc, pkg_dir, run_dir, "baseline_all_shapes", {})
    # Option<Int> None (the baseline covers Some); null decodes to None.
    assert_rich_positive(ahflc, pkg_dir, run_dir, "option_none", {"opt": None})
    # Float 1.5: an unambiguous fractional value that (unlike 1.0 -> bare `1`)
    # survives value_to_json, so the result deep-equal directly observes it.
    assert_rich_positive(ahflc, pkg_dir, run_dir, "float_fractional", {"flag": 1.5})

    # NEGATIVES — real CLI exact-decode gate. Numeric provenance triad (all report
    # "expected integer", but the boundary literals themselves are the provenance
    # evidence): UINT64_MAX (UnsignedInteger, > INT64_MAX), UINT64_MAX+1 (positive
    # IntegerFallback), INT64_MIN-1 (negative IntegerFallback).
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
    # Float accept->reject FLIP: bare integer `1` where the schema wants Float.
    # The old schema-free validator ACCEPTED this; the exact codec rejects it.
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "float_bare_int",
        {"flag": 1}, "wire-codec: expected float (no int widening)",
    )
    # Unit wrong-kind: CLI exact admission evidence (not a native-variant claim).
    assert_rich_negative(
        ahflc, pkg_dir, run_dir, "unit_wrong_kind",
        {"nothing": 5}, "wire-codec: expected null for Unit",
    )


def run_structural_priority_cases(ahflc: str, run_dir: Path) -> None:
    pkg_dir = run_dir / "rich_input_pkg"
    if not pkg_dir.exists():
        write_rich_package(pkg_dir)

    # missing-workflow: a legal input DOM but a workflow name absent from the
    # program. Admission passes structurally; the run reaches the canonical
    # not-found failure rather than an input error.
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
        timeout=20,
    )
    if missing.returncode == 0:
        raise AssertionError("missing-workflow run unexpectedly succeeded")
    missing_output = missing.stdout + missing.stderr
    if "not found in program" not in missing_output:
        raise AssertionError(
            f"missing-workflow missing canonical not-found diagnostic:\n{missing_output}"
        )
    # A legal DOM must NOT trip an input-schema or parse diagnostic: the run got
    # far enough to reach the runtime's not-found seam.
    competing_diagnostics = (
        "does not match workflow input schema",
        "failed to parse runtime input JSON",
    )
    for competing in competing_diagnostics:
        if competing in missing_output:
            raise AssertionError(
                f"missing-workflow leaked competing diagnostic {competing!r}:\n{missing_output}"
            )

    # malformed input SYNTAX takes priority over everything downstream. Use a
    # BOGUS --workflow name too, so the parse error must beat the not-found seam:
    # if syntax were not checked first, this would report not-found instead.
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
        timeout=20,
    )
    if malformed.returncode == 0:
        raise AssertionError("malformed --input run unexpectedly succeeded")
    malformed_output = malformed.stdout + malformed.stderr
    if "failed to parse runtime input JSON" not in malformed_output:
        raise AssertionError(
            f"malformed --input missing parse diagnostic:\n{malformed_output}"
        )
    # Parse priority: the competing not-found diagnostic must NOT appear.
    if "not found in program" in malformed_output:
        raise AssertionError(
            f"malformed --input let not-found preempt the syntax error:\n{malformed_output}"
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
    # G4a user-nominal POSITIVE: smoke::Echo returns the user struct smoke::Response;
    # the JSON body decodes exactly under the projected response_wire_binding and the
    # workflow emits the bound value.
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
    # G4a user-nominal POSITIVE (gRPC): same smoke::Response struct decoded exactly.
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

    # RFC 0026 C2b G4b: shared-admission Phase-A NEGATIVE. A text_plain response
    # format on a capability whose declared response is the user struct
    # smoke::Response. Under G4b the CLI runs prepare_wire_response_schema in
    # Phase A (before any secret/network/registry work), so the shared admission
    # helper rejects the non-String verified root and returns an error directly —
    # the run NEVER reaches make_http_capability, and no factory poison binding is
    # minted here. This CLI case therefore proves: the shared admission helper
    # fails closed in Phase A, zero fake-curl requests occur, and the failure
    # never enters retry / circuit-breaker / RetryExhausted (even with retries
    # requested and the breaker enabled).
    #
    # The factory-minted poison binding's own properties (retry=0, CB-off,
    # attempts==1, zero transport) are proven separately by the capability_bridge
    # unit test (G4a); this CLI case does not re-claim that evidence.
    textplain_phase_a_admission_path = run_dir / "http_textplain_phase_a_admission_bindings.json"
    write_http_bindings(
        textplain_phase_a_admission_path,
        max_retries=3,
        response_format="text_plain",
        circuit_breaker={"enabled": True, "failure_threshold": 1},
    )
    result, curl_capture_path = run_ahflc(
        ahflc, run_dir, source_path, textplain_phase_a_admission_path, "http_success"
    )
    assert_failure(
        result,
        curl_capture_path,
        "response schema admission failed",
        expect_curl_invoked=False,
        expected_request_count=0,
        forbidden_fragment="retry_exhausted",
    )

    # RFC 0026 C2b G4b: the rich --input exact-decode matrix (schema-guided codec
    # path through a real package/sysroot) and the structural error-priority
    # cases (missing-workflow not-found, malformed-input syntax priority).
    run_rich_input_matrix(ahflc, run_dir)
    run_structural_priority_cases(ahflc, run_dir)


if __name__ == "__main__":
    main()
