#!/usr/bin/env python3
"""Always-on KR6.5 E2 binary/structural gate (not execution evidence)."""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

# RFC 0026 E4-B1: the E2 capability artifact is the byte-identical pre-B1 module
# (its complete old bytes) followed by trailing custom sections. The pre-B1
# module was independently locked at this length/digest, so the gate asserts
# full-prefix equality against it rather than re-deriving the boundary from the
# known-section walk.
#
# RFC 0026 dual-track elimination: the opaque WireJson lane is gone. The E2
# capability agent is now a P6-frame module like E1: run2 is deleted (runv is
# the sole agent entry point) and the sole capability import uses the bridge
# functype (i32)->(i32,i32) instead of the old opaque tuple
# (i32,i32)->(i32,i32,i32). The type section grew from 5 to 6 entries (the
# bridge type is appended after runv), and the export catalogue swapped run2
# for runv.
E2_PREFIX_LEN = 418
E2_PREFIX_MD5 = "85a489ff92290f1a31fd0daa6ff744b6"
WIRE_SCHEMA_SECTION_NAME = "ahfl.wire-schema.v1"
WIRE_SCHEMA_TABLE_MAGIC = b"AHFLWS"


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
    fail("out-of-domain ULEB128")


def read_name(data: bytes, offset: int) -> tuple[str, int]:
    size, offset = read_u32(data, offset)
    end = offset + size
    if end > len(data):
        fail("truncated wasm name")
    try:
        return data[offset:end].decode("utf-8"), end
    except UnicodeDecodeError as exc:
        fail(f"invalid UTF-8 wasm name: {exc}")


def sections(data: bytes) -> tuple[dict[int, bytes], int, bytes]:
    """Parse a B1 E2 module into its known sections plus the wire-schema
    custom section. Returns (known-section payloads, first-custom-section start
    offset, wire-schema-section payload). Fails closed if the wire-schema
    section is absent or if any known section follows a custom section.

    Capability agents carry THREE custom sections: the P6 core-layout
    (ahfl.core-layout.v1), the AHFLXM exec-manifest
    (ahfl.wasm-exec-manifest.v1), and the wire-schema section
    (ahfl.wire-schema.v1). The first-custom-section start offset is still
    the end of the locked pre-B1 prefix."""
    if data[:8] != b"\0asm\x01\0\0\0":
        fail("CLI output is not wasm v1")
    result: dict[int, bytes] = {}
    order: list[int] = []
    first_custom_start: int | None = None
    wire_schema_payload: bytes | None = None
    offset = 8
    while offset < len(data):
        section_start = offset
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if end > len(data):
            fail("truncated wasm section")
        if section_id == 0:
            if first_custom_start is None:
                first_custom_start = section_start
            # Read the custom section name to identify the wire-schema section.
            name, name_end = read_name(data, offset)
            if name == WIRE_SCHEMA_SECTION_NAME:
                if wire_schema_payload is not None:
                    fail("duplicate wire-schema custom section")
                wire_schema_payload = data[name_end:end]
        else:
            if first_custom_start is not None:
                fail("known section follows the trailing custom section")
            if section_id in result:
                fail("duplicate wasm section")
            order.append(section_id)
            result[section_id] = data[offset:end]
        offset = end
    if order != [1, 2, 3, 5, 6, 7, 10]:
        fail(f"non-canonical E2 section order: {order}")
    if first_custom_start is None or wire_schema_payload is None:
        fail("E2 capability artifact is missing its wire-schema custom section")
    return result, first_custom_start, wire_schema_payload


def function_types(payload: bytes) -> list[tuple[list[int], list[int]]]:
    count, offset = read_u32(payload, 0)
    result: list[tuple[list[int], list[int]]] = []
    for _ in range(count):
        if offset >= len(payload) or payload[offset] != 0x60:
            fail("invalid function type")
        offset += 1
        param_count, offset = read_u32(payload, offset)
        params = list(payload[offset : offset + param_count])
        offset += param_count
        result_count, offset = read_u32(payload, offset)
        results = list(payload[offset : offset + result_count])
        offset += result_count
        result.append((params, results))
    if offset != len(payload):
        fail("trailing bytes in type section")
    return result


def imports(payload: bytes) -> list[tuple[str, str, int, int]]:
    count, offset = read_u32(payload, 0)
    result: list[tuple[str, str, int, int]] = []
    for _ in range(count):
        module, offset = read_name(payload, offset)
        field, offset = read_name(payload, offset)
        if offset >= len(payload):
            fail("truncated import")
        kind = payload[offset]
        offset += 1
        type_index, offset = read_u32(payload, offset)
        result.append((module, field, kind, type_index))
    if offset != len(payload):
        fail("trailing bytes in import section")
    return result


def exports(payload: bytes) -> list[tuple[str, int, int]]:
    count, offset = read_u32(payload, 0)
    result: list[tuple[str, int, int]] = []
    for _ in range(count):
        name, offset = read_name(payload, offset)
        if offset >= len(payload):
            fail("truncated export")
        kind = payload[offset]
        offset += 1
        index, offset = read_u32(payload, offset)
        result.append((name, kind, index))
    if offset != len(payload):
        fail("trailing bytes in export section")
    return result


def code_body(payload: bytes, defined_index: int) -> bytes:
    count, offset = read_u32(payload, 0)
    if defined_index >= count:
        fail("defined function is absent")
    for index in range(count):
        size, offset = read_u32(payload, offset)
        end = offset + size
        if end > len(payload):
            fail("truncated code body")
        if index == defined_index:
            return payload[offset:end]
        offset = end
    fail("defined function body not found")


