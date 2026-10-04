#!/usr/bin/env python3
"""RFC 0026 P6-2 (KR6.6) structured-control-flow Node execution evidence.

P6-1 emitted a computed-goto handler INLINE in step()'s dispatch arm. P6-2
compiles every such handler to its OWN `() -> i32` wasm function whose body is

    block (result i32)          <- the structured early-exit target
      ... lowered region ...
      (goto == latch globals; br <label_depth> with the new state id)
    end

so `step()` becomes a thin dispatch ladder that CALLs the handler. The load
bearing detail is the `br` DEPTH: a goto nested inside k statement-level ifs
must skip all k labels to reach the handler block. Emitting `br 0` (or any wrong
depth) lands on an inner `if` instead, which the wasm validator rejects or which
misroutes the state id -- exactly the class of bug this script exists to catch.

For each real-frontend fixture the producer compiles the wasm; this script
compiles the emitted module with the Node v22 WebAssembly engine
(WebAssembly.compile REJECTS a malformed module) and drives the stable
step() ABI, asserting the reached state ids and transition_count match the
frozen oracles captured at HEAD 620fadd8 (WH-9 B0).

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
    ("p6_cascade.ahfl", "4-way cascade, second arm taken (br depth 1)"),
    ("p6_cascade_high.ahfl", "4-way cascade, outermost arm taken"),
    ("p6_elseless_fallthrough.ahfl", "else-less nested if, implicit fall-through"),
    ("p6_elseless_taken.ahfl", "else-less nested if, inner arm taken (br depth 2)"),
]

# Frozen oracles (WH-9 B0, HEAD 620fadd8), keyed by fixture stem.
FROZEN_OBS: dict[str, dict[str, object]] = {
    "p6_cascade": {
        "status": "completed", "entered_ids": [5, 2, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 5,
    },
    "p6_cascade_high": {
        "status": "completed", "entered_ids": [5, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 5,
    },
    "p6_elseless_fallthrough": {
        "status": "completed", "entered_ids": [3, 2, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
    "p6_elseless_taken": {
        "status": "completed", "entered_ids": [3, 1, 0],
        "final_state_id": 0, "transition_count": 2, "initial_state_id": 3,
    },
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_structured_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 structured-control-flow execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    if not producer.is_file() or not golden_dir.is_dir():
        return fail("missing P6 producer or golden directory")
    sources = [(golden_dir / name, desc) for name, desc in FIXTURES]
    if any(not src.is_file() for src, _ in sources):
        return fail("missing a P6 structured-control-flow fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-structured-node-") as td:
        td_path = Path(td)
        cases: list[tuple[str, Path, dict[str, object]]] = []
        for src, desc in sources:
            wasm = td_path / (src.stem + ".wasm")
            compile_run = subprocess.run(
                [str(producer), str(src), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if compile_run.returncode != 0:
                return fail(f"{src.name} producer exited {compile_run.returncode}: "
                            f"{compile_run.stderr}")
            obs = FROZEN_OBS[src.stem]
            cases.append((desc, wasm, obs))

        host = td_path / "p6_structured_host.mjs"
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
  // The regression: a wrong `br` label depth (or an unbalanced if/else/end)
  // makes this compile reject the module before it could ever run.
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
console.log("P6 structured-control-flow Node compile/step execution passed");
''',
            encoding="utf-8",
        )

        args: list[str] = [node, str(host)]
        for _desc, wasm, obs in cases:
            args.extend([
                str(wasm),
                str(obs["initial_state_id"]),
                ",".join(str(x) for x in obs["entered_ids"]),
                str(obs["transition_count"]),
            ])
        executed = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if executed.returncode != 0:
            return fail(
                f"Node P6 structured host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-2 structured-control-flow Node embedded-engine "
          "execution passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
