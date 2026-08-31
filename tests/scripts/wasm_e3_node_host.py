#!/usr/bin/env python3
"""Optional Node persistent-instance execution evidence for KR6.5 E3."""
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
        return fail("usage: wasm_e3_node_host.py <same-frontend-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for E3 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing E3 same-frontend producer or fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-e3-node-") as td:
        artifact = Path(td) / "e3.wasm"
        emitted = subprocess.run(
            [str(producer), str(source), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if emitted.returncode != 0:
            return fail(
                f"E3 producer exited {emitted.returncode}: {emitted.stderr}"
            )
        expected = (
            "schedule=first,second completed_nodes=2 "
            "transition_count=2 identity_output=1"
        )
        if emitted.stdout.strip() != expected:
            return fail(f"malformed native E3 observation: {emitted.stdout!r}")

        script = Path(td) / "host.mjs"
        script.write_text(
            r'''
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("identity workflow expanded import authority");
const {exports: e} = await WebAssembly.instantiate(module, {});
if (e.ahfl_abi_version.value !== 1 || e.workflow_node_count.value !== 2)
  throw new Error("workflow immutable globals mismatch");
for (const name of ["step", "current_state"]) {
  let trapped = false;
  try { e[name](); } catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
  if (!trapped || e.workflow_completed_count.value !== 0 || e.transition_count.value !== 0)
    throw new Error(`${name} did not trap before effects`);
}
const input = new TextEncoder().encode('{"value":"identity"}');
const ptr = e.alloc(input.length);
new Uint8Array(e.memory.buffer, ptr, input.length).set(input);
const first = e.run2(ptr, input.length);
if (first[0] !== 0 || first[1] !== ptr || first[2] !== input.length)
  throw new Error(`run2 identity tuple mismatch: ${first}`);
if (e.workflow_completed_count.value !== 2 || e.transition_count.value !== 2)
  throw new Error("run2 workflow counters mismatch");
const actual = new Uint8Array(e.memory.buffer, ptr, input.length);
if (new TextDecoder().decode(actual) !== new TextDecoder().decode(input))
  throw new Error("borrowed workflow frame was read-modify-written");
const second = e.run2(ptr, input.length);
if (second[0] !== 0 || second[1] !== ptr || second[2] !== input.length ||
    e.workflow_completed_count.value !== 2 || e.transition_count.value !== 2)
  throw new Error("persistent run2 did not reset and replay the full schedule");
if (e.run(ptr, input.length) !== ptr || e.workflow_completed_count.value !== 2 ||
    e.transition_count.value !== 2)
  throw new Error("legacy run did not execute the same identity schedule");
e.dealloc(ptr, input.length);
console.log("E3 Node persistent workflow execution passed");
''',
            encoding="utf-8",
        )
        executed = subprocess.run(
            [node, str(script), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node E3 host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
    print("OK: KR6.5 E3 Node persistent workflow execution passed (not wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
