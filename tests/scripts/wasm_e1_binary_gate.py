#!/usr/bin/env python3
"""Always-on KR6.5 E1 binary/structural gate (not execution evidence)."""
from __future__ import annotations

import subprocess
import sys
import tempfile
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
    fail("non-canonical/out-of-domain ULEB128")


def sections(data: bytes) -> tuple[dict[int, bytes], list[str]]:
    if data[:8] != b"\0asm\x01\0\0\0":
        fail("CLI output is not a wasm v1 binary")
    result: dict[int, bytes] = {}
    order: list[int] = []
    custom_names: list[str] = []
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
                custom_names.append(data[name_start:name_end].decode("utf-8"))
            except UnicodeDecodeError as exc:
                fail(f"invalid UTF-8 custom section name: {exc}")
        elif section_id in result:
            fail("E1 artifact contains a duplicate standard section")
        order.append(section_id)
        result[section_id] = data[offset:end]
        offset = end
    # KR6.6 identity-final frame lane: the E1 identity agent is now a P6-frame
    # module. The standard sections keep their canonical order; the two P6-frame
    # custom sections (core-layout + wire-schema) follow Code.
    if order != [1, 3, 5, 6, 7, 10, 0, 0]:
        fail(f"non-canonical E1 section order: {order}")
    if custom_names != ["ahfl.core-layout", "ahfl.wire-schema"]:
        fail(f"unexpected E1 custom sections: {custom_names}")
    return result, custom_names


def read_name(data: bytes, offset: int) -> tuple[str, int]:
    size, offset = read_u32(data, offset)
    end = offset + size
    if end > len(data):
        fail("truncated export name")
    try:
        return data[offset:end].decode("utf-8"), end
    except UnicodeDecodeError as exc:
        fail(f"invalid UTF-8 export name: {exc}")


def export_names(payload: bytes) -> list[str]:
    count, offset = read_u32(payload, 0)
    names: list[str] = []
    for _ in range(count):
        name, offset = read_name(payload, offset)
        if offset >= len(payload):
            fail("truncated export descriptor")
        offset += 1  # kind
        _, offset = read_u32(payload, offset)  # index
        names.append(name)
    if offset != len(payload):
        fail("trailing bytes in export section")
    return names


def code_body(payload: bytes, function_index: int) -> bytes:
    count, offset = read_u32(payload, 0)
    if function_index >= count:
        fail("run function index is absent from code section")
    for index in range(count):
        size, offset = read_u32(payload, offset)
        end = offset + size
        if end > len(payload):
            fail("truncated code body")
        if index == function_index:
            return payload[offset:end]
        offset = end
    fail("run body not found")


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


def expect_unsupported(ahflc: Path, source: Path) -> None:
    with tempfile.NamedTemporaryFile(suffix=".wasm", delete=False) as tmp:
        out_path = tmp.name
    Path(out_path).unlink(missing_ok=True)  # ahflc must not create it on failure
    try:
        proc = subprocess.run(
            [str(ahflc), "build", "--verify=off", "--output", out_path, str(source)],
            capture_output=True,
            timeout=60,
        )
        stderr = proc.stderr.decode(errors="replace")
        if (
            proc.returncode == 0
            or Path(out_path).exists()
            or (
                "wasm.UNSUPPORTED_ORCHESTRATION" not in stderr
                and "wasm.UNSUPPORTED_CAPABILITY_FRAME" not in stderr
                and "wasm.ENTRY_AMBIGUOUS" not in stderr
            )
        ):
            fail(
            f"unsupported CLI fixture did not fail closed ({source.name}): "
            f"rc={proc.returncode}, stderr={stderr!r}"
        )
    finally:
        Path(out_path).unlink(missing_ok=True)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        fail("usage: wasm_e1_binary_gate.py <ahflc> <tests-dir>")
    ahflc = Path(argv[0])
    tests_dir = Path(argv[1])
    source = tests_dir / "golden" / "wasm" / "e1_identity_agent.ahfl"
    if not ahflc.is_file() or not source.is_file():
        fail("missing ahflc or E1 source fixture")

    wasi_first = emit(ahflc, source, "wasi")
    wasi_second = emit(ahflc, source, "wasi")
    browser = emit(ahflc, source, "browser")
    if wasi_first != wasi_second:
        fail("NON-DETERMINISTIC: two E1 emissions differ byte-for-byte")
    if wasi_first != browser:
        fail("no-import wasi/browser E1 artifacts differ")

    parsed, _custom_names = sections(wasi_first)
    expected_exports = [
        "memory",
        "alloc",
        "dealloc",
        "step",
        "current_state",
        "transition_count",
        "run_frame",
    ]
    if export_names(parsed[7]) != expected_exports:
        fail("E1 export catalogue/order mismatch")
    # Fixture declaration order is [Done, Start] with initial Start (id 1).
    # The first global must therefore initialize to i32.const 1, proving the
    # encoder did not silently assume state zero.
    if not parsed[6].startswith(b"\x04\x7f\x01\x41\x01\x0b"):
        fail("current_state global does not use the non-zero Core initial state id")

    # The frame entry returns the current status and value pointer.
    frame = code_body(parsed[10], 5)
    if not frame.endswith(b"\x0b"):
        fail("frame entry is missing its end opcode")

    # RFC 0026 P6-7 frame bridge: a computed final carrying a String is
    # legal since string-frame (rodata PtrLen construction), so the old String-output
    # reject fixture was promoted to string_passthrough. The still-failing
    # shape here is a workflow value projection, which remains workflow-frame work.
    expect_unsupported(ahflc, tests_dir / "golden" / "ir" / "ok_workflow_value_flow.ahfl")

    print("all KR6.5 E1 binary/structural gates passed (not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
