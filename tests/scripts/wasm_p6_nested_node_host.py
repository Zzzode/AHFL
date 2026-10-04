#!/usr/bin/env python3
"""RFC 0026 P6-1 (KR6.6) nested-control-flow Node execution evidence.

The flat p6_scalar_cond fixture only ever emits a single terminal if, so it
cannot catch a blocktype that is chosen from divergence alone. This script
covers the shapes that used to make emit_core_wasm produce an INVALID module:

  * a fully-diverging if nested inside a fall-through (void) outer if,
  * the same outer guard actually TAKEN (inner if entered),
  * an else-less nested if and its implicit-else fall-through,
  * a fully-diverging if nested three void blocks deep, and its taken path.

For each real-frontend fixture the producer compiles the wasm; this script
compiles the emitted module with the Node v22 WebAssembly engine
(WebAssembly.compile REJECTS a malformed module, which is the exact
regression) and drives the stable step() ABI, asserting the reached state ids
and transition_count match the frozen oracles captured at HEAD 620fadd8
(WH-9 B0).

SKIP (77) when the node interpreter is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Fixture file name (relative to the golden dir) -> human description.
FIXTURES = [
    "p6_nested_fallthrough.ahfl",
    "p6_nested_taken_high.ahfl",
    "p6_nested_elseless.ahfl",
    "p6_nested_elseless_taken.ahfl",
    "p6_nested_depth3.ahfl",
    "p6_nested_depth3_taken.ahfl",
    # P6-4: a projection chain that descends THROUGH a nested struct field. The
    # field's slot holds the child aggregate's address, so the walk must deref.
    # A well-formed-but-wrong body reads the parent's own first slot and takes
    # the wrong branch, which only a real-engine state-path comparison catches.
    "p6_nested_projection.ahfl",
]

# Frozen oracles (WH-9 B0, HEAD 620fadd8), keyed by fixture stem.
FROZEN_OBS: dict[str, dict[str, object]] = {
    "p6_nested_fallthrough": {
        "status": "completed", "entered_ids": [4, 3, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 4,
    },
    "p6_nested_taken_high": {
        "status": "completed", "entered_ids": [4, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 4,
    },
    "p6_nested_elseless": {
        "status": "completed", "entered_ids": [3, 2, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
    "p6_nested_elseless_taken": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
    "p6_nested_depth3": {
        "status": "completed", "entered_ids": [4, 3, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 4,
    },
    "p6_nested_depth3_taken": {
        "status": "completed", "entered_ids": [4, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 4,
    },
    "p6_nested_projection": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_nested_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 nested-host execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    if not producer.is_file() or not golden_dir.is_dir():
        return fail("missing P6 producer or golden directory")
    sources = [golden_dir / name for name in FIXTURES]
    if any(not src.is_file() for src in sources):
        return fail("missing a P6 nested-control-flow fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-nested-node-") as td:
        td_path = Path(td)
        cases: list[tuple[str, Path, dict[str, object]]] = []
        for src in sources:
            wasm = td_path / (src.stem + ".wasm")
            compile_run = subprocess.run(
                [str(producer), str(src), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if compile_run.returncode != 0:
                return fail(f"{src.name} producer exited {compile_run.returncode}: "
                            f"{compile_run.stderr}")
            obs = FROZEN_OBS[src.stem]
            cases.append((src.stem, wasm, obs))

        host = td_path / "p6_nested_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

// argv pairs: <wasm> <initial> <entered,csv> <transitions>, repeated.
const cases = [];
for (let i = 2; i < process.argv.length; i += 4) {
  cases.push({
    path: process.argv[i],
    initial: Number(process.argv[i + 1]),
    entered: process.argv[i + 2].split(",").map(Number),
    transitions: Number(process.argv[i + 3]),
  });
}

for (const c of cases) {
  const bytes = fs.readFileSync(c.path);
  // The regression: a divergence-only i32 blocktype nested in a void branch
  // made this compile reject the module before it could ever run.
  const module = await WebAssembly.compile(bytes);
  if (WebAssembly.Module.imports(module).length !== 0)
    throw new Error(`${c.path}: scalar agent expanded import authority`);
  const {exports} = await WebAssembly.instantiate(module, {});
  if (exports.ahfl_abi_version.value !== 1)
    throw new Error(`${c.path}: abi version mismatch`);
  // Native observation includes the initial state first; step() results begin
  // AFTER the first transition.
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
console.log("P6 nested-control-flow Node compile/step execution passed");
''',
            encoding="utf-8",
        )

        args: list[str] = [node, str(host)]
        for _stem, wasm, obs in cases:
            args.extend([
                str(wasm),
                str(obs["initial_state_id"]),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["transition_count"]),
            ])
        executed = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if executed.returncode != 0:
            return fail(
                f"Node P6 nested host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-1 nested-control-flow Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
