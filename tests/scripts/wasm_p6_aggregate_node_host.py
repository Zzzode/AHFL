#!/usr/bin/env python3
"""RFC 0026 P6-4 (KR6.6) aggregate-memory Node execution evidence.

P6-4 gives an aggregate value (a struct, or an enum with a payload) a runtime
representation: an i32 ADDRESS into the module's private linear memory pointing
at its P4-D shaped bytes. Three module-owned regions are reserved below the bump
heap (`core_wasm_abi_constants.hpp`), so the compiler, the emitted module, and
this host all derive their addresses from ONE place:

    [kP6AggregateInputBase   = 1024, + input_size)  the agent input frame
    [kP6AggregateContextBase = 4096, + ctx_size)    the context frame (0-init)
    [kP6AggregateScratchBase = 7168,            )   constructor scratch slots

This script is the memory-side witness the P6-4 slice needs, BECAUSE observable
`value_json` output still awaits the P6-7 frame decision. Rather than reading
JSON, it writes the input frame as RAW BYTES at the exact P4-D offsets the
producer reports (`aggregate_base` + `field@offset`), runs the emitted module,
and reads the transformed fields back out of linear memory. That proves the
load/store offsets end-to-end over real memory — no serialization in the loop.

The producer reports the offsets it used, so this script MIRRORS the layout data
instead of re-deriving it (a test-only mirror, exactly as the slice spec says).
A wrong field offset, a source-order store, or a mis-sized load changes the
branch the agent takes, so the state-id path and the read-back bytes disagree.

SKIP (77) when the node interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Frozen oracle (WH-9 B0, HEAD 620fadd8).
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


def parse_frame(stdout: str) -> tuple[int, list[tuple[str, int, int]]]:
    """Parse the producer's `aggregate_base=<N> fields=<name>@<off>:<val>,...`."""
    base_match = re.search(r"aggregate_base=(\d+)", stdout)
    fields_match = re.search(r"fields=(\S+)", stdout)
    if base_match is None or fields_match is None:
        raise ValueError(f"malformed aggregate frame report: {stdout!r}")
    fields: list[tuple[str, int, int]] = []
    for entry in fields_match.group(1).split(","):
        name, rest = entry.split("@", 1)
        offset, value = rest.split(":", 1)
        fields.append((name, int(offset), int(value)))
    return int(base_match.group(1)), fields


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_aggregate_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 aggregate execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    source = golden_dir / "p6_aggregate.ahfl"
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or p6_aggregate.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-aggregate-node-") as td:
        td_path = Path(td)
        wasm = td_path / "p6_aggregate.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"producer exited {compile_run.returncode}: {compile_run.stderr}")
        obs = FROZEN_OBS
        base, fields = parse_frame(compile_run.stdout)
        if [name for name, _o, _v in fields] != ["a", "b"]:
            return fail(f"unexpected input frame fields: {fields}")

        host = td_path / "p6_aggregate_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

// The producer's reported P4-D input frame: base address + (name, offset,
// value) per field. The host writes RAW BYTES at those offsets, so the module's
// own load offsets must agree or the observed branch changes.
const base = Number(process.argv[3]);
const fields = process.argv[4].split(",").map((entry) => {
  const [name, rest] = entry.split("@");
  const [offset, value] = rest.split(":");
  return {name, offset: Number(offset), value: Number(value)};
});

const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("aggregate agent expanded import authority");
const {exports} = await WebAssembly.instantiate(module, {});
if (exports.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

// Write each field as an i64 at its P4-D offset (every Int field here is
// unbounded -> i64, 8-byte aligned). DataView writes the little-endian form the
// wasm memory model requires.
const view = new DataView(exports.memory.buffer);
for (const f of fields) {
  if (f.offset % 8 !== 0)
    throw new Error(`field ${f.name} offset ${f.offset} is not i64-aligned`);
  view.setBigInt64(base + f.offset, BigInt(f.value), true);
}

// The frozen oracle's entered-id sequence drives step(); the wasm path is that
// sequence minus the leading initial id.
const expectedEntered = process.argv[5].split(",").map(Number);
const initial = Number(process.argv[6]);
const transitions = Number(process.argv[7]);
const expected = expectedEntered.slice(1);
if (expectedEntered[0] !== initial)
  throw new Error("frozen initial id does not match initial_state_id");
if (exports.current_state() !== initial)
  throw new Error(`initial state ${exports.current_state()} != ${initial}`);
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
const path = sequence.slice(0, expected.length);
if (JSON.stringify(path) !== JSON.stringify(expected))
  throw new Error(
    `state path ${path} != oracle ${expected} ` + "(a wrong field offset misroutes the branch)");
if (exports.transition_count.value !== transitions)
  throw new Error(`transition_count ${exports.transition_count.value} != ${transitions}`);

// The context frame is zero-initialised, so the store the handler performed is
// observable: read the first P4-D context field back and require it to equal the
// sum the fixture computed (a + b). 4096 is kP6AggregateContextBase.
const ctxBase = 4096;
const sum = view.getBigInt64(ctxBase, true);
const a = BigInt(fields[0].value);
const b = BigInt(fields[1].value);
if (sum !== a + b)
  throw new Error(
    `context sum ${sum} != ${a + b} ` + "(a wrong ctx store offset or width)");

console.log("P6 aggregate memory Node execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(wasm), str(base),
                ",".join(f"{name}@{offset}:{value}" for name, offset, value in fields),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["initial_state_id"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node P6 aggregate host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-4 aggregate-memory Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
