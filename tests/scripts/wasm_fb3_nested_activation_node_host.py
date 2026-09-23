#!/usr/bin/env python3
"""RFC 0026 FB-3b (CORE-FNBODY-DESIGN §6.3) Node evidence: closure envs of
nested / recursive activations must not alias.

`step_sum(n, acc)` recurses n->1; every activation bumps a FRESH capturing
closure env for `add = \\x -> x + n`, applies it indirectly, and recurses. The
activation tree sums 4+3+2+1 = 10. If recursive activations reused one static
env slot, the captured ranks would collapse and the sum would differ, routing
the computed goto to Fail. This is the 7d6e7224 per-activation-heap P0 class
applied to closures. Real Node-engine evidence, NOT wasmtime. SKIP (77) when
node is unavailable.
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


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        return fail("usage: wasm_fb3_nested_activation_node_host.py <producer>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for FB-3 nested-activation env execution")
        return SKIP
    producer = Path(argv[0])
    source = (Path(__file__).resolve().parents[1] / "golden" / "wasm"
              / "fb3_nested_activation.ahfl")
    if not producer.is_file() or not source.is_file():
        return fail("missing FB-3 producer or fb3_nested_activation.ahfl fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-fb3-nested-") as td:
        td_path = Path(td)
        wasm_path = td_path / "fb3_nested_activation.wasm"
        native = subprocess.run(
            [str(producer), str(source), str(wasm_path)],
            capture_output=True, text=True, timeout=60,
        )
        if native.returncode != 0:
            return fail(f"FB-3 producer exited {native.returncode}: {native.stderr}")
        if re.search(r"initial_state_id=(\d+)", native.stdout) is None:
            return fail(f"malformed native observation: {native.stdout!r}")

        host = td_path / "fb3_nested_host.mjs"
        host.write_text(
            r"""
import fs from "node:fs";
const {instance} = await WebAssembly.instantiate(
    fs.readFileSync(process.argv[2]), {});
const c = instance.exports;
// One step runs the whole recursive activation tree; every activation's
// capturing closure env must be a distinct heap allocation, so the captured
// ranks survive as 4,3,2,1 and sum to 10 (routing Done, not Fail).
const next = c.step();
if (next !== 1)
  throw new Error(`nested-activation closure envs aliased: routed to state ${next}, expected Done(1)`);
if (c.transition_count.value !== 1)
  throw new Error("expected exactly one Init->Done transition");
if (c.step() !== 1)
  throw new Error("final Done state is not stable");
console.log("FB-3 nested-activation closure envs did not alias");
""",
            encoding="utf-8",
        )
        driven = subprocess.run([node, str(host), str(wasm_path)],
                               capture_output=True, text=True, timeout=60)
        if driven.returncode != 0:
            return fail(f"Node FB-3 nested-activation host failed: {driven.stderr}")

    print("OK: RFC 0026 FB-3 nested-activation closure-env no-alias Node embedded-engine execution "
          "passed (real Node v22 engine, NOT wasmtime evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
