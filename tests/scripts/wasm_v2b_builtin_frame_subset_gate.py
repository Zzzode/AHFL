#!/usr/bin/env python3
"""RFC 0026 P6-7 frame-bridge v2 rung V2-B structural gate.

Pins the Decimal/Duration frame decision the p6_scalar_kind rationale comment
states: a computed final carrying Decimal/Duration EMITS a frame section and
materializes the compile-time i64 words, but those shapes are intentionally
UNREACHABLE through a conforming embedded-host observation (frame-bridge v2
section 0 non-goals; section 10 V2-B). This gate pins BOTH sides of that
decision so the comment and the code cannot drift:

  1. the certified embedded host rejects 'decimal'/'duration' in BOTH its
     input packer and its output reader with the rung-E frame-subset fail
     (exactly two joint float/decimal/duration rejection sites);
  2. the computed-final Decimal/Duration fixture nonetheless compiles, carries
     the ahfl.core-layout.v1 frame section, and plans a zero payload arena
     (the words are i64 constants, never a rodata/PtrLen String).

Engine-independent: no Node execution is required.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HOST = Path("tests/conformance/node_embedded_host.mjs")

# One joint case header serves packValue (input) and readValue (output); the
# decision requires both sites to keep rejecting the shapes.
SUBSET_REJECTION = 'case "float":\n    case "decimal":\n    case "duration":'


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_v2b_builtin_frame_subset_gate.py <repo-root> "
                    "<p6-producer>")
    repo_root = Path(argv[0])
    producer = Path(argv[1])
    host = repo_root / HOST
    if not host.is_file() or not producer.is_file():
        return fail("missing repo host or p6 producer")

    host_source = host.read_text(encoding="utf-8")
    rejection_sites = host_source.count(SUBSET_REJECTION)
    if rejection_sites != 2:
        return fail(
            f"the certified embedded host must reject decimal/duration in BOTH "
            f"packValue and readValue (2 sites), found {rejection_sites}: the "
            f"Decimal/Duration computed-final is intentionally host-"
            f"unobservable (frame-bridge v2 V2-B non-goal)")

    source = (repo_root / "tests" / "golden" / "wasm" /
              "v2b_builtin_i64_final.ahfl")
    if not source.is_file():
        return fail(f"missing fixture {source}")
    with tempfile.TemporaryDirectory(prefix="ahfl-v2b-builtin-") as td:
        wasm = Path(td) / "builtin.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode != 0:
            return fail(f"a Decimal/Duration computed final must still emit: "
                        f"{run.stderr}")
        arena = re.search(
            r"frame_arena base=\d+ cap=(\d+) rodata_base=\d+ rodata_extent=(\d+)",
            run.stdout)
        if arena is None:
            return fail("the Decimal/Duration final emits no p6 frame section")
        if arena.group(1) != "0" or arena.group(2) != "0":
            return fail(
                f"i64-word finals reserve no String arena/rodata, got "
                f"cap={arena.group(1)} rodata_extent={arena.group(2)}")

    print("V2-B Decimal/Duration emit-but-host-unobservable frame subset gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
