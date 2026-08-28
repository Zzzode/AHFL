#!/usr/bin/env python3
"""Unit tests for the WASM toolchain preflight decision logic (KR6.4-pre).

These are pure-function tests: they exercise version parsing, the minimum-version
gate, discovery precedence, provenance-aware skip-vs-fail policy, and subprocess
failure normalization — WITHOUT requiring a real wasmtime binary. This is what
makes the gate itself testable on a machine (like the current CI image) that has
no wasmtime.

Crucially, the skip-vs-fail tests drive the SAME provenance split the CMake
integration produces (explicit AHFL_WASMTIME vs PATH-discovered
AHFL_DETECTED_WASMTIME), so they cover the real wiring, not just an ideal branch.

Run: python3 wasm_preflight_test.py
"""
from __future__ import annotations

import importlib.util
import stat
import subprocess
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


def _write_fake_wasmtime(dir_path: Path, script: str) -> Path:
    fake = dir_path / "wasmtime"
    fake.write_text("#!/usr/bin/env bash\n" + script)
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
    return fake


# A fake that answers --version with `ver` and, on `run`, prints `run_stdout`
# and exits `run_rc`. Covers the full happy/sad matrix without a real engine.
def _fake_script(ver: str, run_stdout: str, run_rc: int) -> str:
    return (
        f'if [ "$1" = "--version" ]; then echo "{ver}"; exit 0; fi\n'
        f'echo "{run_stdout}"; exit {run_rc}\n'
    )


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
    # Regex is ANCHORED to a wasmtime-named line: an unrelated version banner
    # (e.g. a wrapper or system warning) must NOT be mistaken for wasmtime.
    expect_raises(
        lambda: wp.parse_version("some-wrapper 3.2.1\nwarning: libfoo 9.9.9"),
        "unrelated x.y.z without wasmtime token raises",
    )
    check(
        wp.parse_version("wrapper note\nwasmtime 22.1.0") == (22, 1, 0),
        "picks the wasmtime-anchored version amid noise",
    )


def test_minimum_gate() -> None:
    print("test_minimum_gate")
    check(wp.version_meets_minimum((15, 0, 0)), "exact minimum accepted")
    check(wp.version_meets_minimum((27, 3, 1)), "newer accepted")
    check(not wp.version_meets_minimum((14, 9, 9)), "one below rejected")
    check(not wp.version_meets_minimum((0, 1, 0)), "ancient rejected")


def test_discovery_precedence() -> None:
    print("test_discovery_precedence")
    d = wp.discover_wasmtime("/explicit/wasmtime", "/detected/wasmtime", {})
    check(d.path == "/explicit/wasmtime" and d.explicit, "explicit arg wins, marked explicit")
    d = wp.discover_wasmtime(None, "/detected/wasmtime", {"AHFL_WASMTIME": "/env/wasmtime"})
    check(d.path == "/env/wasmtime" and d.explicit, "env path beats detected, marked explicit")
    d = wp.discover_wasmtime(None, "/detected/wasmtime", {})
    check(d.path == "/detected/wasmtime" and not d.explicit, "detected path used, NOT explicit")
    d = wp.discover_wasmtime(None, None, {})
    check(not d.explicit, "bare PATH lookup is non-explicit")


def test_missing_toolchain_skips() -> None:
    print("test_missing_toolchain_skips")
    rc = wp.preflight(None, _FIXTURE, environ={}, detected_path=None)
    check(rc == wp.SKIP_EXIT, f"nothing found -> skip 77 (got {rc})")


def test_explicit_bad_path_fails() -> None:
    print("test_explicit_bad_path_fails")
    rc = wp.preflight("/nonexistent/wasmtime", _FIXTURE, environ={})
    check(rc == wp.FAIL_EXIT, f"explicit nonexistent path FAILs (got {rc})")
    rc = wp.preflight(None, _FIXTURE, environ={"AHFL_WASMTIME": "/nonexistent/wasmtime"})
    check(rc == wp.FAIL_EXIT, f"explicit env nonexistent path FAILs (got {rc})")


def test_detected_bad_path_skips() -> None:
    print("test_detected_bad_path_skips")
    # A PATH-discovered (non-explicit) nonexistent/broken binary must SKIP.
    rc = wp.preflight(None, _FIXTURE, environ={}, detected_path="/nonexistent/wasmtime")
    check(rc == wp.SKIP_EXIT, f"detected nonexistent path SKIPs (got {rc})")


def test_explicit_too_old_fails_but_detected_too_old_skips() -> None:
    # This is the exact real-wiring bug Codex found: the same too-old wasmtime
    # must FAIL when explicitly named and SKIP when only PATH-discovered.
    print("test_explicit_too_old_fails_but_detected_too_old_skips")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), 'echo "wasmtime 9.0.0"\n')
        rc_explicit = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc_explicit == wp.FAIL_EXIT, f"explicit too-old FAILs (got {rc_explicit})")
        rc_detected = wp.preflight(None, _FIXTURE, environ={}, detected_path=str(fake))
        check(rc_detected == wp.SKIP_EXIT, f"detected too-old SKIPs (got {rc_detected})")


def test_explicit_unparseable_fails() -> None:
    print("test_explicit_unparseable_fails")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), 'echo "wasmtime dev"\n')
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit unparseable version FAILs (got {rc})")


