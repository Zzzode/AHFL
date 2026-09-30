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
  4. (when a repo root is passed) the shipped examples/durable-resume package
     round-trips end to end via --manifest.
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
struct Response { value: String; }

capability Echo(request: Request) -> Response;

agent EchoAgent {
    input: Request;
    context: Unit;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

flow for EchoAgent {
    state Init { goto Done; }
    state Done { return Echo(Request { value: input.value }); }
}

workflow SmokeWorkflow {
    input: Request;
    output: Response;
    node only: EchoAgent(input);
    return: Response { value: only.value };
}
"""


# RFC 0026 C2b G4c: a package fixture whose pending capability returns a RICH
# struct (Int / Float / Option<Int> / Unit alongside a String), so a real
# suspend -> resume round-trip proves the raw --resume-pending-result bytes are
# decoded EXACTLY under the capability's verified wire binding (schema-guided
# codec) and flow through to a Completed workflow output. std::option needs an
# import, which needs a package manifest + std sysroot (a detached single file
# cannot express it).
_RICH_MANIFEST = """manifest_version = 1

[package]
name = "g4c-durable-rich"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "smoke"
root = "."

[targets.workflow]
kind = "handoff"
entry = "smoke::main::RichWorkflow"
exports = [
  { kind = "workflow", name = "smoke::main::RichWorkflow" },
  { kind = "agent", name = "smoke::main::RichAgent" },
]

[dependencies]
std = { source = "sysroot" }
"""

_RICH_SOURCE = """module smoke::main;

import std::option as option;

pub struct Request { value: String; }
pub struct RichReply {
    value: String;
    count: Int;
    ratio: Float;
    note: option::Option<Int>;
    nothing: Unit;
}

pub capability DraftReply(request: Request) -> RichReply;

pub agent RichAgent {
    input: Request;
    context: Unit;
    output: RichReply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [DraftReply];
    transition Init -> Done;
}

flow for RichAgent {
    state Init {
        goto Done;
    }
    state Done { return DraftReply(Request { value: input.value }); }
}

pub workflow RichWorkflow {
    input: Request;
    output: RichReply;
    node only: RichAgent(input);
    return: only;
}
"""


def sysroot_root() -> Path:
    # std/ahfl.toml lives at the repo root; this script is at
    # <repo>/tests/scripts/durable_resume_cli_smoke.py.
    repo_root = Path(__file__).resolve().parents[2]
    if not (repo_root / "std" / "ahfl.toml").is_file():
        raise AssertionError(f"std sysroot manifest not found under {repo_root}")
    return repo_root


def write_rich_package(pkg_dir: Path) -> None:
    pkg_dir.mkdir(parents=True, exist_ok=True)
    (pkg_dir / "ahfl.toml").write_text(_RICH_MANIFEST, encoding="utf-8")
    (pkg_dir / "main.ahfl").write_text(_RICH_SOURCE, encoding="utf-8")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: durable_resume_cli_smoke.py <ahflc> <work-dir> [<repo-root>]",
              file=sys.stderr)
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

        # (2c): a RICH schema-guided round-trip. The pending capability returns a
        # struct carrying Int / Float / Option<Int> / Unit; on resume the raw
        # --resume-pending-result object is decoded EXACTLY under that binding and
        # the workflow completes with the deep result. Proves the G4c raw path runs
        # decode_json (not the old schema-free materialization) end to end.
        rich_pkg = work / "rich_pkg"
        write_rich_package(rich_pkg)
        rich_snapshot = work / "rich.snapshot"
        if rich_snapshot.exists():
            rich_snapshot.unlink()
        rich_base = [
            str(ahflc), "run",
            "--manifest", str(rich_pkg / "ahfl.toml"),
            "--target", "workflow",
            "--sysroot", str(sysroot_root()),
            "--workflow", "smoke::main::RichWorkflow",
            "--input", '{"_type":"smoke::main::Request","value":"hi"}',
            "--llm-config", str(config),
            "--recovery-store", str(rich_snapshot),
            "--output-format", "json",
        ]
        rich_suspend = subprocess.run(
            rich_base + ["--suspend-capability", "smoke::main::DraftReply"],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(rich_suspend.returncode == 0,
                f"rich suspend should exit 0, got {rich_suspend.returncode}: {rich_suspend.stderr}")
        require(rich_snapshot.exists(), "rich suspend must persist a resume record")
        require(json.loads(rich_suspend.stdout)["audit"]["workflow_completed"] == 0,
                "rich suspend must not complete")
        # Resume with a raw object exercising every rich shape: Int, Float 1.0,
        # Option<Int> None, Unit null, alongside the String.
        rich_resume = subprocess.run(
            rich_base + ["--resume-pending-result",
                         '{"_type":"smoke::main::RichReply","value":"rich-ok","count":5,'
                         '"ratio":1.0,"note":null,"nothing":null}'],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(rich_resume.returncode == 0,
                f"rich resume should exit 0, got {rich_resume.returncode}: {rich_resume.stderr}")
        rich_report = json.loads(rich_resume.stdout)
        require(rich_report["audit"]["workflow_completed"] == 1, "rich resume must complete")
        rich_out = rich_report.get("result", rich_report.get("output"))
        # Deep field check. value_to_json folds Float 1.0 -> 1 and Unit/None -> null
        # (P0-16), so the completed decode itself is the schema-guided evidence; the
        # fields still deep-match the injected object under that canonical folding.
        require(rich_out == {
            "_type": "smoke::main::RichReply",
            "value": "rich-ok",
            "count": 5,
            "ratio": 1,
            "note": None,
            "nothing": None,
        }, f"rich resume output mismatch, got {rich_out}")

        # (2d): the SAME rich fixture with Option<Int> Some(9), proving the Some
        # variant decodes through the raw path. Full suspend->resume authority chain
        # + deep-equal (not sparse .get()) so a missing suspend / field drift fails.
        rich_snapshot_some = work / "rich_some.snapshot"
        if rich_snapshot_some.exists():
            rich_snapshot_some.unlink()
        rich_base_some = [
            str(ahflc), "run",
            "--manifest", str(rich_pkg / "ahfl.toml"),
            "--target", "workflow",
            "--sysroot", str(sysroot_root()),
            "--workflow", "smoke::main::RichWorkflow",
            "--input", '{"_type":"smoke::main::Request","value":"hi"}',
            "--llm-config", str(config),
            "--recovery-store", str(rich_snapshot_some),
            "--output-format", "json",
        ]
        some_suspend = subprocess.run(
            rich_base_some + ["--suspend-capability", "smoke::main::DraftReply"],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(some_suspend.returncode == 0,
                f"rich Some suspend should exit 0, got {some_suspend.returncode}: "
                f"{some_suspend.stderr}")
        require(rich_snapshot_some.exists(), "rich Some suspend must persist a resume record")
        require(json.loads(some_suspend.stdout)["audit"]["workflow_completed"] == 0,
                "rich Some suspend must not complete")
        rich_resume_some = subprocess.run(
            rich_base_some + ["--resume-pending-result",
                              '{"_type":"smoke::main::RichReply","value":"some-ok","count":1,'
                              '"ratio":2.5,"note":9,"nothing":null}'],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(rich_resume_some.returncode == 0,
                f"rich Some resume should exit 0, got {rich_resume_some.returncode}: "
                f"{rich_resume_some.stderr}")
        some_report = json.loads(rich_resume_some.stdout)
        require(some_report["audit"]["workflow_completed"] == 1, "rich Some resume must complete")
        some_out = some_report.get("result", some_report.get("output"))
        require(some_out == {
            "_type": "smoke::main::RichReply",
            "value": "some-ok",
            "count": 1,
            "ratio": 2.5,
            "note": 9,
            "nothing": None,
        }, f"rich Some resume output mismatch, got {some_out}")

        # (3): a malformed --resume-pending-result is rejected by the Phase A.3
        # syntax admission (before secrets/network) with a NONZERO exit and a
        # targeted diagnostic. The public process exit is 1: run_workflow_with_llm
        # returns 2 internally, but cli_driver maps every nonzero to
        # ExitCode::CompileError — so we assert nonzero, not == 2.
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
        # The failure is the A3 admission error, not a downstream runtime/schema/
        # not-found diagnostic (no descriptor / no snapshot were involved).
        bad_combined = bad.stdout + bad.stderr
        for competing in ("not found in program", "does not match workflow input schema",
                          "pending-call identity mismatch"):
            require(competing not in bad_combined,
                    f"malformed pending leaked competing diagnostic {competing!r}:\n{bad_combined}")

        # (3b): a DUPLICATE object key in --resume-pending-result is also rejected by
        # the Phase A.3 syntax admission (parse_json rejects duplicate keys), same
        # nonzero targeted contract — no recovery store needed. A distinctive token
        # in the payload proves the fixed admission diagnostic does not echo it.
        dup_token = "G4C_DUP_KEY_SECRET"
        dup = subprocess.run(
            [
                str(ahflc), "run",
                "--workflow", "smoke::SmokeWorkflow",
                "--input", '{"_type":"smoke::Request","value":"hi"}',
                "--llm-config", str(config),
                "--resume-pending-result",
                '{"value":"' + dup_token + '","value":"other"}',
                str(source),
            ],
            env=env, check=False, capture_output=True, text=True, timeout=30,
        )
        require(dup.returncode != 0,
                f"duplicate-key --resume-pending-result should fail, got exit {dup.returncode}")
        require("resume-pending-result" in dup.stderr,
                "duplicate-key resume-pending-result must produce a targeted diagnostic")
        dup_combined = dup.stdout + dup.stderr
        require(dup_token not in dup_combined,
                f"duplicate-key admission diagnostic must not echo the payload token:\n{dup_combined}")
        for competing in ("not found in program", "does not match workflow input schema",
                          "pending-call identity mismatch"):
            require(competing not in dup_combined,
                    f"duplicate-key pending leaked competing diagnostic {competing!r}:\n"
                    f"{dup_combined}")

        # (4): the shipped examples/durable-resume package round-trips end to end.
        if len(sys.argv) >= 4:
            repo = Path(sys.argv[3])
            manifest = repo / "examples" / "durable-resume" / "ahfl.toml"
            wf = "durable_resume::main::ReplyWorkflow"
            ex_snap = work / "example.snapshot"
            if ex_snap.exists():
                ex_snap.unlink()
            ex_intent = work / "example.intent.jsonl"
            if ex_intent.exists():
                ex_intent.unlink()
            ex_base = [
                str(ahflc), "run", "--manifest", str(manifest), "--workflow", wf,
                "--llm-config", str(config), "--recovery-store", str(ex_snap),
                "--intent-log", str(ex_intent),
                "--output-format", "json",
            ]
            ex_suspend = subprocess.run(
                ex_base + ["--suspend-capability", "durable_resume::main::DraftReply"],
                env=env, check=False, capture_output=True, text=True, timeout=30,
            )
            require(ex_suspend.returncode == 0,
                    f"example suspend should exit 0, got {ex_suspend.returncode}: {ex_suspend.stderr}")
            require(ex_snap.exists(), "example suspend must write a resume record")
            require(json.loads(ex_suspend.stdout)["audit"]["workflow_completed"] == 0,
                    "example suspend must not complete")
            # Exactly-once: the durable_write intent is logged once, before dispatch.
            intents_after_suspend = [
                line for line in ex_intent.read_text().splitlines() if line.strip()
            ] if ex_intent.exists() else []
            require(len(intents_after_suspend) == 1,
                    f"suspend must log exactly one write-ahead intent, got {intents_after_suspend}")
            first_intent = json.loads(intents_after_suspend[0])
            require(first_intent.get("capability") == "durable_resume::main::DraftReply"
                    and first_intent.get("idempotency_key"),
                    f"intent record must name the capability + a key, got {first_intent}")
            ex_resume = subprocess.run(
                ex_base + ["--resume-pending-result",
                           '{"_type":"durable_resume::main::Reply","id":"T-42",'
                           '"answer":"Use the reset link."}'],
                env=env, check=False, capture_output=True, text=True, timeout=30,
            )
            require(ex_resume.returncode == 0,
                    f"example resume should exit 0, got {ex_resume.returncode}: {ex_resume.stderr}")
            ex_report = json.loads(ex_resume.stdout)
            require(ex_report["audit"]["workflow_completed"] == 1, "example resume must complete")
            ex_out = ex_report.get("result", ex_report.get("output"))
            require(ex_out is not None and ex_out.get("answer") == "Use the reset link.",
                    f"example resume output must be the injected reply, got {ex_out}")
            # Exactly-once across resume: the memoized durable_write is replayed, not
            # re-dispatched, so NO second intent is written.
            intents_after_resume = [
                line for line in ex_intent.read_text().splitlines() if line.strip()
            ]
            require(len(intents_after_resume) == 1,
                    f"resume must NOT re-log the intent (memo replay), got {intents_after_resume}")
    finally:
        server.shutdown()
        server.server_close()

    print("durable-resume CLI smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
