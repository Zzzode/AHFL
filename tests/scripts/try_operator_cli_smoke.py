#!/usr/bin/env python3
"""Exercise typed try propagation through check, Core, build and wasm3 run."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    ahflc = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    manifest = root / "tests/integration/try_operator/ahfl.toml"
    common = ["--manifest", str(manifest), "--sysroot", str(root)]
    env = os.environ.copy()
    env["AHFL_TEST_TRY_OPERATOR_KEY"] = "unused"

    def invoke(*args: str) -> str:
        completed = subprocess.run(
            [str(ahflc), *args], capture_output=True, text=True,
            env=env, timeout=60,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"{args[0]} failed ({completed.returncode}):\n"
                f"{completed.stdout}\n{completed.stderr}"
            )
        return completed.stdout

    # The boundaries target includes String errors; check exercises the actual
    # CLI pipeline rather than stopping after semantic analysis. Execution
    # imports both tutorial recipes rather than duplicating their bodies.
    for target in ("option", "result", "boundaries", "workflow"):
        invoke("check", *common, "--target", target)

    core = json.loads(invoke("emit", "core-ir-json", *common, "--target", "workflow"))
    if not core:
        raise AssertionError("Core emission produced an empty artifact")

    with tempfile.TemporaryDirectory(prefix="ahfl-try-cli-") as td:
        work = Path(td)
        wasm = work / "try.wasm"
        invoke("build", *common, "--target", "workflow", "--output", str(wasm))
        if not wasm.read_bytes().startswith(b"\x00asm"):
            raise AssertionError("build did not produce a WebAssembly module")
        config = work / "llm.json"
        config.write_text(json.dumps({
            "endpoint": "http://127.0.0.1:1/v1", "model": "unused",
            "api_key_secret": "AHFL_TEST_TRY_OPERATOR_KEY", "max_retries": 0,
        }), encoding="utf-8")

        # Both divisions would fail: the returned code must preserve the first
        # failure rather than replacing it with the second operation's error.
        cases = (
            (24, 3, 2, False, 24 * 65536 + 3 * 256 + 2, "Ok", 4),
            (24, 3, 2, True, None, "Ok", 4),
            (24, 0, 0, False, 24 * 65536, "Err", 1),
            (24, 3, 0, False, 24 * 65536 + 3 * 256, "Err", 2),
        )
        for a, b, c, missing, rgb, variant, payload in cases:
            request = {
                "_type": "try_operator::execution::Request",
                "a": a, "b": b, "c": c, "missing_r": missing,
            }
            report = json.loads(invoke(
                "run", *common, "--target", "workflow",
                "--input", json.dumps(request), "--llm-config", str(config),
                "--output-format", "json",
            ))
            if report["audit"]["workflow_completed"] != 1:
                raise AssertionError(f"workflow did not complete: {report}")
            if report["audit"]["node_completed"] != 3:
                raise AssertionError(f"all three nodes must execute: {report}")
            expected = {
                "_type": "try_operator::execution::Response",
                "rgb": rgb, "flag": missing,
                "closure_sum": a + b + 2 * c,
                "quotient": {
                    "_enum": "std::result::Result", "_variant": variant,
                    "_payload": [payload],
                },
            }
            actual = report.get("result", report.get("output"))
            if actual != expected:
                raise AssertionError(f"input {request}: expected {expected}, got {actual}")

    print("try operator check, Core, build and wasm3 execution passed")


if __name__ == "__main__":
    main()
