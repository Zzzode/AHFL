#!/usr/bin/env python3
"""Optional real-wasmtime KR6.5 E1 differential (execution evidence)."""
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

_OBSERVATION_RE = re.compile(
    r"^final_state_id=(\d+) transition_count=(\d+) identity_output=1$"
)


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
        print("SKIP: no wasmtime found for KR6.5 E1 execution differential")
        return wp.SKIP_EXIT

    # Before a usable version is established, preserve the shared preflight
    # provenance policy: explicit misconfiguration FAILs; incidental PATH tools
    # that are absent/broken/too old SKIP.
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
        return fail("missing E1 conformance producer or source fixture")

    with tempfile.TemporaryDirectory(prefix="ahfl-e1-wasm-") as td:
        artifact = Path(td) / "e1.wasm"
        native = subprocess.run(
            [str(probe), str(source), str(artifact)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if native.returncode != 0:
            return fail(
                f"same-frontend native/Core producer exited {native.returncode}: "
                f"{native.stderr.strip()!r}"
            )
        match = _OBSERVATION_RE.fullmatch(native.stdout.strip())
        if match is None:
            return fail(f"unrecognized native observation: {native.stdout!r}")
        expected_state = int(match.group(1))
        if int(match.group(2)) != 1:
            return fail("native observation did not execute exactly one transition")

        # Version is already known usable. Any invocation failure or malformed
        # output is now a hard conformance failure, not a false toolchain skip.
        try:
            wasm = subprocess.run(
                [discovery.path, "run", "--invoke", "step", str(artifact)],
                capture_output=True,
                text=True,
                timeout=60,
            )
        except (FileNotFoundError, PermissionError, subprocess.TimeoutExpired, OSError) as exc:
            return fail(f"usable wasmtime failed to launch E1 module: {exc}")
        if wasm.returncode != 0:
            return fail(
                f"wasmtime E1 step exited {wasm.returncode}: {wasm.stderr.strip()!r}"
            )
        lines = [line.strip() for line in wasm.stdout.splitlines() if line.strip()]
        if len(lines) != 1 or not re.fullmatch(r"-?\d+", lines[0]):
            return fail(f"unrecognized wasmtime step output: {wasm.stdout!r}")
        observed_state = int(lines[0])
        if observed_state != expected_state:
            return fail(
                f"native final state id {expected_state} != wasm step {observed_state}"
            )

    print(
        "OK: KR6.5 E1 real-wasmtime execution differential passed "
        f"(wasmtime {'.'.join(map(str, version))}, final_state_id={expected_state})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
