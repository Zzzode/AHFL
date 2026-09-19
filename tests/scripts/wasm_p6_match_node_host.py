#!/usr/bin/env python3
"""RFC 0026 P6-3 (KR6.6) match-lowering Node execution evidence.

P6-3 lowers `match` to three nested wasm blocks per arm (S = match-completion,
B = the arm chain + fallback, C_i = one arm) with NO relooper, exactly as RFC
0026 Q2 mandates:

    block (S)                     <- a completing arm `br`s here
      block (B)                   <- the arm chain, then the fallback
        block (C_i)               <- one arm
          <binding latches>       <- the scrutinee copied into arm bindings
          <pattern test>; i32.eqz; br_if 0   <- mismatch -> next arm
          <guard>; i32.eqz; br_if 0          <- guard false -> next arm
          <body>                  <- completes (`br` to S) or diverges
        end
        ... remaining arms ...
        <fallback>
      end
    end

The load-bearing details are the `br` DEPTHS (`br 0` must reach the next arm, a
completion `br` must reach S past B and C_i) and the `i32.eqz` inversion of the
pattern test and guard. Getting either wrong is a wasm validation rejection or a
misrouted state id, which is exactly what this script catches.

For each real-frontend fixture the producer reports the native AgentRuntime
state-id observation; this script compiles the emitted module with the Node v22
WebAssembly engine (WebAssembly.compile REJECTS a malformed module) and drives
the stable step() ABI, asserting the reached state ids and transition_count match
native. One fixture (`p6_match_arm_trap.ahfl`) must instead TRAP inside a matched
arm: its native run is `failed`, and step() must raise a real
WebAssembly.RuntimeError with no state transition.

SKIP (77) when the node interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Fixture file name (relative to the golden dir) -> human description. Every
# fixture must COMPLETE natively; the trap fixture is listed separately.
FIXTURES = [
    ("p6_match_enum.ahfl", "statement if-let: tag match + fall-through goto"),
    ("p6_match_fallthrough.ahfl", "statement if-let: no arm matches, fallback runs"),
    ("p6_match_expr.ahfl", "expression match: arm values joined through the result"),
    ("p6_match_guard.ahfl", "guarded arm taken; the unguarded sibling is the fallback"),
    # A payload arm binding latched from its RECORDED SITE. Latching from the
    # scrutinee root instead stores the enum address (i32) into the i64 binding
    # slot, which WebAssembly.compile rejects; a byte-pattern scan cannot see it.
    ("p6_match_binding_payload.ahfl", "payload arm binding latches its own P4-D slot"),
    ("p6_match_or.ahfl", "or-pattern: the second alternative matches"),
    # The scratch/i64 grouping regression: an i32 match-scratch slot and an i64
    # SSA let in ONE handler. A local index that forgets the i32 scratch group
    # lands the i64 let inside the i32 group, which WebAssembly.compile rejects.
    ("p6_match_result_i64.ahfl", "i32 match result + i64 let share one local layout"),
]

TRAP_FIXTURES = [
    ("p6_match_arm_trap.ahfl", "matched arm divides by zero -> RuntimeError"),
]


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_observation(stdout: str) -> dict[str, object]:
    match = re.search(
        r"native_status=(\w+) entered_ids=([0-9,]*) "
        r"final_state_id=(-?\d+) transition_count=(\d+) "
        r"initial_state_id=(\d+)",
        stdout,
    )
    if match is None:
        raise ValueError(f"malformed native observation: {stdout!r}")
    entered = [int(x) for x in match.group(2).split(",") if x]
    return {
        "status": match.group(1),
        "entered_ids": entered,
        "final_state_id": int(match.group(3)),
        "transition_count": int(match.group(4)),
        "initial_state_id": int(match.group(5)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_p6_match_node_host.py <p6-producer> <golden-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 match execution")
        return SKIP
    producer = Path(argv[0])
    golden_dir = Path(argv[1])
    if not producer.is_file() or not golden_dir.is_dir():
        return fail("missing P6 producer or golden directory")
    sources = [(golden_dir / name, desc) for name, desc in FIXTURES]
    traps = [(golden_dir / name, desc) for name, desc in TRAP_FIXTURES]
    if any(not src.is_file() for src, _ in sources + traps):
        return fail("missing a P6 match-lowering fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-match-node-") as td:
        td_path = Path(td)
        cases: list[tuple[str, Path, dict[str, object]]] = []
        trap_cases: list[tuple[str, Path, int]] = []
        for src, desc in sources:
            wasm = td_path / (src.stem + ".wasm")
            native = subprocess.run(
                [str(producer), str(src), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if native.returncode != 0:
                return fail(f"{src.name} producer exited {native.returncode}: "
                            f"{native.stderr}")
            obs = parse_observation(native.stdout.strip())
            if obs["status"] != "completed" or not obs["entered_ids"]:
                return fail(f"{src.name} native run did not complete: {obs}")
            cases.append((desc, wasm, obs))
        for src, desc in traps:
            wasm = td_path / (src.stem + ".wasm")
            native = subprocess.run(
                [str(producer), str(src), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if native.returncode != 0:
                return fail(f"{src.name} producer exited {native.returncode}: "
                            f"{native.stderr}")
            obs = parse_observation(native.stdout.strip())
            if obs["status"] != "failed" or int(obs["transition_count"]) != 0:
                return fail(f"{src.name} native run was expected to fail without a "
                            f"transition: {obs}")
            trap_cases.append((desc, wasm, int(obs["initial_state_id"])))

        host = td_path / "p6_match_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

// Cases come first, then a literal "--trap" separator, then the trap cases.
const trapStart = process.argv.indexOf("--trap");
// Cases: quadruples <wasm> <initial> <entered,csv> <transitions>, up to the marker.
const cases = [];
for (let i = 2; i < trapStart; i += 4) {
  cases.push({
    path: process.argv[i],
    initial: Number(process.argv[i + 1]),
    entered: process.argv[i + 2].split(",").map(Number),
    transitions: Number(process.argv[i + 3]),
  });
}
// Trap cases: pairs <wasm> <initial>, after the marker.
const traps = [];
for (let i = trapStart + 1; i < process.argv.length; i += 2) {
  traps.push({path: process.argv[i], initial: Number(process.argv[i + 1])});
}

for (const c of cases) {
  const bytes = fs.readFileSync(c.path);
  // The regression: a wrong `br` depth (or an unbalanced block/end) makes this
  // compile reject the module before it could ever run.
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
    throw new Error(`${c.path}: native initial id mismatch`);
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
    throw new Error(`${c.path}: state path ${path} != native ${expected}`);
  if (exports.transition_count.value !== c.transitions)
    throw new Error(
      `${c.path}: transition_count ${exports.transition_count.value} != ${c.transitions}`);
}

for (const t of traps) {
  const module = await WebAssembly.compile(fs.readFileSync(t.path));
  const {exports} = await WebAssembly.instantiate(module, {});
  if (exports.current_state() !== t.initial)
    throw new Error(`${t.path}: initial state ${exports.current_state()} != ${t.initial}`);
  let trapped = false;
  try {
    exports.step();
  } catch (error) {
    if (!(error instanceof WebAssembly.RuntimeError))
      throw new Error(`${t.path}: expected a WebAssembly.RuntimeError, got ${error}`);
    trapped = true;
  }
  if (!trapped)
    throw new Error(`${t.path}: matched arm did not trap`);
  if (exports.transition_count.value !== 0 ||
      exports.current_state() !== t.initial)
    throw new Error(`${t.path}: a trapping arm must not transition`);
}
console.log("P6 match-lowering Node compile/step execution passed");
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
        args.append("--trap")
        for _desc, wasm, initial in trap_cases:
            args.extend([str(wasm), str(initial)])
        executed = subprocess.run(args, capture_output=True, text=True, timeout=60)
        if executed.returncode != 0:
            return fail(
                f"Node P6 match host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: RFC 0026 P6-3 match-lowering Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
