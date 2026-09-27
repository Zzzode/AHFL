#!/usr/bin/env python3
"""Always-on KR6.5 E1 binary/structural gate (not execution evidence)."""
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
    fail("non-canonical/out-of-domain ULEB128")


def sections(data: bytes) -> dict[int, bytes]:
    if data[:8] != b"\0asm\x01\0\0\0":
        fail("CLI output is not a wasm v1 binary")
    result: dict[int, bytes] = {}
    order: list[int] = []
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        size, offset = read_u32(data, offset)
        end = offset + size
        if end > len(data):
            fail("section extends past artifact")
        if section_id == 0 or section_id in result:
            fail("E1 artifact contains a custom or duplicate section")
        order.append(section_id)
        result[section_id] = data[offset:end]
        offset = end
    if order != [1, 3, 5, 6, 7, 10]:
        fail(f"non-canonical E1 section order: {order}")
    return result


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


def expect_unsupported(ahflc: Path, source: Path) -> None:
    proc = subprocess.run(
        [str(ahflc), "emit", "wasm", str(source)],
        capture_output=True,
        timeout=60,
    )
    stderr = proc.stderr.decode(errors="replace")
    if (
        proc.returncode == 0
        or proc.stdout
        or (
            "wasm.UNSUPPORTED_ORCHESTRATION" not in stderr
            and "wasm.UNSUPPORTED_CAPABILITY_FRAME" not in stderr
            and "wasm.ENTRY_AMBIGUOUS" not in stderr
        )
    ):
        fail(
            f"unsupported CLI fixture did not fail closed ({source.name}): "
            f"rc={proc.returncode}, stdout={len(proc.stdout)} bytes, stderr={stderr!r}"
        )


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

    parsed = sections(wasi_first)
    expected_exports = [
        "memory",
        "alloc",
        "dealloc",
        "run",
        "run2",
        "step",
        "current_state",
        "transition_count",
        "ahfl_abi_version",
    ]
    if export_names(parsed[7]) != expected_exports:
        fail("E1 export catalogue/order mismatch")
    # Fixture declaration order is [Done, Start] with initial Start (id 1).
    # The first global must therefore initialize to i32.const 1, proving the
    # encoder did not silently assume state zero.
    if not parsed[6].startswith(b"\x05\x7f\x01\x41\x01\x0b"):
        fail("current_state global does not use the non-zero Core initial state id")

    # Fixed E1 function index 5 is run. This is deliberately structural proof,
    # not real-wasmtime execution evidence.
    run = code_body(parsed[10], 5)
    if not run.endswith(b"\x20\x00\x0b"):
        fail("identity run does not end in local.get 0; end")
    if any(0x28 <= byte <= 0x3E for byte in run):
        fail("identity run contains a frame load/store opcode")

    # RFC 0026 P6-7 frame-bridge v2: a computed final carrying a String is
    # legal since V2-B (rodata PtrLen construction), so the old String-output
    # reject fixture was promoted to v2b_string_passthrough. The still-failing
    # shape here is a workflow value projection, which remains V2-D work.
    expect_unsupported(ahflc, tests_dir / "golden" / "ir" / "ok_workflow_value_flow.ahfl")

    print("all KR6.5 E1 binary/structural gates passed (not execution evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
