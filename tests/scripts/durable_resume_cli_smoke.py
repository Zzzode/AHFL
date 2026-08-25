#!/usr/bin/env python3
"""Shell-level smoke for the RFC 0022 durable-resume CLI flags.

`ahflc run` gained --recovery-store, --resume-pending-result, and
--suspend-capability (RFC 0022). This smoke pins the shell-observable contract:

  1. The flags are accepted by the parser (not rejected as unknown options).
  2. On a normally-completing run, --recovery-store is inert: no snapshot file is
     written (a snapshot appears only when the workflow suspends).
  2b. A real shell round-trip: --suspend-capability forces a capability to
     PENDING, the run suspends + persists a snapshot + exits 0; a second run
     loads it and resumes with --resume-pending-result to the injected result.
  3. --resume-pending-result rejects invalid JSON with a targeted diagnostic and
     a nonzero exit.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class OkHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {"choices": [{"message": {"content": '{"value":"done"}'}}]}
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt: str, *args: object) -> None:
        return


SOURCE = """module smoke;

struct Request { value: String; }
struct Context { value: String = "pending"; }
struct Response { value: String; }

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
    state Done { return Response { value: ctx.value }; }
}

workflow SmokeWorkflow {
    input: Request;
    output: Response;
    node only: EchoAgent(input);
    return: Response { value: only.value };
}
"""


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: durable_resume_cli_smoke.py <ahflc> <work-dir>", file=sys.stderr)
        return 2
    ahflc = Path(sys.argv[1])
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)

    source = work / "smoke.ahfl"
    source.write_text(SOURCE, encoding="utf-8")

    server = HTTPServer(("127.0.0.1", 0), OkHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        config = work / "llm.json"
        config.write_text(
            json.dumps(
                {
                    "endpoint": f"http://127.0.0.1:{server.server_port}/v1",
                    "model": "smoke",
                    "api_key_secret": "AHFL_TEST_DURABLE_RESUME_CLI_KEY",
                    "max_retries": 0,
                }
            ),
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["AHFL_TEST_DURABLE_RESUME_CLI_KEY"] = "durable-resume-cli-secret"

        snapshot = work / "recovery.json"
        if snapshot.exists():
            snapshot.unlink()

        # (1)+(2): a normal run with --recovery-store completes and writes no snapshot.
        completed = subprocess.run(
            [
                str(ahflc), "run",
                "--workflow", "smoke::SmokeWorkflow",
                "--input", '{"_type":"smoke::Request","value":"hi"}',
                "--llm-config", str(config),
                "--recovery-store", str(snapshot),
                "--output-format", "json",
                str(source),
            ],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(
            "unknown option" not in (completed.stdout + completed.stderr).lower(),
            "--recovery-store was rejected as an unknown option",
        )
        require(completed.returncode == 0,
                f"completing run should exit 0, got {completed.returncode}: {completed.stderr}")
        require(not snapshot.exists(),
                "no snapshot must be written when the workflow completes (flag is inert on success)")

        # (2b): a REAL shell round-trip. --suspend-capability forces Echo to
        # PENDING on a fresh run -> the workflow suspends, exits 0, and writes a
        # snapshot; a second run loads it and resumes with the injected result,
        # completing without re-invoking the live capability.
        roundtrip_snapshot = work / "roundtrip.json"
        if roundtrip_snapshot.exists():
            roundtrip_snapshot.unlink()
        base = [
            str(ahflc), "run",
            "--workflow", "smoke::SmokeWorkflow",
            "--input", '{"_type":"smoke::Request","value":"hi"}',
            "--llm-config", str(config),
            "--recovery-store", str(roundtrip_snapshot),
            "--output-format", "json",
            str(source),
        ]
        suspend = subprocess.run(
            base + ["--suspend-capability", "smoke::Echo"],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(suspend.returncode == 0,
                f"suspended run should exit 0 (durably parked), got {suspend.returncode}: {suspend.stderr}")
        require(roundtrip_snapshot.exists(),
                "a suspended run must persist a resume record to --recovery-store")
        suspend_report = json.loads(suspend.stdout)
        require(suspend_report["audit"]["workflow_completed"] == 0,
                "suspended run must not report a completed workflow")

        resume = subprocess.run(
            base + ["--resume-pending-result",
                    '{"_type":"smoke::Response","value":"resumed-ok"}'],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(resume.returncode == 0,
                f"resumed run should exit 0, got {resume.returncode}: {resume.stderr}")
        resume_report = json.loads(resume.stdout)
        require(resume_report["audit"]["workflow_completed"] == 1,
                "resumed run must complete the workflow")
        result = resume_report.get("result", resume_report.get("output"))
        require(result is not None and result.get("value") == "resumed-ok",
                f"resumed output must be the injected result, got {result}")

        # (3): invalid --resume-pending-result JSON is rejected with exit 2.
        bad = subprocess.run(
            [
                str(ahflc), "run",
                "--workflow", "smoke::SmokeWorkflow",
                "--input", '{"_type":"smoke::Request","value":"hi"}',
                "--llm-config", str(config),
                "--resume-pending-result", "{not json",
                str(source),
            ],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(bad.returncode != 0,
                f"invalid --resume-pending-result JSON should fail, got exit {bad.returncode}")
        require("resume-pending-result" in bad.stderr,
                "invalid resume-pending-result must produce a targeted diagnostic")
    finally:
        server.shutdown()
        server.server_close()

    print("durable-resume CLI smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
