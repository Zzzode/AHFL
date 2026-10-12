#!/usr/bin/env python3
"""Run production-exported alloc in Node; follow the native host-test convention."""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    node = shutil.which("node")
    if node is None:
        print("SKIP: Node is unavailable for exported allocator execution")
        return 77
    ahflc = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()

    def invoke(*args: str) -> None:
        completed = subprocess.run(
            [str(ahflc), *args], capture_output=True, text=True, timeout=60,
        )
        if completed.returncode != 0:
            raise AssertionError(f"CLI failed: {completed.stdout}\n{completed.stderr}")

    with tempfile.TemporaryDirectory(prefix="ahfl-alloc-node-") as td:
        work = Path(td)
        unchecked = work / "unchecked.wasm"
        invoke(
            "build", "--manifest", str(root / "tests/integration/try_operator/ahfl.toml"),
            "--target", "workflow", "--sysroot", str(root), "--output", str(unchecked),
        )
        checked = work / "checked.wasm"
        package = work / "checked"
        package.mkdir()
        shutil.copyfile(
            root / "tests/integration/wasm_capability_workflow/ahfl.toml",
            package / "ahfl.toml",
        )
        shutil.copyfile(
            root / "tests/golden/wasm/e3_capability_workflow.ahfl",
            package / "e3_capability_workflow.ahfl",
        )
        invoke(
            "build", "--manifest", str(package / "ahfl.toml"), "--target", "workflow",
            "--sysroot", str(root), "--output", str(checked),
        )
        host = work / "host.mjs"
        host.write_text(r"""
import fs from 'node:fs';
import assert from 'node:assert/strict';
for (const [path, checked] of [[process.argv[2], false], [process.argv[3], true]]) {
  const module = await WebAssembly.compile(fs.readFileSync(path));
  const imports = {};
  for (const item of WebAssembly.Module.imports(module)) {
    assert.equal(item.kind, 'function');
    (imports[item.module] ??= {})[item.name] = () => 0;
  }
  const fresh = async () => (await WebAssembly.instantiate(module, imports)).exports;
  const c = await fresh();
  const start = c.alloc(0);
  assert(start > 0 && start < 65536);
  assert.equal(start % 8, 0);
  assert.equal(c.alloc(0), start, 'zero-length allocation must not advance');
  let cursor = start;
  for (const length of [1, 3, 4]) {
    assert.equal(c.alloc(length), cursor);
    cursor += 8;
    assert.equal(c.alloc(0), cursor);
    assert.equal(cursor % 8, 0);
  }
  for (const length of [65536, 65537, 0xffffffff]) {
    if (checked) {
      assert.equal(c.alloc(length), 0, 'checked allocation must report failure');
    } else {
      assert.throws(() => c.alloc(length), WebAssembly.RuntimeError);
    }
    assert.equal(c.alloc(0), cursor, 'failure must not advance');
  }
  // The last real byte fits; its padding ends exactly at the page boundary.
  assert.equal(c.alloc(65536 - cursor - 1), cursor);
  assert.equal(c.alloc(0), 65536);
  if (checked) assert.equal(c.alloc(1), 0);
  else assert.throws(() => c.alloc(1), WebAssembly.RuntimeError);
  assert.equal(c.alloc(0), 65536);
  const exact = await fresh();
  const exactStart = exact.alloc(0);
  assert.equal(exact.alloc(65536 - exactStart), exactStart);
  assert.equal(exact.alloc(0), 65536);
}
console.log('exported alloc alignment, capacity, overflow and zero probe passed');
""", encoding="utf-8")
        result = subprocess.run(
            [node, str(host), str(unchecked), str(checked)],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode != 0:
            raise AssertionError(f"Node allocator execution failed: {result.stderr}")
        print(result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
