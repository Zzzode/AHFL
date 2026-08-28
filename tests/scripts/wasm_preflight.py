#!/usr/bin/env python3
"""WASM toolchain preflight (KR6.4-pre / RFC 0026 Objective 6A).

The 6A mainline (KR6.5+) lowers Core-IR to WASM and requires a real `wasmtime`
to execute the product. This preflight is the gate that keeps that dependency
*optional and honest*, mirroring the z3 / NuSMV / llama.cpp precedent:

  * discovery order is deterministic — an explicit path (argv / AHFL_WASMTIME)
    wins, then PATH;
  * a MISSING toolchain SKIPs with a clear reason (exit SKIP_EXIT), but a path
    the user *explicitly* pointed at that is broken or too old FAILs — we never
    disguise a misconfiguration as a skip;
  * the smoke actually EXECUTES a fixed WASM fixture and asserts exit code +
    observable stdout, not merely `--version`.

The version-parsing and skip/fail decision logic are pure functions so they can
be unit-tested without any wasmtime binary present (see wasm_preflight_test.py).
This module is never linked into the AHFL core build; it is a test-only harness.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

# ctest treats this exit code as "skipped" (see SKIP_RETURN_CODE in CMake).
SKIP_EXIT = 77
FAIL_EXIT = 1

# Minimum wasmtime we are willing to accept. wasmtime has shipped stable WASI
# preview1 command support since well before this; 15.0.0 is a conservative
# floor that predates none of our target CI images while still rejecting truly
# ancient builds. Bump deliberately (with a test) when a newer feature is used.
MIN_WASMTIME = (15, 0, 0)

SENTINEL = "ahfl-wasm-preflight-ok"

_VERSION_RE = re.compile(r"(\d+)\.(\d+)\.(\d+)")


class PreflightError(Exception):
    """A misconfiguration that must FAIL (not skip)."""


@dataclass(frozen=True)
class Discovery:
    path: str | None
    explicit: bool  # True if the user named it (argv/env), False if PATH lookup


def discover_wasmtime(argv_path: str | None, environ: dict[str, str]) -> Discovery:
    """Resolve wasmtime with a deterministic precedence.

    1. an explicit argv path (from CMake AHFL_WASMTIME),
    2. the AHFL_WASMTIME environment variable,
    3. a PATH lookup for `wasmtime`.

    The first two are *explicit*: if named but unusable, the caller must FAIL.
    A bare PATH miss is *not* explicit and yields a skip.
    """
    if argv_path:
        return Discovery(path=argv_path, explicit=True)
    env_path = environ.get("AHFL_WASMTIME")
    if env_path:
        return Discovery(path=env_path, explicit=True)
    # Honor the caller-supplied PATH (so the decision logic is testable with a
    # synthetic environ) rather than always reading the process environment.
    # An absent PATH key means "no search path", not "fall back to os.environ".
    found = shutil.which("wasmtime", path=environ.get("PATH", ""))
    return Discovery(path=found, explicit=False)


def parse_version(version_output: str) -> tuple[int, int, int]:
    """Extract (major, minor, patch) from `wasmtime --version` output.

    Tolerant of the several shapes wasmtime has emitted across releases, e.g.
    'wasmtime 27.0.0', 'wasmtime-cli 15.0.1', 'wasmtime 18.0.2 (abcd1234 2024-…)'.
    Raises PreflightError if no dotted triple is present, so an unrecognized
    format FAILs loudly rather than silently mis-gating.
    """
    match = _VERSION_RE.search(version_output)
    if not match:
        raise PreflightError(
            f"could not parse a version from wasmtime output: {version_output!r}"
        )
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)))


def version_meets_minimum(
    version: tuple[int, int, int], minimum: tuple[int, int, int] = MIN_WASMTIME
) -> bool:
    return version >= minimum


def _query_version(path: str) -> str:
    try:
        proc = subprocess.run(
            [path, "--version"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except FileNotFoundError as exc:
        raise PreflightError(f"wasmtime path does not exist: {path}") from exc
    except OSError as exc:
        raise PreflightError(f"could not execute wasmtime at {path}: {exc}") from exc
    if proc.returncode != 0:
        raise PreflightError(
            f"`{path} --version` exited {proc.returncode}: {proc.stderr.strip()!r}"
        )
    # wasmtime prints the version to stdout; fall back to stderr just in case.
    return (proc.stdout + proc.stderr).strip()


def run_fixture(path: str, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Execute the fixture module under wasmtime, returning the completed proc."""
    return subprocess.run(
        [path, "run", str(fixture)],
        capture_output=True,
        text=True,
        timeout=60,
    )


def preflight(argv_path: str | None, fixture: Path, environ: dict[str, str]) -> int:
    disc = discover_wasmtime(argv_path, environ)

    if disc.path is None:
        print("SKIP: no wasmtime found (argv/AHFL_WASMTIME unset, not on PATH)")
        return SKIP_EXIT

    # From here the toolchain was named or discovered; any problem with an
    # *explicit* path is a FAIL, while a broken *PATH-discovered* binary is
    # treated as "effectively absent" and skips.
    try:
        version_text = _query_version(disc.path)
        version = parse_version(version_text)
        if not version_meets_minimum(version):
            raise PreflightError(
                f"wasmtime {'.'.join(map(str, version))} is below the required "
                f"minimum {'.'.join(map(str, MIN_WASMTIME))}"
            )

        if not fixture.exists():
            raise PreflightError(f"missing WASM fixture: {fixture}")

        proc = run_fixture(disc.path, fixture)
        if proc.returncode != 0:
            raise PreflightError(
                f"fixture run exited {proc.returncode}: {proc.stderr.strip()!r}"
            )
        if SENTINEL not in proc.stdout:
            raise PreflightError(
                f"fixture stdout missing sentinel {SENTINEL!r}: {proc.stdout!r}"
            )
    except PreflightError as err:
        if disc.explicit:
            print(f"FAIL: {err}", file=sys.stderr)
            return FAIL_EXIT
        print(f"SKIP: PATH-discovered wasmtime unusable ({err})")
        return SKIP_EXIT

    print(
        f"OK: wasmtime {'.'.join(map(str, version))} at {disc.path} "
        f"ran fixture and emitted {SENTINEL!r}"
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="WASM toolchain preflight")
    parser.add_argument(
        "wasmtime",
        nargs="?",
        default=None,
        help="explicit wasmtime path (from CMake AHFL_WASMTIME); optional",
    )
    parser.add_argument(
        "--fixture",
        default=str(Path(__file__).resolve().parent.parent / "fixtures" / "wasm" / "preflight.wasm"),
        help="path to the preflight.wasm fixture",
    )
    args = parser.parse_args(argv)
    return preflight(args.wasmtime, Path(args.fixture), dict(os.environ))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
