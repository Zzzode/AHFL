#!/usr/bin/env python3
"""Optional Node embedded-host execution evidence for KR6.5 E2."""
from __future__ import annotations

import json
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
        return fail("usage: wasm_e2_node_host.py <same-frontend-producer> <source>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for E2 embedded-host execution")
        return SKIP
    producer = Path(argv[0])
    source = Path(argv[1])
    if not producer.is_file() or not source.is_file():
        return fail("missing E2 same-frontend producer or fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-e2-node-") as td:
        artifact = Path(td) / "e2.wasm"
        emitted = subprocess.run(
            [str(producer), str(source), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if emitted.returncode != 0:
            return fail(
                f"E2 producer exited {emitted.returncode}: "
                f"{emitted.stderr}"
            )
        if "transition_count=1 capability_calls=1" not in emitted.stdout:
            return fail(f"malformed native E2 observation: {emitted.stdout!r}")
        observations = {}
        for line in emitted.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                observations[key] = value
        if "input_json" not in observations or "output_json" not in observations:
            return fail(f"missing native value_json observation: {emitted.stdout!r}")
        expected = Path(td) / "expected.json"
        expected.write_text(
            json.dumps(
                {"input": observations["input_json"],
                 "output": observations["output_json"]},
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
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
const expected = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const encoder = new TextEncoder();
const inputBytes = encoder.encode(expected.input);
const outputBytes = encoder.encode(expected.output);

async function fresh(mode) {
  let instance;
  let calls = 0;
  const callback = (ptr, len) => {
    calls += 1;
    const input = new Uint8Array(instance.exports.memory.buffer, ptr, len);
    if (new TextDecoder().decode(input) !== expected.input) {
      throw new Error("opaque input frame bytes changed");
    }
    if (mode === "ok") {
      const out = instance.exports.alloc(outputBytes.length);
      new Uint8Array(instance.exports.memory.buffer, out, outputBytes.length).set(outputBytes);
      return [0, out, outputBytes.length];
    }
    if (mode === "ok-null") return [0, 0, 0];
    if (mode === "ok-zero-len") return [0, 1234, 0];
    if (mode === "error") return [1, 1234, 9];
    if (mode === "pending") return [2, 0, 99];
    if (mode === "pending-nonnull") return [2, 1234, 9];
    return [77, 1234, 9];
  };
  const created = await WebAssembly.instantiate(module, {ahfl_cap: {[field]: callback}});
  instance = created;
  return {
    instance,
    calls: () => calls,
    input: () => {
      const ptr = instance.exports.alloc(inputBytes.length);
      new Uint8Array(instance.exports.memory.buffer, ptr, inputBytes.length).set(inputBytes);
      return [ptr, inputBytes.length];
    },
  };
}

{
  const host = await fresh("ok");
  const [ptr, len] = host.input();
  const result = host.instance.exports.run2(ptr, len);
  if (result[0] !== 0 || result[1] === 0 || result[2] !== outputBytes.length || host.calls() !== 1)
    throw new Error("OK tuple/ownership mismatch");
  const actual = new Uint8Array(host.instance.exports.memory.buffer, result[1], result[2]);
  if (new TextDecoder().decode(actual) !== expected.output)
    throw new Error("OK result bytes mismatch");
  host.instance.exports.dealloc(result[1], result[2]);
  if (host.instance.exports.transition_count.value !== 1)
    throw new Error("run2 transition count mismatch");
}
for (const [mode, expected] of [
  ["ok-null", [1,0,0]], ["ok-zero-len", [1,0,0]],
  ["error", [1,0,0]], ["pending-nonnull", [1,0,0]], ["unknown", [1,0,0]],
]) {
  const host = await fresh(mode);
  const result = host.instance.exports.run2(...host.input());
  if (result.some((value, index) => value !== expected[index]) || host.calls() !== 1)
    throw new Error(`${mode} normalization mismatch: ${result}`);
}
{
  const host = await fresh("pending");
  const result = host.instance.exports.run2(...host.input());
  if (result[0] !== 2 || result[1] !== 0 || result[2] !== 0 || host.calls() !== 1)
    throw new Error("PENDING normalization mismatch");
  let trapped = false;
  try { host.instance.exports.run2(...host.input()); } catch (error) {
    trapped = error instanceof WebAssembly.RuntimeError;
  }
  if (!trapped || host.calls() !== 1) throw new Error("pending latch did not trap before replay");
}
{
  const host = await fresh("ok");
  let trapped = false;
  try { host.instance.exports.run(...host.input()); } catch (error) {
    trapped = error instanceof WebAssembly.RuntimeError;
  }
  if (!trapped || host.calls() !== 0) throw new Error("legacy run reached capability effect");
}
{
  const host = await fresh("ok");
  if (host.instance.exports.step() !== 0 || host.calls() !== 0)
    throw new Error("step invoked capability or returned wrong final state");
}
console.log("E2 Node embedded-host execution passed");
''',
            encoding="utf-8",
        )
        executed = subprocess.run(
            [node, str(script), str(artifact), str(expected)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if executed.returncode != 0:
            return fail(
                f"Node E2 host exited {executed.returncode}: "
                f"stdout={executed.stdout!r} stderr={executed.stderr!r}"
            )
    print("OK: KR6.5 E2 Node embedded-host execution passed (not wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
