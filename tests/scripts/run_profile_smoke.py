#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from ahfl_source_revision import compute_source_revision


class SummaryHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = json.dumps(
            {
                "choices": [
                    {
                        "message": {
                            "content": json.dumps(
                                {
                                    "_type": "execution_demo::types::GeneratedSummary",
                                    "summary": "local deterministic summary",
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
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt: str, *args: object) -> None:
        return


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(
    ahflc: Path,
    project: Path,
    sysroot: Path,
    args: list[str],
    env: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(ahflc), "run", "--sysroot", str(sysroot), *args],
        cwd=project,
        env=env,
        check=False,
        capture_output=True,
        text=True,
        timeout=30,
    )


def parse_json_output(result: subprocess.CompletedProcess[str], label: str) -> dict[str, object]:
    require(
        result.returncode == 0,
        f"{label} failed with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
    )
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"{label} did not emit JSON: {error}\n{result.stdout}") from error
    require(isinstance(value, dict), f"{label} JSON must be an object")
    require(value.get("schema") == "ahfl.run-report", f"{label} schema mismatch: {value}")
    require(value.get("schema_version") == 1, f"{label} schema version mismatch: {value}")
    return value


def result_ticket(report: dict[str, object]) -> str:
    result = report.get("result")
    require(isinstance(result, dict), f"run report result must be an object: {report}")
    ticket = result.get("ticket_id")
    require(isinstance(ticket, str), f"run report ticket_id must be a string: {result}")
    return ticket


def main() -> int:
    require(
        len(sys.argv) == 5,
        "usage: run_profile_smoke.py <ahflc> <repo-root> <work-dir> <evidence-path>",
    )
    ahflc = Path(sys.argv[1]).resolve()
    repo_root = Path(sys.argv[2]).resolve()
    work_dir = Path(sys.argv[3]).resolve()
    evidence_path = Path(sys.argv[4]).resolve()
    project = work_dir / "execution-demo"

    if project.exists():
        shutil.rmtree(project)
    project.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(repo_root / "examples/execution-demo", project)

    server = HTTPServer(("127.0.0.1", 0), SummaryHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        config_path = project / "llm_config.example.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["endpoint"] = f"http://127.0.0.1:{server.server_port}/v1"
        config["model"] = "local-summary"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

        env = os.environ.copy()
        env["AHFL_GLM_API_KEY"] = "local-test-secret"

        default_result = run(
            ahflc, project, repo_root, ["--output-format", "json"], env
        )
        default_report = parse_json_output(default_result, "default profile")
        require(result_ticket(default_report) == "INC-1001", "default profile used wrong input")

        low_result = run(
            ahflc,
            project,
            repo_root,
            ["--profile", "low-risk", "--output-format", "json"],
            env,
        )
        low_report = parse_json_output(low_result, "low-risk profile")
        require(result_ticket(low_report) == "INC-1002", "low-risk profile used wrong input")

        override_result = run(
            ahflc,
            project,
            repo_root,
            [
                "--profile",
                "low-risk",
                "--input-file",
                "inputs/high-severity.json",
                "--output-format",
                "json",
            ],
            env,
        )
        override_report = parse_json_output(override_result, "CLI input override")
        require(result_ticket(override_report) == "INC-1001", "CLI input did not override profile")

        jsonl_result = run(
            ahflc,
            project,
            repo_root,
            ["--profile", "low-risk", "--output-format", "jsonl"],
            env,
        )
        require(jsonl_result.returncode == 0, f"JSONL run failed: {jsonl_result.stderr}")
        event_lines = [json.loads(line) for line in jsonl_result.stdout.splitlines() if line]
        require(event_lines, "JSONL run emitted no events")
        require(
            all(event.get("schema") == "ahfl.run-event" for event in event_lines),
            "JSONL run emitted a non-event line",
        )
        require(
            event_lines[-1].get("type") == "run_completed",
            "JSONL terminal event was not last",
        )

        quiet_result = run(
            ahflc,
            project,
            repo_root,
            ["--profile", "low-risk", "--output-format", "quiet"],
            env,
        )
        require(quiet_result.returncode == 0, f"quiet run failed: {quiet_result.stderr}")
        quiet_value = json.loads(quiet_result.stdout)
        require(quiet_value.get("ticket_id") == "INC-1002", "quiet output used wrong input")
        require("schema" not in quiet_value, "quiet output leaked report envelope")

        conflict_result = run(
            ahflc,
            project,
            repo_root,
            ["--input", "{}", "--input-file", "inputs/low-risk.json"],
            env,
        )
        require(conflict_result.returncode == 2, "input conflict did not return usage error")
        require(
            "--input cannot be combined with --input-file" in conflict_result.stderr,
            "input conflict diagnostic mismatch",
        )
    finally:
        server.shutdown()
        server.server_close()

    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_path.write_text(
        json.dumps(
            {
                "schema": "ahfl.beta-evidence.run-profiles.v1",
                "status": "passed",
                "criterion": "BETA-01",
                "source_revision": compute_source_revision(repo_root),
                "reference_workflow": "examples/execution-demo",
                "profiles": ["default", "low-risk"],
                "formats": ["human", "json", "jsonl", "quiet"],
                "cli_precedence": ["input-file", "output-format"],
                "provider": "local-openai-compatible-stub",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    print("run profile smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
