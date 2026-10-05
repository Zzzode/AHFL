#!/usr/bin/env python3
"""Integration gate for the `ahflc visualize` command.

Runs the visualize command against a golden execution-plan JSON and verifies:
1. HTML output contains the expected structure (cards, edges, pan/zoom).
2. DOT output is a valid digraph with the expected nodes and edges.
3. Mermaid output is a valid graph LR with the expected nodes and edges.
4. HTML with trace contains execution state (status badges, timeline).
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_visualize(ahflc: Path, plan: Path, *args: str) -> subprocess.CompletedProcess[str]:
    cmd = [str(ahflc), "visualize", str(plan), *args]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def main() -> int:
    require(len(sys.argv) == 3, "usage: visualize_gate.py <ahflc-binary> <tests-dir>")
    ahflc = Path(sys.argv[1]).resolve()
    tests_dir = Path(sys.argv[2]).resolve()
    require(ahflc.is_file(), f"ahflc binary missing: {ahflc}")

    plan = tests_dir / "golden" / "plan" / "ok_workflow_value_flow.with_package.execution-plan.json"
    require(plan.is_file(), f"golden execution-plan missing: {plan}")

    # ── 1. HTML output (static) ──────────────────────────────────────────
    result = run_visualize(ahflc, plan, "-o", "-")
    require(result.returncode == 0, f"visualize html failed: {result.stderr}")
    html = result.stdout
    require("<!DOCTYPE html>" in html, "HTML missing DOCTYPE")
    require("AHFL Workflow Canvas" in html, "HTML missing title")
    require("first" in html, "HTML missing node 'first'")
    require("second" in html, "HTML missing node 'second'")
    require("EchoAgent" in html, "HTML missing agent type")
    require("card" in html, "HTML missing card class")
    require("edge" in html, "HTML missing edge class")
    require("fitToView" in html, "HTML missing fit-to-view")
    # No trace → no execution state.
    require('"exec":' not in html, "static HTML should not contain exec state")
    require('"trace":' not in html, "static HTML should not contain trace summary")

    # ── 2. DOT output ────────────────────────────────────────────────────
    result = run_visualize(ahflc, plan, "--format", "dot", "-o", "-")
    require(result.returncode == 0, f"visualize dot failed: {result.stderr}")
    dot = result.stdout
    require("digraph" in dot, "DOT missing digraph")
    require("rankdir=LR" in dot, "DOT missing rankdir")
    require('"first"' in dot, "DOT missing node 'first'")
    require('"second"' in dot, "DOT missing node 'second'")
    require('"first" -> "second"' in dot, "DOT missing edge first->second")

    # ── 3. Mermaid output ────────────────────────────────────────────────
    result = run_visualize(ahflc, plan, "--format", "mermaid", "-o", "-")
    require(result.returncode == 0, f"visualize mermaid failed: {result.stderr}")
    mmd = result.stdout
    require("graph LR" in mmd, "Mermaid missing graph LR")
    require("first" in mmd, "Mermaid missing node 'first'")
    require("second" in mmd, "Mermaid missing node 'second'")
    require("first --> second" in mmd, "Mermaid missing edge first-->second")

    # ── 4. HTML with trace ───────────────────────────────────────────────
    trace_lines = [
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 0,
                     "type": "run_started", "monotonic_offset_ns": 0,
                     "payload": {"run_id": 0}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 1,
                     "type": "workflow_started", "monotonic_offset_ns": 1000,
                     "payload": {"run_id": 0, "workflow_id": 0}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 2,
                     "type": "node_scheduled", "monotonic_offset_ns": 2000,
                     "payload": {"workflow_id": 0, "node_id": 0, "execution_slot": 0,
                                 "dependencies": []}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 3,
                     "type": "node_started", "monotonic_offset_ns": 3000,
                     "payload": {"node_id": 0, "agent_id": 0}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 4,
                     "type": "agent_state_entered", "monotonic_offset_ns": 4000,
                     "payload": {"node_id": 0, "agent_id": 0, "state_id": 0}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 5,
                     "type": "node_completed", "monotonic_offset_ns": 5000,
                     "payload": {"node_id": 0, "output_value_id": 0}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 6,
                     "type": "node_scheduled", "monotonic_offset_ns": 6000,
                     "payload": {"workflow_id": 0, "node_id": 1, "execution_slot": 1,
                                 "dependencies": [0]}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 7,
                     "type": "node_started", "monotonic_offset_ns": 7000,
                     "payload": {"node_id": 1, "agent_id": 1}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 8,
                     "type": "node_completed", "monotonic_offset_ns": 8000,
                     "payload": {"node_id": 1, "output_value_id": 1}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 9,
                     "type": "workflow_completed", "monotonic_offset_ns": 9000,
                     "payload": {}}),
        json.dumps({"schema": "ahfl.run-event", "schema_version": 1, "event_id": 10,
                     "type": "run_completed", "monotonic_offset_ns": 10000,
                     "payload": {}}),
    ]
    with tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False) as f:
        f.write("\n".join(trace_lines) + "\n")
        trace_path = Path(f.name)

    try:
        result = run_visualize(ahflc, plan, str(trace_path), "-o", "-")
        require(result.returncode == 0, f"visualize html+trace failed: {result.stderr}")
        html = result.stdout
        require('"trace":' in html, "trace HTML missing trace summary")
        require('"exec":' in html, "trace HTML missing exec state")
        require('"status":"completed"' in html, "trace HTML missing completed status")
        require('"state_transitions"' in html, "trace HTML missing state transitions")
        require('"initial_state":"Init"' in html, "trace HTML missing initial state")
        require('"final_states":["Done"]' in html, "trace HTML missing final states")
        require("status-badge" in html, "trace HTML missing status badge CSS")
        require("timeline" in html, "trace HTML missing timeline")
        require("flowing" in html, "trace HTML missing flowing edge animation")
    finally:
        trace_path.unlink(missing_ok=True)

    # ── 5. Error handling ────────────────────────────────────────────────
    result = run_visualize(ahflc, plan, "--format", "invalid")
    require(result.returncode != 0, "invalid format should fail")

    result = run_visualize(ahflc, Path("/nonexistent/plan.json"))
    require(result.returncode != 0, "nonexistent plan should fail")

    print("all visualize gates passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