def emit(ahflc: Path, source: Path, profile: str) -> bytes:
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as tmp:
        out_path = tmp.name
    try:
        proc = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--wasm-profile", profile,
             "--output", out_path, str(source)],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            fail(
                f"ahflc build ({profile}) exited {proc.returncode}: "
                f"{proc.stderr.decode(errors='replace')}"
            )
        return Path(out_path).read_bytes()
    finally:
        Path(out_path).unlink(missing_ok=True)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        fail("usage: wasm_e2_binary_gate.py <ahflc> <tests-dir>")
    ahflc = Path(argv[0])
    source = Path(argv[1]) / "golden" / "wasm" / "e2_capability_agent.ahfl"
    if not ahflc.is_file() or not source.is_file():
        fail("missing ahflc or E2 source fixture")

    first = emit(ahflc, source, "wasi")
    second = emit(ahflc, source, "wasi")
    browser = emit(ahflc, source, "browser")
    if first != second:
        fail("NON-DETERMINISTIC: two E2 emissions differ byte-for-byte")
    if first != browser:
        fail("E2 wasi/browser artifacts differ")

    parsed, first_custom_start, wire_schema_payload = sections(first)

    # Full old-prefix equality: the pre-B1 module bytes must survive verbatim as
    # the prefix, and the first custom section (AHFLXM, per D-C) must begin
    # exactly where the old module ended.
    prefix = first[:E2_PREFIX_LEN]
    if first_custom_start != E2_PREFIX_LEN:
        fail(
            f"first custom section starts at {first_custom_start}, "
            f"expected the locked pre-B1 prefix length {E2_PREFIX_LEN}"
        )
    if len(prefix) != E2_PREFIX_LEN:
        fail(f"E2 module shorter than the locked pre-B1 prefix: {len(first)} bytes")
    # MD5 here is a non-security golden checksum; usedforsecurity=False keeps the
    # gate alive on FIPS-restricted interpreters (Python >= 3.9).
    prefix_md5 = hashlib.md5(prefix, usedforsecurity=False).hexdigest()
    if prefix_md5 != E2_PREFIX_MD5:
        fail(
            f"E2 pre-B1 prefix changed: md5 {prefix_md5} != locked {E2_PREFIX_MD5}"
        )

    # The wire-schema section payload is already name-framing-stripped by
    # sections(); it is the raw AHFLWS table bytes.
    table = wire_schema_payload
    if not table.startswith(WIRE_SCHEMA_TABLE_MAGIC):
        fail("wire-schema custom payload does not start with the AHFLWS table magic")
    # magic (6) + canonical single-byte LEB format_version == 1 for the current
    # v1. Assert the version consumes exactly one byte so a non-canonical overlong
    # encoding (e.g. 0x81 0x00) cannot masquerade as version 1.
    version_offset = len(WIRE_SCHEMA_TABLE_MAGIC)
    version, version_end = read_u32(table, version_offset)
    if version != 1:
        fail(f"wire-schema table format version {version} != 1")
    if version_end != version_offset + 1:
        fail("wire-schema table format version is not a canonical single-byte LEB")

    # Type section: 4 fixed ABI types, then the runv type ()->(i32,i32) at
    # index 4, then the bridge import type (i32)->(i32,i32) at index 5. The
    # old type 4 (i32,i32)->(i32,i32,i32) was the run2/opaque-capability tuple,
    # deleted with the WireJson lane.
    types = function_types(parsed[1])
    if (len(types) != 6
            or types[4] != ([], [0x7F, 0x7F])
            or types[5] != ([0x7F], [0x7F, 0x7F])):
        fail("runv/bridge function type mismatch")
    imported = imports(parsed[2])
    if len(imported) != 1:
        fail(f"expected one reachable capability import, got {imported}")
    module, field, kind, type_index = imported[0]
    if module != "ahfl_cap" or not field.startswith("cap_") or kind != 0 or type_index != 5:
        fail(f"ahfl_cap import contract mismatch: {imported[0]}")
    try:
        int(field.removeprefix("cap_"))
    except ValueError:
        fail("capability field is not SymbolId-based decimal spelling")

    exported = exports(parsed[7])
    names = [item[0] for item in exported]
    expected = [
        "memory", "alloc", "dealloc", "run", "step",
        "current_state", "transition_count", "ahfl_abi_version", "runv",
    ]
    if names != expected:
        fail(f"E2 export catalogue/order mismatch: {names}")
    by_name = {name: (kind_value, index) for name, kind_value, index in exported}
    if by_name["run"] != (0, 6) or by_name["runv"] != (0, 7):
        fail("import-aware run/runv function indices are wrong")

    legacy_run = code_body(parsed[10], 5)
    if legacy_run != b"\x00\x00\x0b":
        fail("capability artifact legacy run does not trap before effects")
    runv = code_body(parsed[10], 6)
    # Bridge call: the import returns (status, result_ptr) via multi-value;
    # runv pops them in reverse order (result_ptr then status).
    if b"\x21\x02\x21\x01" not in runv:
        fail("runv does not pop the bridge (status, result_ptr) return")
    # PENDING on the agent lane traps (no scheduler to suspend).
    if b"\x20\x01\x41\x02\x46\x04\x40\x00\x0b" not in runv:
        fail("runv does not trap on PENDING (agent lane)")
    # OK: return (AHFL_CAP_OK=0, result_ptr).
    if b"\x20\x01\x41\x00\x46\x04\x40\x41\x00\x20\x02\x0f" not in runv:
        fail("runv does not return (OK, result_ptr) on bridge success")

    print("all KR6.5 E2 binary/structural gates passed (not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
