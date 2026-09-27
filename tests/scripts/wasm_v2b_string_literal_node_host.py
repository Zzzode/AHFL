#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-B Node execution evidence.

Real Node v22 (NOT wasmtime) evidence for in-module STRING (PtrLen)
construction: the emitted computed-final module carries ONE additive
Data(11) section whose single active segment initializes the rodata
region [256,1024) with the hash-consed, byte-sorted, 8-aligned literal
image. This host:

  1. compiles the fixture with the p6 producer;
  2. parses the Data section and asserts rodata layout / hash-cons /
     extent exactly (KAT);
  3. drives runv over both if-branches and asserts the output frame's
     inline PtrLen points at the expected rodata bytes, agreeing
     canonically with what the evaluator would observe.

SKIP (77) when node is unavailable.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77

# Per-fixture plan: (fixture stem, flag input, expected string, expected
# output-int leaf at @8 or "" when the output nominal has no Int field).
RODATA_IMAGE = bytes.fromhex(
    "6e6f6e6500000000"  # "none" @0 + 4 zero pad
    "736f6d6500000000"  # "some" @8 + 4 zero pad
).hex()

PLANS: list[tuple[str, int, str, str]] = [
    ("v2b_computed_string", 1, "some", "1"),
    ("v2b_computed_string", 0, "none", "0"),
    ("v2b_bounded_string", 1, "some", ""),
    ("v2b_bounded_string", 0, "none", ""),
]


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_v2b_string_literal_node_host.py <p6-producer> "
                    "<tests-golden-wasm-dir>")
    node = shutil.which("node")
    if node is None:
        print("SKIP: node is unavailable for V2-B string-literal execution")
        return SKIP
    producer = Path(argv[0])
    wasm_dir = Path(argv[1])
    host = (Path(__file__).resolve().parent.parent / "conformance" /
            "node_embedded_host_v2b_probe.mjs")

    for stem, flag, expected, expected_int in PLANS:
        source = wasm_dir / f"{stem}.ahfl"
        if not producer.is_file() or not source.is_file():
            return fail(f"missing producer or fixture {source}")
        with tempfile.TemporaryDirectory(prefix=f"ahfl-{stem}-") as td:
            wasm_path = str(Path(td) / f"{stem}.wasm")
            proc = subprocess.run(
                [str(producer), str(source), wasm_path],
                capture_output=True, text=True, timeout=60,
            )
            if proc.returncode != 0:
                return fail(f"producer failed for {stem}: {proc.stderr}")
            ran = subprocess.run(
                [node, str(host), wasm_path, "12288", str(flag),
                 expected, expected_int, RODATA_IMAGE],
                capture_output=True, text=True, timeout=60,
            )
            if ran.returncode != 0:
                return fail(
                    f"Node V2-B host failed for {stem} flag={flag}: "
                    f"stdout={ran.stdout!r} stderr={ran.stderr!r}")

    print("OK: RFC 0026 P6-7 frame-bridge V2-B in-module String(PtrLen) "
          "construction Node execution passed (rodata Data section layout / "
          "hash-cons / extent + computed-string final canonical bytes; real "
          "Node v22, NOT wasmtime)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
