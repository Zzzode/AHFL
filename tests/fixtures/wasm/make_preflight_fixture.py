#!/usr/bin/env python3
"""Generate the WASM toolchain preflight fixture (KR6.4-pre / RFC 0026 6A).

This emits a minimal, self-contained WASI *command* module that, when run under
a real `wasmtime` (or any WASI-preview1 host), writes a fixed sentinel line to
stdout and exits 0. It is the fixture the preflight smoke *executes* — proving
the discovered toolchain can load and run a real module, not merely report
`--version`.

The module is hand-assembled here (rather than via `wat2wasm`, which is not a
build dependency) so the bytes are auditable and regenerable from source. Run:

    python3 make_preflight_fixture.py            # writes preflight.wasm next to this file
    python3 make_preflight_fixture.py --check     # verify committed bytes match

The emitted module:
  * imports `wasi_snapshot_preview1.fd_write`,
  * exports linear `memory` and `_start`,
  * writes SENTINEL to fd 1 (stdout) via a single `fd_write`, then returns
    (a WASI command's clean `_start` return == exit code 0).
"""
from __future__ import annotations

import sys
from pathlib import Path

# The sentinel the smoke asserts on stdout. Kept ASCII + newline-terminated so
# no host-specific encoding or buffering quirk can alter the observable bytes.
SENTINEL = b"ahfl-wasm-preflight-ok\n"

# --- WASM binary encoding helpers -------------------------------------------


def uleb(n: int) -> bytes:
    """Unsigned LEB128."""
    if n < 0:
        raise ValueError("uleb requires n >= 0")
    out = bytearray()
    while True:
        byte = n & 0x7F
        n >>= 7
        if n:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def sleb(n: int) -> bytes:
    """Signed LEB128 (used by i32.const operands)."""
    out = bytearray()
    while True:
        byte = n & 0x7F
        n >>= 7
        # Sign-bit-aware termination.
        if (n == 0 and not (byte & 0x40)) or (n == -1 and (byte & 0x40)):
            out.append(byte)
            return bytes(out)
        out.append(byte | 0x80)


def section(section_id: int, payload: bytes) -> bytes:
    return bytes([section_id]) + uleb(len(payload)) + payload


def vec(items: list[bytes]) -> bytes:
    return uleb(len(items)) + b"".join(items)


# --- Opcodes / type tags -----------------------------------------------------

I32 = 0x7F
FUNC_TYPE = 0x60

OP_I32_CONST = 0x41
OP_I32_STORE = 0x36
OP_CALL = 0x10
OP_DROP = 0x1A
OP_END = 0x0B

# Fixed memory layout for the single iovec + result cell.
IOVEC_PTR = 0  # {buf: i32, len: i32} lives at [0, 8)
STR_PTR = 8  # SENTINEL bytes copied here by the data segment
NWRITTEN_PTR = 64  # fd_write writes the byte count here (we drop the result)


def i32_const(value: int) -> bytes:
    return bytes([OP_I32_CONST]) + sleb(value)


def i32_store(offset: int = 0, align_log2: int = 2) -> bytes:
    return bytes([OP_I32_STORE]) + uleb(align_log2) + uleb(offset)


def build_module() -> bytes:
    # Type section: type 0 = fd_write sig, type 1 = _start sig.
    fd_write_type = bytes([FUNC_TYPE]) + vec([bytes([I32])] * 4) + vec([bytes([I32])])
    start_type = bytes([FUNC_TYPE]) + vec([]) + vec([])
    type_sec = section(1, vec([fd_write_type, start_type]))

    # Import section: wasi_snapshot_preview1.fd_write : type 0 -> func index 0.
    mod_name = b"wasi_snapshot_preview1"
    fn_name = b"fd_write"
    import_entry = (
        uleb(len(mod_name)) + mod_name + uleb(len(fn_name)) + fn_name + bytes([0x00]) + uleb(0)
    )
    import_sec = section(2, vec([import_entry]))

    # Function section: local func 0 (module func index 1) has type 1.
    func_sec = section(3, vec([uleb(1)]))

    # Memory section: one min-1-page memory.
    mem_sec = section(5, vec([bytes([0x00]) + uleb(1)]))

    # Export section: memory 0 and _start (func index 1).
    export_mem = uleb(len(b"memory")) + b"memory" + bytes([0x02]) + uleb(0)
    export_start = uleb(len(b"_start")) + b"_start" + bytes([0x00]) + uleb(1)
    export_sec = section(7, vec([export_mem, export_start]))

    # Code section: _start body.
    body = bytearray()
    body += i32_const(IOVEC_PTR)  # &iovec.buf
    body += i32_const(STR_PTR)  # = pointer to string
    body += i32_store()
    body += i32_const(IOVEC_PTR + 4)  # &iovec.len
    body += i32_const(len(SENTINEL))  # = string length
    body += i32_store()
    body += i32_const(1)  # fd = stdout
    body += i32_const(IOVEC_PTR)  # iovs
    body += i32_const(1)  # iovs_len
    body += i32_const(NWRITTEN_PTR)  # nwritten out-ptr
    body += bytes([OP_CALL]) + uleb(0)  # call fd_write (import 0)
    body += bytes([OP_DROP])  # ignore byte count
    body += bytes([OP_END])
    func_body = vec([]) + bytes(body)  # zero locals, then the body
    code_entry = uleb(len(func_body)) + func_body
    code_sec = section(10, vec([code_entry]))

    # Data section: active segment, memory 0, offset = STR_PTR, SENTINEL bytes.
    data_offset_expr = i32_const(STR_PTR) + bytes([OP_END])
    data_entry = uleb(0) + data_offset_expr + uleb(len(SENTINEL)) + SENTINEL
    data_sec = section(11, vec([data_entry]))

    magic = b"\x00asm"
    version = b"\x01\x00\x00\x00"
    return (
        magic
        + version
        + type_sec
        + import_sec
        + func_sec
        + mem_sec
        + export_sec
        + code_sec
        + data_sec
    )


def main() -> int:
    out_path = Path(__file__).with_name("preflight.wasm")
    module = build_module()
    if "--check" in sys.argv[1:]:
        if not out_path.exists():
            print(f"missing {out_path}", file=sys.stderr)
            return 1
        if out_path.read_bytes() != module:
            print(f"{out_path} is stale — re-run make_preflight_fixture.py", file=sys.stderr)
            return 1
        print(f"{out_path} matches generator ({len(module)} bytes)")
        return 0
    out_path.write_bytes(module)
    print(f"wrote {out_path} ({len(module)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
