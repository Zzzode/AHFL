#!/usr/bin/env python3

from __future__ import annotations

import json
import signal
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from ahfl_source_revision import compute_source_revision


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class SummaryHandler(BaseHTTPRequestHandler):
    request_count = 0
    lock = threading.Lock()

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        with self.lock:
            type(self).request_count += 1
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": json.dumps(
                                {
                                    "_type": "execution_demo::types::GeneratedSummary",
                                    "summary": "durable recovered summary",
                                }
                            )
                        }
                    }
                ],
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

    def log_message(self, fmt: str, *args: object) -> None:
        return


def wait_for(path: Path, process: subprocess.Popen[str], timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(
                f"worker exited before fault marker: {process.returncode}\n{stdout}\n{stderr}"
            )
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {path}")


def main() -> int:
    require(
        len(sys.argv) == 5,
        "usage: reference_workflow_recovery_smoke.py "
        "<worker> <repo-root> <work-dir> <evidence-path>",
    )
    worker = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    work = Path(sys.argv[3]).resolve()
    evidence = Path(sys.argv[4]).resolve()
    if work.exists():
        import shutil

        shutil.rmtree(work)
    work.mkdir(parents=True)
    approval = work / "operator-approval.json"

    server = HTTPServer(("127.0.0.1", 0), SummaryHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = f"http://127.0.0.1:{server.server_port}/v1"
    try:
        crash = subprocess.Popen(
            [str(worker), "crash", str(repo), str(work), endpoint, str(approval)],
            cwd=repo,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        wait_for(work / "fault-ready.json", crash)
        crash.send_signal(signal.SIGKILL)
        crash_stdout, crash_stderr = crash.communicate(timeout=10)
        require(crash.returncode == -signal.SIGKILL, f"worker was not SIGKILLed: {crash.returncode}")
        require("FAULT_READY" in crash_stdout, f"missing fault ready output: {crash_stderr}")
        require(SummaryHandler.request_count == 1, "initial run must make exactly one HTTP request")

        recovery_path = work / "workflow-recovery.json"
        receipt_path = work / "capability-receipts.json"
        require(recovery_path.exists(), "workflow checkpoint was not committed")
        require(receipt_path.exists(), "capability receipt cache was not committed")
        recovery = json.loads(recovery_path.read_text())
        require(recovery["schema"] == "ahfl.workflow-recovery.v1", "recovery schema mismatch")
        require(recovery["checkpoint_id"] == 1, "checkpoint must include intake and decision")
        require(len(recovery["completed_nodes"]) == 2, "checkpoint must contain two nodes")

        (work / "workflow-recovery.json.tmp").write_text('{"schema":"partial')
        (work / "capability-receipts.json.tmp").write_text('{"schema":"partial')

        denied = subprocess.run(
            [str(worker), "resume", str(repo), str(work), endpoint, str(approval)],
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        require(denied.returncode == 4, f"resume without approval was not denied: {denied.stderr}")
        require("operator approval required" in denied.stderr, "missing approval diagnostic")
        require(SummaryHandler.request_count == 1, "denied resume must not call provider")

        approval.write_text(
            json.dumps(
                {
                    "schema": "ahfl.operator-approval.v1",
                    "decision": "approved",
                    "checkpoint_id": 1,
                    "operator": "beta-test",
                }
            )
            + "\n"
        )
        resumed = subprocess.run(
            [str(worker), "resume", str(repo), str(work), endpoint, str(approval)],
            cwd=repo,
            check=False,
            capture_output=True,
            text=True,
            timeout=60,
        )
        require(
            resumed.returncode == 0,
            f"approved resume failed: {resumed.returncode}\n{resumed.stdout}\n{resumed.stderr}",
        )
        report = json.loads(resumed.stdout)
        require(report["schema"] == "ahfl.run-report", "run report schema mismatch")
        require(report["run"]["status"] == "completed", "resumed run did not complete")
        nodes = {node["name"]: node for node in report["nodes"]}
        require(nodes["intake"]["restored_from_checkpoint_id"] == 1, "intake not restored")
        require(nodes["decide"]["restored_from_checkpoint_id"] == 1, "decision not restored")
        require(nodes["respond"]["restored_from_checkpoint_id"] is None, "responder was not replayed")
        require(report["audit"]["node_restored"] == 2, "audit restoration count mismatch")
        replay_nodes = report["replay"]["nodes"]
        require(sum(1 for node in replay_nodes if node["restored"]) == 2, "replay restoration mismatch")
        require(report["result"]["summary"] == "durable recovered summary", "wrong recovered result")
        metrics = json.loads((work / "process-metrics.json").read_text())
        require(
            metrics["schema"] == "ahfl.reference-worker-process-metrics.v1",
            "process metrics schema mismatch",
        )
        require(metrics["peak_rss_bytes"] > 0, "process metrics did not capture peak RSS")
        require(
            metrics["allocator_in_use_bytes"] >= 0,
            "process metrics allocator in-use is invalid",
        )
        require(
            metrics["allocator_reserved_bytes"] >= metrics["allocator_in_use_bytes"],
            "process metrics allocator reserved bytes are inconsistent",
        )

        events = [json.loads(line) for line in (work / "events.jsonl").read_text().splitlines()]
        require(events[-1]["type"] == "run_completed", "terminal event must be last")
        require(sum(event["type"] == "node_restored" for event in events) == 2, "node restore events missing")
        completed = [event for event in events if event["type"] == "capability_completed"]
        require(len(completed) == 1, "expected one capability completion")
        require(completed[0]["payload"]["cache_hit"] is True, "capability receipt was not reused")
        require(SummaryHandler.request_count == 1, "resume duplicated external side effect")

        require((work / "workflow-recovery.json.tmp").exists(), "partial recovery temp was consumed")
        require((work / "capability-receipts.json.tmp").exists(), "partial receipt temp was consumed")
        otel = json.loads((work / "otel-trace.json").read_text())
        spans = otel["resourceSpans"][0]["scopeSpans"][0]["spans"]
        require(spans[0]["name"] == "ahfl.run", "OTel root span missing")
        require(any(span["name"] == "ahfl.capability" for span in spans), "OTel capability span missing")
    finally:
        server.shutdown()
        server.server_close()

    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(
        json.dumps(
            {
                "schema": "ahfl.beta-evidence.reference-workflow-recovery.v1",
                "status": "passed",
                "criterion": "BETA-07",
                "source_revision": compute_source_revision(repo),
                "reference_workflow": "examples/execution-demo",
                "provider": "local-openai-compatible-http",
                "capability_adapter": "LLMCapabilityProvider",
                "durable_stores": ["WorkflowRecoveryStore", "persistent-response-cache"],
                "faults": [
                    "sigkill_after_provider_commit",
                    "partial_checkpoint_temp",
                    "partial_receipt_temp",
                    "missing_operator_approval",
                ],
                "restored_node_ids": [0, 1],
                "replayed_node_ids": [2],
                "external_request_count": SummaryHandler.request_count,
                "idempotency": "persistent capability receipt cache hit",
                "human_approval": "required",
                "terminal_invariant": True,
                "replay_projection": True,
                "audit_projection": True,
                "budgets": {
                    "max_workflow_total_tokens": 128,
                    "max_node_total_tokens": 128,
                    "max_workflow_total_cost_usd": 0.01,
                    "max_node_total_cost_usd": 0.01,
                    "latency_timeout_seconds": 5,
                },
                "observability_adapters": ["ahfl.run-event JSONL", "OTLP-compatible JSON"],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )
    print("reference workflow recovery smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
