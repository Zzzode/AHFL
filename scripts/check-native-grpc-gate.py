#!/usr/bin/env python3
"""Fail closed on native gRPC implementation before RFC0004 is implementation-ready."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
RFC0004_REL = Path("docs") / "rfcs" / "0004-native-grpc-transport.zh.md"
DECISION_GATE_REL = Path("docs") / "plans" / "native-grpc-decision-gate.zh.md"
DECISION_EVIDENCE_REL = Path("docs") / "plans" / "native-grpc-decision-evidence.json"
SELF = Path(__file__).resolve()

ACCEPTED_STATUSES = {"accepted"}
IMPLEMENTATION_ALLOWED_STATUSES = {"implementing", "implemented", "stabilized"}
NO_GO_STATUSES = {"postponed", "rejected", "out-of-scope"}
EVIDENCE_SCHEMA = "ahfl.native_grpc_decision_evidence.v1"
OWNER_DECISION_SCHEMA = "ahfl.native_grpc_owner_decision.v1"
BENCHMARK_SCHEMA = "ahfl.native_grpc_benchmark.v1"
BUILD_MATRIX_SCHEMA = "ahfl.native_grpc_build_matrix.v1"
DEPENDENCY_POLICY_SCHEMA = "ahfl.native_grpc_dependency_policy.v1"
FEATURE_FLAG_SCHEMA = "ahfl.native_grpc_feature_flag.v1"
FALLBACK_SEMANTICS_SCHEMA = "ahfl.native_grpc_fallback_semantics.v1"
TEST_STRATEGY_SCHEMA = "ahfl.native_grpc_test_strategy.v1"
EXPECTED_RFC = "0004-native-grpc-transport"
GATE_STATUSES = {"missing", "planned", "complete", "not_applicable"}
DECISION_STATES = {"pending", "go", "no-go"}
STALE_EVIDENCE_RE = re.compile(r"\b(?:TBD|TODO|DEFERRED|PLACEHOLDER)\b", re.IGNORECASE)
ALLOWED_REMOTE_EVIDENCE_SCHEMES = {"http", "https"}
REQUIRED_BENCHMARK_SCENARIOS = {
    "small_unary",
    "large_structured_response",
    "high_concurrency",
}
REQUIRED_BENCHMARK_TRANSPORTS = {
    "grpc_json_transcoding",
    "native_grpc",
}
REQUIRED_BENCHMARK_METRICS = {
    "p50_latency_ms",
    "p95_latency_ms",
    "p99_latency_ms",
    "throughput_qps",
    "cpu_time_ms",
    "peak_rss_bytes",
    "serialized_payload_bytes",
}
REQUIRED_BUILD_PLATFORMS = {"linux", "macos", "windows"}
REQUIRED_BUILD_NUMERIC_FIELDS = {
    "clean_configure_seconds",
    "clean_build_seconds",
    "incremental_build_seconds",
    "binary_size_delta_bytes",
}
REQUIRED_BUILD_TEXT_FIELDS = {
    "dependency_source",
    "ci_cache_strategy",
    "failure_mode",
    "local_setup_impact",
}
REQUIRED_FALLBACK_SCENARIOS = {
    "native_unavailable",
    "schema_mismatch",
    "transport_failure",
    "timeout",
}
REQUIRED_TEST_AREAS = {
    "unit",
    "integration",
    "mock_server",
    "capability_binding",
    "release_evidence",
}
REQUIRED_GATES: tuple[str, ...] = (
    "runtime_owner_decision",
    "benchmark",
    "build_matrix",
    "dependency_policy",
    "feature_flag",
    "fallback_semantics",
    "test_strategy",
)
OWNER_DECISION_GO_CONDITIONS = tuple(gate for gate in REQUIRED_GATES if gate != "runtime_owner_decision")
EVIDENCE_KEYS = {"schema", "rfc", "updated_at", "decision", "gates"}
DECISION_KEYS = {"state", "owner", "signed_off_at", "record"}
GATE_KEYS = {"status", "owner", "evidence", "notes"}
SKIP_DIRS = {
    ".git",
    ".cache",
    ".idea",
    ".vscode",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "node_modules",
    "third_party",
}
SCAN_SUFFIXES = {
    ".cmake",
    ".cpp",
    ".cxx",
    ".cc",
    ".hpp",
    ".hxx",
    ".h",
    ".proto",
    ".py",
    ".sh",
    ".yml",
    ".yaml",
    ".txt",
}
FORBIDDEN_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "native gRPC build flag",
        re.compile(r"\bAHFL_ENABLE_GRPC_NATIVE\b"),
    ),
    (
        "native gRPC C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]grpcpp/", re.MULTILINE),
    ),
    (
        "native Protobuf C++ include",
        re.compile(r"^\s*#\s*include\s*[<\"]google/protobuf/", re.MULTILINE),
    ),
    (
        "CMake gRPC package lookup",
        re.compile(r"\bfind_package\s*\(\s*gRPC\b", re.IGNORECASE),
    ),
    (
        "CMake Protobuf package lookup",
        re.compile(r"\bfind_package\s*\(\s*Protobuf\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC FetchContent wiring",
        re.compile(r"\bFetchContent_(?:Declare|MakeAvailable)\s*\([^)]*\bgrpc\b", re.IGNORECASE),
    ),
    (
        "CMake gRPC target link",
        re.compile(r"\bgRPC::grpc\+\+\b|\bgrpc::grpc\+\+\b"),
    ),
    (
        "CMake Protobuf target link",
        re.compile(r"\bprotobuf::libprotobuf\b|\bprotobuf::protoc\b"),
    ),
    (
        "native gRPC proto service contract",
        re.compile(r"\bservice\s+(?:LLMInference|StreamingInference|ToolCalling)\b"),
    ),
)


def display_path(root: Path, path: Path) -> Path:
    try:
        return path.relative_to(root)
    except ValueError:
        return path


def frontmatter_status(root: Path, path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n"):
        raise RuntimeError(f"{display_path(root, path)}: missing YAML frontmatter")
    end = text.find("\n---", 4)
    if end == -1:
        raise RuntimeError(f"{display_path(root, path)}: unterminated YAML frontmatter")
    for line in text[4:end].splitlines():
        if line.startswith("status:"):
            return line.split(":", 1)[1].strip().strip("\"'")
    raise RuntimeError(f"{display_path(root, path)}: missing status field")


def relative_to_root(root: Path, path: Path) -> Path:
    return path.relative_to(root)


def should_scan(root: Path, path: Path, exempt_paths: set[Path]) -> bool:
    if path in exempt_paths:
        return False
    rel = relative_to_root(root, path)
    if any(part in SKIP_DIRS for part in rel.parts):
        return False
    if path.name in {"package-lock.json", "compile_commands.json"}:
        return False
    return path.suffix in SCAN_SUFFIXES or path.name in {"CMakeLists.txt"}


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def exempt_paths(root: Path) -> set[Path]:
    paths = {
        root / RFC0004_REL,
        root / DECISION_GATE_REL,
        root / DECISION_EVIDENCE_REL,
    }
    if is_relative_to(SELF, root):
        paths.add(SELF)
    return {path.resolve() for path in paths}


def read_decision_evidence(root: Path) -> tuple[dict[str, object] | None, list[str]]:
    path = root / DECISION_EVIDENCE_REL
    if not path.exists():
        return None, [f"{DECISION_EVIDENCE_REL}: missing native gRPC decision evidence file"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return None, [f"{DECISION_EVIDENCE_REL}:{exc.lineno}: invalid JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return None, [f"{DECISION_EVIDENCE_REL}: top-level value must be an object"]
    return data, []


def validate_evidence_shape(evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []
    unknown_evidence_keys = sorted(str(key) for key in evidence if key not in EVIDENCE_KEYS)
    if unknown_evidence_keys:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: contains unknown field(s) {unknown_evidence_keys}; "
            f"allowed fields are {sorted(EVIDENCE_KEYS)}"
        )

    if evidence.get("schema") != EVIDENCE_SCHEMA:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: schema must be {EVIDENCE_SCHEMA!r}"
        )
    if evidence.get("rfc") != EXPECTED_RFC:
        failures.append(f"{DECISION_EVIDENCE_REL}: rfc must be {EXPECTED_RFC!r}")

    decision = evidence.get("decision")
    if not isinstance(decision, dict):
        failures.append(f"{DECISION_EVIDENCE_REL}: decision must be an object")
    else:
        unknown_decision_keys = sorted(str(key) for key in decision if key not in DECISION_KEYS)
        if unknown_decision_keys:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: decision contains unknown field(s) "
                f"{unknown_decision_keys}; allowed fields are {sorted(DECISION_KEYS)}"
            )
        state = decision.get("state")
        if state not in DECISION_STATES:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: decision.state must be one of "
                f"{sorted(DECISION_STATES)}"
            )

    gates = evidence.get("gates")
    if not isinstance(gates, dict):
        failures.append(f"{DECISION_EVIDENCE_REL}: gates must be an object")
        return failures

    unknown_gates = sorted(str(gate) for gate in gates if gate not in REQUIRED_GATES)
    if unknown_gates:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: gates contains unknown gate(s) {unknown_gates}; "
            f"allowed gates are {list(REQUIRED_GATES)}"
        )

    for gate in REQUIRED_GATES:
        gate_value = gates.get(gate)
        if not isinstance(gate_value, dict):
            failures.append(f"{DECISION_EVIDENCE_REL}: gates.{gate} must be an object")
            continue
        unknown_gate_keys = sorted(str(key) for key in gate_value if key not in GATE_KEYS)
        if unknown_gate_keys:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate} contains unknown field(s) "
                f"{unknown_gate_keys}; allowed fields are {sorted(GATE_KEYS)}"
            )
        status = gate_value.get("status")
        if status not in GATE_STATUSES:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate}.status must be one of "
                f"{sorted(GATE_STATUSES)}"
            )
        evidence_refs = gate_value.get("evidence")
        if evidence_refs is not None and (
            not isinstance(evidence_refs, list)
            or not all(isinstance(item, str) and item for item in evidence_refs)
        ):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate}.evidence must be a list of "
                "non-empty strings when present"
            )
    return failures


def validate_artifact_reference(root: Path, ref: str, context: str) -> list[str]:
    failures: list[str] = []
    if STALE_EVIDENCE_RE.search(ref):
        failures.append(f"{DECISION_EVIDENCE_REL}: {context} evidence reference {ref!r} is stale")
        return failures

    parsed = urlparse(ref)
    if parsed.scheme:
        if parsed.scheme in ALLOWED_REMOTE_EVIDENCE_SCHEMES and parsed.netloc:
            return failures
        failures.append(
            f"{DECISION_EVIDENCE_REL}: {context} evidence reference {ref!r} must be an "
            "http(s) URL or an existing repository-relative artifact path"
        )
        return failures

    candidate = Path(ref)
    if candidate.is_absolute() or ".." in candidate.parts:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: {context} evidence reference {ref!r} must stay "
            "inside the repository"
        )
        return failures

    resolved = (root / candidate).resolve()
    try:
        resolved.relative_to(root)
    except ValueError:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: {context} evidence reference {ref!r} escapes "
            "the repository"
        )
        return failures

    if not resolved.exists() or not resolved.is_file():
        failures.append(
            f"{DECISION_EVIDENCE_REL}: {context} evidence artifact {ref!r} does not exist"
        )
    return failures


def local_artifact_path(root: Path, ref: str) -> Path | None:
    parsed = urlparse(ref)
    if parsed.scheme:
        return None
    candidate = Path(ref)
    if candidate.is_absolute() or ".." in candidate.parts:
        return None
    resolved = (root / candidate).resolve()
    try:
        resolved.relative_to(root)
    except ValueError:
        return None
    if not resolved.exists() or not resolved.is_file():
        return None
    return resolved


def validate_numeric_metric(value: object, path: str) -> list[str]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return [f"{path} must be a number"]
    if value < 0:
        return [f"{path} must be non-negative"]
    return []


def validate_non_empty_string(value: object, path: str) -> list[str]:
    if not isinstance(value, str) or not value:
        return [f"{path} must be a non-empty string"]
    return []


def validate_string_list(value: object, path: str) -> list[str]:
    if not isinstance(value, list) or not value:
        return [f"{path} must be a non-empty array"]
    failures: list[str] = []
    for index, item in enumerate(value):
        failures.extend(validate_non_empty_string(item, f"{path}[{index}]"))
    return failures


def validate_artifact_header(data: dict[str, object], rel: Path | str, schema: str) -> list[str]:
    failures: list[str] = []
    if data.get("schema") != schema:
        failures.append(f"{rel}: artifact schema must be {schema!r}")
    if data.get("rfc") != EXPECTED_RFC:
        failures.append(f"{rel}: artifact rfc must be {EXPECTED_RFC!r}")
    return failures


def validate_owner_decision_artifact(
    root: Path,
    path: Path,
    expected_decision: dict[str, object],
) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid owner decision JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: owner decision artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, OWNER_DECISION_SCHEMA)
    expected_decision_state = expected_decision.get("state")
    decision = data.get("decision")
    if decision not in {"go", "no-go"}:
        failures.append(f"{rel}: decision must be 'go' or 'no-go'")
    elif expected_decision_state not in {"go", "no-go"}:
        failures.append(
            f"{DECISION_EVIDENCE_REL}: decision.state must be 'go' or 'no-go' "
            "when runtime_owner_decision is complete"
        )
    elif decision != expected_decision_state:
        failures.append(
            f"{rel}: decision {decision!r} must match "
            f"{DECISION_EVIDENCE_REL} decision.state {expected_decision_state!r}"
        )

    for field in ("owner", "signed_off_at", "decision_record", "scope", "rationale"):
        failures.extend(validate_non_empty_string(data.get(field), f"{rel}: {field}"))

    mirrored_fields = (
        ("owner", "owner"),
        ("signed_off_at", "signed_off_at"),
        ("decision_record", "record"),
    )
    for artifact_field, decision_field in mirrored_fields:
        artifact_value = data.get(artifact_field)
        expected_value = expected_decision.get(decision_field)
        if (
            isinstance(artifact_value, str)
            and artifact_value
            and isinstance(expected_value, str)
            and expected_value
            and artifact_value != expected_value
        ):
            failures.append(
                f"{rel}: {artifact_field} {artifact_value!r} must match "
                f"{DECISION_EVIDENCE_REL} decision.{decision_field} {expected_value!r}"
            )

    if decision == "go":
        conditions = data.get("required_before_implementation")
        failures.extend(
            validate_string_list(conditions, f"{rel}: required_before_implementation")
        )
        if isinstance(conditions, list):
            missing_conditions = sorted(
                set(OWNER_DECISION_GO_CONDITIONS)
                - {item for item in conditions if isinstance(item, str)}
            )
            if missing_conditions:
                failures.append(
                    f"{rel}: required_before_implementation missing gates {missing_conditions}"
                )
    elif decision == "no-go":
        failures.extend(
            validate_non_empty_string(
                data.get("continued_transport_scope"),
                f"{rel}: continued_transport_scope",
            )
        )
    return failures


def validate_benchmark_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid benchmark JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: benchmark artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, BENCHMARK_SCHEMA)

    environment = data.get("environment")
    if not isinstance(environment, dict):
        failures.append(f"{rel}: benchmark artifact environment must be an object")
    else:
        for field in ("platform", "runner", "cpu_model", "timestamp"):
            value = environment.get(field)
            if not isinstance(value, str) or not value:
                failures.append(f"{rel}: benchmark environment.{field} must be a non-empty string")

    runs = data.get("runs")
    if not isinstance(runs, list) or not runs:
        failures.append(f"{rel}: benchmark artifact runs must be a non-empty array")
        return failures

    seen_pairs: set[tuple[str, str]] = set()
    for index, run in enumerate(runs):
        run_path = f"{rel}: runs[{index}]"
        if not isinstance(run, dict):
            failures.append(f"{run_path} must be an object")
            continue
        scenario = run.get("scenario")
        transport = run.get("transport")
        if scenario not in REQUIRED_BENCHMARK_SCENARIOS:
            failures.append(
                f"{run_path}.scenario must be one of {sorted(REQUIRED_BENCHMARK_SCENARIOS)}"
            )
        if transport not in REQUIRED_BENCHMARK_TRANSPORTS:
            failures.append(
                f"{run_path}.transport must be one of {sorted(REQUIRED_BENCHMARK_TRANSPORTS)}"
            )
        if isinstance(scenario, str) and isinstance(transport, str):
            seen_pairs.add((scenario, transport))

        metrics = run.get("metrics")
        if not isinstance(metrics, dict):
            failures.append(f"{run_path}.metrics must be an object")
            continue
        for metric in REQUIRED_BENCHMARK_METRICS:
            if metric not in metrics:
                failures.append(f"{run_path}.metrics.{metric} is required")
                continue
            failures.extend(validate_numeric_metric(metrics[metric], f"{run_path}.metrics.{metric}"))

    expected_pairs = {
        (scenario, transport)
        for scenario in REQUIRED_BENCHMARK_SCENARIOS
        for transport in REQUIRED_BENCHMARK_TRANSPORTS
    }
    missing_pairs = sorted(expected_pairs - seen_pairs)
    if missing_pairs:
        failures.append(f"{rel}: benchmark artifact missing scenario/transport pairs {missing_pairs}")
    return failures


def validate_build_matrix_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid build matrix JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: build matrix artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, BUILD_MATRIX_SCHEMA)
    platforms = data.get("platforms")
    if not isinstance(platforms, list) or not platforms:
        failures.append(f"{rel}: build matrix platforms must be a non-empty array")
        return failures

    seen_platforms: set[str] = set()
    for index, platform in enumerate(platforms):
        platform_path = f"{rel}: platforms[{index}]"
        if not isinstance(platform, dict):
            failures.append(f"{platform_path} must be an object")
            continue
        platform_name = platform.get("platform")
        if platform_name not in REQUIRED_BUILD_PLATFORMS:
            failures.append(f"{platform_path}.platform must be one of {sorted(REQUIRED_BUILD_PLATFORMS)}")
        elif isinstance(platform_name, str):
            seen_platforms.add(platform_name)
        for field in REQUIRED_BUILD_TEXT_FIELDS:
            failures.extend(validate_non_empty_string(platform.get(field), f"{platform_path}.{field}"))
        for field in REQUIRED_BUILD_NUMERIC_FIELDS:
            if field not in platform:
                failures.append(f"{platform_path}.{field} is required")
                continue
            failures.extend(validate_numeric_metric(platform[field], f"{platform_path}.{field}"))

    missing_platforms = sorted(REQUIRED_BUILD_PLATFORMS - seen_platforms)
    if missing_platforms:
        failures.append(f"{rel}: build matrix missing platforms {missing_platforms}")
    return failures


def validate_dependency_policy_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid dependency policy JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: dependency policy artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, DEPENDENCY_POLICY_SCHEMA)
    for field in (
        "dependency_source",
        "license_review",
        "vendoring_policy",
        "cache_policy",
        "local_setup_impact",
        "approved_by",
    ):
        failures.extend(validate_non_empty_string(data.get(field), f"{rel}: {field}"))
    return failures


def validate_feature_flag_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid feature flag JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: feature flag artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, FEATURE_FLAG_SCHEMA)
    if data.get("build_flag") != "AHFL_ENABLE_GRPC_NATIVE":
        failures.append(f"{rel}: build_flag must be 'AHFL_ENABLE_GRPC_NATIVE'")
    if data.get("default_enabled") is not False:
        failures.append(f"{rel}: default_enabled must be false until native transport release evidence is complete")
    failures.extend(validate_non_empty_string(data.get("runtime_config"), f"{rel}: runtime_config"))
    failures.extend(validate_string_list(data.get("disabled_diagnostics"), f"{rel}: disabled_diagnostics"))
    failures.extend(validate_non_empty_string(data.get("release_evidence_gate"), f"{rel}: release_evidence_gate"))
    return failures


def validate_fallback_semantics_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid fallback semantics JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: fallback semantics artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, FALLBACK_SEMANTICS_SCHEMA)
    scenarios = data.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        failures.append(f"{rel}: fallback scenarios must be a non-empty array")
        return failures

    seen_scenarios: set[str] = set()
    for index, scenario in enumerate(scenarios):
        scenario_path = f"{rel}: scenarios[{index}]"
        if not isinstance(scenario, dict):
            failures.append(f"{scenario_path} must be an object")
            continue
        name = scenario.get("scenario")
        if name not in REQUIRED_FALLBACK_SCENARIOS:
            failures.append(f"{scenario_path}.scenario must be one of {sorted(REQUIRED_FALLBACK_SCENARIOS)}")
        elif isinstance(name, str):
            seen_scenarios.add(name)
        for field in ("behavior", "diagnostic", "fallback_transport"):
            failures.extend(validate_non_empty_string(scenario.get(field), f"{scenario_path}.{field}"))
        if not isinstance(scenario.get("fail_closed"), bool):
            failures.append(f"{scenario_path}.fail_closed must be a boolean")

    missing_scenarios = sorted(REQUIRED_FALLBACK_SCENARIOS - seen_scenarios)
    if missing_scenarios:
        failures.append(f"{rel}: fallback semantics missing scenarios {missing_scenarios}")
    return failures


def validate_test_strategy_artifact(root: Path, path: Path) -> list[str]:
    rel = display_path(root, path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{rel}:{exc.lineno}: invalid test strategy JSON: {exc.msg}"]
    if not isinstance(data, dict):
        return [f"{rel}: test strategy artifact must be a JSON object"]

    failures = validate_artifact_header(data, rel, TEST_STRATEGY_SCHEMA)
    coverage = data.get("coverage")
    if not isinstance(coverage, list) or not coverage:
        failures.append(f"{rel}: test strategy coverage must be a non-empty array")
        return failures

    seen_areas: set[str] = set()
    for index, entry in enumerate(coverage):
        entry_path = f"{rel}: coverage[{index}]"
        if not isinstance(entry, dict):
            failures.append(f"{entry_path} must be an object")
            continue
        area = entry.get("area")
        if area not in REQUIRED_TEST_AREAS:
            failures.append(f"{entry_path}.area must be one of {sorted(REQUIRED_TEST_AREAS)}")
        elif isinstance(area, str):
            seen_areas.add(area)
        failures.extend(validate_string_list(entry.get("tests"), f"{entry_path}.tests"))

    missing_areas = sorted(REQUIRED_TEST_AREAS - seen_areas)
    if missing_areas:
        failures.append(f"{rel}: test strategy missing coverage areas {missing_areas}")
    return failures


STRUCTURED_GATE_VALIDATORS = {
    "benchmark": (BENCHMARK_SCHEMA, validate_benchmark_artifact),
    "build_matrix": (BUILD_MATRIX_SCHEMA, validate_build_matrix_artifact),
    "dependency_policy": (DEPENDENCY_POLICY_SCHEMA, validate_dependency_policy_artifact),
    "feature_flag": (FEATURE_FLAG_SCHEMA, validate_feature_flag_artifact),
    "fallback_semantics": (FALLBACK_SEMANTICS_SCHEMA, validate_fallback_semantics_artifact),
    "test_strategy": (TEST_STRATEGY_SCHEMA, validate_test_strategy_artifact),
}


def validate_evidence_artifacts(root: Path, evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []

    decision = decision_object(evidence)
    decision_state = decision.get("state")
    if decision_state in {"go", "no-go"}:
        for field in ("owner", "signed_off_at", "record"):
            value = decision.get(field)
            if not isinstance(value, str) or not value:
                failures.append(
                    f"{DECISION_EVIDENCE_REL}: decision.{field} must be a non-empty string "
                    f"when decision.state is {decision_state!r}"
                )
        record = decision.get("record")
        if isinstance(record, str) and record:
            failures.extend(validate_artifact_reference(root, record, "decision.record"))

    complete_local_artifacts: dict[str, list[Path]] = {}
    for gate, gate_value in gates_object(evidence).items():
        if not isinstance(gate_value, dict):
            continue
        evidence_refs = gate_value.get("evidence")
        if not isinstance(evidence_refs, list):
            continue
        if gate_value.get("status") != "complete" and not evidence_refs:
            continue
        for index, ref in enumerate(evidence_refs):
            if isinstance(ref, str):
                failures.extend(validate_artifact_reference(root, ref, f"gates.{gate}.evidence[{index}]"))
                local_path = local_artifact_path(root, ref)
                if local_path is not None and gate_value.get("status") == "complete":
                    complete_local_artifacts.setdefault(gate, []).append(local_path)
    for gate, (schema, validator) in STRUCTURED_GATE_VALIDATORS.items():
        if not gate_is_complete(evidence, gate):
            continue
        artifacts = [path for path in complete_local_artifacts.get(gate, []) if path.suffix == ".json"]
        if not artifacts:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.{gate} requires at least one "
                f"repository-local JSON artifact with schema {schema!r}"
            )
        for artifact in artifacts:
            failures.extend(validator(root, artifact))

    if gate_is_complete(evidence, "runtime_owner_decision"):
        artifacts = [
            path
            for path in complete_local_artifacts.get("runtime_owner_decision", [])
            if path.suffix == ".json"
        ]
        if not artifacts:
            failures.append(
                f"{DECISION_EVIDENCE_REL}: gates.runtime_owner_decision requires at least one "
                f"repository-local JSON artifact with schema {OWNER_DECISION_SCHEMA!r}"
            )
        for artifact in artifacts:
            failures.extend(validate_owner_decision_artifact(root, artifact, decision))
    return failures


def decision_object(evidence: dict[str, object]) -> dict[str, object]:
    decision = evidence.get("decision")
    return decision if isinstance(decision, dict) else {}


def gates_object(evidence: dict[str, object]) -> dict[str, object]:
    gates = evidence.get("gates")
    return gates if isinstance(gates, dict) else {}


def gate_is_complete(evidence: dict[str, object], gate: str) -> bool:
    gate_value = gates_object(evidence).get(gate)
    if not isinstance(gate_value, dict):
        return False
    evidence_refs = gate_value.get("evidence")
    return gate_value.get("status") == "complete" and isinstance(evidence_refs, list) and bool(evidence_refs)


def validate_status_transition(status: str, evidence: dict[str, object]) -> list[str]:
    failures: list[str] = []
    decision = decision_object(evidence)
    decision_state = decision.get("state")

    if status in ACCEPTED_STATUSES:
        if decision_state != "go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'go'"
            )
        if not gate_is_complete(evidence, "runtime_owner_decision"):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires a "
                "complete runtime_owner_decision gate with evidence"
            )

    if status in IMPLEMENTATION_ALLOWED_STATUSES:
        if decision_state != "go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'go'"
            )
        for gate in REQUIRED_GATES:
            if not gate_is_complete(evidence, gate):
                failures.append(
                    f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                    f"complete gate {gate!r} with evidence"
                )

    if status in NO_GO_STATUSES:
        if decision_state != "no-go":
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires "
                "decision.state == 'no-go'"
            )
        if not gate_is_complete(evidence, "runtime_owner_decision"):
            failures.append(
                f"{DECISION_EVIDENCE_REL}: RFC0004 status {status!r} requires a "
                "complete runtime_owner_decision gate with evidence"
            )

    return failures


def scan_native_markers(root: Path, status: str) -> list[str]:
    failures: list[str] = []
    excluded_paths = exempt_paths(root)
    for path in sorted(root.rglob("*")):
        path = path.resolve()
        if not path.is_file() or not should_scan(root, path, excluded_paths):
            continue
        if re.search(r"\bnative[_-]grpc\b", path.name, re.IGNORECASE):
            failures.append(
                f"{relative_to_root(root, path)}: native gRPC source file is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for label, pattern in FORBIDDEN_PATTERNS:
            match = pattern.search(text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{relative_to_root(root, path)}:{line}: {label} is forbidden while RFC0004 "
                f"status is {status!r}. Complete the native gRPC decision gate first."
            )
    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=ROOT,
        help="repository root to check; defaults to this script's repository",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.root.resolve()
    status = frontmatter_status(root, root / RFC0004_REL)

    evidence, evidence_failures = read_decision_evidence(root)
    failures = list(evidence_failures)
    if evidence is not None:
        failures.extend(validate_evidence_shape(evidence))
        failures.extend(validate_evidence_artifacts(root, evidence))
        failures.extend(validate_status_transition(status, evidence))

    marker_failures = scan_native_markers(root, status)
    if status not in IMPLEMENTATION_ALLOWED_STATUSES:
        failures.extend(marker_failures)
    elif marker_failures and failures:
        failures.extend(marker_failures)

    if failures:
        print("Native gRPC gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    if status in IMPLEMENTATION_ALLOWED_STATUSES:
        print(
            f"Native gRPC gate passed: RFC0004 status is {status}; "
            "decision evidence is complete enough for implementation markers."
        )
    else:
        print(f"Native gRPC gate passed: RFC0004 status is {status}; no native markers found.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
