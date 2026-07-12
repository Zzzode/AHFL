#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import statistics
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


class SoakHandler(BaseHTTPRequestHandler):
    request_count = 0
    transient_disconnects_remaining = 0
    lock = threading.Lock()

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        with self.lock:
            type(self).request_count += 1
            disconnect = type(self).transient_disconnects_remaining > 0
            if disconnect:
                type(self).transient_disconnects_remaining -= 1
        if disconnect:
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
                                    "summary": "long soak summary",
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--minimum-seconds", type=float, required=True)
    parser.add_argument("--minimum-iterations", type=int, required=True)
    parser.add_argument("--evidence-path", type=Path, required=True)
    parser.add_argument("--contract-kind", choices=("smoke", "hour-scale"), required=True)
    return parser.parse_args()


def run_worker(
    worker: Path,
    repo: Path,
    work: Path,
    endpoint: str,
    control: Path,
    minimum_seconds: float,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(worker),
            "soak",
            str(repo),
            str(work),
            endpoint,
            str(control),
        ],
        cwd=repo,
        check=False,
        capture_output=True,
        text=True,
        timeout=minimum_seconds + 180,
    )


def main() -> int:
    args = parse_args()
    worker = args.worker.resolve()
    repo = args.repo_root.resolve()
    work = args.work_dir.resolve()
    evidence = args.evidence_path.resolve()
    require(args.minimum_seconds > 0, "minimum seconds must be positive")
    require(args.minimum_iterations > 0, "minimum iterations must be positive")
    if args.contract_kind == "hour-scale":
        require(
            args.minimum_seconds >= 3600,
            "hour-scale contract requires at least 3600 seconds",
        )

    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    server = HTTPServer(("127.0.0.1", 0), SoakHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = f"http://127.0.0.1:{server.server_port}/v1"

    control = work / "soak-control.json"
    control.write_text(
        json.dumps(
            {
                "schema": "ahfl.reference-soak-control.v1",
                "minimum_duration_seconds": args.minimum_seconds,
                "minimum_iterations": args.minimum_iterations,
            }
        )
        + "\n",
        encoding="utf-8",
    )
    SoakHandler.request_count = 0
    SoakHandler.transient_disconnects_remaining = 1
    try:
        result = run_worker(
            worker,
            repo,
            work,
            endpoint,
            control,
            args.minimum_seconds,
        )
    finally:
        server.shutdown()
        server.server_close()

    require(
        result.returncode == 0,
        f"long-lived soak worker failed:\n{result.stdout}\n{result.stderr}",
    )
    worker_report = json.loads(result.stdout)
    require(
        worker_report["schema"] == "ahfl.reference-worker-soak.v1",
        "long-lived worker report schema mismatch",
    )
    require(
        worker_report["process_model"] == "single-long-lived-worker",
        "long soak did not use one long-lived worker process",
    )
    elapsed = float(worker_report["duration_seconds"])
    iteration = int(worker_report["iterations"])
    require(elapsed >= args.minimum_seconds, "long soak duration threshold not met")
    require(iteration >= args.minimum_iterations, "long soak iteration threshold not met")
    provider_retry_count = int(worker_report["provider_retry_count"])
    require(
        provider_retry_count >= 1,
        "long soak did not recover from the injected transient provider disconnect",
    )
    require(
        SoakHandler.request_count == iteration + provider_retry_count,
        "long soak provider request count drifted",
    )
    require(worker_report["stable_event_count"] > 0, "long soak event count is invalid")
    for key in ("peak_rss", "allocator_in_use", "allocator_reserved"):
        metric = worker_report[key]
        require(metric["samples"] > 0, f"{key} has no samples")
        if iteration > 5:
            require(
                metric["samples"] < iteration,
                f"{key} sampling grows at the workflow iteration rate",
            )
        require(metric["maximum_bytes"] >= metric["minimum_bytes"], f"{key} bounds invalid")
    require(
        worker_report["allocator_reserved"]["minimum_bytes"]
        >= worker_report["allocator_in_use"]["minimum_bytes"],
        "long soak allocator samples are inconsistent",
    )

    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(
        json.dumps(
            {
                "schema": "ahfl.production-confidence-soak.v1",
                "status": "passed",
                "source_revision": compute_source_revision(repo),
                "kind": args.contract_kind,
                "process_model": "single-long-lived-worker",
                "reference_workflow": "examples/execution-demo",
                "duration_seconds": elapsed,
                "minimum_duration_seconds": args.minimum_seconds,
                "iterations": iteration,
                "minimum_iterations": args.minimum_iterations,
                "stable_event_count": worker_report["stable_event_count"],
                "provider_request_count": SoakHandler.request_count,
                "provider_retry_count": provider_retry_count,
                "throughput_runs_per_second": iteration / elapsed,
                "latency_seconds": worker_report["latency_seconds"],
                "peak_rss": worker_report["peak_rss"],
                "allocator_in_use": worker_report["allocator_in_use"],
                "allocator_reserved": worker_report["allocator_reserved"],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    print("reference workflow long soak passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
