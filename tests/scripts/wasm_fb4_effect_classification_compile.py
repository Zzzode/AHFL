#!/usr/bin/env python3
"""RFC 0026 FB-4 fix-forward: source/Core effect-classification agreement.

Two sema-valid shapes used to be rejected by the Core verifier
FN_CALL_EFFECT_KIND because the source FnEffectScanner classified the callee
effectful while the structural Core derivation classified it pure:

  * an `effect <cap>` clause whose body performs no capability (the clause is an
    upper bound, not a forced classification);
  * an fn that only CONSTRUCTS an effectful closure and passes it to a pure fn
    that never invokes it (closure construction is not an effect).

The lowerer now reconciles every direct-call shape against the closure-aware
structural analyze_fn_effects, so both fixtures compile to a VALID module. Node
(when available) additionally validates the emitted bytes; the compile-clean
assertion itself needs no engine.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

VALIDATE_JS = r"""
import fs from "node:fs";
const bytes = fs.readFileSync(process.argv[2]);
await WebAssembly.compile(bytes);
console.log("validated");
"""


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        return fail("usage: wasm_fb4_effect_classification_compile.py <producer> <source>...")
    producer = Path(argv[0])
    sources = [Path(p) for p in argv[1:]]
    if not producer.is_file() or any(not s.is_file() for s in sources):
        return fail("missing producer or an effect-classification fixture")

    node = shutil.which("node")
    for source in sources:
        with tempfile.TemporaryDirectory(prefix="ahfl-fb4-classify-") as td:
            td_path = Path(td)
            wasm = td_path / "module.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode != 0:
                return fail(
                    f"{source.name}: a sema-valid effect shape failed to compile: "
                    f"{(run.stdout + run.stderr).strip()}")
            if not wasm.exists():
                return fail(f"{source.name}: compiled clean but wrote no wasm artifact")
            if node is not None:
                host = td_path / "validate.mjs"
                host.write_text(VALIDATE_JS, encoding="utf-8")
                validated = subprocess.run([node, str(host), str(wasm)],
                                           capture_output=True, text=True, timeout=60)
                if validated.returncode != 0:
                    return fail(
                        f"{source.name}: emitted module fails WebAssembly.compile: "
                        f"{validated.stderr.strip()}")

    print("FB-4 source/Core effect-classification agreement compile lane passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
