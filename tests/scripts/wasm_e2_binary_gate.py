#!/usr/bin/env python3
"""Always-on KR6.5 E2 binary/structural gate (not execution evidence)."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


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


def sections(data: bytes) -> dict[int, bytes]:
    if data[:8] != b"\0asm\x01\0\0\0":
        fail("CLI output is not wasm v1")
    result: dict[int, bytes] = {}
    order: list[int] = []
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if end > len(data) or section_id == 0 or section_id in result:
            fail("custom, duplicate, or truncated wasm section")
        order.append(section_id)
        result[section_id] = data[offset:end]
        offset = end
    if order != [1, 2, 3, 5, 6, 7, 10]:
        fail(f"non-canonical E2 section order: {order}")
    return result


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
    proc = subprocess.run(
        [str(ahflc), "emit", "wasm", "--wasm-profile", profile, str(source)],
        capture_output=True,
        timeout=60,
    )
    if proc.returncode != 0:
        fail(
            f"ahflc emit wasm ({profile}) exited {proc.returncode}: "
            f"{proc.stderr.decode(errors='replace')}"
        )
    return proc.stdout


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

    parsed = sections(first)
    types = function_types(parsed[1])
    if len(types) != 5 or types[4] != ([0x7F, 0x7F], [0x7F, 0x7F, 0x7F]):
        fail("run2/ahfl_cap multi-value function type mismatch")
    imported = imports(parsed[2])
    if len(imported) != 1:
        fail(f"expected one reachable capability import, got {imported}")
    module, field, kind, type_index = imported[0]
    if module != "ahfl_cap" or not field.startswith("cap_") or kind != 0 or type_index != 4:
        fail(f"ahfl_cap import contract mismatch: {imported[0]}")
    try:
        int(field.removeprefix("cap_"))
    except ValueError:
        fail("capability field is not SymbolId-based decimal spelling")

    exported = exports(parsed[7])
    names = [item[0] for item in exported]
    expected = [
        "memory", "alloc", "dealloc", "run", "run2", "step",
        "current_state", "transition_count", "ahfl_abi_version",
    ]
    if names != expected:
        fail(f"append-only E2 export catalogue/order mismatch: {names}")
    by_name = {name: (kind_value, index) for name, kind_value, index in exported}
    if by_name["run"] != (0, 6) or by_name["run2"] != (0, 7):
        fail("import-aware run/run2 function indices are wrong")

    legacy_run = code_body(parsed[10], 5)
    if legacy_run != b"\x00\x00\x0b":
        fail("capability artifact legacy run does not trap before effects")
    run2 = code_body(parsed[10], 6)
    if b"\x23\x04\x04\x40\x00\x0b" not in run2:
        fail("run2 does not check pending_latched before orchestration")
    if b"\x20\x00\x20\x01\x10\x00" not in run2:
        fail("run2 does not forward opaque ptr/len to the sole import")
    if b"\x41\x01\x24\x04" not in run2:
        fail("run2 does not latch valid PENDING")

    print("all KR6.5 E2 binary/structural gates passed (not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
