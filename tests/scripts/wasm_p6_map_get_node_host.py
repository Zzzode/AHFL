#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) bounded-Map keyed-lookup Node execution evidence.

The fixture reads `input.table[7]` on a Map<Int, Int>(4); the probe lowers it
to the Core KeyGet op and emits a bounded linear scan over the backing
entries. The host writes the inline (ptr,len) header at the reported frame
offset and the [key@0, value@value_offset] entries one `stride` apart in the
module's backing region, using ONLY the P4-D facts the producer reports.

The reached state-id path pins:
  * the scan COMPARES KEYS (key 7 is at slot 1, never slot 0),
  * the returned word is the VALUE at value_offset (42, not the key 7),
  * the stride / offsets are exactly the P4-D facts.
A positional miscompile, a wrong value_offset, or a wrong stride routes Low.

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


def parse_map(stdout: str) -> dict[str, object]:
    keys = [
        "map_base", "backing_base", "header_offset", "ptr_offset", "len_offset",
        "len", "stride", "value_offset", "capacity", "backing_size",
        "key_wide", "value_wide",
    ]
    values: dict[str, object] = {}
    for key in keys:
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


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_map_get_node_host.py <p6-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 map execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or p6_map_get.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-map-node-") as td:
        td_path = Path(td)
        wasm = td_path / "p6_map_get.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode != 0:
            return fail(f"producer exited {run.returncode}: {run.stderr}")
        obs = parse_observation(run.stdout)
        if obs["status"] != "completed" or not obs["entered_ids"]:
            return fail(f"native map run did not complete: {obs}")
        layout = parse_map(run.stdout)
        if len(layout["keys"]) != layout["len"] or len(layout["values"]) != layout["len"]:
            return fail(f"map live entry counts disagree with len: {layout}")

        host = td_path / "p6_map_host.mjs"
        host.write_text(
            r'''
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

const c = await instantiate_agent();
materialize(c);

// Done=0 High=1 Low=2 Decide=3. The keyed lookup of 7 must yield value 42.
const expectedEntered = process.argv[4].split(",").map(Number);
const expected = expectedEntered.slice(1);
const initial = Number(process.argv[5]);
if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);
const sequence = [];
let previous = initial;
let guard = expected.length + 2;
while (guard-- > 0) {
  const next = c.step();
  sequence.push(next);
  if (next !== previous) { previous = next; continue; }
  break;
}
const path = sequence.slice(0, expected.length);
if (JSON.stringify(path) !== JSON.stringify(expected))
  throw new Error(`Map keyed lookup routed ${path}, expected ${expected} ` +
    "(key scan / value_offset / stride miscompiled)");
if (c.transition_count.value !== Number(process.argv[6]))
  throw new Error("transition_count mismatch");

// Missing key -> the scan must TRAP (evaluator returns "key not found").
// Drive a FRESH instance: after the successful run the machine is in the
// terminal Done state, so the frame must be rebuilt on a new instance with
// key 7 removed from the live entries.
const absent = await instantiate_agent();
materialize(absent);
{
  const v = new DataView(absent.memory.buffer);
  v.setBigInt64(layout.backing_base + 1 * layout.stride, 99n, true);
  let trapped = false;
  try {
    absent.step();
  } catch (error) {
    trapped = error instanceof WebAssembly.RuntimeError;
  }
  if (!trapped)
    throw new Error("Map keyed lookup of an absent key did not trap");
}

console.log("P6 bounded-Map keyed lookup Node step execution passed");
''',
            encoding="utf-8",
        )

        driven = subprocess.run(
            [
                node, str(host), str(wasm),
                json.dumps(layout),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["initial_state_id"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if driven.returncode != 0:
            return fail(f"Node map host failed: {driven.stderr}")

    print("OK: CORE-GAPS bounded-Map keyed lookup Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
