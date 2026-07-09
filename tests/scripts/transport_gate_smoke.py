#!/usr/bin/env python3
"""Smoke tests for the RFC0004 transport decision gate checker."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REQUIRED_GATES = (
    "runtime_owner_decision",
    "benchmark",
    "build_matrix",
    "dependency_policy",
    "feature_flag",
    "fallback_semantics",
    "test_strategy",
)

REQUIRED_BENCHMARK_SCENARIOS = (
    "small_unary",
    "large_structured_response",
    "high_concurrency",
)

REQUIRED_BENCHMARK_TRANSPORTS = (
    "http_json",
    "http2_json_optimized",
    "grpc_json_transcoding",
    "native_grpc",
)


def owner_decision_artifact(
    *,
    decision: str = "go",
    owner: str = "runtime-owner",
    signed_off_at: str = "2026-07-08T00:00:00Z",
    decision_record: str = "docs/plans/evidence/decision.json",
) -> dict[str, object]:
    artifact: dict[str, object] = {
        "schema": "ahfl.native_grpc_owner_decision.v1",
        "rfc": "0004-native-grpc-transport",
        "decision": decision,
        "owner": owner,
        "signed_off_at": signed_off_at,
        "decision_record": decision_record,
        "scope": "native transport decision gate smoke fixture",
        "rationale": "machine-checkable owner decision fixture",
    }
    if decision == "go":
        artifact["required_before_implementation"] = [
            "benchmark",
            "build_matrix",
            "dependency_policy",
            "feature_flag",
            "fallback_semantics",
            "test_strategy",
        ]
    else:
        artifact["continued_transport_scope"] = "maintain grpc_json_transcoding only"
    return artifact


def build_matrix_artifact() -> dict[str, object]:
    platforms = []
    for platform in ("linux", "macos", "windows"):
        platforms.append(
            {
                "platform": platform,
                "dependency_source": "system package manager",
                "clean_configure_seconds": 1.0,
                "clean_build_seconds": 2.0,
                "incremental_build_seconds": 0.5,
                "binary_size_delta_bytes": 4096,
                "ci_cache_strategy": "cache by dependency digest",
                "failure_mode": "fail closed on missing dependency",
                "local_setup_impact": "documented setup step",
            }
        )
    return {
        "schema": "ahfl.native_grpc_build_matrix.v1",
        "rfc": "0004-native-grpc-transport",
        "platforms": platforms,
    }


def benchmark_artifact() -> dict[str, object]:
    runs: list[dict[str, object]] = []
    for scenario in REQUIRED_BENCHMARK_SCENARIOS:
        for transport in REQUIRED_BENCHMARK_TRANSPORTS:
            runs.append(
                {
                    "scenario": scenario,
                    "transport": transport,
                    "metrics": {
                        "p50_latency_ms": 1.0,
                        "p95_latency_ms": 2.0,
                        "p99_latency_ms": 3.0,
                        "throughput_qps": 1000.0,
                        "cpu_time_ms": 10.0,
                        "peak_rss_bytes": 1048576,
                        "serialized_payload_bytes": 128,
                    },
                }
            )
    return {
        "schema": "ahfl.native_grpc_benchmark.v1",
        "rfc": "0004-native-grpc-transport",
        "environment": {
            "platform": "test-platform",
            "runner": "transport_gate_smoke",
            "cpu_model": "test-cpu",
            "timestamp": "2026-07-08T00:00:00Z",
        },
        "runs": runs,
    }


def dependency_policy_artifact() -> dict[str, object]:
    return {
        "schema": "ahfl.native_grpc_dependency_policy.v1",
        "rfc": "0004-native-grpc-transport",
        "dependency_source": "system package manager or vendored cache",
        "license_review": "Apache-2.0 compatible dependency review",
        "vendoring_policy": "no vendoring without process owner approval",
        "cache_policy": "content-addressed CI cache",
        "local_setup_impact": "documented local installation steps",
        "approved_by": "process-owner",
    }


def feature_flag_artifact() -> dict[str, object]:
    return {
        "schema": "ahfl.native_grpc_feature_flag.v1",
        "rfc": "0004-native-grpc-transport",
        "build_flag": "AHFL_ENABLE_" + "GRPC_NATIVE",
        "runtime_config": "runtime.transport." + "native_grpc",
        "default_enabled": False,
        "disabled_diagnostics": ["runtime.grpc_native.disabled"],
        "release_evidence_gate": "ahfl.runtime.native_grpc_release_evidence",
    }


def fallback_semantics_artifact() -> dict[str, object]:
    scenarios = []
    for scenario in ("native_unavailable", "schema_mismatch", "transport_failure", "timeout"):
        scenarios.append(
            {
                "scenario": scenario,
                "behavior": "fail closed unless explicit fallback is configured",
                "diagnostic": f"runtime.grpc_native.{scenario}",
                "fallback_transport": "grpc_json_transcoding",
                "fail_closed": True,
            }
        )
    return {
        "schema": "ahfl.native_grpc_fallback_semantics.v1",
        "rfc": "0004-native-grpc-transport",
        "scenarios": scenarios,
    }


def test_strategy_artifact() -> dict[str, object]:
    return {
        "schema": "ahfl.native_grpc_test_strategy.v1",
        "rfc": "0004-native-grpc-transport",
        "coverage": [
            {"area": "unit", "tests": ["ahfl.runtime.grpc_native.unit"]},
            {"area": "integration", "tests": ["ahfl.runtime.grpc_native.integration"]},
            {"area": "mock_server", "tests": ["ahfl.runtime.grpc_native.mock_server"]},
            {"area": "capability_binding", "tests": ["ahfl.runtime.grpc_native.capability_binding"]},
            {"area": "release_evidence", "tests": ["ahfl.runtime.grpc_native.release_evidence"]},
        ],
    }


def collect_artifact_refs(evidence: dict[str, object]) -> set[str]:
    refs: set[str] = set()
    decision = evidence.get("decision")
    if isinstance(decision, dict):
        record = decision.get("record")
        if isinstance(record, str) and record and "://" not in record:
            refs.add(record)
    gates = evidence.get("gates")
    if isinstance(gates, dict):
        for gate_value in gates.values():
            if not isinstance(gate_value, dict):
                continue
            evidence_refs = gate_value.get("evidence")
            if not isinstance(evidence_refs, list):
                continue
            for ref in evidence_refs:
                if isinstance(ref, str) and ref and "://" not in ref:
                    refs.add(ref)
    return refs


def artifact_payload(ref: str) -> dict[str, object]:
    if ref.endswith("/runtime_owner_decision.json"):
        return owner_decision_artifact()
    if ref.endswith("/benchmark.json"):
        return benchmark_artifact()
    if ref.endswith("/build_matrix.json"):
        return build_matrix_artifact()
    if ref.endswith("/dependency_policy.json"):
        return dependency_policy_artifact()
    if ref.endswith("/feature_flag.json"):
        return feature_flag_artifact()
    if ref.endswith("/fallback_semantics.json"):
        return fallback_semantics_artifact()
    if ref.endswith("/test_strategy.json"):
        return test_strategy_artifact()
    return {"artifact": ref}


def write_repo(
    root: Path,
    *,
    status: str,
    evidence: dict[str, object],
    updated: str = "2026-07-08",
) -> None:
    (root / "docs" / "rfcs").mkdir(parents=True)
    (root / "docs" / "plans").mkdir(parents=True)
    (root / "docs" / "rfcs" / "0004-native-grpc-transport.zh.md").write_text(
        f"---\nstatus: {status}\nupdated: {updated}\n---\n\n# RFC0004\n", encoding="utf-8"
    )
    (root / "docs" / "plans" / "native-grpc-decision-evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    for ref in collect_artifact_refs(evidence):
        artifact = root / ref
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text(json.dumps(artifact_payload(ref), indent=2) + "\n", encoding="utf-8")


def evidence(*, decision_state: str = "pending", complete_gates: set[str] | None = None) -> dict[str, object]:
    complete_gates = complete_gates or set()
    gates: dict[str, object] = {}
    for gate in REQUIRED_GATES:
        is_complete = gate in complete_gates
        gates[gate] = {
            "status": "complete" if is_complete else "missing",
            "owner": "owner",
            "evidence": [f"docs/plans/evidence/{gate}.json"] if is_complete else [],
            "notes": "test fixture",
        }
    return {
        "schema": "ahfl.native_grpc_decision_evidence.v1",
        "rfc": "0004-native-grpc-transport",
        "updated_at": "2026-07-08T00:00:00Z",
        "decision": {
            "state": decision_state,
            "owner": "runtime-owner" if decision_state != "pending" else "",
            "signed_off_at": "2026-07-08T00:00:00Z" if decision_state != "pending" else "",
            "record": "docs/plans/evidence/decision.json" if decision_state != "pending" else "",
        },
        "gates": gates,
    }


def run_checker(checker: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), "--root", str(root)],
        check=False,
        text=True,
        capture_output=True,
    )


def assert_fails(result: subprocess.CompletedProcess[str], needle: str) -> None:
    if result.returncode == 0:
        raise AssertionError(f"expected failure, got success: stdout={result.stdout!r}")
    combined = result.stdout + result.stderr
    if needle not in combined:
        raise AssertionError(f"expected {needle!r} in output: {combined!r}")


def assert_passes(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"expected success, got {result.returncode}: stdout={result.stdout!r} stderr={result.stderr!r}"
        )


def test_draft_rejects_implementation_markers(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=evidence())
        (root / "CMakeLists.txt").write_text(
            "option(AHFL_ENABLE_" + "GRPC_NATIVE \"test\" OFF)\n", encoding="utf-8"
        )
        assert_fails(run_checker(checker, root), "native gRPC build flag")


def test_draft_rejects_json_build_flag_marker(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=evidence())
        (root / "CMakePresets.json").write_text(
            json.dumps(
                {
                    "version": 8,
                    "configurePresets": [
                        {
                            "name": "native",
                            "cacheVariables": {
                                "AHFL_ENABLE_" + "GRPC_NATIVE": "ON",
                            },
                        }
                    ],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "native gRPC build flag")


def test_draft_rejects_toml_runtime_config_marker(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=evidence())
        (root / "ahfl.toml").write_text(
            "[runtime.transport]\n" + "native_" + "grpc = true\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "native gRPC runtime config")


def test_rejects_unknown_rfc_status(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="almost_accepted", evidence=evidence())
        assert_fails(run_checker(checker, root), "status must be one of")


def test_accepted_requires_owner_decision(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=evidence())
        assert_fails(run_checker(checker, root), "runtime_owner_decision")


def test_rejects_unknown_gate_name(checker: Path) -> None:
    fixture = evidence()
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    gates["benchmarks"] = {
        "status": "complete",
        "owner": "owner",
        "evidence": ["docs/plans/evidence/benchmark.json"],
        "notes": "typo fixture",
    }
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "unknown gate")


def test_rejects_unknown_top_level_field(checker: Path) -> None:
    fixture = evidence()
    fixture["extra"] = "unexpected"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "unknown field")


def test_rejects_unknown_decision_field(checker: Path) -> None:
    fixture = evidence()
    decision = fixture["decision"]
    assert isinstance(decision, dict)
    decision["approver"] = "unexpected"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "decision contains unknown field")


def test_rejects_unknown_gate_field(checker: Path) -> None:
    fixture = evidence()
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    benchmark_gate = gates["benchmark"]
    assert isinstance(benchmark_gate, dict)
    benchmark_gate["artifact"] = "unexpected"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "gates.benchmark contains unknown field")


def test_rejects_non_utc_updated_at(checker: Path) -> None:
    fixture = evidence()
    fixture["updated_at"] = "2026-07-08"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "updated_at must be a UTC timestamp")


def test_rejects_calendar_invalid_updated_at(checker: Path) -> None:
    fixture = evidence()
    fixture["updated_at"] = "2026-02-30T00:00:00Z"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "updated_at must be a valid UTC timestamp")


def test_rejects_stale_decision_evidence_for_updated_rfc(checker: Path) -> None:
    fixture = evidence()
    fixture["updated_at"] = "2026-07-08T00:00:00Z"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture, updated="2026-07-09")
        assert_fails(run_checker(checker, root), "updated_at date must be on or after")


def test_rejects_non_standard_json_constant_in_decision_evidence(checker: Path) -> None:
    fixture = evidence()
    fixture["updated_at"] = float("nan")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        result = run_checker(checker, root)
        assert_fails(result, "invalid decision evidence JSON")
        assert_fails(result, "non-standard JSON constant 'NaN' is not allowed")


def test_rejects_pending_decision_with_signoff_fields(checker: Path) -> None:
    fixture = evidence()
    decision = fixture["decision"]
    assert isinstance(decision, dict)
    decision["owner"] = "runtime-owner"
    decision["signed_off_at"] = "2026-07-08T00:00:00Z"
    decision["record"] = "docs/plans/evidence/decision.json"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "decision.owner must be an empty string")


def test_rejects_go_decision_with_invalid_signed_off_at(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    decision = fixture["decision"]
    assert isinstance(decision, dict)
    decision["signed_off_at"] = "2026-07-08"
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "decision.signed_off_at must be a UTC timestamp")


def test_rejects_go_decision_without_owner_gate(checker: Path) -> None:
    fixture = evidence(decision_state="go")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "requires a complete runtime_owner_decision gate")


def test_rejects_pending_decision_with_complete_owner_gate(checker: Path) -> None:
    fixture = evidence(complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "cannot have a complete runtime_owner_decision gate")


def test_rejects_duplicate_gate_evidence(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    runtime_gate = gates["runtime_owner_decision"]
    assert isinstance(runtime_gate, dict)
    runtime_gate["evidence"] = [
        "docs/plans/evidence/runtime_owner_decision.json",
        "docs/plans/evidence/runtime_owner_decision.json",
    ]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "must not contain duplicate entries")


def test_missing_gate_rejects_evidence(checker: Path) -> None:
    fixture = evidence()
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    benchmark_gate = gates["benchmark"]
    assert isinstance(benchmark_gate, dict)
    benchmark_gate["evidence"] = ["docs/plans/evidence/benchmark.json"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "must be empty when status is 'missing'")


def test_complete_gate_rejects_empty_evidence(checker: Path) -> None:
    fixture = evidence()
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    benchmark_gate = gates["benchmark"]
    assert isinstance(benchmark_gate, dict)
    benchmark_gate["status"] = "complete"
    benchmark_gate["evidence"] = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="draft", evidence=fixture)
        assert_fails(run_checker(checker, root), "must be non-empty when status is 'complete'")


def test_incomplete_gate_statuses_reject_evidence(checker: Path) -> None:
    for status in ("planned", "not_applicable"):
        fixture = evidence()
        gates = fixture["gates"]
        assert isinstance(gates, dict)
        benchmark_gate = gates["benchmark"]
        assert isinstance(benchmark_gate, dict)
        benchmark_gate["status"] = status
        benchmark_gate["evidence"] = ["docs/plans/evidence/benchmark.json"]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_repo(root, status="draft", evidence=fixture)
            assert_fails(run_checker(checker, root), f"must be empty when status is {status!r}")


def test_accepted_allows_structured_owner_decision(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(
            root,
            status="accepted",
            evidence=evidence(decision_state="go", complete_gates={"runtime_owner_decision"}),
        )
        assert_passes(run_checker(checker, root))


def test_draft_rejects_signed_owner_decision(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(
            root,
            status="draft",
            evidence=evidence(decision_state="go", complete_gates={"runtime_owner_decision"}),
        )
        assert_fails(run_checker(checker, root), "status 'draft' requires decision.state == 'pending'")


def test_implementing_requires_complete_gate_evidence(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(
            root,
            status="implementing",
            evidence=evidence(decision_state="go", complete_gates={"runtime_owner_decision"}),
        )
        assert_fails(run_checker(checker, root), "benchmark")


def test_implementing_allows_markers_after_complete_evidence(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(
            root,
            status="implementing",
            evidence=evidence(decision_state="go", complete_gates=set(REQUIRED_GATES)),
        )
        (root / "CMakeLists.txt").write_text(
            "option(AHFL_ENABLE_" + "GRPC_NATIVE \"test\" OFF)\n", encoding="utf-8"
        )
        assert_passes(run_checker(checker, root))


def test_complete_gate_rejects_placeholder_evidence(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    runtime_gate = gates["runtime_owner_decision"]
    assert isinstance(runtime_gate, dict)
    runtime_gate["evidence"] = ["evidence://runtime_owner_decision"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "absolute https URL without credentials or fragments")


def test_rejects_http_remote_evidence_reference(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    runtime_gate = gates["runtime_owner_decision"]
    assert isinstance(runtime_gate, dict)
    runtime_gate["evidence"] = ["http://example.invalid/native-grpc-owner-decision.json"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "absolute https URL without credentials or fragments")


def test_rejects_incomplete_https_remote_evidence_reference(checker: Path) -> None:
    bad_refs = (
        "https://example.invalid",
        "https://user@example.invalid/native-grpc-owner-decision.json",
        "https://example.invalid/native-grpc-owner-decision.json#section",
    )
    for ref in bad_refs:
        fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
        gates = fixture["gates"]
        assert isinstance(gates, dict)
        runtime_gate = gates["runtime_owner_decision"]
        assert isinstance(runtime_gate, dict)
        runtime_gate["evidence"] = [ref]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_repo(root, status="accepted", evidence=fixture)
            assert_fails(run_checker(checker, root), "absolute https URL without credentials or fragments")


def test_rejects_non_posix_local_evidence_reference(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    runtime_gate = gates["runtime_owner_decision"]
    assert isinstance(runtime_gate, dict)
    runtime_gate["evidence"] = [r"docs\plans\evidence\runtime_owner_decision.json"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "must use POSIX path separators")


def test_rejects_control_character_evidence_reference(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    runtime_gate = gates["runtime_owner_decision"]
    assert isinstance(runtime_gate, dict)
    runtime_gate["evidence"] = ["docs/plans/evidence/runtime_owner_decision.json\n"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        assert_fails(run_checker(checker, root), "must not contain control characters")


def test_complete_gate_rejects_missing_repo_artifact(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").unlink()
        assert_fails(run_checker(checker, root), "does not exist")


def test_complete_owner_decision_rejects_invalid_artifact_schema(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps({"schema": "wrong"}, indent=2) + "\n", encoding="utf-8"
        )
        assert_fails(run_checker(checker, root), "ahfl.native_grpc_owner_decision.v1")


def test_complete_owner_decision_rejects_decision_mismatch(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(owner_decision_artifact(decision="no-go"), indent=2) + "\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "must match")


def test_complete_owner_decision_rejects_mirrored_field_mismatch(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(owner_decision_artifact(decision_record="docs/plans/evidence/other.json"), indent=2)
            + "\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "decision_record")


def test_complete_owner_decision_rejects_condition_field_mismatch(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        artifact = owner_decision_artifact()
        artifact["continued_transport_scope"] = "not valid for go"
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(artifact, indent=2) + "\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "only allowed for no-go decisions")

    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        artifact = owner_decision_artifact()
        artifact["required_before_implementation"] = [
            "benchmark",
            "benchmark",
            "unknown_gate",
        ]
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(artifact, indent=2) + "\n",
            encoding="utf-8",
        )
        result = run_checker(checker, root)
        assert_fails(result, "must not contain duplicate entries")
        assert_fails(result, "contains unknown gates")
        assert_fails(result, "missing gates")

    fixture = evidence(decision_state="no-go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="rejected", evidence=fixture)
        artifact = owner_decision_artifact(decision="no-go")
        artifact["required_before_implementation"] = ["benchmark"]
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(artifact, indent=2) + "\n",
            encoding="utf-8",
        )
        assert_fails(run_checker(checker, root), "only allowed for go decisions")


def test_rejected_allows_structured_no_go_owner_decision(checker: Path) -> None:
    fixture = evidence(decision_state="no-go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="rejected", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").write_text(
            json.dumps(owner_decision_artifact(decision="no-go"), indent=2) + "\n",
            encoding="utf-8",
        )
        assert_passes(run_checker(checker, root))


def test_complete_benchmark_rejects_invalid_artifact_schema(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "benchmark.json").write_text(
            json.dumps({"schema": "wrong"}, indent=2) + "\n", encoding="utf-8"
        )
        assert_fails(run_checker(checker, root), "ahfl.native_grpc_benchmark.v1")


def test_complete_benchmark_requires_local_structured_artifact(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    gates = fixture["gates"]
    assert isinstance(gates, dict)
    benchmark_gate = gates["benchmark"]
    assert isinstance(benchmark_gate, dict)
    benchmark_gate["evidence"] = ["https://example.invalid/native-grpc-benchmark.json"]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        assert_fails(run_checker(checker, root), "repository-local JSON artifact")


def test_complete_benchmark_rejects_invalid_environment_timestamp(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "benchmark.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["environment"]["timestamp"] = "2026-07-08"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "benchmark environment.timestamp must be a UTC timestamp")


def test_complete_benchmark_rejects_calendar_invalid_environment_timestamp(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "benchmark.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["environment"]["timestamp"] = "2026-02-30T00:00:00Z"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "benchmark environment.timestamp must be a valid UTC timestamp")


def test_complete_benchmark_rejects_non_standard_metric_constant(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "benchmark.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["runs"][0]["metrics"]["p95_latency_ms"] = float("nan")
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        result = run_checker(checker, root)
        assert_fails(result, "invalid benchmark JSON")
        assert_fails(result, "non-standard JSON constant 'NaN' is not allowed")


def test_complete_benchmark_rejects_duplicate_scenario_transport_pair(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "benchmark.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["runs"][1] = dict(data["runs"][0])
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "duplicates benchmark scenario/transport pair")


def test_complete_build_matrix_rejects_invalid_artifact_schema(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "build_matrix.json").write_text(
            json.dumps({"schema": "wrong"}, indent=2) + "\n", encoding="utf-8"
        )
        assert_fails(run_checker(checker, root), "ahfl.native_grpc_build_matrix.v1")


def test_complete_build_matrix_rejects_duplicate_platform(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "build_matrix.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["platforms"][1] = dict(data["platforms"][0])
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "duplicates build matrix platform")


def test_complete_feature_flag_rejects_duplicate_disabled_diagnostics(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "feature_flag.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["disabled_diagnostics"] = [
            "runtime.grpc_native.disabled",
            "runtime.grpc_native.disabled",
        ]
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "disabled_diagnostics must not contain duplicate entries")


def test_complete_feature_flag_rejects_wrong_runtime_config(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "feature_flag.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["runtime_config"] = "runtime.transport.grpc_native"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(
            run_checker(checker, root),
            "runtime_config must be 'runtime.transport." + "native_grpc'",
        )


def test_complete_feature_flag_rejects_wrong_release_evidence_gate(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "feature_flag.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["release_evidence_gate"] = "runtime.grpc_native.release"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(
            run_checker(checker, root),
            "release_evidence_gate must be 'ahfl.runtime.native_grpc_release_evidence'",
        )


def test_complete_feature_flag_rejects_unscoped_disabled_diagnostic(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "feature_flag.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["disabled_diagnostics"] = ["runtime.transport.disabled"]
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "must start with 'runtime.grpc_native.'")


def test_complete_fallback_semantics_rejects_duplicate_scenario(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "fallback_semantics.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["scenarios"][1] = dict(data["scenarios"][0])
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "duplicates fallback scenario")


def test_complete_fallback_semantics_rejects_non_json_fallback_transport(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "fallback_semantics.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["scenarios"][0]["fallback_transport"] = "native_grpc"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "fallback_transport must be one of")


def test_complete_fallback_semantics_rejects_fail_open_scenario(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "fallback_semantics.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["scenarios"][0]["fail_closed"] = False
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "fail_closed must be true")


def test_complete_fallback_semantics_rejects_unscoped_diagnostic(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "fallback_semantics.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["scenarios"][0]["diagnostic"] = "runtime.transport.timeout"
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "diagnostic must start with 'runtime.grpc_native.'")


def test_complete_test_strategy_rejects_duplicate_test_names(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "test_strategy.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["coverage"][0]["tests"] = [
            "ahfl.runtime.grpc_native.unit",
            "ahfl.runtime.grpc_native.unit",
        ]
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "coverage[0].tests must not contain duplicate entries")


def test_complete_test_strategy_rejects_duplicate_coverage_area(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        artifact = root / "docs" / "plans" / "evidence" / "test_strategy.json"
        data = json.loads(artifact.read_text(encoding="utf-8"))
        data["coverage"][1] = dict(data["coverage"][0])
        artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        assert_fails(run_checker(checker, root), "duplicates test coverage area")


def test_complete_structured_artifacts_reject_unknown_fields(checker: Path) -> None:
    cases = [
        ("runtime_owner_decision.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("benchmark.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("benchmark.json", lambda data: data["environment"].__setitem__("extra", "unexpected")),
        ("benchmark.json", lambda data: data["runs"][0].__setitem__("extra", "unexpected")),
        ("benchmark.json", lambda data: data["runs"][0]["metrics"].__setitem__("extra", 1)),
        ("build_matrix.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("build_matrix.json", lambda data: data["platforms"][0].__setitem__("extra", "unexpected")),
        ("dependency_policy.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("feature_flag.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("fallback_semantics.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("fallback_semantics.json", lambda data: data["scenarios"][0].__setitem__("extra", "unexpected")),
        ("test_strategy.json", lambda data: data.__setitem__("extra", "unexpected")),
        ("test_strategy.json", lambda data: data["coverage"][0].__setitem__("extra", "unexpected")),
    ]
    for file_name, mutate in cases:
        fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_repo(root, status="implementing", evidence=fixture)
            artifact = root / "docs" / "plans" / "evidence" / file_name
            data = json.loads(artifact.read_text(encoding="utf-8"))
            mutate(data)
            artifact.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            assert_fails(run_checker(checker, root), "unknown field")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: transport_gate_smoke.py <check-native-grpc-gate.py>", file=sys.stderr)
        return 2
    checker = Path(sys.argv[1]).resolve()
    test_draft_rejects_implementation_markers(checker)
    test_draft_rejects_json_build_flag_marker(checker)
    test_draft_rejects_toml_runtime_config_marker(checker)
    test_rejects_unknown_rfc_status(checker)
    test_accepted_requires_owner_decision(checker)
    test_rejects_unknown_gate_name(checker)
    test_rejects_unknown_top_level_field(checker)
    test_rejects_unknown_decision_field(checker)
    test_rejects_unknown_gate_field(checker)
    test_rejects_non_utc_updated_at(checker)
    test_rejects_calendar_invalid_updated_at(checker)
    test_rejects_stale_decision_evidence_for_updated_rfc(checker)
    test_rejects_non_standard_json_constant_in_decision_evidence(checker)
    test_rejects_pending_decision_with_signoff_fields(checker)
    test_rejects_go_decision_with_invalid_signed_off_at(checker)
    test_rejects_go_decision_without_owner_gate(checker)
    test_rejects_pending_decision_with_complete_owner_gate(checker)
    test_rejects_duplicate_gate_evidence(checker)
    test_missing_gate_rejects_evidence(checker)
    test_complete_gate_rejects_empty_evidence(checker)
    test_incomplete_gate_statuses_reject_evidence(checker)
    test_accepted_allows_structured_owner_decision(checker)
    test_draft_rejects_signed_owner_decision(checker)
    test_implementing_requires_complete_gate_evidence(checker)
    test_implementing_allows_markers_after_complete_evidence(checker)
    test_complete_gate_rejects_placeholder_evidence(checker)
    test_rejects_http_remote_evidence_reference(checker)
    test_rejects_incomplete_https_remote_evidence_reference(checker)
    test_rejects_non_posix_local_evidence_reference(checker)
    test_rejects_control_character_evidence_reference(checker)
    test_complete_gate_rejects_missing_repo_artifact(checker)
    test_complete_owner_decision_rejects_invalid_artifact_schema(checker)
    test_complete_owner_decision_rejects_decision_mismatch(checker)
    test_complete_owner_decision_rejects_mirrored_field_mismatch(checker)
    test_complete_owner_decision_rejects_condition_field_mismatch(checker)
    test_rejected_allows_structured_no_go_owner_decision(checker)
    test_complete_benchmark_rejects_invalid_artifact_schema(checker)
    test_complete_benchmark_requires_local_structured_artifact(checker)
    test_complete_benchmark_rejects_invalid_environment_timestamp(checker)
    test_complete_benchmark_rejects_calendar_invalid_environment_timestamp(checker)
    test_complete_benchmark_rejects_non_standard_metric_constant(checker)
    test_complete_benchmark_rejects_duplicate_scenario_transport_pair(checker)
    test_complete_build_matrix_rejects_invalid_artifact_schema(checker)
    test_complete_build_matrix_rejects_duplicate_platform(checker)
    test_complete_feature_flag_rejects_duplicate_disabled_diagnostics(checker)
    test_complete_feature_flag_rejects_wrong_runtime_config(checker)
    test_complete_feature_flag_rejects_wrong_release_evidence_gate(checker)
    test_complete_feature_flag_rejects_unscoped_disabled_diagnostic(checker)
    test_complete_fallback_semantics_rejects_duplicate_scenario(checker)
    test_complete_fallback_semantics_rejects_non_json_fallback_transport(checker)
    test_complete_fallback_semantics_rejects_fail_open_scenario(checker)
    test_complete_fallback_semantics_rejects_unscoped_diagnostic(checker)
    test_complete_test_strategy_rejects_duplicate_test_names(checker)
    test_complete_test_strategy_rejects_duplicate_coverage_area(checker)
    test_complete_structured_artifacts_reject_unknown_fields(checker)
    print("transport gate smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
