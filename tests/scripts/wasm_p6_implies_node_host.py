#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) Node embedded-engine evidence for `=>` lowering.

The producer compiles the real-frontend boolean implication fixture through
the full AHFL->Core->P4-D->wasm pipeline. This script instantiates the emitted
module with the Node v22 WebAssembly engine and drives the stable step() ABI,
asserting the wasm state-id path matches the frozen oracle captured at HEAD
620fadd8 (WH-9 B0). The fixture is true only under the Sema shape
`a => b == !a || b`: a miscompile to `&&`, plain `||`, or lhs-only routes to
the opposite branch. SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Frozen oracle (WH-9 B0, HEAD 620fadd8): the wasm state-id path the producer
# emitted at freeze time. The tree-walking evaluator that previously produced
# this observation was retired in WH-9; the constant is the frozen oracle.
FROZEN_OBS = {
    "status": "completed",
    "entered_ids": [3, 1, 0],
    "final_state_id": 0,
    "transition_count": 2,
    "initial_state_id": 3,
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_implies_node_host.py <p6-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or implies fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-implies-") as td:
        td_path = Path(td)
        wasm_path = td_path / "p6_implies.wasm"

        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"P6 producer exited {compile_run.returncode}: {compile_run.stderr}")
        obs = FROZEN_OBS

        host = td_path / "p6_implies_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

const bytes = fs.readFileSync(process.argv[2]);
const module_ = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module_).length !== 0)
  throw new Error("implies agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module_, {});
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
const observedPath = sequence.slice(0, expectedSequence.length);
if (JSON.stringify(observedPath) !== JSON.stringify(expectedSequence))
  throw new Error(`implies state-id path ${observedPath} != oracle ${expectedSequence}`);
if (c.transition_count.value !== expectedTransitions)
  throw new Error(`transition_count ${c.transition_count.value} != ${expectedTransitions}`);

console.log("P6 implies Node step execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(wasm_path),
                str(obs["initial_state_id"]),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node implies host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: CORE-GAPS implies Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
