#!/usr/bin/env python3
"""Exercise automatic LLM checkpoint handshakes, compaction and cold CLI resume."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from llm_provider_runtime_smoke import write_source


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def final_response(value: str = "done") -> dict:
    return {"choices": [{"message": {"content": json.dumps({"value": value})}}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12}}


def tool_response(identifier: str, name: str = "lookup", arguments: dict | None = None) -> dict:
    return {"choices": [{"message": {"content": None, "tool_calls": [
        {"id": identifier, "type": "function", "function": {
            "name": name, "arguments": json.dumps(arguments or {})}}]}}],
        "usage": {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12}}


class ScenarioServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, mode: str):
        super().__init__(("127.0.0.1", 0), Handler)
        self.mode = mode
        self.requests: list[dict] = []
        self.main_count = 0
        self.summary_count = 0
        self.exact_history = False
        self.blocked = threading.Event()
        self.release = threading.Event()
        self.mutex = threading.Lock()
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def close(self) -> None:
        self.release.set()
        self.shutdown()
        self.server_close()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_: object) -> None:
        pass

    def do_POST(self) -> None:
        server: ScenarioServer = self.server
        request = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        messages = request.get("messages", [])
        summarizing = "Summarize the supplied" in messages[0].get("content", "")
        with server.mutex:
            server.requests.append(request)
            if summarizing:
                server.summary_count += 1
                payload = final_response()
                payload["choices"][0]["message"]["content"] = json.dumps({
                    "summary": "Keep the task. Read immutable history_id 3 chunk_index 4 for the original lookup fact."})
                current = 0
            else:
                server.main_count += 1
                current = server.main_count
                payload = final_response()
        if server.mode == "crash" and not summarizing:
            if current == 1:
                payload = tool_response("read-1")
            elif current == 2:
                require(any(m.get("role") == "tool" and "exact completed tool receipt" in m.get("content", "")
                            for m in messages), "blocked request does not carry completed tool receipt")
                server.blocked.set()
                server.release.wait(30)
            elif current == 3:
                require(sum(m.get("role") == "tool" for m in messages) == 1,
                        "cold model request duplicated the completed tool exchange")
                require(any(m.get("tool_call_id") == "read-1" and "exact completed tool receipt" in m.get("content", "")
                            for m in messages), "cold resume lost the exact completed tool receipt")
        if server.mode == "compact" and not summarizing:
            if current <= 2:
                payload = tool_response(f"read-{current}")
            elif current == 3:
                require(server.summary_count == 1, "large history did not trigger automatic semantic compact")
                require(any(t.get("function", {}).get("name") == "ahfl_session_history_get"
                            for t in request.get("tools", [])), "exact history tool not advertised")
                payload = tool_response("recover-fact", "ahfl_session_history_get", {"history_id": 3, "chunk_index": 4})
            elif current == 4:
                server.exact_history = any(m.get("tool_call_id") == "recover-fact" and
                    "EXACT-OLD-FACT-739" in m.get("content", "") for m in messages)
        body = json.dumps(payload).encode()
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class Case:
    def __init__(self, ahflc: Path, work: Path, mode: str, explicit: bool = False):
        self.ahflc = ahflc
        self.work = work
        work.mkdir(parents=True)
        self.home = work / "home"
        self.home.mkdir(mode=0o700)
        self.source = work / "smoke.ahfl"
        write_source(self.source)
        self.server = ScenarioServer(mode)
        self.config = work / "llm.json"
        self.settings = {"endpoint": f"http://127.0.0.1:{self.server.server_port}/v1",
            "model": "local-test", "api_key_secret": "env:AHFL_CONTEXT_SESSION_TEST_KEY", "max_retries": 0}
        if mode == "compact":
            self.settings.update(model_context_window_tokens=10000, model_max_output_tokens=1024, max_tokens=256)
        self.config.write_text(json.dumps(self.settings))
        self.explicit_path = work / "recovery.json" if explicit else None
        self.catalog: Path | None = None
        if mode in ("crash", "compact"):
            content = "exact completed tool receipt"
            if mode == "compact":
                content = "a" * 19000 + "EXACT-OLD-FACT-739" + "a" * 1000
            self.catalog = work / "tools.json"
            self.catalog.write_text(json.dumps({"format": "ahfl.llm_tool_catalog", "tools": [{
                "name": "lookup", "description": "Read the immutable business record",
                "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
                "result": content}]}))
        self.env = os.environ.copy()
        self.env.update(HOME=str(self.home), AHFL_CONTEXT_SESSION_TEST_KEY="not-a-real-credential")

    def command(self, value: str = "hello") -> list[str]:
        command = [str(self.ahflc), "run", "--workflow", "smoke::SmokeWorkflow", "--input",
            json.dumps({"_type": "smoke::Request", "value": value}), "--llm-config", str(self.config),
            "--output-format", "jsonl", "--verbosity", "trace"]
        if self.catalog:
            command += ["--tool-catalog", str(self.catalog)]
        if self.explicit_path:
            command += ["--recovery-store", str(self.explicit_path)]
        return command + [str(self.source)]

    def run(self, value: str = "hello") -> subprocess.CompletedProcess[str]:
        return subprocess.run(self.command(value), env=self.env, capture_output=True, text=True, timeout=30)

    def assert_success(self, result: subprocess.CompletedProcess[str], expected_usage: int | None = None) -> list[dict]:
        require(result.returncode == 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines() if line]
        require(events and events[-1].get("payload", {}).get("status") == "completed",
                "canonical terminal event is not completed: " + result.stdout + result.stderr)
        require("not-a-real-credential" not in result.stdout, "run events exposed a credential")
        completed = [event for event in events if event.get("type") == "capability_completed"]
        usage = [event for event in events if event.get("type") == "capability_usage_recorded"]
        require(len(completed) == 2 and len(usage) == 2,
                "terminal report lost or duplicated a completed LLM frontier: " + result.stdout)
        require(all(event["payload"]["cost_estimated"] is False for event in usage),
                "default config without prices reported estimated costs")
        require({event["payload"]["invocation_id"] for event in completed} ==
                {event["payload"]["invocation_id"] for event in usage},
                "usage is not attached to both completed invocations")
        if expected_usage is not None:
            totals = [event["payload"]["total_tokens"] for event in usage]
            require(totals == [expected_usage, expected_usage] and sum(totals) == expected_usage * 2,
                    "two LLM frontiers did not preserve their complete usage: " + result.stdout)
        return events

    def interrupt_after_receipt(self) -> None:
        process = subprocess.Popen(self.command(), env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            require(self.server.blocked.wait(20), "CLI did not reach request after durable tool completion")
            before = len(self.server.requests)
            busy = self.run()
            require(busy.returncode != 0 and "busy" in busy.stderr, "concurrent CLI run did not fail busy")
            require(len(self.server.requests) == before, "busy CLI made an external request")
        finally:
            process.kill()
            process.communicate(timeout=5)
            self.server.release.set()

    def session_root(self) -> Path:
        if self.explicit_path:
            return Path(str(self.explicit_path) + ".llm")
        active = list((self.home / ".ahfl" / "runs").glob("*/active.json"))
        require(len(active) == 1, "fixture has no unique managed active run")
        manifest = json.loads(active[0].read_text())
        return active[0].parent / str(manifest["namespace"]) / "llm"

    def close(self) -> None:
        self.server.close()


def assert_completed_receipt(case: Case) -> None:
    payload = json.loads(json.loads((case.session_root() / "1.json").read_text())["payload"])
    receipts = payload.get("receipts", [])
    require(len(receipts) == 1 and receipts[0].get("state") == 2,
            "interrupted session did not preserve one Completed tool receipt")
    require("exact completed tool receipt" in json.dumps(receipts), "durable receipt content is incomplete")


def main() -> int:
    require(len(sys.argv) == 3, "usage: llm_context_session_cli_smoke.py <ahflc> <work-dir>")
    ahflc = Path(sys.argv[1]).resolve()
    work_root = Path(sys.argv[2]).resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=work_root))
    automatic = Case(ahflc, work / "automatic", "plain")
    try:
        automatic.assert_success(automatic.run(), expected_usage=12)
        require(len(automatic.server.requests) == 2, "default run did not automatically resolve two LLM frontiers")
        automatic.assert_success(automatic.run(), expected_usage=12)
        require(len(automatic.server.requests) == 4, "completed same-input run reused an old session")
    finally:
        automatic.close()
    fresh_explicit = Case(ahflc, work / "fresh-explicit", "plain", explicit=True)
    try:
        fresh_explicit.assert_success(fresh_explicit.run(), expected_usage=12)
        require(not fresh_explicit.explicit_path.exists(), "fresh explicit success retained an internal checkpoint")
        require(not list(fresh_explicit.session_root().glob("[0-9]*.json")),
                "fresh explicit success retained completed LLM sessions")
        fresh_explicit.assert_success(fresh_explicit.run(), expected_usage=12)
        require(len(fresh_explicit.server.requests) == 4,
                "fresh explicit same-input invocation reused the completed run")
    finally:
        fresh_explicit.close()
    for explicit in (False, True):
        case = Case(ahflc, work / ("explicit" if explicit else "managed"), "crash", explicit)
        try:
            case.interrupt_after_receipt()
            assert_completed_receipt(case)
            if explicit:
                before = len(case.server.requests)
                checkpoint = case.explicit_path
                original_checkpoint = checkpoint.read_bytes()
                checkpoint.write_text("{invalid}")
                rejected = case.run()
                require(rejected.returncode != 0 and len(case.server.requests) == before,
                        "malformed recovery record made an HTTP request")
                checkpoint.write_bytes(original_checkpoint)
                record = case.session_root() / "1.json"
                original_record = record.read_bytes()
                record.write_text("{invalid}")
                rejected = case.run()
                require(rejected.returncode != 0 and len(case.server.requests) == before,
                        "malformed LLM session made an HTTP request")
                record.write_bytes(original_record)
                envelope = json.loads(original_record)
                envelope["hmac_sha256"] = "0" * 64
                record.write_text(json.dumps(envelope))
                rejected = case.run()
                require(rejected.returncode != 0 and len(case.server.requests) == before,
                        "HMAC mismatch made an HTTP request")
                record.write_bytes(original_record)
                rejected = case.run("changed-input")
                require(rejected.returncode != 0 and len(case.server.requests) == before,
                        "changed input resumed incompatible session or made an HTTP request")
                altered = dict(case.settings, temperature=0.2)
                case.config.write_text(json.dumps(altered))
                rejected = case.run()
                require(rejected.returncode != 0 and len(case.server.requests) == before,
                        "changed model config resumed incompatible session or made an HTTP request")
                case.config.write_text(json.dumps(case.settings))
            resumed_events = case.assert_success(case.run())
            require(len(case.server.requests) == 4, "cold CLI resume repeated earlier model rounds")
            resumed_usage = [event["payload"] for event in resumed_events
                             if event.get("type") == "capability_usage_recorded"]
            require([(item["prompt_tokens"], item["completion_tokens"]) for item in resumed_usage] ==
                    [(20, 4), (10, 2)],
                    "cold resume lost usage from the completed first tool/model round")
            require(sum(item["total_tokens"] for item in resumed_usage) >= 36,
                    "cold resume lost usage from an earlier successful model or tool round")
            if explicit:
                assert_completed_receipt(case)
                require(case.explicit_path.exists(), "explicit outer frontier disappeared after inner completion")
                sessions = [json.loads(json.loads(path.read_text())["payload"])
                            for path in case.session_root().glob("*.json") if path.stem.isdigit()]
                require(len(sessions) == 2 and all(session.get("completed") for session in sessions),
                        "cold resume did not retain both Completed inner sessions")
                require(sorted(session["total_tokens"] + session["reserved_tokens"] for session in sessions) ==
                        sorted(item["total_tokens"] for item in resumed_usage),
                        "terminal report differs from the durable session usage ledger")
                before = len(case.server.requests)
                manual = case.command()
                manual = manual[:-1] + ["--resume-pending-result", json.dumps({
                    "_type": "smoke::Response", "value": "manual-override"})] + manual[-1:]
                rejected = subprocess.run(manual, env=case.env, capture_output=True, text=True, timeout=30)
                require(rejected.returncode != 0 and "persisted LLM session owns" in rejected.stderr,
                        "completed inner session permitted overriding its stale outer frontier")
                require(len(case.server.requests) == before,
                        "manual override of completed inner session made an HTTP request")
                replay_events = case.assert_success(case.run())
                require(len(case.server.requests) == before,
                        "stale outer frontier repeated a Completed inner model session")
                replay_usage = [event["payload"] for event in replay_events
                                if event.get("type") == "capability_usage_recorded"]
                require(replay_usage == resumed_usage,
                        "completed cold replay lost, duplicated or changed persisted usage metadata")
        finally:
            case.close()
    compact = Case(ahflc, work / "compact", "compact")
    try:
        compact.assert_success(compact.run())
        require(compact.server.summary_count == 1 and compact.server.exact_history,
                "automatic compact did not retain readable exact old history")
        require(compact.server.main_count == 5, "compact did not complete inner tools and second workflow frontier")
    finally:
        compact.close()
    print("LLM context and session CLI smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
