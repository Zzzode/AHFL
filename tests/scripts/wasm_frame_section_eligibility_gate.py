#!/usr/bin/env python3
"""RFC 0026 P6-7 rung A corpus-wide frame-section eligibility gate.

The P4-D frame lane (`ahfl.core-layout.v1` + the boundary-root block of
`ahfl.wire-schema.v1`) is admitted for ONLY the frame-lane agents the design
names (sections 3.4/4.1/11): no capability import, no outlined fn / closure.
Keying eligibility off the raw-input projection alone latched for the
FB-1..FB-4 outlined-fn/closure agents (and even mutated a capability module's
wire-schema section), changing the bytes of modules the rung-A byte-identity
invariant pins.

This gate emits EVERY tests/golden/wasm fixture and asserts:
  * the core-layout frame custom section appears in EXACTLY the pinned frame
    fixtures (the two census cases plus the rung-A regression fixtures) and in
    no other successfully emitted module -- the frame lane and the
    wire-JSON capability / FB lanes never mix;
  * every successful emission is deterministic (two runs byte-identical).

It is a structural gate, not execution evidence.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# The ONLY fixtures allowed to carry the ahfl.core-layout.v1 frame section.
# Anything else emitting one is a byte-identity / lane-mixing regression.
FRAME_FIXTURES = frozenset(
    {
        "p6_aggregate",
        "p6_collection",
        "p6_frame_two_containers",
    }
)

CORE_LAYOUT_SECTION = b"ahfl.core-layout.v1"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_u32(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    for _ in range(5):
        if offset >= len(data):
            fail("truncated ULEB128")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte & 0x80 == 0:
            return value, offset
        shift += 7
    fail("non-canonical/out-of-domain ULEB128")


def custom_section_names(data: bytes) -> list[str]:
    if data[:8] != b"\0asm\x01\0\0\0":
        fail("CLI output is not a wasm v1 binary")
    names: list[str] = []
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if end > len(data):
            fail("section extends past artifact")
        if section_id == 0:
            name_len, name_start = read_u32(data, offset)
            name_end = name_start + name_len
            if name_end > end:
                fail("custom section name exceeds its section bounds")
            try:
                names.append(data[name_start:name_end].decode("utf-8"))
            except UnicodeDecodeError as exc:
                fail(f"invalid UTF-8 custom section name: {exc}")
        offset = end
    return names


def emit(ahflc: Path, source: Path) -> tuple[int, bytes]:
    proc = subprocess.run(
        [str(ahflc), "emit", "wasm", str(source)],
        capture_output=True,
        timeout=60,
    )
    return proc.returncode, proc.stdout


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        fail("usage: wasm_frame_section_eligibility_gate.py <ahflc> <tests-dir>")
    ahflc = Path(argv[0])
    tests_dir = Path(argv[1])
    wasm_dir = tests_dir / "golden" / "wasm"
    if not ahflc.is_file() or not wasm_dir.is_dir():
        fail("missing ahflc or golden/wasm directory")

    frame_section_seen: set[str] = set()
    checked = 0
    for source in sorted(wasm_dir.glob("*.ahfl")):
        name = source.stem
        rc, first = emit(ahflc, source)
        if rc != 0:
            continue  # a deliberate compile-failure fixture (reject path)
        checked += 1
        rc2, second = emit(ahflc, source)
        if rc2 != 0 or first != second:
            fail(f"non-deterministic emission for {source.name}")
        names = custom_section_names(first)
        if CORE_LAYOUT_SECTION.decode("utf-8") in names:
            frame_section_seen.add(name)
            if name not in FRAME_FIXTURES:
                fail(
                    f"{source.name} carries the P6-frame core-layout section but is not a "
                    f"pinned frame fixture (the frame lane must not mix with capability/FB "
                    f"agents; byte-identity regression)"
                )

    missing = FRAME_FIXTURES - frame_section_seen
    if missing:
        fail(
            "pinned frame fixtures emitted no core-layout section: "
            + ", ".join(sorted(missing))
        )
    print(
        f"all P6-7 frame-section eligibility gates passed "
        f"({checked} fixtures, {len(frame_section_seen)} frame modules)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
