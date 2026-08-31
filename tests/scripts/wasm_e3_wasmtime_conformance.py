#!/usr/bin/env python3
"""Optional real-wasmtime E3 legacy-run pointer passthrough evidence."""
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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True)
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
        print("SKIP: no wasmtime found for KR6.5 E3 workflow execution")
        return wp.SKIP_EXIT
    try:
        version = wp.parse_version(wp._query_version(discovery.path))
        if not wp.version_meets_minimum(version):
            raise wp.PreflightError(
                f"wasmtime {version} is below minimum {wp.MIN_WASMTIME}"
            )
    except wp.PreflightError as exc:
        if discovery.explicit:
            return fail(f"explicitly configured wasmtime is unusable: {exc}")
        print(f"SKIP: PATH-discovered wasmtime is unusable ({exc})")
        return wp.SKIP_EXIT

    probe = Path(args.probe)
    source = Path(args.source)
    if not probe.is_file() or not source.is_file():
        return fail("missing E3 conformance producer or source fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-e3-wasmtime-") as td:
        artifact = Path(td) / "e3.wasm"
        native = subprocess.run(
            [str(probe), str(source), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if native.returncode != 0:
            return fail(
                f"same-frontend E3 producer exited {native.returncode}: "
                f"{native.stderr.strip()!r}"
            )
        expected = (
            "schedule=first,second completed_nodes=2 "
            "transition_count=2 identity_output=1"
        )
        if native.stdout.strip() != expected:
            return fail(f"unrecognized native E3 observation: {native.stdout!r}")

        ptr = 1024
        length = 23
        try:
            invoked = subprocess.run(
                [
                    discovery.path,
                    "run",
                    "--invoke",
                    "run",
                    str(artifact),
                    str(ptr),
                    str(length),
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
        except (FileNotFoundError, PermissionError, subprocess.TimeoutExpired, OSError) as exc:
            return fail(f"usable wasmtime failed to launch E3 module: {exc}")
        if invoked.returncode != 0:
            return fail(
                f"wasmtime E3 run exited {invoked.returncode}: "
                f"{invoked.stderr.strip()!r}"
            )
        lines = [line.strip() for line in invoked.stdout.splitlines() if line.strip()]
        if len(lines) != 1 or not re.fullmatch(r"-?\d+", lines[0]):
            return fail(f"unrecognized wasmtime E3 run output: {invoked.stdout!r}")
        if int(lines[0]) != ptr:
            return fail(f"wasmtime E3 run returned {lines[0]}, expected pointer {ptr}")

    print(
        "OK: KR6.5 E3 real-wasmtime workflow run passthrough passed "
        f"(wasmtime {'.'.join(map(str, version))}; persistent counters not observed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
