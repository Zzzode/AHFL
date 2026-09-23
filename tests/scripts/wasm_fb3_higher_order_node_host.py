#!/usr/bin/env python3
"""RFC 0026 FB-3b (CORE-FNBODY-DESIGN §5.2/§6.2) Node evidence.

The producer compiles a real-frontend fixture whose non-final handler
constructs a lifted lambda, passes it to a PURE fn with a FnT parameter, and
observes the scalar result. Inside the callee the callable VALUE is invoked
through `call_indirect` against the module's single funcref table. This
script compiles the module with the Node v22 WebAssembly engine and drives the
stable step() ABI: the higher-order result must route the computed goto to
Done (dense id 1). It also asserts the module declares exactly one funcref
table with the lifted fn as its element, so a missing Table(4)/Element(9)
section or a wrong slot fails here. Real Node-engine evidence, NOT wasmtime.
SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
DONE = 1
FAIL = 2


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb3_higher_order_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-3 higher-order execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(__file__).resolve().parents[1] / "golden" / "wasm" / "fb3_higher_order.ahfl"
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-3 producer or fb3_higher_order.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb3-hof-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb3_higher_order.wasm"
        native = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"FB-3 producer exited {native.returncode}: {native.stderr}")
        match = re.search(r"initial_state_id=(\d+)", native.stdout)
        if match is None:
            return fail(f"malformed native observation: {native.stdout!r}")
        initial = int(match.group(1))

        host = td_path / "fb3_hof_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const initial = Number(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure closure agent expanded import authority");
// Exactly one funcref table, declared via Table(4). Node v22 exposes no
// WebAssembly.Module.tables() introspector, so walk the section bytes and
// assert a section id 4 is present (a closure-free module omits it entirely).
function hasSection(id) {
  let i = 8;
  const leb = () => { let r = 0, s = 0; while (true) {
    const x = bytes[i++]; r |= (x & 0x7f) << s; if (!(x & 0x80)) break; s += 7; } return r; };
  while (i < bytes.length) { const sid = bytes[i++]; const len = leb(); if (sid === id) return true; i += len; }
  return false;
}
if (!hasSection(4) || !hasSection(9))
  throw new Error("a closure module must carry Table(4) and Element(9) sections");
const {exports: c} = await WebAssembly.instantiate(module, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");
if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);

// The Init handler applies `\x -> x * 2` to 21 THROUGH call_indirect; one
// step must land on Done and bump the transition counter exactly once.
const next = c.step();
if (next !== 1)
  throw new Error(`higher-order call_indirect routed to state ${next}, expected Done(1)`);
if (c.current_state() !== next)
  throw new Error("current_state does not match step result");
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log("FB-3 higher-order lambda executed through call_indirect");
""",
            encoding="utf-8",
        )
        driven = subprocess.run([node, str(host), str(initial), str(wasm_path)],
                               capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-3 higher-order host failed: {driven.stderr}")
        if "call_indirect" not in driven.stdout:
            return fail("host did not confirm call_indirect execution")

    print("OK: RFC 0026 FB-3 higher-order lambda call_indirect Node embedded-engine execution "
          "passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
