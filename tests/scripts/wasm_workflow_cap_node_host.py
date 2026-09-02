#!/usr/bin/env python3
"""RFC 0026 KR6.5 E4-B2-C capability-workflow Node embedded-host execution.

Real Node-engine evidence (NOT wasmtime evidence, NOT durable-resume evidence).
Drives the emit-only capability-workflow producer's module through Node with a
mock `ahfl_cap` import to exercise the four capability statuses (OK / PENDING /
ERROR / unknown), the OK cap->identity frame forwarding, the node-event buffer
record layout + final counts, the pending-latch re-entry negative, the
pending-nonnull non-latching ERROR, the corrupt-count defensive coordinate gate,
and the checked-alloc fail/no-advance exact sequence. Each status runs on a FRESH
module instance. Node observes only final layout/counts; the body-before-count
WRITE ORDER is an opcode-order contract owned by wasm_backend.cpp.
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
        return fail("usage: wasm_workflow_cap_node_host.py <emit-only-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for capability-workflow embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing capability-workflow producer or fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-b2c-node-") as td:
        artifact = Path(td) / "capwf.wasm"
        emitted = subprocess.run(
            [str(producer), str(source), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if emitted.returncode != 0:
            return fail(f"producer exited {emitted.returncode}: {emitted.stderr}")
        if "import_count=1 packaged_instances=2" not in emitted.stdout:
            return fail(f"malformed producer observation: {emitted.stdout!r}")

        script = Path(td) / "host.mjs"
        script.write_text(
            r'''
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
const module = await WebAssembly.compile(bytes);
const listed = WebAssembly.Module.imports(module);
if (listed.length !== 1 || listed[0].module !== "ahfl_cap" ||
    !listed[0].name.startsWith("cap_")) throw new Error("bad import catalogue");
const field = listed[0].name;

const EVENT_BASE = 1024;
const RECORDS_BASE = 1032;
const RECORD = 40;
const HEAP_BASE = 1112; // N=2 nodes: align_up(1024 + 8 + 2*40, 8)
const enc = new TextEncoder();
const dec = new TextDecoder();
const payload = enc.encode("workflow-input");

async function fresh(mode) {
  let instance;
  let calls = 0;
  const callback = (ptr, len) => {
    calls += 1;
    if (dec.decode(new Uint8Array(instance.exports.memory.buffer, ptr, len)) !==
        "workflow-input") {
      throw new Error("opaque input frame bytes changed");
    }
    if (mode === "ok" || mode === "corrupt-count") {
      if (mode === "corrupt-count") {
        // Tamper the event_count header BEFORE returning a legal OK frame; the
        // scheduler's defensive coordinate gate must reject it.
        new DataView(instance.exports.memory.buffer).setUint32(EVENT_BASE, 99, true);
      }
      const out = instance.exports.alloc(payload.length);
      new Uint8Array(instance.exports.memory.buffer, out, payload.length).set(payload);
      return [0, out, payload.length];
    }
    if (mode === "ok-null") return [0, 0, 0];
    if (mode === "ok-zero-len") return [0, 1234, 0];
    if (mode === "error") return [1, 1234, 9];
    if (mode === "pending") return [2, 0, 99];
    if (mode === "pending-nonnull") return [2, 1234, 9];
    return [77, 1234, 9];
  };
  instance = await WebAssembly.instantiate(module, {ahfl_cap: {[field]: callback}});
  return {
    instance,
    calls: () => calls,
    input: () => {
      const ptr = instance.exports.alloc(payload.length);
      new Uint8Array(instance.exports.memory.buffer, ptr, payload.length).set(payload);
      return [ptr, payload.length];
    },
    eventCount: () =>
        new DataView(instance.exports.memory.buffer).getUint32(EVENT_BASE, true),
  };
}

function recStr(r) {
  return JSON.stringify(r, (k, v) => (typeof v === "bigint" ? v.toString() : v));
}

function readRecord(host, index) {
  const dv = new DataView(host.instance.exports.memory.buffer);
  const base = RECORDS_BASE + index * RECORD;
  return {
    tag: dv.getUint8(base + 0),
    pad1: dv.getUint8(base + 1), pad2: dv.getUint8(base + 2), pad3: dv.getUint8(base + 3),
    node: dv.getUint32(base + 4, true),
    sched: dv.getUint32(base + 8, true),
    cap: dv.getUint32(base + 12, true),
    src: dv.getBigUint64(base + 16, true),
    ord: dv.getBigUint64(base + 24, true),
    status: dv.getUint32(base + 32, true),
    resv: dv.getUint32(base + 36, true),
  };
}

// ---- OK: cap->identity forwarding, two records, event_count == 2 -----------
{
  const host = await fresh("ok");
  const result = host.instance.exports.run2(...host.input());
  if (result[0] !== 0) throw new Error("OK run2 did not return OK");
  if (host.calls() !== 1) throw new Error("OK expected exactly one import call");
  // P0-1: the workflow output frame is non-null and forwards the capability
  // result (identity `second` returns `first`'s echo output) byte-for-byte.
  if (result[1] === 0 || result[2] !== payload.length)
    throw new Error("OK output frame is null or wrong length: " + result);
  const outBytes = new Uint8Array(host.instance.exports.memory.buffer, result[1], result[2]);
  if (dec.decode(outBytes) !== "workflow-input")
    throw new Error("OK output frame bytes are not the forwarded capability result");
  if (host.eventCount() !== 2) throw new Error("OK event_count != 2");
  const r0 = readRecord(host, 0); // node0 = capability (first)
  if (r0.tag !== 1 || r0.node !== 0 || r0.sched !== 0 || r0.cap !== 0 ||
      r0.src !== 1n || r0.ord !== 0n || r0.status !== 0 ||
      r0.pad1 !== 0 || r0.pad2 !== 0 || r0.pad3 !== 0 || r0.resv !== 0)
    throw new Error("capability record layout mismatch: " + recStr(r0));
  const r1 = readRecord(host, 1); // node1 = identity (second)
  if (r1.tag !== 0 || r1.node !== 1 || r1.sched !== 1 || r1.cap !== 0 ||
      r1.src !== 0n || r1.ord !== 0n || r1.status !== 0 ||
      r1.pad1 !== 0 || r1.pad2 !== 0 || r1.pad3 !== 0 || r1.resv !== 0)
    throw new Error("identity record layout mismatch: " + recStr(r1));
}

// ---- non-OK normalization: no event, count stays 0, one call ---------------
for (const mode of ["ok-null", "ok-zero-len", "error", "unknown"]) {
  const host = await fresh(mode);
  const result = host.instance.exports.run2(...host.input());
  if (result[0] !== 1 || result[1] !== 0 || result[2] !== 0)
    throw new Error(mode + " did not normalize to (ERROR,0,0): " + result);
  if (host.calls() !== 1) throw new Error(mode + " expected one import call");
  if (host.eventCount() !== 0) throw new Error(mode + " wrote an event on non-OK");
}

// ---- P0-2 pending-nonnull: ERROR and does NOT latch; a 2nd run2 re-runs -----
{
  const host = await fresh("pending-nonnull");
  const first = host.instance.exports.run2(...host.input());
  if (first[0] !== 1 || first[1] !== 0 || first[2] !== 0)
    throw new Error("pending-nonnull did not normalize to (ERROR,0,0)");
  if (host.calls() !== 1) throw new Error("pending-nonnull expected one import call");
  if (host.eventCount() !== 0) throw new Error("pending-nonnull wrote an event");
  // No latch was set: the same instance accepts another run2 (no trap).
  let trapped = false;
  let second;
  try { second = host.instance.exports.run2(...host.input()); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (trapped) throw new Error("pending-nonnull incorrectly latched the instance");
  if (second[0] !== 1 || second[1] !== 0 || second[2] !== 0)
    throw new Error("pending-nonnull 2nd run2 not (ERROR,0,0): " + second);
  if (host.calls() !== 2) throw new Error("pending-nonnull 2nd run2 did not re-call import");
  if (host.eventCount() !== 0) throw new Error("pending-nonnull 2nd run2 wrote an event");
}

// ---- P0-3 corrupt-count: defensive coordinate gate rejects, no record ------
{
  const host = await fresh("corrupt-count");
  const before = recStr(readRecord(host, 0)); // fresh instance: all-zero record
  const result = host.instance.exports.run2(...host.input());
  if (result[0] !== 1 || result[1] !== 0 || result[2] !== 0)
    throw new Error("corrupt-count did not return (ERROR,0,0)");
  if (host.calls() !== 1) throw new Error("corrupt-count expected one import call");
  // The scheduler must NOT have published its own event_count (it stays at the
  // host-injected value, proving the coordinate gate fired before any write).
  if (host.eventCount() !== 99)
    throw new Error("corrupt-count: scheduler published despite coordinate mismatch");
  // And it must NOT have written any record body: the record is byte-unchanged.
  if (recStr(readRecord(host, 0)) !== before)
    throw new Error("corrupt-count: record body written despite coordinate mismatch");
}

// ---- PENDING: (PENDING,0,0), no event, latch traps 2nd same-instance run2 --
{
  const host = await fresh("pending");
  const result = host.instance.exports.run2(...host.input());
  if (result[0] !== 2 || result[1] !== 0 || result[2] !== 0)
    throw new Error("PENDING did not return (PENDING,0,0)");
  if (host.calls() !== 1) throw new Error("PENDING expected one import call");
  if (host.eventCount() !== 0) throw new Error("PENDING wrote an event");
  const callsBefore = host.calls();
  const countBefore = host.eventCount();
  const r0Before = recStr(readRecord(host, 0));
  let trapped = false;
  try { host.instance.exports.run2(...host.input()); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (!trapped) throw new Error("pending latch did not trap the 2nd run2");
  if (host.calls() !== callsBefore) throw new Error("2nd run2 called the import after PENDING");
  if (host.eventCount() !== countBefore) throw new Error("2nd run2 changed event_count");
  if (recStr(readRecord(host, 0)) !== r0Before)
    throw new Error("2nd run2 mutated record bytes after PENDING");
}

// ---- legacy run is pointer-only: pre-effect trap, no import call -----------
{
  const host = await fresh("ok");
  let trapped = false;
  try { host.instance.exports.run(...host.input()); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (!trapped) throw new Error("legacy run did not pre-effect trap");
  if (host.calls() !== 0) throw new Error("legacy run reached a capability effect");
}

// ---- P0-4 checked alloc exact sequence: fresh instance, fail first ---------
{
  const host = await fresh("ok");
  // Over-capacity request on the untouched heap must return 0 without advancing.
  if (host.instance.exports.alloc(70000) !== 0)
    throw new Error("over-capacity alloc did not return 0");
  // The next allocation must start at the original heap_base (proving no advance).
  if (host.instance.exports.alloc(1) !== HEAP_BASE)
    throw new Error("post-fail alloc did not return the original heap_base");
  // And a subsequent success advances by the previous length.
  if (host.instance.exports.alloc(1) !== HEAP_BASE + 1)
    throw new Error("successful alloc did not advance heap_next");
}

console.log("capability-workflow Node execution passed");
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
                f"Node capability-workflow host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
    print("OK: capability-workflow Node execution passed (not wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
