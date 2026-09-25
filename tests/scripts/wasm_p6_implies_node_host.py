#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) Node embedded-engine evidence for `=>` lowering.

The producer compiles the real-frontend boolean implication fixture through
the full AHFL->Core->P4-D->wasm pipeline and reports the native AgentRuntime
state-id sequence. This script instantiates the emitted module with the Node
v22 WebAssembly engine and drives the stable step() ABI, asserting the wasm
state-id path matches native. The fixture is true only under the Sema shape
`a => b == !a || b`: a miscompile to `&&`, plain `||`, or lhs-only routes to
the opposite branch. SKIP (77) when node is unavailable.
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
    return {
        "status": match.group(1),
        "entered_ids": [int(x) for x in match.group(2).split(",") if x],
        "final_state_id": int(match.group(3)),
        "transition_count": int(match.group(4)),
        "initial_state_id": int(match.group(5)),
    }


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

        native = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"P6 producer exited {native.returncode}: {native.stderr}")
        obs = parse_observation(native.stdout.strip())
        if obs["status"] != "completed" or not obs["entered_ids"]:
            return fail(f"native implies run did not complete: {obs}")

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
  throw new Error("native initial id does not match initial_state_id");
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
  throw new Error(`implies state-id path ${observedPath} != native ${expectedSequence}`);
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
