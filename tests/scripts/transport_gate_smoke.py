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
    "grpc_json_transcoding",
    "native_grpc",
)


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
    if ref.endswith("/benchmark.json"):
        return benchmark_artifact()
    return {"artifact": ref}


def write_repo(root: Path, *, status: str, evidence: dict[str, object]) -> None:
    (root / "docs" / "rfcs").mkdir(parents=True)
    (root / "docs" / "plans").mkdir(parents=True)
    (root / "docs" / "rfcs" / "0004-native-grpc-transport.zh.md").write_text(
        f"---\nstatus: {status}\n---\n\n# RFC0004\n", encoding="utf-8"
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
        "updated_at": "2026-07-08",
        "decision": {
            "state": decision_state,
            "owner": "runtime-owner" if decision_state != "pending" else "",
            "signed_off_at": "2026-07-08" if decision_state != "pending" else "",
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


def test_accepted_requires_owner_decision(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=evidence())
        assert_fails(run_checker(checker, root), "runtime_owner_decision")


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
        assert_fails(run_checker(checker, root), "http(s) URL or an existing repository-relative artifact path")


def test_complete_gate_rejects_missing_repo_artifact(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates={"runtime_owner_decision"})
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="accepted", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "runtime_owner_decision.json").unlink()
        assert_fails(run_checker(checker, root), "does not exist")


def test_complete_benchmark_rejects_invalid_artifact_schema(checker: Path) -> None:
    fixture = evidence(decision_state="go", complete_gates=set(REQUIRED_GATES))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_repo(root, status="implementing", evidence=fixture)
        (root / "docs" / "plans" / "evidence" / "benchmark.json").write_text(
            json.dumps({"schema": "wrong"}, indent=2) + "\n", encoding="utf-8"
        )
        assert_fails(run_checker(checker, root), "benchmark artifact schema")


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


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: transport_gate_smoke.py <check-native-grpc-gate.py>", file=sys.stderr)
        return 2
    checker = Path(sys.argv[1]).resolve()
    test_draft_rejects_implementation_markers(checker)
    test_accepted_requires_owner_decision(checker)
    test_implementing_requires_complete_gate_evidence(checker)
    test_implementing_allows_markers_after_complete_evidence(checker)
    test_complete_gate_rejects_placeholder_evidence(checker)
    test_complete_gate_rejects_missing_repo_artifact(checker)
    test_complete_benchmark_rejects_invalid_artifact_schema(checker)
    test_complete_benchmark_requires_local_structured_artifact(checker)
    print("transport gate smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
