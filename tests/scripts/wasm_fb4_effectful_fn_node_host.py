#!/usr/bin/env python3
"""RFC 0026 FB-4 (CORE-FNBODY-DESIGN §5.3) Node evidence.

A non-final flow handler calls an EFFECTFUL fn through a transitive wrapper:

    Init  -> ask_cap(input.n)             ordered CoreCallStmt
    ask_cap(n) -> call_cap(n)             transitive effect (structural)
    call_cap(n) -> Bump(Frame{n})         outlined fn -> ahfl_cap import

The transitive effect is computed structurally from the fn body call graph (no
spelling is trusted), the outlined effect fn routes its capability through the
SAME ahfl_cap import ordinal a handler capability statement would use, and the
capability's result drives the computed goto. The host binds the single import,
materializes the input Frame at the fixed P4-D input base, and asserts:

  * the module declares exactly one ahfl_cap import (the fn-reached capability);
  * step() invokes it EXACTLY ONCE with the handler's argument (import ordinal
    ordering is correct — a wrong ordinal would call the wrong/absent import);
  * the bumped result (41 -> 42) routes Init to Done (result forwarded through
    two ordered effect calls and one capability call).

Single-run ordering/execution only; durable replay / no-reinvoke of in-fn
effects awaits the wire checkpoint framework (D2b). Real Node v22 engine
evidence, NOT wasmtime. SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
DONE = 1

# Frozen oracle (WH-9 B0, HEAD 620fadd8): the wasm-path initial_state_id.
FROZEN_INITIAL_STATE_ID = 0


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


HOST_JS = r"""
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const mod = await WebAssembly.compile(bytes);
const declared = WebAssembly.Module.imports(mod);
if (declared.length !== 1 || declared[0].module !== "ahfl_cap")
  throw new Error("expected exactly one ahfl_cap import, got " + JSON.stringify(declared));

let instance;
const calls = [];
const importObj = { ahfl_cap: {} };
// Bump: read the i64 field at the request frame, allocate a result frame of the
// SAME opaque length, write n + 1, return (OK, ptr, len).
for (const imp of declared) {
  importObj.ahfl_cap[imp.name] = (ptr, len) => {
    const mem = new DataView(instance.exports.memory.buffer);
    const n = mem.getBigInt64(ptr, true);
    calls.push({ field: imp.field, n: Number(n), len });
    const out = instance.exports.alloc(Number(len));
    mem.setBigInt64(out, n + 1n, true);
    return [0, out, len];
  };
}
instance = await WebAssembly.instantiate(mod, importObj);
const c = instance.exports;
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");
if (c.current_state() !== 0)
  throw new Error(`initial state ${c.current_state()} != 0`);

// Materialize the input Frame at the fixed P4-D input frame base; the leading
// `n: Int` field sits at offset 0 (i64).
new DataView(c.memory.buffer).setBigInt64(1024, 41n, true);

const next = c.step();
if (next !== 1)
  throw new Error(`effectful fn routed to state ${next}, expected Done(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (calls.length !== 1)
  throw new Error("expected exactly one ahfl_cap invocation, got " + calls.length);
if (calls[0].n !== 41 || calls[0].len !== 8)
  throw new Error("capability received wrong frame: " + JSON.stringify(calls[0]));
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log("FB-4 capability invocation:", JSON.stringify(calls));
console.log("FB-4 transitive effectful fn routed through the ahfl_cap import sequence");
"""


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb4_effectful_fn_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-4 effectful-fn execution")
        return SKIP
    producer = Path(argv[0])
    source = (Path(__file__).resolve().parents[1] / "golden" / "wasm"
              / "fb4_effectful_fn.ahfl")
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-4 producer or fb4_effectful_fn.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb4-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb4_effectful_fn.wasm"
        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            return fail(f"FB-4 producer exited {compile_run.returncode}: {compile_run.stderr}")
        if FROZEN_INITIAL_STATE_ID != 0:
            return fail(f"frozen initial state oracle drifted from 0: {FROZEN_INITIAL_STATE_ID}")

        host = td_path / "fb4_host.mjs"
        host.write_text(HOST_JS, encoding="utf-8")
        driven = subprocess.run([node, str(host), str(wasm_path)],
                                capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-4 effectful-fn host failed: {driven.stderr}")
        if "ahfl_cap import sequence" not in driven.stdout:
            return fail("host did not confirm the ahfl_cap import sequence")

    print("OK: RFC 0026 FB-4 transitive effectful fn ordered CoreCallStmt Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
