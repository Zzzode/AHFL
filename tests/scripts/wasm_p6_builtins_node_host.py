#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) Node embedded-engine evidence for the bounded
enum-constructor builtins (option_some / result_ok / result_err).

App code reaches the identity constructor through enum syntax; the bodyless
hook lowering is the same CoreConstructExpr. The fixture unwraps two Some
payloads and routes High only when both payload extractions are correct.
The wasm state-id path is pinned against the frozen oracle captured at HEAD
620fadd8 (WH-9 B0). SKIP (77) when node is unavailable.
"""
from __future__ import annotations

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


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_builtins_node_host.py <p6-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 builtins execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or p6_builtins.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-builtins-node-") as td:
        td_path = Path(td)
        wasm = td_path / "p6_builtins.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"producer exited {compile_run.returncode}: {compile_run.stderr}")
        obs = FROZEN_OBS
        entered = obs["entered_ids"]
        transitions = obs["transition_count"]
        initial = obs["initial_state_id"]
        expected = entered[1:]

        host = td_path / "p6_builtins_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const module_ = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module_).length !== 0)
  throw new Error("builtins agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module_, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");
const expected = process.argv[3].split(",").map(Number);
const initial = Number(process.argv[4]);
if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);
const seq = [];
let previous = initial;
let guard = expected.length + 2;
while (guard-- > 0) {
  const next = c.step();
  seq.push(next);
  if (next !== previous) { previous = next; continue; }
  break;
}
const path = seq.slice(0, expected.length);
if (JSON.stringify(path) !== JSON.stringify(expected))
  throw new Error(`constructor builtins routed ${path}, expected ${expected}`);
if (c.transition_count.value !== Number(process.argv[5]))
  throw new Error("transition_count mismatch");
console.log("P6 bounded constructor builtins Node step execution passed");
''',
            encoding="utf-8",
        )
        driven = subprocess.run(
            [node, str(host), str(wasm), ",".join(map(str, expected)),
             str(initial), str(transitions)],
            capture_output=True, text=True, timeout=60,
        )
        if driven.returncode != 0:
            return fail(f"Node builtins host failed: {driven.stderr}")

    print("OK: CORE-GAPS bounded constructor builtins Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
