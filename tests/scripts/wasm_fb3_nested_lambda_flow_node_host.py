#!/usr/bin/env python3
r"""RFC 0026 FB-3b fix-forward: NESTED lambda lifted in a flow handler.

The Init handler constructs an OUTER lambda whose body constructs a further
INNER capturing lambda and forwards it to a higher-order fn; the outer closure
is then invoked indirectly. Regression for the P0 lifetime defect where the
long-lived LambdaLifter referenced a destroyed Pass-1.5 interner and the
producer crashed (SIGSEGV / ASan stack-use-after-scope) when lifting a nested
lambda from a handler. The Node host asserts the module carries the funcref
Table(4)/Element(9) sections, runs the nested closure chain through
call_indirect, and routes to Done (r == 5). Real Node-engine evidence, NOT
wasmtime. SKIP (77) when node is unavailable.
"""
from __future__ import annotations

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


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb3_nested_lambda_flow_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-3 nested-lambda execution")
        return SKIP
    producer = Path(argv[0])
    source = (Path(__file__).resolve().parents[1] / "golden" / "wasm"
              / "fb3_nested_lambda_flow.ahfl")
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-3 producer or fb3_nested_lambda_flow.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb3-nlf-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb3_nested_lambda_flow.wasm"
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

        host = td_path / "fb3_nlf_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const initial = Number(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("a pure closure agent expanded import authority");
function hasSection(id) {
  let i = 8;
  const leb = () => { let r = 0, s = 0; while (true) {
    const x = bytes[i++]; r |= (x & 0x7f) << s; if (!(x & 0x80)) break; s += 7; } return r; };
  while (i < bytes.length) { const sid = bytes[i++]; const len = leb(); if (sid === id) return true; i += len; }
  return false;
}
if (!hasSection(4) || !hasSection(9))
  throw new Error("a nested-closure module must carry Table(4) and Element(9) sections");
const {exports: c} = await WebAssembly.instantiate(module, {});
if (c.current_state() !== initial)
  throw new Error(`initial state ${c.current_state()} != ${initial}`);
// f(1) -> hof(inner,1) -> inner(1) = 1 + a + x = 5 routes Init -> Done.
const next = c.step();
if (next !== 1)
  throw new Error(`nested closure chain routed to state ${next}, expected Done(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log("FB-3 nested flow lambda executed through call_indirect");
""",
            encoding="utf-8",
        )
        driven = subprocess.run([node, str(host), str(initial), str(wasm_path)],
                               capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-3 nested-lambda host failed: {driven.stderr}")
        if "call_indirect" not in driven.stdout:
            return fail("host did not confirm call_indirect execution")

    print("OK: RFC 0026 FB-3 nested flow-lambda lift Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
