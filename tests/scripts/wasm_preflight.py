#!/usr/bin/env python3
"""WASM toolchain preflight (KR6.4-pre / RFC 0026 Objective 6A).

The 6A mainline (KR6.5+) lowers Core-IR to WASM and requires a real `wasmtime`
to execute the product. This preflight is the gate that keeps that dependency
*optional and honest*, mirroring the z3 / NuSMV / llama.cpp precedent:

  * discovery PROVENANCE is preserved — an EXPLICIT path (a `-DAHFL_WASMTIME=…`
    cache value or the AHFL_WASMTIME environment variable = user intent) is
    distinguished from a PATH-discovered one (not user intent);
  * a MISSING toolchain SKIPs with a clear reason (exit SKIP_EXIT). A path the
    user *explicitly* named that is broken / too old / unparseable FAILs — we
    never disguise a misconfiguration as a skip. A merely PATH-discovered one
    that turns out unusable SKIPs (it is not the user's misconfiguration);
  * the smoke actually EXECUTES a fixed WASM fixture and asserts exit code +
    observable stdout, not merely `--version`.

The version-parsing and skip/fail decision logic are pure functions so they can
be unit-tested without any wasmtime binary present (see wasm_preflight_test.py).
The CMake integration forwards discovery provenance verbatim via
--explicit-wasmtime / --detected-wasmtime, so the harness — not CMake — owns the
skip/fail decision (this is what fixes the earlier bug where a PATH-discovered
too-old wasmtime FAILed instead of skipping).

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
# Keep this in sync with scripts/bootstrap-wasmtime.sh (WASMTIME_VERSION floor).
MIN_WASMTIME = (15, 0, 0)

SENTINEL = "ahfl-wasm-preflight-ok"

# Anchor the version to a line that actually names wasmtime, so an unrelated
# "x.y.z" from a wrapper banner or a system warning cannot be mistaken for the
# toolchain version. wasmtime prints e.g. "wasmtime 27.0.0" or
# "wasmtime-cli 15.0.1"; we accept either spelling of the program token.
_VERSION_RE = re.compile(r"\bwasmtime(?:-cli)?\b[^\n]*?(\d+)\.(\d+)\.(\d+)")

# subprocess failure modes we normalize into PreflightError so the skip/fail
# policy (which keys off provenance) applies uniformly instead of leaking a
# traceback.
_SUBPROCESS_ERRORS = (
    FileNotFoundError,
    PermissionError,
    subprocess.TimeoutExpired,
    OSError,
)


class PreflightError(Exception):
    """A toolchain problem. Whether it FAILs or SKIPs depends on provenance."""


@dataclass(frozen=True)
class Discovery:
    path: str | None
    explicit: bool  # True if the user named it (explicit cache/env), False if PATH


def discover_wasmtime(
    explicit_path: str | None,
    detected_path: str | None,
    environ: dict[str, str],
) -> Discovery:
    """Resolve wasmtime while PRESERVING provenance.

    Precedence:
      1. an explicit path (CMake -DAHFL_WASMTIME or the AHFL_WASMTIME env var) —
         user intent, so an unusable one must FAIL;
      2. a CMake PATH-discovery result forwarded by the caller — not user intent;
      3. a direct PATH lookup (used when invoked outside CMake) — not user intent.

    Only tier 1 is marked ``explicit``.
    """
    if explicit_path:
        return Discovery(path=explicit_path, explicit=True)
    env_path = environ.get("AHFL_WASMTIME")
    if env_path:
        return Discovery(path=env_path, explicit=True)
    if detected_path:
        return Discovery(path=detected_path, explicit=False)
    # Honor the caller-supplied PATH (so the decision logic is testable with a
    # synthetic environ) rather than always reading the process environment.
    # An absent PATH key means "no search path", not "fall back to os.environ".
    found = shutil.which("wasmtime", path=environ.get("PATH", ""))
    return Discovery(path=found, explicit=False)


def parse_version(version_output: str) -> tuple[int, int, int]:
    """Extract (major, minor, patch) from `wasmtime --version` output.

    Anchored to a line naming wasmtime, tolerant of the several shapes wasmtime
    has emitted across releases, e.g. 'wasmtime 27.0.0', 'wasmtime-cli 15.0.1',
    'wasmtime 18.0.2 (abcd1234 2024-…)'. Raises PreflightError if no
    wasmtime-anchored dotted triple is present, so an unrecognized format FAILs
    loudly rather than silently mis-gating.
    """
    match = _VERSION_RE.search(version_output)
    if not match:
        raise PreflightError(
            f"could not parse a wasmtime version from output: {version_output!r}"
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
    except _SUBPROCESS_ERRORS as exc:
        raise PreflightError(f"could not run `{path} --version`: {exc}") from exc
    if proc.returncode != 0:
        raise PreflightError(
            f"`{path} --version` exited {proc.returncode}: {proc.stderr.strip()!r}"
        )
    # wasmtime prints the version to stdout; fall back to stderr just in case.
    return (proc.stdout + proc.stderr).strip()


def run_fixture(path: str, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Execute the fixture module under wasmtime, returning the completed proc.

    Normalizes launch/timeout failures into PreflightError so the caller's
    provenance-based skip/fail policy applies (a PATH-discovered wasmtime that
    hangs or is corrupt SKIPs; an explicitly named one FAILs).
    """
    try:
        return subprocess.run(
            [path, "run", str(fixture)],
            capture_output=True,
            text=True,
            timeout=60,
        )
    except _SUBPROCESS_ERRORS as exc:
        raise PreflightError(f"could not run `{path} run {fixture}`: {exc}") from exc


