#!/usr/bin/env python3
"""RFC 0026 P6-7 frame bridge rung string-frame structural gate.

Pins the Timestamp/UUID frame decision: a computed final carrying
Timestamp/UUID EMITS a frame section and materializes the compile-time words,
but those shapes are intentionally UNREACHABLE through a conforming
embedded-host observation (frame bridge section 0 non-goals; section 10
string-frame). This gate pins BOTH sides of that decision so the comment and the code
cannot drift:

  1. the certified embedded host rejects 'timestamp'/'uuid' at every frame
     walk site with the rung-E frame-subset fail: the input packer and the
     output reader (the two original string-frame sites), and with frame bridge capability-bridge
     fix-forward the bridge-argument classifier (a third site that must also
     refuse to marshal an unobservable Timestamp/UUID across the bridge);
  2. the computed-final i64-word fixture nonetheless compiles, carries the
     ahfl.core-layout frame section, and plans a zero payload arena (the
     words are i64 constants, never a rodata/PtrLen String).

Decimal/Duration/Map are supported by the Node host (WH-5c.7 rich wire-type
matrix port); the rejection pattern pins only Timestamp/UUID as
host-unobservable.

Engine-independent: no Node execution is required.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

HOST = Path("tests/conformance/node_embedded_host.mjs")

# Joint case headers serve the frame-walk rejection sites: packValue (input),
# readValue (output), and the capability-bridge fix-forward bridge-argument classifier. The
# string-frame decision requires the pack/read pair; capability-bridge adds the bridge site.
# Decimal/Duration/Map are supported (WH-5c.7 rich wire-type matrix port); the
# rejection pattern pins only Timestamp/UUID as host-unobservable.
SUBSET_REJECTION = 'case "timestamp":\n    case "uuid":'
EXPECTED_REJECTION_SITES = 3


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("usage: wasm_builtin_frame_subset_gate.py <repo-root> "
                    "<p6-producer>")
    repo_root = Path(argv[0])
    producer = Path(argv[1])
    host = repo_root / HOST
    if not host.is_file() or not producer.is_file():
        return fail("missing repo host or p6 producer")

    host_source = host.read_text(encoding="utf-8")
    rejection_sites = host_source.count(SUBSET_REJECTION)
    if rejection_sites != EXPECTED_REJECTION_SITES:
        return fail(
            f"the certified embedded host must reject timestamp/uuid at the "
            f"input packer, the output reader, and the bridge-argument "
            f"classifier ({EXPECTED_REJECTION_SITES} sites), found "
            f"{rejection_sites}: the Timestamp/UUID shapes are intentionally "
            f"host-unobservable (frame bridge string-frame non-goals, kept across "
            f"the capability-bridge bridge)")

    source = (repo_root / "tests" / "golden" / "wasm" /
              "builtin_i64_final.ahfl")
    if not source.is_file():
        return fail(f"missing fixture {source}")
    with tempfile.TemporaryDirectory(prefix="ahfl-v2b-builtin-") as td:
        wasm = Path(td) / "builtin.wasm"
        run = subprocess.run(
            [str(producer), str(source), str(wasm)],
            capture_output=True, text=True, timeout=60,
        )
        if run.returncode != 0:
            return fail(f"an i64-word computed final must still emit: "
                        f"{run.stderr}")
        arena = re.search(
            r"frame_arena base=\d+ cap=(\d+) rodata_base=\d+ rodata_extent=(\d+)",
            run.stdout)
        if arena is None:
            return fail("the i64-word final emits no p6 frame section")
        if arena.group(1) != "0" or arena.group(2) != "0":
            return fail(
                f"i64-word finals reserve no String arena/rodata, got "
                f"cap={arena.group(1)} rodata_extent={arena.group(2)}")

    print("string-frame Timestamp/UUID emit-but-host-unobservable frame subset gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
