#!/usr/bin/env python3
"""Generate criterion-specific beta evidence for runtime identity and projections."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from ahfl_source_revision import compute_source_revision


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)


def write_evidence(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def require_no_match(repo: Path, pattern: str, paths: list[str], label: str) -> None:
    result = run(["rg", "-n", pattern, *paths], repo)
    if result.returncode == 0:
        raise RuntimeError(f"{label} still present:\n{result.stdout}")
    if result.returncode not in {1}:
        raise RuntimeError(result.stderr)


def main() -> int:
    args = parse_args()
    repo = args.repo_root.resolve()
    build_dir = args.build_dir.resolve()
    out_dir = args.out_dir.resolve()

    runtime_tests = run(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "-R",
            (
                "^(ahfl\\.runtime\\."
                "(execution_(event|report|metadata|renderer|projection|otel)_all|"
                "workflow_(runtime|recovery)_all)|"
                "ahflc\\.run\\.llm_provider_runtime\\.smoke)$"
            ),
        ],
        repo,
    )
    if runtime_tests.returncode != 0:
        raise RuntimeError(runtime_tests.stdout + runtime_tests.stderr)

    require_no_match(
        repo,
        r"struct NodeExecutionResult|std::vector<std::string> execution_order",
        ["src/runtime/engine/workflow_runtime.hpp"],
        "string-based workflow result identity",
    )
    require_no_match(
        repo,
        r"print_workflow_result|=== AHFL Workflow Execution ===|print_llm_provider_observability",
        ["src/tooling/cli", "src/runtime"],
        "old ad hoc runtime printer",
    )
    require_no_match(
        repo,
        r"unordered_map<std::string,\s*CumulativeBudgetTotals>|workflow_budget_key|node_budget_key",
        ["src/runtime/providers/llm"],
        "string-based cumulative budget identity",
    )
    state_event_source = (repo / "src/runtime/engine/workflow_runtime.cpp").read_text(
        encoding="utf-8"
    )
    if "AgentStateEntered{" not in state_event_source:
        raise RuntimeError("WorkflowRuntime does not emit AgentStateEntered events")
    call_identity_source = (repo / "include/ahfl/compiler/ir/expr.hpp").read_text(encoding="utf-8")
    if "SymbolRef callee_ref" not in call_identity_source:
        raise RuntimeError("IR CallExpr does not preserve callee symbol identity")
    renderer_source = (repo / "src/runtime/engine/execution_renderer.cpp").read_text(
        encoding="utf-8"
    )
    for builder in (
        "build_execution_replay_projection",
        "build_execution_audit_projection",
    ):
        if builder not in renderer_source:
            raise RuntimeError(f"execution JSON renderer does not consume {builder}")
    projection_source = (repo / "src/runtime/engine/execution_projection.cpp").read_text(
        encoding="utf-8"
    )
    for builder in (
        "build_execution_scheduler_projection",
        "build_execution_checkpoint_projection",
    ):
        if builder not in projection_source:
            raise RuntimeError(f"runtime does not implement canonical {builder}")
    otel_source = (repo / "src/runtime/engine/execution_otel.cpp").read_text(
        encoding="utf-8"
    )
    if "build_execution_otel_trace" not in otel_source:
        raise RuntimeError("runtime does not implement canonical OTel projection")
    recovery_source = (repo / "src/runtime/engine/workflow_recovery.cpp").read_text(
        encoding="utf-8"
    )
    if "materialize_workflow_recovery_snapshot" not in recovery_source:
        raise RuntimeError("runtime does not materialize recovery from checkpoint projection")
    event_source = (repo / "include/ahfl/runtime/execution_event.hpp").read_text(
        encoding="utf-8"
    )
    if "CapabilityUsageRecorded" not in event_source:
        raise RuntimeError("provider usage is not represented by canonical execution events")
    require_no_match(
        repo,
        r"ahfl\.reference-provider-audit\.v1|(?:pre-crash-)?provider-audit\.json",
        ["src", "include", "tests"],
        "parallel provider audit fact source",
    )

    revision = compute_source_revision(repo)
    runtime_test_pattern = (
        "^(ahfl\\.runtime\\."
        "(execution_(event|report|metadata|renderer|projection|otel)_all|"
        "workflow_(runtime|recovery)_all)|"
        "ahflc\\.run\\.llm_provider_runtime\\.smoke)$"
    )
    common = {
        "status": "passed",
        "source_revision": revision,
        "test_command": (
            "ctest --test-dir "
            + str(build_dir)
            + " --output-on-failure -R "
            + f"'{runtime_test_pattern}'"
        ),
        "test_summary": runtime_tests.stdout.strip().splitlines()[-1],
    }

    write_evidence(
        out_dir / "runtime-identity.json",
        {
            **common,
            "schema": "ahfl.beta-evidence.runtime-identity.v1",
            "status": "passed",
            "criterion": "BETA-02",
            "strong_ids": [
                "RunId",
                "WorkflowId",
                "WorkflowNodeId",
                "AgentId",
                "CapabilityId",
                "InvocationId",
                "RuntimeValueId",
                "ExecutionEventId",
            ],
            "canonical_stores": [
                "ExecutionMetadataStore",
                "ExecutionEventStore",
                "WorkflowResult.values",
            ],
            "string_result_fields_rejected": True,
            "capability_symbol_identity_preserved": True,
            "agent_state_events_emitted": True,
            "budget_accumulation_uses_typed_ids": True,
        },
    )
    write_evidence(
        out_dir / "event-projections.json",
        {
            **common,
            "schema": "ahfl.beta-evidence.event-projections.v1",
            "criterion": "BETA-03",
            "event_store": "ExecutionEventStore",
            "report": "ExecutionReport",
            "projections": [
                "human",
                "json",
                "jsonl",
                "quiet",
                "replay",
                "audit",
                "scheduler",
                "checkpoint",
                "otel",
            ],
            "old_ad_hoc_printers_removed": True,
            "replay_projection": "ExecutionReplayProjection",
            "audit_projection": "ExecutionAuditProjection",
            "scheduler_projection": "ExecutionSchedulerProjection",
            "checkpoint_projection": "ExecutionCheckpointProjection",
            "otel_projection": "ExecutionOtelTrace",
            "recovery_snapshot": "WorkflowRecoverySnapshot",
            "canonical_provider_facts": [
                "usage",
                "cache_outcome",
                "provider_degradation",
                "policy_notices",
                "ranged_diagnostics",
            ],
            "projection_tests": [
                "ahfl.runtime.execution_renderer_all",
                "ahfl.runtime.execution_projection_all",
                "ahfl.runtime.execution_otel_all",
                "ahfl.runtime.workflow_recovery_all",
                "ahflc.run.llm_provider_runtime.smoke",
            ],
            "status": "passed",
        },
    )

    freeze = run(["python3", "scripts/check-product-scope-freeze.py"], repo)
    if freeze.returncode != 0:
        raise RuntimeError(freeze.stdout + freeze.stderr)
    write_evidence(
        out_dir / "product-scope-freeze.json",
        {
            "schema": "ahfl.beta-evidence.product-scope-freeze.v1",
            "status": "passed",
            "criterion": "BETA-10",
            "source_revision": revision,
            "test_command": "python3 scripts/check-product-scope-freeze.py",
            "test_output": freeze.stdout.strip(),
        },
    )
    print("beta runtime evidence generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
