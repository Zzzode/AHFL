#!/usr/bin/env python3
"""RFC 0026 P6-1 (KR6.6) Node embedded-engine execution evidence.

The producer compiles one real-frontend arithmetic conditional-goto fixture
(and one integer divide-by-zero trap fixture) through the full
AHFL->Core->P4-D->wasm pipeline and reports the native AgentRuntime state-id
sequence. This script instantiates the emitted modules with the Node v22
WebAssembly engine and drives the stable step() ABI: the computed-goto
handler's scalar result is observable purely through the state ids step()
moves to and transition_count, with no frame encoding or import callback.

This is embedded-engine evidence, NOT wasmtime evidence. SKIP (77) when the
node interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_observation(stdout: str) -> dict[str, object]:
    match = re.search(
        r"native_status=(\w+) entered_ids=([0-9,]*) "
        r"final_state_id=(-?\d+) transition_count=(\d+) "
        r"initial_state_id=(\d+)",
        stdout,
    )
    if match is None:
        raise ValueError(f"malformed native observation: {stdout!r}")
    entered = [int(x) for x in match.group(2).split(",") if x]
    return {
        "status": match.group(1),
        "entered_ids": entered,
        "final_state_id": int(match.group(3)),
        "transition_count": int(match.group(4)),
        "initial_state_id": int(match.group(5)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail(
            "usage: wasm_p6_node_host.py <p6-producer> <cond-source> "
            "(trap source is derived alongside as p6_scalar_trap.ahfl)"
        )
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    cond_source = Path(argv[1])
    trap_source = cond_source.with_name("p6_scalar_trap.ahfl")
    if not producer.is_file() or not cond_source.is_file() or not trap_source.is_file():
        return fail("missing P6 producer or scalar fixture(s)")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-node-") as td:
        td_path = Path(td)
        cond_wasm = td_path / "p6_cond.wasm"
        trap_wasm = td_path / "p6_trap.wasm"

        cond_native = subprocess.run(
            [str(producer), str(cond_source), str(cond_wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if cond_native.returncode != 0:
            return fail(f"P6 cond producer exited {cond_native.returncode}: "
                        f"{cond_native.stderr}")
        cond_obs = parse_observation(cond_native.stdout.strip())
        if cond_obs["status"] != "completed" or not cond_obs["entered_ids"]:
            return fail(f"native cond run did not complete: {cond_obs}")

        trap_native = subprocess.run(
            [str(producer), str(trap_source), str(trap_wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if trap_native.returncode != 0:
            return fail(f"P6 trap producer exited {trap_native.returncode}: "
                        f"{trap_native.stderr}")
        trap_obs = parse_observation(trap_native.stdout.strip())

        host = td_path / "p6_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

function load(path) {
  return fs.readFileSync(path);
}

// --- arithmetic conditional goto, driven only through step() state ids ---
const condBytes = load(process.argv[2]);
const condModule = await WebAssembly.compile(condBytes);
if (WebAssembly.Module.imports(condModule).length !== 0)
  throw new Error("scalar agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(condModule, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const expectedInitial = Number(process.argv[3]);
// The native observation includes the initial state before any transition;
// step() results begin AFTER the first transition, so the wasm path is the
// entered-id sequence minus its leading initial id.
const expectedEntered = process.argv[4].split(",").map(Number);
const expectedSequence = expectedEntered.slice(1);
const expectedTransitions = Number(process.argv[5]);

if (expectedEntered[0] !== expectedInitial)
  throw new Error("native initial id does not match initial_state_id");
if (c.current_state() !== expectedInitial)
  throw new Error(`initial state ${c.current_state()} != ${expectedInitial}`);
const sequence = [];
let previous = expectedInitial;
let guard = expectedSequence.length + 2;
// Drive step() until it reports a stable final state (step returns the same
// id twice). The native observation is the exact expected id sequence.
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
  // Stable final state: no further transition.
  if (c.current_state() !== next)
    throw new Error("final state is not stable");
  break;
}
const observedPath = sequence.slice(0, expectedSequence.length);
if (JSON.stringify(observedPath) !== JSON.stringify(expectedSequence))
  throw new Error(`state-id path ${observedPath} != native ${expectedSequence}`);
if (c.transition_count.value !== expectedTransitions)
  throw new Error(`transition_count ${c.transition_count.value} != ${expectedTransitions}`);

// The legacy run() ABI must reach the same final id with the same counter.
const input = new TextEncoder().encode("identity");
const ptr = c.alloc(input.length);
new Uint8Array(c.memory.buffer, ptr, input.length).set(input);
const outPtr = c.run(ptr, input.length);
if (outPtr !== ptr || c.current_state() !== expectedSequence[expectedSequence.length - 1] ||
    c.transition_count.value !== expectedTransitions)
  throw new Error("legacy run() did not reach the same final state");
c.dealloc(ptr, input.length);

// --- integer divide-by-zero: step() traps, state and counter unchanged ---
const trapBytes = load(process.argv[6]);
const {exports: t} = await WebAssembly.instantiate(
  await WebAssembly.compile(trapBytes), {});
const trapInitial = t.current_state();
const trapCount = t.transition_count.value;
let trapped = false;
try {
  t.step();
} catch (error) {
  trapped = error instanceof WebAssembly.RuntimeError;
}
if (!trapped)
  throw new Error("division by zero did not trap");
if (t.current_state() !== trapInitial || t.transition_count.value !== trapCount)
  throw new Error("trap mutated state or transition_count");

console.log("P6 scalar Node step/run execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(cond_wasm),
                str(cond_obs["initial_state_id"]),
                ",".join(str(x) for x in cond_obs["entered_ids"]),
                str(cond_obs["transition_count"]),
                str(trap_wasm),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node P6 host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
        # The trap fixture's native run fails (the evaluator reports division
        # by zero); the wasm engine traps. Both fail the same handler without a
        # transition, which is the cross-engine agreement the test asserts.
        if trap_obs["status"] != "failed" or trap_obs["transition_count"] != 0:
            return fail(f"native trap fixture unexpectedly non-failing: {trap_obs}")

    print("OK: RFC 0026 P6-1 scalar Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
