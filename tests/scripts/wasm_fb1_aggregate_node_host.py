#!/usr/bin/env python3
"""RFC 0026 FB-1 fix-forward Node evidence: outlined-fn AGGREGATE results.

The producer compiles a real-frontend fixture whose non-final handler makes
repeated, nested, distinct-fn, and aggregate-argument direct calls to outlined
pure fns that return structs. The fn bodies materialize each aggregate from the
per-activation runtime bump heap (never the module-shared compile-time static
scratch, which aliased across native `call` activations). This script compiles
the emitted module with the Node v22 WebAssembly engine and drives step();
every aggregate must survive, routing the computed goto to Good. A single-call
control with the old codegen reached Good while all four alias shapes reached
Bad; with the fix they all reach Good. Real Node-engine evidence, NOT wasmtime.
SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        return fail("usage: wasm_fb1_aggregate_node_host.py <producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-1 aggregate execution")
        return SKIP
    producer = Path(argv[1])
    source = Path(argv[2])
    if not producer.is_file() or not source.is_file():
        return fail("missing producer or fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb1-agg-node-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb1_aggregate.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"producer exited {compile_run.returncode}: {compile_run.stderr}")

        host = td_path / "fb1_agg_host.mjs"
        # States: Init=0, Good=1, Bad=2.
        host.write_text(
            r"""
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure direct-call agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");
if (c.current_state() !== 0)
  throw new Error(`initial state ${c.current_state()} != 0`);
const next = c.step();
if (next !== 1)
  throw new Error(`aggregate aliasing routed to state ${next}, expected Good(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Good transition");
if (c.step() !== 1)
  throw new Error("final Good state is not stable");
console.log("FB-1 aggregate direct-call aliasing resolved");
""")
        driven = subprocess.run([node, str(host), str(wasm_path)],
                               capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-1 aggregate host failed: {driven.stderr}")

    print("FB-1 aggregate outlined-fn Node embedded-engine execution passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
