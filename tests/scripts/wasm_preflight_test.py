#!/usr/bin/env python3
"""Unit tests for the WASM toolchain preflight decision logic (KR6.4-pre).

These are pure-function tests: they exercise version parsing, the minimum-version
gate, discovery precedence, and — via a fake wasmtime shim — the skip-vs-fail
policy, WITHOUT requiring a real wasmtime binary. This is what makes the gate
itself testable on a machine (like the current CI image) that has no wasmtime.

Run: python3 wasm_preflight_test.py
"""
from __future__ import annotations

import importlib.util
import os
import stat
import sys
import tempfile
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("wasm_preflight", _HERE / "wasm_preflight.py")
assert _spec and _spec.loader
wp = importlib.util.module_from_spec(_spec)
# Register before exec so dataclass string-annotation resolution can find the
# module in sys.modules (frozen=True dataclasses look up cls.__module__).
sys.modules["wasm_preflight"] = wp
_spec.loader.exec_module(wp)

_FIXTURE = _HERE.parent / "fixtures" / "wasm" / "preflight.wasm"

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    if not cond:
        _failures.append(msg)
        print(f"  FAIL: {msg}")
    else:
        print(f"  ok:   {msg}")


def expect_raises(fn, msg: str) -> None:
    try:
        fn()
    except wp.PreflightError:
        print(f"  ok:   {msg}")
        return
    _failures.append(msg)
    print(f"  FAIL: {msg} (no PreflightError raised)")


def test_parse_version_formats() -> None:
    print("test_parse_version_formats")
    cases = {
        "wasmtime 27.0.0": (27, 0, 0),
        "wasmtime-cli 15.0.1": (15, 0, 1),
        "wasmtime 18.0.2 (abcd1234 2024-01-02)": (18, 0, 2),
        "wasmtime-cli 24.0.0\n": (24, 0, 0),
        "  wasmtime 100.2.3  ": (100, 2, 3),
    }
    for text, expected in cases.items():
        check(wp.parse_version(text) == expected, f"parse {text!r} -> {expected}")
    # Garbage / versionless output must FAIL loudly, not mis-parse.
    expect_raises(lambda: wp.parse_version("wasmtime (dev build)"), "versionless output raises")
    expect_raises(lambda: wp.parse_version(""), "empty output raises")


def test_minimum_gate() -> None:
    print("test_minimum_gate")
    check(wp.version_meets_minimum((15, 0, 0)), "exact minimum accepted")
    check(wp.version_meets_minimum((27, 3, 1)), "newer accepted")
    check(not wp.version_meets_minimum((14, 9, 9)), "one below rejected")
    check(not wp.version_meets_minimum((0, 1, 0)), "ancient rejected")


def test_discovery_precedence() -> None:
    print("test_discovery_precedence")
    d = wp.discover_wasmtime("/explicit/wasmtime", {"AHFL_WASMTIME": "/env/wasmtime"})
    check(d.path == "/explicit/wasmtime" and d.explicit, "argv path wins over env")
    d = wp.discover_wasmtime(None, {"AHFL_WASMTIME": "/env/wasmtime"})
    check(d.path == "/env/wasmtime" and d.explicit, "env path used when no argv, marked explicit")
    d = wp.discover_wasmtime(None, {})
    # No argv, no env: PATH lookup. On this CI image wasmtime is absent, so
    # path is None and not explicit -> a skip candidate.
    check(not d.explicit, "PATH lookup is non-explicit")


def _write_fake_wasmtime(dir_path: Path, script: str) -> Path:
    fake = dir_path / "wasmtime"
    fake.write_text("#!/usr/bin/env bash\n" + script)
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return fake


def test_missing_toolchain_skips() -> None:
    print("test_missing_toolchain_skips")
    # No argv, no env, and we cannot rely on PATH — force a clean environ.
    rc = wp.preflight(None, _FIXTURE, environ={})
    # On an image without wasmtime on PATH this is a skip; if a real wasmtime is
    # present it may legitimately pass. Either is acceptable, but never FAIL.
    check(rc in (wp.SKIP_EXIT, 0), f"missing/PATH toolchain -> skip or ok (got {rc})")


def test_explicit_bad_path_fails() -> None:
    print("test_explicit_bad_path_fails")
    rc = wp.preflight("/nonexistent/wasmtime", _FIXTURE, environ={})
    check(rc == wp.FAIL_EXIT, f"explicit nonexistent path FAILs (got {rc})")
    rc = wp.preflight(None, _FIXTURE, environ={"AHFL_WASMTIME": "/nonexistent/wasmtime"})
    check(rc == wp.FAIL_EXIT, f"explicit env nonexistent path FAILs (got {rc})")


def test_explicit_too_old_fails() -> None:
    print("test_explicit_too_old_fails")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), 'echo "wasmtime 9.0.0"\n')
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit too-old wasmtime FAILs (got {rc})")


def test_explicit_unparseable_fails() -> None:
    print("test_explicit_unparseable_fails")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), 'echo "wasmtime dev"\n')
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit unparseable version FAILs (got {rc})")


def test_path_discovered_broken_skips() -> None:
    print("test_path_discovered_broken_skips")
    # A wasmtime found only via PATH that is too old should SKIP (not our
    # misconfiguration to blame), distinguishing it from an explicit bad path.
    with tempfile.TemporaryDirectory() as td:
        _write_fake_wasmtime(Path(td), 'echo "wasmtime 9.0.0"\n')
        environ = {"PATH": td}
        rc = wp.preflight(None, _FIXTURE, environ=environ)
        check(rc == wp.SKIP_EXIT, f"PATH-discovered too-old wasmtime SKIPs (got {rc})")


def test_good_version_bad_run_fails_when_explicit() -> None:
    print("test_good_version_bad_run_fails_when_explicit")
    # Version passes, but the fixture run fails: an explicit toolchain must FAIL,
    # proving the smoke does not stop at --version.
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(
            Path(td),
            'if [ "$1" = "--version" ]; then echo "wasmtime 27.0.0"; exit 0; fi\n'
            'echo "boom" >&2; exit 3\n',
        )
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit good-version failing-run FAILs (got {rc})")


def test_good_version_wrong_stdout_fails() -> None:
    print("test_good_version_wrong_stdout_fails")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(
            Path(td),
            'if [ "$1" = "--version" ]; then echo "wasmtime 27.0.0"; exit 0; fi\n'
            'echo "some-other-output"; exit 0\n',
        )
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit run without sentinel FAILs (got {rc})")


def test_fake_end_to_end_ok() -> None:
    print("test_fake_end_to_end_ok")
    # Simulate a healthy wasmtime: correct version AND emits the sentinel on the
    # fixture run. This exercises the full happy path without a real engine.
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(
            Path(td),
            'if [ "$1" = "--version" ]; then echo "wasmtime 27.0.0"; exit 0; fi\n'
            f'echo "{wp.SENTINEL}"; exit 0\n',
        )
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == 0, f"healthy fake wasmtime returns OK (got {rc})")


def main() -> int:
    tests = [
        test_parse_version_formats,
        test_minimum_gate,
        test_discovery_precedence,
        test_missing_toolchain_skips,
        test_explicit_bad_path_fails,
        test_explicit_too_old_fails,
        test_explicit_unparseable_fails,
        test_path_discovered_broken_skips,
        test_good_version_bad_run_fails_when_explicit,
        test_good_version_wrong_stdout_fails,
        test_fake_end_to_end_ok,
    ]
    for t in tests:
        t()
    if _failures:
        print(f"\n{len(_failures)} assertion(s) failed")
        return 1
    print("\nall wasm-preflight unit assertions passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
