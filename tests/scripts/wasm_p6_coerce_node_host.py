#!/usr/bin/env python3
"""RFC 0026 P6-6 (KR6.6) coercion physical-effect Node execution evidence.

P6-6 lifts the KR6.5 `coercion_plans` arena gate: a `CoreCoerceExpr` now lowers
to a real physical effect derived from the P4-D layout table. This script is the
execution witness for the ONE real conversion the P6 value model performs plus
its layout-derived negative:

  * `p6_coerce.ahfl` — `BoundedInt(0..100) <: Int` where the bounded side is the
    wasm32 i32 scalar repr and the target is i64. The `IntWiden` proof op names a
    repr growth, so the handler function must contain `i64.extend_i32_s` (0xac).
    The widened value is NEGATIVE in source, so a dropped extension (or a
    zero-extension) reads it as a huge positive and takes Low: the state-id path
    is what pins sign extension.
  * `p6_coerce_bounds.ahfl` — a BOUNDS RELAXATION whose endpoints are the same
    P4-D bytes. The layout check proves the coercion physically a no-op, so its
    handler must contain NO widening instruction. Emitting one would be a
    wasted (and, for a negative operand, wrong) conversion.

Both modules are compiled with the real Node v22 WebAssembly engine
(`WebAssembly.compile` rejects a malformed module outright) and driven through
the stable step() ABI, asserting the reached state ids and transition_count
match the frozen oracles captured at HEAD 620fadd8 (WH-9 B0). The opcode
assertions read the module's CODE section with a real LEB128 walker, not a
whole-file byte scan.

This is embedded-engine evidence, NOT wasmtime evidence. SKIP (77) when the node
interpreter is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# The two coercion shapes: file name -> whether the layout-derived physical
# effect is a real i32 -> i64 widening.
FIXTURES = [
    ("p6_coerce.ahfl", True),
    ("p6_coerce_bounds.ahfl", False),
]

# Frozen oracles (WH-9 B0, HEAD 620fadd8), keyed by fixture stem.
FROZEN_OBS: dict[str, dict[str, object]] = {
    "p6_coerce": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
    "p6_coerce_bounds": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_coerce_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 coercion execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    if not producer.is_file() or not golden_dir.is_dir():
        return fail("missing P6 producer or golden directory")
    sources = [(golden_dir / name, widens) for name, widens in FIXTURES]
    if any(not src.is_file() for src, _ in sources):
        return fail("missing a P6 coercion fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-coerce-node-") as td:
        td_path = Path(td)
        cases: list[tuple[str, Path, dict[str, object], bool]] = []
        for src, widens in sources:
            wasm = td_path / (src.stem + ".wasm")
            compile_run = subprocess.run(
                [str(producer), str(src), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if compile_run.returncode != 0:
                return fail(f"{src.name} producer exited {compile_run.returncode}: {compile_run.stderr}")
            obs = FROZEN_OBS[src.stem]
            cases.append((src.stem, wasm, obs, widens))

        host = td_path / "p6_coerce_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

const kOpI64ExtendI32S = 0xac;
const kSectionCode = 10;

// Minimal unsigned LEB128 reader. `cursor` is a one-field mutable holder so a
// sequence of reads advances a single offset (the JS mirror of the C++ tests'
// `read_u32_leb(bytes, offset)`).
function readU32Leb(bytes, cursor) {
  let value = 0;
  for (let shift = 0; shift < 35; shift += 7) {
    if (cursor.offset >= bytes.length) throw new Error("truncated u32 LEB");
    const byte = bytes[cursor.offset++];
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) return value >>> 0;
  }
  throw new Error("overlong u32 LEB");
}

// Extract every function body from the CODE section (the byte range of each
// entry's instruction stream), the same walk the C++ backend tests perform.
function functionBodies(bytes) {
  const bodies = [];
  let offset = 8; // magic + version
  while (offset < bytes.length) {
    const section = bytes[offset++];
    const cursor = {offset};
    const size = readU32Leb(bytes, cursor);
    offset = cursor.offset;
    const end = offset + size;
    if (section !== kSectionCode) {
      offset = end;
      continue;
    }
    const count = readU32Leb(bytes, cursor);
    offset = cursor.offset;
    for (let i = 0; i < count; ++i) {
      const bodySize = readU32Leb(bytes, cursor);
      const start = cursor.offset;
      bodies.push(bytes.subarray(start, start + bodySize));
      cursor.offset = start + bodySize;
    }
    break;
  }
  return bodies;
}

// argv runs: <wasm> <initial> <entered,csv> <transitions> <widens01>, repeated.
const cases = [];
for (let i = 2; i + 4 < process.argv.length; i += 5) {
  cases.push({
    path: process.argv[i],
    initial: Number(process.argv[i + 1]),
    entered: process.argv[i + 2].split(",").map(Number),
    transitions: Number(process.argv[i + 3]),
    widens: process.argv[i + 4] === "1",
  });
}
if (cases.length === 0) throw new Error("no coercion cases supplied");

for (const c of cases) {
  const bytes = fs.readFileSync(c.path);
  // The regression this slice could introduce: a malformed handler body (an
  // extension emitted with the wrong operand type, or a stray instruction in
  // the no-op case) makes WebAssembly.compile reject the module outright.
  const module = await WebAssembly.compile(bytes);
  if (WebAssembly.Module.imports(module).length !== 0)
    throw new Error(`${c.path}: scalar agent expanded import authority`);

  // Layout-derived opcode evidence: the extension appears IFF the P4-D layout
  // check proved a repr growth. A whole-module byte scan would false-positive
  // (0xac is a legal immediate byte), so the walk reads CODE-section bodies.
  const bodies = functionBodies(bytes);
  if (bodies.length === 0) throw new Error(`${c.path}: no code section bodies`);
  const extensions = bodies.reduce(
    (n, body) => n + body.reduce((m, b) => m + (b === kOpI64ExtendI32S ? 1 : 0), 0), 0);
  if (c.widens && extensions !== 1)
    throw new Error(
      `${c.path}: i32 -> i64 widening expected exactly one i64.extend_i32_s, found ${extensions}`);
  if (!c.widens && extensions !== 0)
    throw new Error(
      `${c.path}: same-layout coercion emitted ${extensions} widening instructions ` +
      "(a physical no-op must emit none)");

  const {exports} = await WebAssembly.instantiate(module, {});
  if (exports.ahfl_abi_version.value !== 1)
    throw new Error(`${c.path}: abi version mismatch`);
  const expected = c.entered.slice(1);
  if (c.entered[0] !== c.initial)
    throw new Error(`${c.path}: frozen initial id mismatch`);
  if (exports.current_state() !== c.initial)
    throw new Error(`${c.path}: initial state ${exports.current_state()} != ${c.initial}`);
  const sequence = [];
  let previous = c.initial;
  let guard = expected.length + 2;
  while (guard-- > 0) {
    const before = exports.transition_count.value;
    const next = exports.step();
    sequence.push(next);
    if (next !== previous) {
      if (exports.current_state() !== next)
        throw new Error(`${c.path}: current_state does not match step result`);
      if (exports.transition_count.value !== before + 1)
        throw new Error(`${c.path}: transition_count not bumped exactly once`);
      previous = next;
      continue;
    }
    if (exports.current_state() !== next)
      throw new Error(`${c.path}: final state is not stable`);
    break;
  }
  const path = sequence.slice(0, expected.length);
  if (JSON.stringify(path) !== JSON.stringify(expected))
    throw new Error(`${c.path}: state path ${path} != oracle ${expected}`);
  if (exports.transition_count.value !== c.transitions)
    throw new Error(
      `${c.path}: transition_count ${exports.transition_count.value} != ${c.transitions}`);
}
console.log("P6 coercion Node compile/step execution passed");
''',
            encoding="utf-8",
        )

        args: list[str] = [node, str(host)]
        for _stem, wasm, obs, widens in cases:
            args.extend([
                str(wasm),
                str(obs["initial_state_id"]),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["transition_count"]),
                "1" if widens else "0",
            ])
        executed = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if executed.returncode != 0:
            return fail(
                f"Node P6 coercion host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-6 coercion Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
