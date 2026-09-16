#!/usr/bin/env python3
"""Negative compile-test for the RFC 0027 P8 IR SSOT exhaustiveness gate.

The fixture (tests/fixtures/ir/ssot/expr_exhaustiveness_negative.cpp) rebuilds
the ahfl::ir::ExprNode alternative type list from the shared X-macro
(expr_nodes.def) and visits it with exactly one handler per alternative. This
harness compiles it twice with -fsyntax-only:

  1. injected build (-DAHFL_SSOT_INJECT_UNHANDLED): a dummy 21st alternative
     (SsotUnhandledExpr) is appended WITHOUT a visitor handler. The compile
     MUST fail with a non-zero exit and a diagnostic naming the unhandled
     type. This is the P8 property "adding a node without handling it breaks
     the build".
  2. clean build: no injection; the rebuilt variant is static_assert-identical
     to ahfl::ir::ExprNode and fully handled. The compile MUST succeed, which
     is the mutation check that the dummy alternative (not some unrelated
     error) is the sole cause of the negative result.

A compiler is picked from $AHFL_CXX, then $CXX, then PATH discovery
(c++/g++/clang++). The repository include root is added automatically.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = (
    REPO_ROOT
    / "tests"
    / "fixtures"
    / "ir"
    / "ssot"
    / "expr_exhaustiveness_negative.cpp"
)
INCLUDE_DIR = REPO_ROOT / "include"
UNHANDLED_TYPE = "SsotUnhandledExpr"
STD_FLAG = "-std=c++23"


def discover_compiler() -> str:
    for candidate in (os.environ.get("AHFL_CXX"), os.environ.get("CXX")):
        if candidate:
            return candidate
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit(
        "ir_ssot_compile_fail: no C++ compiler found (set AHFL_CXX or CXX)"
    )


def syntax_only(compiler: str, extra: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            compiler,
            STD_FLAG,
            "-fsyntax-only",
            f"-I{INCLUDE_DIR}",
            *extra,
            str(FIXTURE),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> int:
    failures: list[str] = []

    if not FIXTURE.is_file():
        print(f"FAIL: missing fixture {FIXTURE}", file=sys.stderr)
        return 1

    compiler = discover_compiler()
    print(f"compiler: {compiler}")
    print(f"fixture:  {FIXTURE.relative_to(REPO_ROOT)}")

    injected = syntax_only(compiler, ["-DAHFL_SSOT_INJECT_UNHANDLED"])
    if injected.returncode == 0:
        failures.append(
            "injected build unexpectedly compiled: an unhandled extra "
            "variant alternative was accepted by the exhaustive visitor"
        )
    elif UNHANDLED_TYPE not in (injected.stderr + injected.stdout):
        failures.append(
            "injected build failed, but the diagnostic does not name the "
            f"unhandled type {UNHANDLED_TYPE!r}; refusing to treat an "
            "unrelated error as the exhaustiveness gate"
        )
    else:
        print(
            "ok: injected build fails with a diagnostic naming "
            f"{UNHANDLED_TYPE}"
        )

    clean = syntax_only(compiler, [])
    if clean.returncode != 0:
        failures.append(
            "clean build unexpectedly failed (the fixture must compile "
            "when the dummy alternative is absent):\n"
            + (clean.stderr or clean.stdout).strip()
        )
    else:
        print("ok: clean build compiles (dummy alternative is the sole failure)")

    if failures:
        print("ir_ssot_compile_fail FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("ir_ssot_compile_fail passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
