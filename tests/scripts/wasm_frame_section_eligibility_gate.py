#!/usr/bin/env python3
"""RFC 0026 P6-7 rung A corpus-wide frame-section eligibility gate.

The P4-D frame lane (`ahfl.core-layout.v1` + the boundary-root block of
`ahfl.wire-schema.v1`) is the UNIFORM execution lane for EVERY agent after
the dual-track elimination: runv is the sole entry point, all capability
imports use the bridge functype (i32)->(i32,i32), and there is no WireJson
lane. Outlined fns, closures, and capability finals are all first-class
P6-frame constructs.

This gate emits EVERY tests/golden/wasm fixture and asserts:
  * the core-layout frame custom section appears in EXACTLY the pinned frame
    fixtures (every agent with a wire-representable boundary) and in no
    other successfully emitted module;
  * every successful emission is deterministic (two runs byte-identical).

It is a structural gate, not execution evidence.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

# The ONLY fixtures allowed to carry the ahfl.core-layout.v1 frame section.
# Anything else emitting one is a byte-identity / lane-mixing regression.
# RFC 0026 dual-track elimination: EVERY agent is a P6-frame module — the
# WireJson lane is gone, so capability/FB agents carry the section too.
FRAME_FIXTURES = frozenset(
    {
        # E1 identity agent: the canonical identity-final module.
        "e1_identity_agent",
        # E2 capability agent: the canonical capability-final module (the
        # bridge protocol is the sole capability calling convention).
        "e2_capability_agent",
        # P6 scalar/computation fixtures with identity or computed finals.
        "p6_aggregate",
        "p6_cascade",
        "p6_cascade_high",
        "p6_coerce",
        "p6_coerce_bounds",
        "p6_collection",
        "p6_elseless_fallthrough",
        "p6_elseless_taken",
        "p6_implies",
        "p6_match_arm_trap",
        "p6_match_binding_payload",
        "p6_match_enum",
        "p6_match_expr",
        "p6_match_fallthrough",
        "p6_match_guard",
        "p6_match_or",
        "p6_match_result_i64",
        "p6_member_base",
        "p6_neg_compare",
        "p6_nested_depth3",
        "p6_nested_depth3_taken",
        "p6_nested_elseless",
        "p6_nested_elseless_taken",
        "p6_nested_fallthrough",
        "p6_nested_projection",
        "p6_nested_taken_high",
        "p6_scalar_cond",
        "p6_scalar_trap",
        "p6_unwrap_none_trap",
        "p6_unwrap_some",
        "p6_frame_two_containers",
        # V2-A computed-final fixtures.
        "v2a_computed_scalar",
        "v2a_computed_aggregate",
        "v2a_computed_enum",
        "v2a_computed_payload_enum",
        "v2a_computed_if_let_return",
        # V2-B String/computed-final and bounded-arena fixtures.
        "v2b_computed_string",
        "v2b_bounded_string",
        "v2b_string_passthrough",
        "v2b_enum_string",
        "v2b_list_string_arena",
        "v2b_list_nested_string_arena",
        "v2b_builtin_i64_final",
        # V2-C capability-bridge fixtures.
        "v2c_multi_arg_bridge",
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
        # FB agents with outlined fns / closures (KR6.6: the frame_lane_eligible
        # gate is removed; fns/closures are first-class P6-frame constructs).
        "fb1_aggregate_direct_call",
        "fb1_direct_call",
        "fb2_bounded_recursion",
        "fb3_bounded_collection",
        "fb3_byvalue_capture",
        "fb3_f64_capture",
        "fb3_higher_order",
        "fb3_nested_activation",
        "fb3_nested_lambda_flow",
        "fb3_string_capture",
        "fb4_construct_only_closure",
        "fb4_effect_clause_pure_body",
        "fb4_effectful_fn",
        # FB-4 Int64 capability ABI: the bridge protocol supports Int64 via
        # spill slots, so this is a valid P6-frame agent (the old opaque
        # rejection was deleted with the WireJson lane).
        "fb4_int64_capability_abi",
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
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as tmp:
        out_path = tmp.name
    try:
        proc = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--output", out_path, str(source)],
            capture_output=True,
            timeout=60,
        )
        data = Path(out_path).read_bytes() if proc.returncode == 0 else b""
        return proc.returncode, data
    finally:
        Path(out_path).unlink(missing_ok=True)


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
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as tmp:
        overflow_out = tmp.name
    Path(overflow_out).unlink(missing_ok=True)  # ahflc must not create it on failure
    try:
        overflow = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--output", overflow_out, str(overflow_source)],
            capture_output=True,
            timeout=60,
        )
        overflow_err = overflow.stderr.decode(errors="replace")
        if overflow.returncode == 0 or Path(overflow_out).exists():
            fail(
                f"{overflow_source.name} must fail closed when its literal image "
                f"exceeds the rodata region, but emission produced an artifact"
            )
    finally:
        Path(overflow_out).unlink(missing_ok=True)
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
