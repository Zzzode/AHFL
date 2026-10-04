#!/usr/bin/env python3
r"""RFC 0026 FB-3b (CORE-FNBODY-DESIGN §3.4 ByValue) Node evidence.

Two distinct capturing closures (`\x -> x + a` with a=10 and `\x -> x + b`
with b=20) are each applied indirectly. The two environments must be separate
runtime-bumped aggregates holding the construction-time snapshot: an env
aliasing or overwrite defect makes the results disagree with 11 / 21 and
routes the computed goto to Fail. The Node host drives the stable step() ABI
and asserts Done. Real Node-engine evidence, NOT wasmtime. SKIP (77) when node
is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Frozen oracle (WH-9 B0, HEAD 620fadd8): the wasm-path initial_state_id.
FROZEN_INITIAL_STATE_ID = 0


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb3_byvalue_capture_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-3 ByValue capture execution")
        return SKIP
    producer = Path(argv[0])
    source = (Path(__file__).resolve().parents[1] / "golden" / "wasm"
              / "fb3_byvalue_capture.ahfl")
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-3 producer or fb3_byvalue_capture.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb3-byvalue-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb3_byvalue_capture.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"FB-3 producer exited {compile_run.returncode}: {compile_run.stderr}")
        initial = FROZEN_INITIAL_STATE_ID

        host = td_path / "fb3_byvalue_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const initial = Number(process.argv[2]);
const {instance} = await WebAssembly.instantiate(
    fs.readFileSync(process.argv[3]), {});
const c = instance.exports;
if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);
// Two capturing closures with distinct snapshots must both route to Done;
// the fixture's branch checks fa(1)==11 AND fb(1)==21, so any env aliasing
// or stale-snapshot defect lands on Fail.
const next = c.step();
if (next !== 1)
  throw new Error(`ByValue capture routed to state ${next}, expected Done(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log("FB-3 ByValue capture isolation executed");
""",
            encoding="utf-8",
        )
        driven = subprocess.run([node, str(host), str(initial), str(wasm_path)],
                               capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-3 ByValue host failed: {driven.stderr}")

    print("OK: RFC 0026 FB-3 ByValue capture isolation Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
