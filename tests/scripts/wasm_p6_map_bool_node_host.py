#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS 2 fix-forward) bounded-Map Bool key/value evidence.

The typed verifier admits Int/Bool/tag-only-enum Map keys and any single-word
scalar value; pre-fix the wasm emitter's width gate compared the operand's
P6ScalarKind::Bool against the slot's IntI32 (place_kind_of_layout maps every
I32 edge to IntI32 and has no Bool case), so it REJECTED the exact Bool shapes
the typed contract promised ("keyed scan key width does not match the key slot
layout" / "... result width does not match the value slot layout"). Bool shares
the i32 compare/load ladder, so the gate now treats Bool as width-compatible
with an I32 slot.

Two @repo-std fixtures pin the delivery end to end in the real Node v22 engine:
  * Map<Bool, Int>(4):  lookup input.table[true]  -> 42 (present, slot 1)
  * Map<Int, Bool>(4): lookup input.table[7]     -> true (present, slot 1)
Each drives the computed goto HIGH, and on a FRESH instance removing the live
entry the keyed lookup must TRAP (the wasm engine traps on absent key). The
producer reports the P4-D facts (including the Bool i32 words as keys/values)
and the host mirrors them; it never re-derives a layout. The wasm state-id
paths are pinned against the frozen oracles captured at HEAD 620fadd8
(WH-9 B0).

SKIP (77) when node is unavailable.
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

# Frozen oracles (WH-9 B0, HEAD 620fadd8), keyed by fixture stem.
FROZEN_OBS: dict[str, dict[str, object]] = {
    "p6_map_bool_key": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
    "p6_map_bool_value": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
}

_INT_KEYS = (
    "map_base", "backing_base", "header_offset", "ptr_offset", "len_offset",
    "len", "stride", "value_offset", "capacity", "backing_size",
    "key_wide", "value_wide",
)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_map(stdout: str) -> dict[str, object]:
    values: dict[str, object] = {}
    for key in _INT_KEYS:
        match = re.search(rf"{key}=(-?\d+)", stdout)
        if match is None:
            raise ValueError(f"malformed map report (missing {key}): {stdout!r}")
        values[key] = int(match.group(1))
    for field in ("keys", "values"):
        match = re.search(rf"{field}=(\S+)", stdout)
        if match is None:
            raise ValueError(f"malformed map report (missing {field}): {stdout!r}")
        values[field] = [int(x) for x in match.group(1).split(",") if x != ""]
    return values


_HOST = r'''
import fs from "node:fs";

const layout = JSON.parse(process.argv[3]);
const instantiate_agent = async () => {
  const bytes = fs.readFileSync(process.argv[2]);
  const compiled = await WebAssembly.compile(bytes);
  if (WebAssembly.Module.imports(compiled).length !== 0)
    throw new Error("map agent expanded import authority");
  const {exports} = await WebAssembly.instantiate(compiled, {});
  return exports;
};

const materialize = (agent) => {
  const v = new DataView(agent.memory.buffer);
  const put = (address, value, wide) =>
      wide ? v.setBigInt64(address, BigInt(value), true)
           : v.setInt32(address, value, true);
  for (let i = 0; i < layout.capacity; ++i) {
    const base = layout.backing_base + i * layout.stride;
    const live = i < layout.keys.length;
    put(base, live ? layout.keys[i] : 0x5a5a, layout.key_wide);
    put(base + layout.value_offset, live ? layout.values[i] : 0,
        layout.value_wide);
  }
  const header = layout.map_base + layout.header_offset;
  v.setInt32(header + layout.ptr_offset, layout.backing_base, true);
  v.setInt32(header + layout.len_offset, layout.len, true);
};

const valueBytes = layout.value_wide ? 8 : 4;
if (layout.value_offset + valueBytes > layout.stride)
  throw new Error("Map value slot does not fit one stride");

const drive = async () => {
  const c = await instantiate_agent();
  materialize(c);

  // Done=0 High=1 Low=2 Decide=3. The present lookup must route High then Done.
  const initial = Number(process.argv[5]);
  if (c.current_state() !== initial)
    throw new Error(`initial state ${c.current_state()} != ${initial}`);
  const first = c.step();
  const second = c.step();
  if (first !== 1 || second !== 0)
    throw new Error(`Bool keyed lookup routed [${first},${second}], expected ` +
      "[1,0] (Bool width gate / value_offset / stride miscompiled)");
  if (c.transition_count.value !== Number(process.argv[6]))
    throw new Error("transition_count mismatch");

  // Missing key -> the scan must TRAP. Fresh instance with the live entry at
  // slot 1 removed (overwritten with an unmatched key).
  const absent = await instantiate_agent();
  materialize(absent);
  {
    const v = new DataView(absent.memory.buffer);
    v.setInt32(layout.backing_base + 1 * layout.stride, 0x5a5a, true);
    let trapped = false;
    try {
      absent.step();
    } catch (error) {
      trapped = error instanceof WebAssembly.RuntimeError;
    }
    if (!trapped)
      throw new Error("Map keyed lookup of an absent key did not trap");
  }
};

await drive();
console.log("P6 Bool-Map keyed lookup Node step execution passed");
'''


def run_case(node: str, producer: Path, source: Path, td: Path) -> None:
    wasm = td / (source.stem + ".wasm")
    compile_run = subprocess.run(
        [str(producer), str(source), str(wasm)],
        capture_output=True, text=True, timeout=60,
    )
    if compile_run.returncode != 0:
        raise AssertionError(f"producer exited {compile_run.returncode}: {compile_run.stderr}")
    obs = FROZEN_OBS[source.stem]
    layout = parse_map(compile_run.stdout)
    if len(layout["keys"]) != layout["len"] or len(layout["values"]) != layout["len"]:
        raise AssertionError(f"map live entry counts disagree with len: {layout}")

    host = td / (source.stem + "_host.mjs")
    host.write_text(_HOST, encoding="utf-8")
    driven = subprocess.run(
        [
            node, str(host), str(wasm), json.dumps(layout),
            ",".join(str(x) for x in obs["entered_ids"]),
            str(obs["initial_state_id"]), str(obs["transition_count"]),
        ],
        capture_output=True, text=True, timeout=60,
    )
    if driven.returncode != 0:
        raise AssertionError(f"Node Bool-Map host failed: {driven.stderr}")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        return fail(
            "usage: wasm_p6_map_bool_node_host.py <p6-producer> <bool-key-source> "
            "<bool-value-source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 Bool-Map execution")
        return SKIP
    producer = Path(argv[0])
    sources = [Path(argv[1]), Path(argv[2])]
    if not producer.is_file() or not all(s.is_file() for s in sources):
        return fail("missing P6 producer or a Bool-Map fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-map-bool-node-") as td:
        td_path = Path(td)
        for source in sources:
            run_case(node, producer, source, td_path)

    print("OK: CORE-GAPS bounded-Map Bool key/value keyed lookup Node "
          "embedded-engine execution passed (real Node v22 engine, NOT "
          "wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
