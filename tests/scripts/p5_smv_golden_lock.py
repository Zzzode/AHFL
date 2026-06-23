#!/usr/bin/env python3
"""P5 SMV golden-lock aggregator.

Runs the AHFL compiler on a fixed set of SMV golden inputs, diffs the output
against checked-in .smv reference files, and exits non-zero on any mismatch.

On failure it prints:
  * a human-readable context summary
  * the exact `diff` command a reviewer can paste to inspect the delta
  * (optionally) a short unified diff of the mismatched output

Negative path: the script also self-tests by intentionally corrupting one
expected file in a temporary location and asserting that the diff-lock does
report a mismatch with the expected diagnostic markers.
"""

from __future__ import annotations

import argparse
import difflib
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Tuple


EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_INFRA = 2
EXIT_NEGATIVE_CASE_UNEXPECTED = 3


@dataclass(frozen=True)
class Case:
    name: str            # stable identifier, used in diff hints
    source: Path         # input .ahfl
    expected: Path       # reference .smv
    extra_args: Tuple[str, ...] = ()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def rel(path: Path, root: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def run_ahflc(ahflc: str, source: Path, extra_args: Iterable[str]) -> Tuple[int, str, str]:
    cmd = [ahflc, "emit", "smv", *extra_args, str(source)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def unified_diff(expected_text: str, actual_text: str,
                 expected_path: str, actual_path: str,
                 context: int = 5) -> str:
    lines = list(difflib.unified_diff(
        expected_text.splitlines(keepends=True),
        actual_text.splitlines(keepends=True),
        fromfile=expected_path,
        tofile=actual_path,
        n=context,
    ))
    return "".join(lines)


def check_case(ahflc: str, case: Case, repo_root: Path,
               failures: List[str]) -> None:
    if not case.source.is_file():
        failures.append(f"[{case.name}] missing source: {case.source}")
        return
    if not case.expected.is_file():
        failures.append(f"[{case.name}] missing expected: {case.expected}")
        return

    rc, stdout, stderr = run_ahflc(ahflc, case.source, case.extra_args)
    if rc != 0:
        failures.append(
            f"[{case.name}] ahflc emit smv exited {rc}\n"
            f"  source : {rel(case.source, repo_root)}\n"
            f"  stderr : {stderr.rstrip()}"
        )
        return

    expected_text = case.expected.read_text()
    actual_text = stdout

    # Normalise line endings for cross-platform determinism.
    expected_text = expected_text.replace("\r\n", "\n")
    actual_text = actual_text.replace("\r\n", "\n")

    if expected_text == actual_text:
        return

    # Emit structured diagnostics with an explicit diff command.
    expected_rel = rel(case.expected, repo_root)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=f".{case.name}.actual.smv", delete=False
    ) as tf:
        tf.write(actual_text)
        actual_tmp = tf.name

    diff_cmd = f"diff -u {case.expected} {actual_tmp}"
    udiff = unified_diff(expected_text, actual_text,
                         expected_rel, f"<actual:{case.name}>")
    failures.append(
        f"[{case.name}] golden mismatch\n"
        f"  source   : {rel(case.source, repo_root)}\n"
        f"  expected : {expected_rel}\n"
        f"  actual   : {actual_tmp}\n"
        f"  diff cmd : {diff_cmd}\n"
        f"  -- diff --\n{udiff}"
    )


# ---------------------------------------------------------------------------
# Case definitions
# ---------------------------------------------------------------------------

def build_cases(tests_dir: Path) -> List[Case]:
    formal = tests_dir / "golden" / "formal"
    ir = tests_dir / "golden" / "ir"
    return [
        Case(
            name="alias_const",
            source=ir / "ok_alias_const.ahfl",
            expected=formal / "ok_alias_const.smv",
        ),
        Case(
            name="expr_temporal",
            source=ir / "ok_expr_temporal.ahfl",
            expected=formal / "ok_expr_temporal.smv",
        ),
        Case(
            name="flow_workflow_semantics",
            source=formal / "ok_flow_workflow_semantics.ahfl",
            expected=formal / "ok_flow_workflow_semantics.smv",
        ),
        Case(
            name="bounded_data_semantics",
            source=formal / "ok_bounded_data_semantics.ahfl",
            expected=formal / "ok_bounded_data_semantics.smv",
        ),
        Case(
            name="project_check_ok",
            source=tests_dir / "integration" / "check_ok" / "app" / "main.ahfl",
            expected=formal / "project_check_ok.smv",
            extra_args=("--search-root", str(tests_dir / "integration" / "check_ok")),
        ),
    ]


# ---------------------------------------------------------------------------
# Negative self-test
# ---------------------------------------------------------------------------

def run_negative_self_test(ahflc: str, tests_dir: Path, repo_root: Path) -> int:
    """Verify the diff-lock itself reports mismatches.

    We write a deliberately corrupted expected file and assert the harness
    catches it and surfaces the "golden mismatch" / "diff cmd" markers.
    """
    formal = tests_dir / "golden" / "formal"
    seed_src = formal / "ok_alias_const.ahfl"
    # Reuse ok_alias_const.ahfl but compare against an empty SMV file - this
    # guarantees mismatch without touching any in-repo golden file.
    with tempfile.TemporaryDirectory() as td:
        bogus_expected = Path(td) / "bogus.smv"
        bogus_expected.write_text("MODULE main\n-- intentionally empty bogus\n")

        bogus_case = Case(
            name="negative_self_test_bogus_expected",
            source=seed_src,
            expected=bogus_expected,
        )
        failures: List[str] = []
        check_case(ahflc, bogus_case, repo_root, failures)

    if not failures:
        print("ERROR: negative case did not trigger a mismatch "
              "(golden-lock may be silently broken)", file=sys.stderr)
        return EXIT_NEGATIVE_CASE_UNEXPECTED

    message = failures[0]
    required_markers = ("golden mismatch", "diff cmd :", "diff -u")
    missing = [m for m in required_markers if m not in message]
    if missing:
        print(
            "ERROR: negative case mismatch message is missing markers: "
            f"{missing}\nGot:\n{message}",
            file=sys.stderr,
        )
        return EXIT_NEGATIVE_CASE_UNEXPECTED

    print(f"[p5_smv_golden_lock] negative self-test passed: "
          f"detected {len(failures)} intentional mismatch(es) "
          f"with required diagnostic markers")
    return EXIT_OK


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ahflc", required=True, help="Path to ahflc binary")
    parser.add_argument(
        "--tests-dir", required=True,
        help="Absolute path to the repo tests/ directory"
    )
    parser.add_argument(
        "--negative-only", action="store_true",
        help="Only run the negative self-test (used for script self-checks)."
    )
    args = parser.parse_args(argv)

    ahflc = args.ahflc
    tests_dir = Path(args.tests_dir).resolve()
    repo_root = tests_dir.parent.resolve()

    if not os.path.isfile(ahflc):
        print(f"ERROR: ahflc binary not found: {ahflc}", file=sys.stderr)
        return EXIT_INFRA
    if not tests_dir.is_dir():
        print(f"ERROR: tests dir not found: {tests_dir}", file=sys.stderr)
        return EXIT_INFRA

    if args.negative_only:
        return run_negative_self_test(ahflc, tests_dir, repo_root)

    cases = build_cases(tests_dir)
    failures: List[str] = []

    for case in cases:
        check_case(ahflc, case, repo_root, failures)

    print(f"[p5_smv_golden_lock] {len(cases) - len(failures)}/{len(cases)} "
          f"golden cases matched", flush=True)

    neg_rc = run_negative_self_test(ahflc, tests_dir, repo_root)
    if neg_rc != EXIT_OK:
        # The negative self-test already printed its diagnostics to stderr.
        # Surface this as an infrastructure failure in the aggregate report.
        failures.append("[negative_self_test] failed (see stderr above)")

    if failures:
        header = (
            "\n============================================================\n"
            f"p5_smv_golden_lock: {len(failures)} failure(s)\n"
            "If these deltas are intentional, attach the output of the "
            "'diff cmd' lines to the PR description and commit the updated "
            "tests/golden/formal/*.smv files in the same patch.\n"
            "============================================================\n"
        )
        print(header + "\n".join(failures), file=sys.stderr)
        return EXIT_MISMATCH

    print(f"[p5_smv_golden_lock] aggregate PASSED "
          f"({len(cases)} cases + 1 negative self-test)")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
