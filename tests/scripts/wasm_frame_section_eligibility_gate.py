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
# RFC 0026 P6-7 frame-bridge v2 rung V2-A adds the three node-only
# computed-final fixtures (scalar / nested aggregate / if-selected tag
# enum): they materialize the output frame, so they are p6-frame
# modules even though no handler projects the raw input. The V2-A
# fix-forward adds two more computed-final fixtures: the
# active-variant-only payload-enum materialization (runtime
# discriminant ladder over the payload union) and an if-let arm whose
# trailing if returns on both branches. V2-B adds the
# String-literal computed final, the bounded-String variant, and the
# input-String passthrough (no Data section, but still a p6-frame
# module with runv + both frame sections). V2-C adds the first
# capability-BRIDGE module (multi-arg control block + disjoint result
# placements), which is likewise a p6-frame module.
FRAME_FIXTURES = frozenset(
    {
        "p6_aggregate",
        "p6_collection",
        "p6_frame_two_containers",
        "v2a_computed_scalar",
        "v2a_computed_aggregate",
        "v2a_computed_enum",
        "v2a_computed_payload_enum",
        "v2a_computed_if_let_return",
        "v2b_computed_string",
        "v2b_bounded_string",
        "v2b_string_passthrough",
        # V2-B fix-forward: String-payload enum final, bounded List<String> /
        # nested-struct-element arena multiplicity fixtures, and the
        # Decimal/Duration i64-word final (emits but intentionally outside the
        # host walk subset).
        "v2b_enum_string",
        "v2b_list_string_arena",
        "v2b_list_nested_string_arena",
        "v2b_builtin_i64_final",
        "v2c_multi_arg_bridge",
        # V2-C fix-forward: two computed bridge handlers in a chain (two dense
        # sites share one import ordinal), a producing match followed by an
        # ordered bridge statement, and a single tag-only-enum bridge argument.
        "v2c_bridge_chain",
        "v2c_route_then_bridge",
        "v2c_single_arg_bridge",
        "v2c_single_enum_bridge",
        # KR6.6: String concatenation computed final (construct-heap region
        # authorized via the v4 presence-gated construct_heap_base extension).
        "kr66_string_concat",
        # KR6.6: f64 collection element ladder (List<Float> element load/store
        # through the dedicated F64 scalar kind on the P6 frame lane).
        "p6_f64_collection",
    }
)

CORE_LAYOUT_SECTION = b"ahfl.core-layout.v1"

# The V2-B fixtures that MUST carry exactly one Data(11) section (one per
# String-literal module); every other successfully emitting fixture must carry
# NONE (byte-identity pin for the additive Data section).
DATA_SECTION_FIXTURES = frozenset(
    {
        "v2b_computed_string",
        "v2b_bounded_string",
        "v2b_enum_string",
        "fb3_string_capture",
        "kr66_string_concat",
    }
)
DATA_SECTION_ID = 11

# A V2-B reject fixture: its one String literal's aligned image exceeds the
# reserved 768-byte rodata region [256,1024), so emission MUST fail closed with
# the rodata RESOURCE gate (no artifact). Pins the extent metering, not just
# the happy-path image.
RODATA_OVERFLOW_FIXTURE = "v2b_rodata_overflow"
RODATA_OVERFLOW_SUBSTR = "rodata region"


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


def section_ids(data: bytes) -> list[int]:
    ids: list[int] = []
    offset = 8
    while offset < len(data):
        ids.append(data[offset])
        offset += 1
        size, offset = read_u32(data, offset)
        offset += size
    return ids


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
    data_section_seen: set[str] = set()
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
        # V2-B: exactly one Data(11) section iff the fixture constructs a
        # String literal; additive byte-identity for every other module.
        data_count = sum(1 for sid in section_ids(first) if sid == DATA_SECTION_ID)
        if name in DATA_SECTION_FIXTURES:
            if data_count != 1:
                fail(f"{source.name} must carry exactly one Data(11) section, got {data_count}")
            data_section_seen.add(name)
        elif data_count != 0:
            fail(
                f"{source.name} unexpectedly carries a Data(11) section (the rodata Data "
                f"section is additive only for V2-B String-literal frame modules)"
            )

    missing = FRAME_FIXTURES - frame_section_seen
    if missing:
        fail(
            "pinned frame fixtures emitted no core-layout section: "
            + ", ".join(sorted(missing))
        )
    missing_data = DATA_SECTION_FIXTURES - data_section_seen
    if missing_data:
        fail(
            "pinned V2-B fixtures emitted no Data(11) section: "
            + ", ".join(sorted(missing_data))
        )

    # V2-B extent metering: an over-large literal image fails closed with the
    # rodata RESOURCE gate and produces NO artifact.
    overflow_source = wasm_dir / f"{RODATA_OVERFLOW_FIXTURE}.ahfl"
    if not overflow_source.is_file():
        fail(f"missing rodata-overflow reject fixture {overflow_source.name}")
    overflow = subprocess.run(
        [str(ahflc), "emit", "wasm", str(overflow_source)],
        capture_output=True,
        timeout=60,
    )
    overflow_err = overflow.stderr.decode(errors="replace")
    if overflow.returncode == 0 or overflow.stdout:
        fail(
            f"{overflow_source.name} must fail closed when its literal image "
            f"exceeds the rodata region, but emission produced an artifact"
        )
    if RODATA_OVERFLOW_SUBSTR not in overflow_err:
        fail(
            f"{overflow_source.name} rejection must name the rodata region "
            f"(extent gate); stderr was: {overflow_err!r}"
        )

    print(
        f"all P6-7 frame-section eligibility gates passed "
        f"({checked} fixtures, {len(frame_section_seen)} frame modules, "
        f"{len(data_section_seen)} rodata modules)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
