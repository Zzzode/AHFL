#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from ahfl_source_revision import compute_source_revision


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class MatrixHandler(BaseHTTPRequestHandler):
    mode = "success"
    request_count = 0
    lock = threading.Lock()

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        with self.lock:
            type(self).request_count += 1

        if self.mode == "disconnect":
            self.connection.shutdown(2)
            self.connection.close()
            return
        if self.mode == "timeout":
            time.sleep(6)
            return
        if self.mode == "rate_limit":
            payload = b'{"error":"rate limited"}'
            self.send_response(429)
            self.send_header("Content-Type", "application/json")
            self.send_header("Retry-After", "0")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if self.mode == "partial_response":
            payload = b'{"choices":[{"message":{"content":"{\\"_type\\":'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload) + 100))
            self.end_headers()
            self.wfile.write(payload)
            self.wfile.flush()
            self.connection.shutdown(2)
            self.connection.close()
            return

        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": json.dumps(
                                {
                                    "_type": "execution_demo::types::GeneratedSummary",
                                    "summary": "production matrix summary",
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


def run_worker(worker: Path, repo: Path, work: Path, endpoint: str) -> subprocess.CompletedProcess[str]:
    approval = work / "unused-approval.json"
    return subprocess.run(
        [str(worker), "run", str(repo), str(work), endpoint, str(approval)],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
        timeout=20,
    )


def validate_completed_run(result: subprocess.CompletedProcess[str]) -> dict[str, object]:
    require(result.returncode == 0, f"normal run failed:\n{result.stdout}\n{result.stderr}")
    report = json.loads(result.stdout)
    require(report["schema"] == "ahfl.run-report", "normal run report schema mismatch")
    require(report["run"]["status"] == "completed", "normal run did not complete")
    require(report["audit"]["terminal_invariant_holds"], "normal run terminal invariant failed")
    return report


def validate_otel_artifact(path: Path) -> None:
    trace = json.loads(path.read_text())
    resource_spans = trace.get("resourceSpans")
    require(isinstance(resource_spans, list) and resource_spans, "OTel resourceSpans missing")
    scope_spans = resource_spans[0].get("scopeSpans")
    require(isinstance(scope_spans, list) and scope_spans, "OTel scopeSpans missing")
    spans = scope_spans[0].get("spans")
    require(isinstance(spans, list) and len(spans) >= 4, "OTel spans missing")
    require(spans[0]["name"] == "ahfl.run", "OTel root span mismatch")
    require(
        all(span["traceId"] == spans[0]["traceId"] for span in spans),
        "OTel trace IDs disagree",
    )


def main() -> int:
    require(
        len(sys.argv) == 7,
        "usage: reference_workflow_production_matrix.py "
        "<worker> <repo-root> <work-dir> <min-iterations> <min-seconds> <evidence-path>",
    )
    worker = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    work = Path(sys.argv[3]).resolve()
    min_iterations = int(sys.argv[4])
    min_seconds = float(sys.argv[5])
    evidence = Path(sys.argv[6]).resolve()
    require(min_iterations >= 10, "bounded soak requires at least 10 iterations")
    require(min_seconds >= 10.0, "bounded soak requires at least 10 seconds")
    if work.exists():
        import shutil

        shutil.rmtree(work)
    work.mkdir(parents=True)

    server = HTTPServer(("127.0.0.1", 0), MatrixHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = f"http://127.0.0.1:{server.server_port}/v1"
    try:
        MatrixHandler.mode = "success"
        MatrixHandler.request_count = 0
        event_counts: list[int] = []
        started = time.monotonic()
        iteration = 0
        while iteration < min_iterations or time.monotonic() - started < min_seconds:
            iteration_work = work / f"soak-{iteration}"
            iteration_work.mkdir()
            report = validate_completed_run(run_worker(worker, repo, iteration_work, endpoint))
            validate_otel_artifact(iteration_work / "otel-trace.json")
            event_counts.append(int(report["audit"]["total_events"]))
            iteration += 1
        elapsed = time.monotonic() - started
        require(len(set(event_counts)) == 1, "soak event count drifted across iterations")
        require(
            MatrixHandler.request_count == iteration,
            "soak must execute exactly one provider request per run",
        )

        faults: dict[str, dict[str, object]] = {}
        for mode in ("disconnect", "rate_limit", "timeout", "partial_response"):
            MatrixHandler.mode = mode
            before = MatrixHandler.request_count
            fault_work = work / f"fault-{mode}"
            fault_work.mkdir()
            result = run_worker(worker, repo, fault_work, endpoint)
            require(result.returncode != 0, f"{mode} unexpectedly succeeded")
            combined = result.stdout + result.stderr
            require("run_completed" in (fault_work / "events.jsonl").read_text(), f"{mode} lacks terminal event")
            report = json.loads(result.stdout)
            require(report["run"]["status"] == "failed", f"{mode} report is not failed")
            require(report["audit"]["terminal_invariant_holds"], f"{mode} terminal invariant failed")
            validate_otel_artifact(fault_work / "otel-trace.json")
            faults[mode] = {
                "status": "passed",
                "request_count": MatrixHandler.request_count - before,
                "diagnostic_excerpt": combined[-240:],
            }
    finally:
        server.shutdown()
        server.server_close()

    recovery_store_test = subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(repo / "build" / "dev"),
            "--output-on-failure",
            "-R",
            "^ahfl\\.runtime\\.workflow_recovery_all$",
        ],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
    )
    require(recovery_store_test.returncode == 0, recovery_store_test.stdout + recovery_store_test.stderr)

    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(
        json.dumps(
            {
                "schema": "ahfl.controlled-pilot-evidence.v1",
                "status": "passed",
                "source_revision": compute_source_revision(repo),
                "reference_workflow": "examples/execution-demo",
                "soak": {
                    "kind": "bounded-ci-soak",
                    "iterations": iteration,
                    "minimum_iterations": min_iterations,
                    "minimum_duration_seconds": min_seconds,
                    "duration_seconds": elapsed,
                    "stable_event_count": event_counts[0],
                    "provider_request_count": iteration,
                },
                "network_faults": faults,
                "process_crash_test": "ahfl.reference_workflow.recovery_smoke",
                "recovery_schema_test": "ahfl.runtime.workflow_recovery_all",
                "recovery_schema_policy": "reject unknown and legacy schemas",
                "otel_adapter_test": "ahfl.runtime.execution_otel_all",
                "provider_budget_test": "ahflc.run.llm_provider_runtime.smoke",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    print("reference workflow production matrix passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
