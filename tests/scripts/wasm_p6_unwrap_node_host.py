#!/usr/bin/env python3
"""RFC 0026 P6 (CORE-GAPS) Node embedded-engine evidence for `unwrap`.

The producer compiles two real-frontend fixtures:
  * p6_unwrap_some.ahfl       — unwrap(Some(41)) yields the payload 41 and
                                routes the computed goto through High;
  * p6_unwrap_none_trap.ahfl — unwrap(None) reaches the explicit UnwrapFailed
                                fallback; step() raises RuntimeError and leaves
                                state id and transition_count untouched.

The lowering introduces no Core node: unwrap is an expression match
(`Some(x) => x`) with an UnwrapFailed trap fallback. The success path pins
payload-slot extraction of a generic Option<Int>; the trap path pins the
fallback `unreachable`. SKIP (77) when node is unavailable.
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


def parse_observation(stdout: str) -> dict[str, object]:
    match = re.search(
        r"native_status=(\w+) entered_ids=([0-9,]*) "
        r"final_state_id=(-?\d+) transition_count=(\d+) "
        r"initial_state_id=(\d+)",
        stdout,
    )
    if match is None:
        raise ValueError(f"malformed native observation: {stdout!r}")
    return {
        "status": match.group(1),
        "entered_ids": [int(x) for x in match.group(2).split(",") if x],
        "final_state_id": int(match.group(3)),
        "transition_count": int(match.group(4)),
        "initial_state_id": int(match.group(5)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        return fail("usage: wasm_p6_unwrap_node_host.py <p6-producer> "
                    "<some-source> <none-trap-source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for P6 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    some_source = Path(argv[1])
    none_source = Path(argv[2])
    if not producer.is_file() or not some_source.is_file() or not none_source.is_file():
        return fail("missing P6 producer or unwrap fixtures")

    with tempfile.TemporaryDirectory(prefix="ahfl-p6-unwrap-") as td:
        td_path = Path(td)
        some_wasm = td_path / "p6_unwrap_some.wasm"
        none_wasm = td_path / "p6_unwrap_none.wasm"

        some_native = subprocess.run(
            [str(producer), str(some_source), str(some_wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if some_native.returncode != 0:
            return fail(f"Some producer exited {some_native.returncode}: {some_native.stderr}")
        some_obs = parse_observation(some_native.stdout.strip())
        if some_obs["status"] != "completed" or not some_obs["entered_ids"]:
            return fail(f"native unwrap-Some run did not complete: {some_obs}")

        none_native = subprocess.run(
            [str(producer), str(none_source), str(none_wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if none_native.returncode != 0:
            return fail(f"None producer exited {none_native.returncode}: {none_native.stderr}")
        none_obs = parse_observation(none_native.stdout.strip())

        host = td_path / "p6_unwrap_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

// --- unwrap(Some(41)): payload extraction routes the goto High ---
const someBytes = fs.readFileSync(process.argv[2]);
const someModule = await WebAssembly.compile(someBytes);
if (WebAssembly.Module.imports(someModule).length !== 0)
  throw new Error("unwrap agent expanded import authority");
const {exports: c} = await WebAssembly.instantiate(someModule, {});
if (c.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");

const expectedInitial = Number(process.argv[3]);
const expectedEntered = process.argv[4].split(",").map(Number);
const expectedSequence = expectedEntered.slice(1);
const expectedTransitions = Number(process.argv[5]);
if (c.current_state() !== expectedInitial)
  throw new Error(`initial state ${c.current_state()} != ${expectedInitial}`);
const sequence = [];
let previous = expectedInitial;
let guard = expectedSequence.length + 2;
while (guard-- > 0) {
  const next = c.step();
  sequence.push(next);
  if (next !== previous) {
    previous = next;
    continue;
  }
  break;
}
const observedPath = sequence.slice(0, expectedSequence.length);
if (JSON.stringify(observedPath) !== JSON.stringify(expectedSequence))
  throw new Error(`unwrap state-id path ${observedPath} != native ${expectedSequence} ` +
    "(payload slot not extracted)");
if (c.transition_count.value !== expectedTransitions)
  throw new Error(`transition_count ${c.transition_count.value} != ${expectedTransitions}`);

// --- unwrap(None): UnwrapFailed fallback traps; state and counter unchanged ---
const noneBytes = fs.readFileSync(process.argv[6]);
const {exports: t} = await WebAssembly.instantiate(
  await WebAssembly.compile(noneBytes), {});
const trapInitial = t.current_state();
const trapCount = t.transition_count.value;
let trapped = false;
try {
  t.step();
} catch (error) {
  trapped = error instanceof WebAssembly.RuntimeError;
}
if (!trapped)
  throw new Error("unwrap(None) did not trap with UnwrapFailed");
if (t.current_state() !== trapInitial || t.transition_count.value !== trapCount)
  throw new Error("unwrap trap mutated state or transition_count");

console.log("P6 unwrap Some/None Node step execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(some_wasm),
                str(some_obs["initial_state_id"]),
                ",".join(str(x) for x in some_obs["entered_ids"]),
                str(some_obs["transition_count"]),
                str(none_wasm),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node unwrap host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
        if none_obs["status"] != "failed" or none_obs["transition_count"] != 0:
            return fail(f"native unwrap-None fixture unexpectedly non-failing: {none_obs}")

    print("OK: CORE-GAPS unwrap Node embedded-engine execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
