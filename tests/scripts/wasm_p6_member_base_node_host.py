#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) Node embedded-engine evidence for member access on
arbitrary aggregate bases.

A dotted IDENTIFIER is one grammar PathExpr, so `pair.x` / `input.a` were
already lowerable; MemberAccessExpr only wraps a NON-PATH primary. The fixture
reads a field off:
  * a parenthesized aggregate local            `(pair).x`;
  * a struct-constructor result                `(Pair{...}).y`;
  * a PURE outlined-fn call result             `make_pair(5).y`;
  * a match expression yielding an aggregate   `(match m {...}).y`.

The wasm state-id path (Decide=3 -> High=1 -> Done=0) is reachable only when
every read selects the correct P4-D field. The wasm emission succeeds and this
host drives the stable step() ABI directly. SKIP (77) when node is
unavailable.
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
    if len(argv) != 2:
        return fail("usage: wasm_p6_member_base_node_host.py <p6-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing P6 producer or member-base fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-member-") as td:
        td_path = Path(td)
        wasm_path = td_path / "p6_member_base.wasm"

        compile_run = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if compile_run.returncode != 0:
            # The probe exits non-zero only when Core lowering / layout / wasm
            # emission fails.
            return fail(f"P6 producer (Core/wasm path) failed: {compile_run.stderr}")

        host = td_path / "p6_member_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

const bytes = fs.readFileSync(process.argv[2]);
const module_ = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module_).length !== 0)
  throw new Error("member-base agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(module_, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

// Done=0 High=1 Low=2 Decide=3. Every arbitrary-base field read must select
// its field (px==1, cy==60, fy==105, my==70); any missed/lowered-wrong read
// routes to Low (2).
const expected = [1, 0];
if (c.current_state() !== 3)
  throw new Error(`initial state ${c.current_state()} != 3`);
const observed = [];
let previous = 3;
let guard = 5;
while (guard-- > 0) {
  const next = c.step();
  observed.push(next);
  if (next !== previous) {
    previous = next;
    continue;
  }
  break;
}
const path = observed.slice(0, expected.length);
if (JSON.stringify(path) !== JSON.stringify(expected))
  throw new Error(`arbitrary-base member access routed ${path}, expected ${expected}`);
if (c.transition_count.value !== 2)
  throw new Error(`transition_count ${c.transition_count.value} != 2`);

console.log("P6 arbitrary-base member access Node step execution passed");
''',
            encoding="utf-8",
        )

        driven = subprocess.run(
            [node, str(host), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if driven.returncode != 0:
            return fail(f"Node member-base host failed: {driven.stderr}")

    print("OK: CORE-GAPS arbitrary-base member access Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