def test_timeout_normalized_per_provenance() -> None:
    print("test_timeout_normalized_per_provenance")
    # A wasmtime whose --version hangs past the timeout: explicit -> FAIL,
    # detected -> SKIP. Proves TimeoutExpired is normalized, not a traceback.
    #
    # We mock subprocess.run to RAISE TimeoutExpired directly rather than spawn a
    # real `sleep` — a real sleep launched via bash leaks a grandchild process
    # that subprocess's timeout kill does not reap, polluting the CI host. The
    # mock is both faster and leak-free.
    import subprocess as _sp

    real_run = _sp.run

    def timeout_run(cmd, **kwargs):  # type: ignore[no-untyped-def]
        raise subprocess.TimeoutExpired(cmd=cmd, timeout=kwargs.get("timeout", 0))

    _sp.run = timeout_run  # type: ignore[assignment]
    try:
        rc_explicit = wp.preflight("/any/wasmtime", _FIXTURE, environ={})
        rc_detected = wp.preflight(None, _FIXTURE, environ={}, detected_path="/any/wasmtime")
    finally:
        _sp.run = real_run  # type: ignore[assignment]
    check(rc_explicit == wp.FAIL_EXIT, f"explicit --version timeout FAILs (got {rc_explicit})")
    check(rc_detected == wp.SKIP_EXIT, f"detected --version timeout SKIPs (got {rc_detected})")


def test_oserror_on_run_normalized_per_provenance() -> None:
    print("test_oserror_on_run_normalized_per_provenance")
    # A fixture path that is a directory makes `wasmtime run <dir>`... still runs
    # our fake; to force an OSError at launch we point at a non-executable file.
    with tempfile.TemporaryDirectory() as td:
        non_exec = Path(td) / "not-exec"
        non_exec.write_text("#!/usr/bin/env bash\necho hi\n")  # intentionally not chmod +x
        rc_explicit = wp.preflight(str(non_exec), _FIXTURE, environ={})
        rc_detected = wp.preflight(None, _FIXTURE, environ={}, detected_path=str(non_exec))
        check(rc_explicit == wp.FAIL_EXIT, f"explicit non-exec FAILs (got {rc_explicit})")
        check(rc_detected == wp.SKIP_EXIT, f"detected non-exec SKIPs (got {rc_detected})")


def test_good_version_bad_run_fails_when_explicit() -> None:
    print("test_good_version_bad_run_fails_when_explicit")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), _fake_script("wasmtime 27.0.0", "boom", 3))
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit good-version failing-run FAILs (got {rc})")


def test_good_version_wrong_stdout_fails() -> None:
    print("test_good_version_wrong_stdout_fails")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), _fake_script("wasmtime 27.0.0", "some-other-output", 0))
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == wp.FAIL_EXIT, f"explicit run without sentinel FAILs (got {rc})")


def test_bootstrap_min_version_in_sync() -> None:
    # Anti-drift: the shell bootstrap's MIN_MAJOR must match the harness's
    # MIN_WASMTIME major, so the two version floors cannot silently diverge.
    print("test_bootstrap_min_version_in_sync")
    import re

    script = (_HERE.parent.parent / "scripts" / "bootstrap-wasmtime.sh").read_text()
    m = re.search(r"^MIN_MAJOR=(\d+)", script, re.MULTILINE)
    check(m is not None, "bootstrap script declares MIN_MAJOR")
    if m is not None:
        shell_min_major = int(m.group(1))
        check(
            shell_min_major == wp.MIN_WASMTIME[0],
            f"bootstrap MIN_MAJOR ({shell_min_major}) == harness MIN_WASMTIME major "
            f"({wp.MIN_WASMTIME[0]})",
        )
    # The pinned DEFAULT_VERSION must itself satisfy the minimum.
    dm = re.search(r'^DEFAULT_VERSION="(\d+)\.', script, re.MULTILINE)
    check(dm is not None, "bootstrap script declares DEFAULT_VERSION")
    if dm is not None:
        check(
            int(dm.group(1)) >= wp.MIN_WASMTIME[0],
            f"DEFAULT_VERSION major ({dm.group(1)}) >= min ({wp.MIN_WASMTIME[0]})",
        )



def test_fake_end_to_end_ok() -> None:
    print("test_fake_end_to_end_ok")
    with tempfile.TemporaryDirectory() as td:
        fake = _write_fake_wasmtime(Path(td), _fake_script("wasmtime 27.0.0", wp.SENTINEL, 0))
        rc = wp.preflight(str(fake), _FIXTURE, environ={})
        check(rc == 0, f"healthy fake wasmtime returns OK (got {rc})")
        # Same healthy binary via the detected (non-explicit) channel also OK.
        rc_detected = wp.preflight(None, _FIXTURE, environ={}, detected_path=str(fake))
        check(rc_detected == 0, f"healthy detected wasmtime returns OK (got {rc_detected})")


def main() -> int:
    tests = [
        test_parse_version_formats,
        test_minimum_gate,
        test_discovery_precedence,
        test_missing_toolchain_skips,
        test_explicit_bad_path_fails,
        test_detected_bad_path_skips,
        test_explicit_too_old_fails_but_detected_too_old_skips,
        test_explicit_unparseable_fails,
        test_timeout_normalized_per_provenance,
        test_oserror_on_run_normalized_per_provenance,
        test_good_version_bad_run_fails_when_explicit,
        test_good_version_wrong_stdout_fails,
        test_bootstrap_min_version_in_sync,
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
