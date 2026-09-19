#!/usr/bin/env python3
"""RFC 0026 P6-5 (KR6.6) bounded-collection Node execution evidence.

P6-5 gives a bounded collection (`List<Int>(4)`) a runtime representation: an i32
ADDRESS into the module's private linear memory pointing at its INLINE `(ptr,len)`
header (P4-D `CoreLayoutContainer`), with the elements in an indirect backing
store `stride` bytes apart. Every backing fact (header offsets, stride, capacity,
backing size, element width) comes from the P4-D layout table.

Observable `value_json` output still awaits the P6-7 frame decision, so this
script writes the frame as RAW BYTES at the exact addresses the producer reports
(the input frame's container header at its P4-D field offset, and the element
backing store at `kP6CollectionBackingBase`, one `stride` apart), runs the
emitted module, and asserts the agent takes the branch only a CORRECT header /
stride / element-width read can reach. A wrong pointer word, a wrong length word,
a wrong stride, or a mis-sized element load all change the branch, and they are
observed through the state ids step() moves to. No JSON in the loop.

The producer reports the layout it used, so this script MIRRORS that data instead
of re-deriving it (a test-only mirror, exactly as the slice spec says).

SKIP (77) when the node interpreter is unavailable.
"""
from __future__ import annotations

import json
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


def parse_collection(stdout: str) -> dict[str, object]:
    """Parse the producer's `collection_base=… backing_base=… … elements=…` report."""
    keys = [
        "collection_base", "backing_base", "header_offset", "ptr_offset",
        "len_offset", "len", "stride", "value_offset", "capacity",
        "backing_size", "element_wide",
    ]
    values: dict[str, object] = {}
    for key in keys:
        match = re.search(rf"{key}=(-?\d+)", stdout)
        if match is None:
            raise ValueError(f"malformed collection report (missing {key}): {stdout!r}")
        values[key] = int(match.group(1))
    elements_match = re.search(r"elements=(\S+)", stdout)
    if elements_match is None:
        raise ValueError(f"malformed collection report (missing elements): {stdout!r}")
    values["elements"] = [int(x) for x in elements_match.group(1).split(",") if x != ""]
    return values


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_collection_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 collection execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    source = golden_dir / "p6_collection.ahfl"
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or p6_collection.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-collection-node-") as td:
        td_path = Path(td)
        wasm = td_path / "p6_collection.wasm"
        native = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"producer exited {native.returncode}: {native.stderr}")
        obs = parse_observation(native.stdout)
        if obs["status"] != "completed" or not obs["entered_ids"]:
            return fail(f"native collection run did not complete: {obs}")
        layout = parse_collection(native.stdout)
        if len(layout["elements"]) != layout["capacity"]:
            return fail(f"element value count != capacity: {layout}")
        if layout["len"] > layout["capacity"] or layout["len"] <= 0:
            return fail(f"reported length is out of range: {layout}")

        host = td_path / "p6_collection_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

// The producer's reported P4-D layout. The host writes RAW BYTES at these
// addresses, so the module's OWN header / stride / load arithmetic must agree
// or the branch changes.
const layout = JSON.parse(process.argv[3]);

const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("collection agent expanded import authority");
const {exports} = await WebAssembly.instantiate(module, {});
if (exports.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const view = new DataView(exports.memory.buffer);

// 1) Materialize the element BACKING STORE at the reported base, one `stride`
//    apart, at the reported element width. DataView writes the little-endian
//    form the wasm memory model requires.
const elementBytes = layout.element_wide ? 8 : 4;
for (let i = 0; i < layout.elements.length; ++i) {
  const address = layout.backing_base + i * layout.stride + layout.value_offset;
  if (layout.value_offset + elementBytes > layout.stride)
    throw new Error("element slot does not fit one stride");
  if (layout.element_wide) {
    view.setBigInt64(address, BigInt(layout.elements[i]), true);
  } else {
    view.setInt32(address, layout.elements[i], true);
  }
}

// 2) Write the container HEADER at its P4-D field offset inside the input
//    frame: `ptr` = the backing base (relative to the module's own view of the
//    address — the module loads the absolute address it was handed) and `len` =
//    the logical element count (NOT the capacity).
const header = layout.collection_base + layout.header_offset;
view.setInt32(header + layout.ptr_offset, layout.backing_base, true);
view.setInt32(header + layout.len_offset, layout.len, true);

// The native entered-id sequence drives step(); the wasm path is that sequence
// minus the leading initial id.
const expectedEntered = process.argv[4].split(",").map(Number);
const initial = Number(process.argv[5]);
const transitions = Number(process.argv[6]);
const expected = expectedEntered.slice(1);
if (expectedEntered[0] !== initial)
  throw new Error("native initial id does not match initial_state_id");
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
    `state path ${path} != native ${expected} ` +
    "(a wrong header offset / stride / element width misroutes the branch)");
if (exports.transition_count.value !== transitions)
  throw new Error(`transition_count ${exports.transition_count.value} != ${transitions}`);

// The fixture's threshold branch is reachable ONLY by reading element 0 (which
// the host wrote ABOVE the threshold) through a correct stride and element
// width: the wrong element (element 1) is BELOW it. Re-deriving the expected
// branch from the reported element values makes the assertion self-checking
// rather than a restatement of the native ids.
const threshold = 50;
const highId = 1;
const lowId = 2;
const first = BigInt(layout.elements[0]);
const second = BigInt(layout.elements[1]);
if (first <= BigInt(threshold) || second > BigInt(threshold))
  throw new Error("fixture element values no longer straddle the threshold");
if (layout.len <= 0)
  throw new Error("host wrote a non-positive length");
if (expected[0] !== highId || expected.includes(lowId))
  throw new Error(`native path ${expected} does not take only the High branch`);

console.log("P6 bounded-collection Node execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(wasm), json.dumps(layout),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["initial_state_id"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node P6 collection host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-5 bounded-collection Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
