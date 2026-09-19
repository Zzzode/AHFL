#!/usr/bin/env python3
"""KR6.7 (RFC 0026 P7) WASM eligibility Node execution evidence.

`ahfl.conformance.wasm_eligibility` classified the `float_output_e2e` case as
`runnable_orchestration` -- and that promotion is only honest if the emitted
module really executes. This lane is the execution witness for the case the
classifier MOVED across the eligibility boundary.

The producer is asked for the WORKFLOW lane's artifact for the manifest entry
(`--workflow runtime::float_output_e2e::FloatPipeline`), which is the artifact
the classifier certified: it resolves the entry through the same
`resolve_core_wasm_entry` seam with the manifest's typed kind + entry, instead of
the agent-lane `CoreAgentId{0}` sibling the old wiring handed the host.

HONEST SCOPE -- read before extending. The orchestration lane is LAYOUT-FREE for
identity frames: the emitted workflow runner returns `(OK, input_ptr, input_len)`
and the legacy `run` returns its own input pointer (`make_workflow_run_body` +
`make_workflow_runner_body`), so neither ever performs a wasm load/store of a
frame field. The observable evidence is therefore the module's own execution
contract, not a byte-offset readback:

  * the module instantiates under the Node engine with no imports;
  * `step()` / `current_state()` trap (RuntimeError) BEFORE any effect, which is
    the workflow module's fail-closed contract (they are stubs there);
  * `run2(ptr, len)` executes the full schedule and returns the identity
    `(OK, ptr, len)` tuple;
  * `workflow_completed_count` / `transition_count` reach the native counters
    the producer reports;
  * the returned frame IS the borrowed input frame: a poison sentinel written
    into it is returned verbatim and NO byte of it is modified, which is what
    "identity passthrough" actually means here.

This pins the lane's real soundness property -- the certified module executes
and its counters match the native runtime -- and does NOT claim to pin an f64
field size/align, because no field access happens on this lane. A codegen change
that biased an f64 field offset would leave this artifact byte-identical; that
is a property of the orchestration subset, and the P6 Node lanes (which DO
perform layout-derived loads/stores) are where field offsets are pinned.

This is embedded-engine evidence, NOT wasmtime evidence. SKIP (77) when the node
interpreter is unavailable.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# The manifest entry whose module the classifier certified. Passed to the
# producer's `--workflow` mode so the artifact under test is the certified one.
CASE_ENTRY = "runtime::float_output_e2e::FloatPipeline"

# The poison sentinel written across the borrowed frame. Not zero: a frame that
# was copied from a zero-initialized source, or truncated, would still read back
# zeros, so the witness has to be distinguishable in every byte.
POISON = bytes([0xAB] * 16)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def parse_observation(stdout: str) -> dict[str, int]:
    match = re.search(
        r"workflow_status=(\w+) completed_nodes=(\d+) transition_count=(\d+)",
        stdout,
    )
    if match is None:
        raise ValueError(f"malformed workflow observation: {stdout!r}")
    return {
        "completed_nodes": int(match.group(2)),
        "transition_count": int(match.group(3)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail(
            "usage: wasm_eligibility_node_host.py <p6-producer> <float-identity-source>"
        )
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for wasm eligibility execution evidence")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing the P6 producer or the float identity fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-eligibility-node-") as td:
        td_path = Path(td)
        wasm = td_path / (source.stem + ".wasm")
        native = subprocess.run(
            [str(producer), "--workflow", CASE_ENTRY, str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"producer exited {native.returncode}: {native.stderr}")
        obs = parse_observation(native.stdout.strip())

        host = td_path / "eligibility_host.mjs"
        host.write_text(
            r'''
import fs from "node:fs";

const bytes = fs.readFileSync(process.argv[2]);
const poison = Buffer.from(process.argv[3], "hex");
const expectedCompleted = Number(process.argv[4]);
const expectedTransitions = Number(process.argv[5]);

// A malformed module is a hard WebAssembly.compile rejection.
const module = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(module).length !== 0)
  throw new Error("f64 identity workflow expanded import authority");
const {exports: e} = await WebAssembly.instantiate(module, {});
if (e.ahfl_abi_version.value !== 1)
  throw new Error("abi version mismatch");
if (e.workflow_node_count.value !== 1)
  throw new Error(`workflow_node_count ${e.workflow_node_count.value} != 1`);

// The workflow lane's fail-closed contract: step()/current_state() are stubs
// that trap BEFORE any effect (a PENDING must never leak through them).
for (const name of ["step", "current_state"]) {
  let trapped = false;
  try { e[name](); } catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
  if (!trapped)
    throw new Error(`${name} did not trap before effects`);
  if (e.workflow_completed_count.value !== 0 || e.transition_count.value !== 0)
    throw new Error(`${name} had an effect before trapping`);
}

// Poison the destination region, then execute the real workflow entry. The
// identity passthrough returns the BORROWED input frame untouched, so the
// sentinel must come back verbatim and no byte of the frame may change -- which
// is the honest content of "identity passthrough" on this lane.
const ptr = e.alloc(poison.length);
if (ptr === 0)
  throw new Error("alloc returned the null frame");
new Uint8Array(e.memory.buffer, ptr, poison.length).set(poison);

const tuple = e.run2(ptr, poison.length);
if (tuple[0] !== 0 || tuple[1] !== ptr || tuple[2] !== poison.length)
  throw new Error(`run2 identity tuple mismatch: ${tuple}`);
if (e.workflow_completed_count.value !== expectedCompleted)
  throw new Error(
    `workflow_completed_count ${e.workflow_completed_count.value} != ${expectedCompleted}`);
if (e.transition_count.value !== expectedTransitions)
  throw new Error(
    `transition_count ${e.transition_count.value} != ${expectedTransitions}`);

const returned = Buffer.from(new Uint8Array(e.memory.buffer, tuple[1], tuple[2]));
if (!returned.equals(poison))
  throw new Error("the borrowed frame bytes were not returned verbatim");
if (tuple[1] !== ptr)
  throw new Error("run2 did not return the borrowed input frame");

// The legacy run entry executes the same identity schedule and returns the same
// borrowed pointer.
if (e.run(ptr, poison.length) !== ptr)
  throw new Error("legacy run did not return the borrowed input frame");
if (e.workflow_completed_count.value !== expectedCompleted ||
    e.transition_count.value !== expectedTransitions)
  throw new Error("legacy run did not replay the same identity schedule");

e.dealloc(ptr, poison.length);
console.log("wasm eligibility workflow execution passed");
''',
            encoding="utf-8",
        )

        executed = subprocess.run(
            [
                node, str(host), str(wasm), POISON.hex(),
                str(obs["completed_nodes"]),
                str(obs["transition_count"]),
            ],
            capture_output=True, text=True, timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node eligibility host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )

    print("OK: KR6.7 wasm eligibility workflow execution passed "
          "(real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
