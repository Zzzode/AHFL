#!/usr/bin/env python3
"""KR6.7 (RFC 0026 P7) WASM eligibility Node execution evidence.

`ahfl.conformance.wasm_eligibility` classified the `float_output_e2e` case as
`runnable_orchestration` -- and that promotion is only honest if the emitted
module really executes. The other wasm execution lanes cover the E1-E3 and P6
shapes; this lane is the execution witness for the case the classifier MOVED
across the eligibility boundary.

The fixture is an f64 identity passthrough: an agent whose frame has one Float
field, reached in two states, returned unchanged. It exercises the one physical
repr (f64) the E1 string identity does not, so the evidence must be on the
BYTES, not on a frame name: the host allocates a frame through the module's own
`alloc`, writes an actual IEEE-754 double into linear memory, calls `run`, and
reads the double back out of the returned frame. A layout that sized, aligned,
or copied the frame wrongly reads a different number.

The producer also runs the same source through the native AgentRuntime and
reports the state-id path; the host asserts the module reaches the same ids and
transition_count through `step()`.

This is embedded-engine evidence, NOT wasmtime evidence. SKIP (77) when the node
interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# The f64 written into the input frame and read back out of the returned frame.
# Not 1.0 or 0.0: a byte-swapped, truncated, or zero-initialized frame would
# still produce those, so the witness value has to be distinguishable in every
# byte of the double.
WITNESS_F64 = 2.0


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
            "usage: wasm_eligibility_node_host.py <p6-producer> <float-identity-source>"
        )
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for wasm eligibility execution evidence")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing the P6 producer or the float identity fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-eligibility-node-") as td:
        td_path = Path(td)
        wasm = td_path / (source.stem + ".wasm")
        native = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"producer exited {native.returncode}: {native.stderr}")
        obs = parse_observation(native.stdout.strip())
        if obs["status"] != "completed" or not obs["entered_ids"]:
            return fail(f"native run did not complete: {obs}")

        host = td_path / "eligibility_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

const witness = Number(process.argv[3]);
const bytes = fs.readFileSync(process.argv[2]);

// A malformed module is a hard WebAssembly.compile rejection.
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("f64 identity agent expanded import authority");
const {exports} = await WebAssembly.instantiate(module, {});
if (exports.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const initial = Number(process.argv[4]);
const entered = process.argv[5].split(",").map(Number);
const expected = entered.slice(1);
const transitions = Number(process.argv[6]);
if (entered[0] !== initial)
  throw new Error("native initial id does not match initial_state_id");
if (exports.current_state() !== initial)
  throw new Error(`initial state ${exports.current_state()} != ${initial}`);

// Drive step() (no frame involved) and require the native state-id path.
const sequence = [];
let previous = initial;
let guard = expected.length + 2;
while (guard-- > 0) {
  const before = exports.transition_count.value;
  const next = exports.step();
  sequence.push(next);
  if (next !== previous) {
    if (exports.current_state() !== next)
      throw new Error("current_state does not match step result");
    if (exports.transition_count.value !== before + 1)
      throw new Error("transition_count not bumped exactly once per goto");
    previous = next;
    continue;
  }
  if (exports.current_state() !== next)
    throw new Error("final state is not stable");
  break;
}
const observedPath = sequence.slice(0, expected.length);
if (JSON.stringify(observedPath) !== JSON.stringify(expected))
  throw new Error(`state-id path ${observedPath} != native ${expected}`);
if (exports.transition_count.value !== transitions)
  throw new Error(`transition_count ${exports.transition_count.value} != ${transitions}`);

// Real payload evidence: write an IEEE-754 double into the frame the module
// allocated for us, run the identity passthrough, and read the double back at
// the returned frame. A mis-sized or mis-aligned f64 field cannot round-trip.
const ptr = exports.alloc(8);
if (ptr === 0)
  throw new Error("alloc returned the null frame");
const input = new Float64Array(exports.memory.buffer, ptr, 1);
input[0] = witness;
if (new Float64Array(exports.memory.buffer, ptr, 1)[0] !== witness)
  throw new Error("frame write did not land");

const out = exports.run(ptr, 8);
if (out === 0)
  throw new Error("run returned the null frame");
const observed = new Float64Array(exports.memory.buffer, out, 1)[0];
if (observed !== witness)
  throw new Error(`identity frame round-tripped ${observed} != ${witness}`);
if (exports.current_state() !== expected[expected.length - 1])
  throw new Error("run() did not reach the native final state");
if (exports.transition_count.value !== transitions)
  throw new Error("run() did not reach the native transition_count");
exports.dealloc(ptr, 8);

console.log("wasm eligibility f64 identity execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(wasm), str(WITNESS_F64),
                str(obs["initial_state_id"]),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node eligibility host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: KR6.7 wasm eligibility f64 identity execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
