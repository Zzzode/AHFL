#!/usr/bin/env python3
"""RFC 0026 FB-1 (CORE-FNBODY-DESIGN) Node embedded-engine execution evidence.

The producer compiles a real-frontend fixture whose non-final handler makes
STATICALLY-RESOLVED PURE direct calls (a non-generic fn, a monomorphized
generic fn, and an fn that calls another fn). The outlined fn bodies are real
wasm functions invoked with a plain `call` (env=0); the scalar return drives a
computed goto. This script compiles the emitted module with the Node v22
WebAssembly engine and drives the stable step() ABI, asserting the reached
state ids and transition_count match the wasm-path expectation frozen at HEAD
620fadd8 (WH-9 B0). This fixture was NOT executable by the tree-walking
evaluator (its native token in the B0 capture is status=failed), so the
frozen value is a wasm-path expectation derived at freeze time and backed by
the checked-in conformance manifest, not a captured native observation. Real
Node-engine evidence, NOT wasmtime evidence. SKIP (77) when node is
unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Frozen oracle (WH-9 B0, HEAD 620fadd8): the wasm-path expectation. The
# retired evaluator could not execute user-defined direct fn calls; the wasm
# path is the authoritative engine evidence. The fixture declares exactly two
# states [Init, Done], so Done = initial+1 = 1.
FROZEN_OBS = {
    "status": "completed",
    "entered_ids": [0, 1],
    "final_state_id": 1,
    "transition_count": 1,
    "initial_state_id": 0,
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        return fail("usage: wasm_fb1_direct_call_node_host.py <producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-1 embedded-host execution")
        return SKIP
    producer = Path(argv[1])
    source = Path(argv[2])
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-1 producer or fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb1-node-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb1_direct_call.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"FB-1 producer exited {compile_run.returncode}: {compile_run.stderr}")
        obs = FROZEN_OBS

        host = td_path / "fb1_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure direct-call agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const expectedInitial = Number(process.argv[3]);
const expectedEntered = process.argv[4].split(",").map(Number);
const expectedSequence = expectedEntered.slice(1);
const expectedTransitions = Number(process.argv[5]);

if (expectedEntered[0] !== expectedInitial)
  throw new Error("frozen initial id does not match initial_state_id");
if (c.current_state() !== expectedInitial)
  throw new Error(`initial state ${c.current_state()} != ${expectedInitial}`);

const sequence = [];
let previous = expectedInitial;
let guard = expectedSequence.length + 2;
while (guard-- > 0) {
  const before = c.transition_count.value;
  const next = c.step();
  sequence.push(next);
  if (next !== previous) {
    if (c.current_state() !== next)
      throw new Error("current_state does not match step result");
    if (c.transition_count.value !== before + 1)
      throw new Error("transition_count not bumped exactly once per goto");
    previous = next;
    continue;
  }
  if (c.current_state() !== next)
    throw new Error("final state is not stable");
  break;
}
const observed = sequence.slice(0, expectedSequence.length);
if (JSON.stringify(observed) !== JSON.stringify(expectedSequence))
  throw new Error(`state-id path ${observed} != oracle ${expectedSequence}`);
if (c.transition_count.value !== expectedTransitions)
  throw new Error(`transition_count ${c.transition_count.value} != ${expectedTransitions}`);

// The identity final forwards the borrowed input frame through legacy run().
const input = new TextEncoder().encode("identity");
const ptr = c.alloc(input.length);
new Uint8Array(c.memory.buffer, ptr, input.length).set(input);
const outPtr = c.run(ptr, input.length);
const expectedFinal = expectedSequence[expectedSequence.length - 1];
if (outPtr !== ptr || c.current_state() !== expectedFinal ||
    c.transition_count.value !== expectedTransitions)
  throw new Error("legacy run() did not reach the same final state");
c.dealloc(ptr, input.length);
''')

        args = [
            node, str(host), str(wasm_path),
            str(obs["initial_state_id"]),
            ",".join(str(x) for x in obs["entered_ids"]),
            str(obs["transition_count"]),
        ]
        driven = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-1 embedded-host execution failed: {driven.stderr}")

    print("FB-1 direct-call Node embedded-engine execution passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
