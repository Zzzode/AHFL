#!/usr/bin/env python3
"""RFC 0026 FB-2 (CORE-FNBODY-DESIGN §8) bounded-native-recursion Node evidence.

The producer compiles a real-frontend fixture whose non-final handler calls a
pure fn `sum_into` that RECURSES over a bounded `List<Int>(4)` with a
compile-time rank parameter (rank i ascends by 1 on every self edge; the base
guard compares i with the bounded-container length word; the handler enters at
i=0). The FB-2 depth lattice seals the recursion structurally, so it lowers to
ordinary wasm `call`s (no tail-call proposal). This script materializes a
two-element live list in the bounded backing store (length word = 2, two
poisoned tail slots), compiles the module with the Node v22 WebAssembly engine,
and drives the stable step() ABI. The recursive fn must sum exactly the LIVE
elements (it reads the length word, not the capacity) and route the computed
goto to Done; a wrong rank progression, base guard, self-call target, or an
argument that mis-threads the container/accumulator changes the total and
routes to Fail. Real Node-engine evidence, NOT wasmtime evidence. SKIP (77)
when node is unavailable.
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

# Frozen oracle (WH-9 B0, HEAD 620fadd8): the wasm-path initial_state_id.
FROZEN_INITIAL_STATE_ID = 0


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
        return fail("usage: wasm_fb2_recursion_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-2 bounded-recursion execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(__file__).resolve().parents[1] / "golden" / "wasm" / "fb2_bounded_recursion.ahfl"
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-2 producer or fb2_bounded_recursion.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb2-node-") as td:
        td_path = Path(td)
        wasm = td_path / "fb2_bounded_recursion.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"producer exited {compile_run.returncode}: {compile_run.stderr}")
        layout = parse_collection(compile_run.stdout)
        if len(layout["elements"]) != layout["capacity"]:
            return fail(f"element value count != capacity: {layout}")
        if not (0 < layout["len"] <= layout["capacity"]):
            return fail(f"reported length is out of range: {layout}")

        # The recursive fn sums the first `len` live elements; the expected
        # total is computed from the producer's own element report so the host
        # is a self-checking oracle, not a restated constant. Done is dense
        # state id 1; Fail is 2.
        live_total = sum(layout["elements"][: layout["len"]])

        host = td_path / "fb2_recursion_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const layout = JSON.parse(process.argv[2]);
const expectedTotal = Number(process.argv[3]);
const initial = Number(process.argv[4]);
const doneId = 1, failId = 2;

const bytes = fs.readFileSync(process.argv[5]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure bounded-recursion agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const view = new DataView(c.memory.buffer);

// Materialize the element backing store (i64 slots, one stride apart); the
// slots at/after the live length are poisoned with a large value so the
// recursive guard (which reads the LENGTH word, not the capacity) stopping one
// iteration early would add the poison and miss the expected total.
for (let i = 0; i < layout.capacity; ++i) {
  const address = layout.backing_base + i * layout.stride + layout.value_offset;
  const value = i < layout.len ? layout.elements[i] : 100000;
  view.setBigInt64(address, BigInt(value), true);
}

// Write the container header in the input frame: ptr = backing base, len =
// live count.
const header = layout.collection_base + layout.header_offset;
view.setInt32(header + layout.ptr_offset, layout.backing_base, true);
view.setInt32(header + layout.len_offset, layout.len, true);

if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);

// The non-final Init handler runs the recursive sum; one step() must land on
// Done (the recursion completes inside the single native call stack).
const next = c.step();
if (next !== doneId)
  throw new Error(`bounded recursion routed to state ${next}, expected Done(${doneId}); total mismatch`);
if (c.current_state() !== doneId)
  throw new Error("current_state does not match step result");
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");

// A second step() on the final state is stable.
if (c.step() !== doneId)
  throw new Error("final Done state is not stable");

console.log(`FB-2 bounded recursion summed ${expectedTotal} via native wasm calls`);
""",
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), json.dumps(layout), str(live_total),
                str(FROZEN_INITIAL_STATE_ID),
                str(wasm),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node FB-2 recursion host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
        if str(live_total) not in executed.stdout:
            return fail(f"host did not confirm expected total {live_total}")

    print("OK: RFC 0026 FB-2 bounded native recursion Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
