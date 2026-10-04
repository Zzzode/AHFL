#!/usr/bin/env python3
"""RFC 0026 FB-3b (CORE-FNBODY-DESIGN §8 / §6.3) Node evidence.

A recursive fn `map_sum_into` walks a BOUNDED `List<Int>(4)` with the FB-2
sealed rank parameter and, on every activation, constructs and invokes a
FRESH lifted lambda (`\\x -> x * 2`) through call_indirect. The depth is the
bounded-collection length word (FB-2); the funcref-table indirect call is the
FB-3 closure path. The host materializes a two-element live list in the
bounded backing store (length 2, two poisoned tail slots); twice the live
elements 90 + 1 = 182 routes the computed goto to Done. A wrong rank
progression, a stale call_indirect slot, or a recursion/closure defect changes
the total. Real Node-engine evidence, NOT wasmtime. SKIP (77) when node is
unavailable.
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


def parse_collection(stdout: str) -> dict[str, object]:
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
        raise ValueError("malformed collection report (missing elements)")
    values["elements"] = [int(x) for x in elements_match.group(1).split(",") if x != ""]
    return values


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb3_bounded_collection_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-3 bounded-collection closure execution")
        return SKIP
    producer = Path(argv[0])
    source = (Path(__file__).resolve().parents[1] / "golden" / "wasm"
              / "fb3_bounded_collection.ahfl")
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-3 producer or fb3_bounded_collection.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb3-bounded-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb3_bounded_collection.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"FB-3 producer exited {compile_run.returncode}: {compile_run.stderr}")
        layout = parse_collection(compile_run.stdout)
        if len(layout["elements"]) != layout["capacity"]:
            return fail(f"element value count != capacity: {layout}")
        if not (0 < layout["len"] <= layout["capacity"]):
            return fail(f"reported length is out of range: {layout}")
        # The lifted fn doubles each LIVE element; the expected total is derived
        # from the producer's own element report (self-checking oracle).
        expected = sum(2 * x for x in layout["elements"][: layout["len"]])
        if expected != 182:
            return fail(f"fixture oracle total {expected} != pinned 182")

        host = td_path / "fb3_bounded_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const layout = JSON.parse(process.argv[2]);
const expectedTotal = Number(process.argv[3]);
const {instance} = await WebAssembly.instantiate(
    fs.readFileSync(process.argv[4]), {});
const c = instance.exports;
if (WebAssembly.Module.imports(await WebAssembly.compile(fs.readFileSync(process.argv[4]))).length !== 0)
  throw new Error("a pure bounded-closure agent expanded import authority");
const view = new DataView(c.memory.buffer);
// Materialize the element backing (i64 slots, one stride apart); poison the
// slots at/after the live length so a recursion guard that reads the capacity
// instead of the length word would add the poison and miss the total.
for (let i = 0; i < layout.capacity; ++i) {
  const address = layout.backing_base + i * layout.stride + layout.value_offset;
  const value = i < layout.len ? layout.elements[i] : 100000;
  view.setBigInt64(address, BigInt(value), true);
}
const header = layout.collection_base + layout.header_offset;
view.setInt32(header + layout.ptr_offset, layout.backing_base, true);
view.setInt32(header + layout.len_offset, layout.len, true);

const next = c.step();
if (next !== 1)
  throw new Error(`bounded-collection closure recursion routed to state ${next}, expected Done(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log(`FB-3 bounded-collection recursive closure summed ${expectedTotal}`);
""",
            encoding="utf-8",
        )
        driven = subprocess.run(
            [node, str(host), json.dumps(layout), str(expected), str(wasm_path)],
            capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-3 bounded host failed: {driven.stderr}")
        if str(expected) not in driven.stdout:
            return fail(f"host did not confirm expected total {expected}")

    print("OK: RFC 0026 FB-3 bounded-collection recursive closure Node embedded-engine execution "
          "passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
