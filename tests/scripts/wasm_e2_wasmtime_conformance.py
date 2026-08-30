#!/usr/bin/env python3
"""Optional real-wasmtime E2 import linkage and ERROR/PENDING evidence."""
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_SPEC = importlib.util.spec_from_file_location("wasm_preflight", _HERE / "wasm_preflight.py")
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("cannot import wasm_preflight")
wp = importlib.util.module_from_spec(_SPEC)
sys.modules["wasm_preflight"] = wp
_SPEC.loader.exec_module(wp)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def u32(value: int) -> bytes:
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            byte |= 0x80
        out.append(byte)
        if not value:
            return bytes(out)


def s32(value: int) -> bytes:
    # Status constants are 0..2, whose signed LEB is one byte.
    return bytes([value])


def name(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return u32(len(encoded)) + encoded


def section(section_id: int, payload: bytes) -> bytes:
    return bytes([section_id]) + u32(len(payload)) + payload


def provider_wasm(field: str, status: int) -> bytes:
    signature = b"\x01\x60\x02\x7f\x7f\x03\x7f\x7f\x7f"
    functions = b"\x01\x00"
    exports = b"\x01" + name(field) + b"\x00\x00"
    instructions = (
        b"\x00"              # no local groups
        b"\x41" + s32(status) +
        b"\x41\x00" +
        b"\x41\x00" +
        b"\x0b"
    )
    code = b"\x01" + u32(len(instructions)) + instructions
    return b"\0asm\x01\0\0\0" + section(1, signature) + section(3, functions) + section(7, exports) + section(10, code)


def imported_field(wasm: bytes) -> str | None:
    matches = re.findall(rb"cap_[0-9]+", wasm)
    unique = sorted(set(matches))
    if len(unique) != 1:
        return None
    return unique[0].decode("ascii")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ahflc", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--explicit-wasmtime", default="")
    parser.add_argument("--detected-wasmtime", default="")
    args = parser.parse_args(argv)

    discovery = wp.discover_wasmtime(
        args.explicit_wasmtime or None,
        args.detected_wasmtime or None,
        dict(os.environ),
    )
    if discovery.path is None:
        print("SKIP: no wasmtime found for KR6.5 E2 preload execution")
        return wp.SKIP_EXIT
    try:
        version = wp.parse_version(wp._query_version(discovery.path))
        if not wp.version_meets_minimum(version):
            raise wp.PreflightError(
                f"wasmtime {version} is below minimum {wp.MIN_WASMTIME}"
            )
        help_result = subprocess.run(
            [discovery.path, "run", "--help"],
            capture_output=True,
            text=True,
            timeout=20,
        )
        if help_result.returncode != 0 or "--preload" not in help_result.stdout:
            raise wp.PreflightError("wasmtime run does not support --preload")
    except (wp.PreflightError, FileNotFoundError, PermissionError, subprocess.TimeoutExpired, OSError) as exc:
        if discovery.explicit:
            return fail(f"explicitly configured wasmtime lacks E2 preload support: {exc}")
        print(f"SKIP: PATH-discovered wasmtime is unusable for E2 preload ({exc})")
        return wp.SKIP_EXIT

    ahflc = Path(args.ahflc)
    source = Path(args.source)
    if not ahflc.is_file() or not source.is_file():
        return fail("missing ahflc or E2 fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-e2-wasmtime-") as td:
        td_path = Path(td)
        target = td_path / "target.wasm"
        emitted = subprocess.run(
            [str(ahflc), "emit", "wasm", str(source)],
            capture_output=True,
            timeout=60,
        )
        if emitted.returncode != 0:
            return fail(
                f"E2 producer exited {emitted.returncode}: "
                f"{emitted.stderr.decode(errors='replace')}"
            )
        target.write_bytes(emitted.stdout)
        field = imported_field(emitted.stdout)
        if field is None:
            return fail("could not identify exactly one SymbolId-based capability import")

        for status, expected_name in ((1, "ERROR"), (2, "PENDING")):
            provider = td_path / f"provider-{status}.wasm"
            provider.write_bytes(provider_wasm(field, status))
            try:
                invoked = subprocess.run(
                    [discovery.path,
                     "run",
                     "--preload",
                     f"ahfl_cap={provider}",
                     "--invoke",
                     "run2",
                     str(target),
                     "1024",
                     "1"],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
            except (FileNotFoundError, PermissionError, subprocess.TimeoutExpired, OSError) as exc:
                return fail(f"usable wasmtime failed to launch E2 module: {exc}")
            if invoked.returncode != 0:
                return fail(
                    f"wasmtime E2 {expected_name} exited {invoked.returncode}: "
                    f"{invoked.stderr.strip()!r}"
                )
            lines = [line.strip() for line in invoked.stdout.splitlines() if line.strip()]
            if lines != [str(status), "0", "0"]:
                return fail(
                    f"wasmtime E2 {expected_name} returned malformed tuple: {invoked.stdout!r}"
                )

    print(
        "OK: KR6.5 E2 real-wasmtime preload ERROR/PENDING execution passed "
        f"(wasmtime {'.'.join(map(str, version))}; not conforming OK-frame evidence)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
