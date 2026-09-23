#!/usr/bin/env python3
"""RFC 0026 FB-2 fix-forward: hostile length word is clamped to capacity.

The recursion depth lattice seals a Len-bounded group against the STATIC
container capacity (the P4-D backing fact), but the input-frame header LENGTH
word is host-written. Before the fix the outlined fn read that word raw, so a
host that reported a huge length against a small capacity backed store drove the
rank guard for ~that many native calls and trapped the engine with "Maximum call
stack size exceeded" — the executed bound did not match the sealed one.

The wasm lane now clamps every fn-body length read to min(header_len,
capacity). This Node host LIES (length word 100000 over a capacity-4 list whose
four slots are materialized) and asserts one step() still completes and routes
to Done (the guard behaves as if length were 4), rather than stack-exhausting.
A positive control asserts length == capacity still works. Real Node v22
evidence. SKIP (77) when node is unavailable.
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
LIED_LEN = 100_000


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_collection(stdout: str) -> dict[str, object]:
    keys = [
        "collection_base", "backing_base", "header_offset", "ptr_offset",
        "len_offset", "stride", "value_offset", "capacity", "element_wide",
    ]
    values: dict[str, object] = {}
    for key in keys:
        match = re.search(rf"{key}=(-?\d+)", stdout)
        if match is None:
            raise ValueError(f"malformed collection report (missing {key}): {stdout!r}")
        values[key] = int(match.group(1))
    elements_match = re.search(r"elements=(\S+)", stdout)
    if elements_match is None:
        raise ValueError("malformed collection report (missing elements)")
    values["elements"] = [int(x) for x in elements_match.group(1).split(",") if x != ""]
    return values


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb2_len_lie_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-2 length-clamp execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(__file__).resolve().parents[1] / "golden" / "wasm" / "fb2_bounded_recursion.ahfl"
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-2 producer or fb2_bounded_recursion.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb2-lenlie-") as td:
        td_path = Path(td)
        wasm = td_path / "fb2_bounded_recursion.wasm"
        native = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"producer exited {native.returncode}: {native.stderr}")
        layout = parse_collection(native.stdout)
        if layout["capacity"] != 4:
            return fail(f"fixture capacity drifted from 4: {layout}")
        if len(layout["elements"]) != layout["capacity"]:
            return fail(f"element value count != capacity: {layout}")
        # After clamping to capacity the recursion sums all four live slots.
        expected_total = sum(layout["elements"][: layout["capacity"]])
        if expected_total != 91:
            return fail(f"fixture total drifted from 91: {expected_total}")

        initial_match = re.search(r"initial_state_id=(\d+)", native.stdout)
        if initial_match is None:
            return fail("malformed native observation (no initial_state_id)")
        initial = int(initial_match.group(1))

        host = td_path / "fb2_len_lie_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const layout = JSON.parse(process.argv[2]);
const expectedTotal = Number(process.argv[3]);
const initial = Number(process.argv[4]);
const liedLen = Number(process.argv[5]);
const doneId = 1;

const bytes = fs.readFileSync(process.argv[6]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure bounded-recursion agent expanded import authority");

function freshInstance() {
  return WebAssembly.instantiate(module, {});
}

function materialize(view, c) {
  // Materialize every CAPACITY backing slot (the clamped guard may walk to it).
  for (let i = 0; i < layout.capacity; ++i) {
    const address = layout.backing_base + i * layout.stride + layout.value_offset;
    view.setBigInt64(address, BigInt(layout.elements[i]), true);
  }
  const header = layout.collection_base + layout.header_offset;
  view.setInt32(header + layout.ptr_offset, layout.backing_base, true);
  return header;
}

async function runOnce(lenWord, label) {
  // A fresh instance per run so the second observation starts at `initial`.
  const {exports: c} = await freshInstance();
  const view = new DataView(c.memory.buffer);
  const header = materialize(view, c);
  view.setInt32(header + layout.len_offset, lenWord, true);
  if (c.current_state() !== initial)
    throw new Error(`${label}: initial state ${c.current_state()} != ${initial}`);
  // Without the fn-body clamp this recurses ~lenWord deep and throws
  // "Maximum call stack size exceeded". With min(lenWord, capacity) it must
  // complete the bounded sum and route to Done.
  let next;
  try {
    next = c.step();
  } catch (err) {
    throw new Error(`${label}: step() trapped with a lied length ${lenWord}: ${err}`);
  }
  if (next !== doneId)
    throw new Error(`${label}: routed to ${next}, expected Done(${doneId}) (total ${expectedTotal})`);
}

// Hostile: claim 100000 live elements against a capacity-4 backing store.
await runOnce(liedLen, "lie");
// Positive control: length exactly at capacity is accepted unchanged.
await runOnce(layout.capacity, "at-capacity");

console.log(`FB-2 length-clamp bounded a lied length to capacity and summed ${expectedTotal}`);
""",
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), json.dumps(layout), str(expected_total),
                str(initial), str(LIED_LEN), str(wasm),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node FB-2 length-lie host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
        if str(expected_total) not in executed.stdout:
            return fail(f"host did not confirm expected total {expected_total}")

    print("OK: RFC 0026 FB-2 hostile length-word clamp Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
