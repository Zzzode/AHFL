#!/usr/bin/env python3
"""Negative compile-tests for the RFC 0027 P8/Q1 IR SSOT exhaustiveness gates.

Each fixture rebuilds one IR node variant's alternative type list from a shared
X-macro .def and visits it with exactly one handler per alternative. This
harness syntax-only-compiles every fixture twice:

  1. injected build (-D<inject>): one dummy extra alternative (<unhandled>) is
     appended to the rebuilt variant WITHOUT a visitor handler. The compile MUST
     fail with a non-zero exit and a diagnostic naming the unhandled type. This
     is the P8/Q1 property "adding a node without handling it breaks the build".
  2. clean build: no injection; the rebuilt variant is static_assert-identical
     to the production variant and fully handled. The compile MUST succeed,
     which is the mutation check that the dummy alternative (not some unrelated
     error) is the sole cause of the negative result.

Fixtures:
  * ahfl::ir::ExprNode (RFC 0027 P8, KR6.13-G) from tests/fixtures/ir/ssot/
    expr_nodes.def, sentinel SsotUnhandledExpr, injected with
    AHFL_SSOT_INJECT_UNHANDLED.
  * ahfl::ir::core::CoreValueTypeNode (RFC 0027 Q1, KR6.13-X) straight from the
    production include/ahfl/compiler/ir/core_value_types.def, sentinel
    SsotUnhandledVt, injected with AHFL_SSOT_INJECT_UNHANDLED_VT.

A compiler is picked from $AHFL_CXX, then $CXX, then PATH discovery
(c++/g++/clang++). The repository include root is added automatically.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"
STD_FLAG = "-std=c++23"


@dataclass(frozen=True)
class FixtureCase:
    label: str
    relative_path: str
    inject_flag: str
    unhandled_type: str


FIXTURES = (
    FixtureCase(
        label="ExprNode",
        relative_path="tests/fixtures/ir/ssot/expr_exhaustiveness_negative.cpp",
        inject_flag="AHFL_SSOT_INJECT_UNHANDLED",
        unhandled_type="SsotUnhandledExpr",
    ),
    FixtureCase(
        label="CoreValueTypeNode",
        relative_path="tests/fixtures/ir/ssot/core_value_type_nodes_negative.cpp",
        inject_flag="AHFL_SSOT_INJECT_UNHANDLED_VT",
        unhandled_type="SsotUnhandledVt",
    ),
)


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


def syntax_only(
    compiler: str, fixture: Path, extra: list[str]
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            compiler,
            STD_FLAG,
            "-fsyntax-only",
            f"-I{INCLUDE_DIR}",
            *extra,
            str(fixture),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def run_case(compiler: str, case: FixtureCase) -> list[str]:
    failures: list[str] = []
    fixture = REPO_ROOT / case.relative_path

    print(f"[{case.label}] fixture: {case.relative_path}")

    if not fixture.is_file():
        return [f"{case.label}: missing fixture {fixture}"]

    injected = syntax_only(compiler, fixture, [f"-D{case.inject_flag}"])
    if injected.returncode == 0:
        failures.append(
            f"{case.label}: injected build unexpectedly compiled: an unhandled "
            "extra variant alternative was accepted by the exhaustive visitor"
        )
    elif case.unhandled_type not in (injected.stderr + injected.stdout):
        failures.append(
            f"{case.label}: injected build failed, but the diagnostic does not "
            f"name the unhandled type {case.unhandled_type!r}; refusing to treat "
            "an unrelated error as the exhaustiveness gate"
        )
    else:
        print(
            f"[{case.label}] ok: injected build fails with a diagnostic naming "
            f"{case.unhandled_type}"
        )

    clean = syntax_only(compiler, fixture, [])
    if clean.returncode != 0:
        failures.append(
            f"{case.label}: clean build unexpectedly failed (the fixture must "
            "compile when the dummy alternative is absent):\n"
            + (clean.stderr or clean.stdout).strip()
        )
    else:
        print(
            f"[{case.label}] ok: clean build compiles (dummy alternative is the "
            "sole failure)"
        )

    return failures


def main() -> int:
    compiler = discover_compiler()
    print(f"compiler: {compiler}")

    failures: list[str] = []
    for case in FIXTURES:
        failures.extend(run_case(compiler, case))

    if failures:
        print("ir_ssot_compile_fail FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("ir_ssot_compile_fail passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
