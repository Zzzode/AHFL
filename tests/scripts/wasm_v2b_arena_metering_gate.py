#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-B fix-forward structural gate.

Engine-independent (no Node) pin for the D6 input-payload arena MULTIPLICITY
fix. The p6 producer reports the planned frame-arena span for every computed
final; this gate runs it over two bounded-collection fixtures and asserts the
declared capacity is align8(sum over every OCCURRENCE of the bounded String
slots), i.e. the container capacity times the element subtree bound:

  * v2b_list_string_arena        : List<String(0,8)>(4)        -> 4*8 = 32
  * v2b_list_nested_string_arena: List<struct{4,2}>(3)         -> 3*(4+2)=24

Metering the shared String shape ONCE (the pre-fix walk) reserved 8 / 8 and
the reference Node packer fail-closed while packing the second element; the
Node execution lane (wasm_v2b_string_literal_node_host.py) drives the real
pack. This gate keeps the compile-time arithmetic pinned even where Node is
unavailable.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path
import re

CASES = (
    ("v2b_list_string_arena", 32),
    ("v2b_list_nested_string_arena", 24),
)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_v2b_arena_metering_gate.py <p6-producer> "
                    "<tests-golden-wasm-dir>")
    producer = Path(argv[0])
    wasm_dir = Path(argv[1])
    if not producer.is_file():
        return fail("missing p6 producer")

    for stem, expected_cap in CASES:
        source = wasm_dir / f"{stem}.ahfl"
        if not source.is_file():
            return fail(f"missing fixture {source}")
        with tempfile.TemporaryDirectory(prefix=f"ahfl-{stem}-meter-") as td:
            wasm = Path(td) / f"{stem}.wasm"
            run = subprocess.run(
                [str(producer), str(source), str(wasm)],
                capture_output=True, text=True, timeout=60,
            )
            if run.returncode != 0:
                return fail(f"producer failed for {stem}: {run.stderr}")
            match = re.search(r"frame_arena base=\d+ cap=(\d+) rodata_base=\d+ "
                              r"rodata_extent=(\d+)", run.stdout)
            if match is None:
                return fail(f"{stem}: no frame_arena report: {run.stdout!r}")
            cap = int(match.group(1))
            rodata_extent = int(match.group(2))
            if cap != expected_cap:
                return fail(
                    f"{stem}: payload arena capacity {cap} != per-occurrence "
                    f"sum {expected_cap} (container String multiplicity "
                    f"under-metered)")
            if rodata_extent != 0:
                return fail(
                    f"{stem}: an input-arena fixture constructs no literal, "
                    f"expected rodata_extent 0, got {rodata_extent}")

    print("V2-B bounded-collection String arena multiplicity metering gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