def preflight(
    explicit_path: str | None,
    fixture: Path,
    environ: dict[str, str],
    detected_path: str | None = None,
) -> int:
    disc = discover_wasmtime(explicit_path, detected_path, environ)

    if disc.path is None:
        print(
            "SKIP: no wasmtime found "
            "(no explicit AHFL_WASMTIME, none PATH-discovered)"
        )
        return SKIP_EXIT

    # From here the toolchain was named or discovered; any problem with an
    # *explicit* path is a FAIL, while a broken *discovered* binary is treated
    # as "effectively absent" and skips.
    try:
        version_text = _query_version(disc.path)
        version = parse_version(version_text)
        if not version_meets_minimum(version):
            raise PreflightError(
                f"wasmtime {'.'.join(map(str, version))} is below the required "
                f"minimum {'.'.join(map(str, MIN_WASMTIME))}"
            )

        if not fixture.exists():
            # A missing fixture is OUR bug, never the toolchain's — always FAIL.
            print(f"FAIL: missing WASM fixture: {fixture}", file=sys.stderr)
            return FAIL_EXIT

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
            print(f"FAIL: explicitly configured wasmtime unusable — {err}", file=sys.stderr)
            return FAIL_EXIT
        print(f"SKIP: PATH-discovered wasmtime unusable ({err})")
        return SKIP_EXIT

    print(
        f"OK: wasmtime {'.'.join(map(str, version))} at {disc.path} "
        f"ran fixture and emitted {SENTINEL!r}"
    )
    return 0


def main(argv: list[str]) -> int:
    default_fixture = (
        Path(__file__).resolve().parent.parent / "fixtures" / "wasm" / "preflight.wasm"
    )
    parser = argparse.ArgumentParser(description="WASM toolchain preflight")
    parser.add_argument(
        "--explicit-wasmtime",
        default=None,
        help="wasmtime path the user EXPLICITLY named (CMake AHFL_WASMTIME); "
        "unusable -> FAIL",
    )
    parser.add_argument(
        "--detected-wasmtime",
        default=None,
        help="wasmtime path discovered on PATH by CMake; unusable -> SKIP",
    )
    parser.add_argument(
        "--fixture",
        default=str(default_fixture),
        help="path to the preflight.wasm fixture",
    )
    args = parser.parse_args(argv)
    return preflight(
        args.explicit_wasmtime,
        Path(args.fixture),
        dict(os.environ),
        detected_path=args.detected_wasmtime,
    )


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
