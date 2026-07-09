#!/usr/bin/env python3
"""Smoke tests for scripts/check-rfc.py."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REQUIRED_SECTIONS = [
    "Summary",
    "Motivation",
    "Goals",
    "Non-Goals",
    "Design",
    "User Impact",
    "Compatibility and Migration",
    "Implementation Plan",
    "Test Plan",
    "Rollout and Stabilization",
    "Alternatives",
    "Open Questions",
    "Decision History",
]


def frontmatter(rfc: str, status: str = "stabilized", updated: str = "2026-07-09") -> str:
    return f"""---
rfc: "{rfc}"
title: "Fixture RFC"
status: "{status}"
area: ["compiler"]
stability: "developer-facing"
created: "2026-07-01"
updated: "{updated}"
authors: ["test"]
shepherd: "test shepherd"
owners:
  compiler: "compiler owner"
required_reviewers: ["compiler"]
tracking_issue: "https://example.invalid/issues/1"
discussion: "https://example.invalid/issues/1"
implementation_prs: []
decision_due: "2026-07-31"
---
"""


def body() -> str:
    return "\n".join(f"## {section}\n\nFixture text.\n" for section in REQUIRED_SECTIONS)


def write_fixture(root: Path, index_updated: str) -> None:
    rfc_dir = root / "docs" / "rfcs"
    rfc_dir.mkdir(parents=True)
    (rfc_dir / "index.yml").write_text(
        f"""schema: 1
updated: "{index_updated}"
rfcs:
  - rfc: "0001"
    title: "Fixture RFC"
    status: "stabilized"
    file: "0001-fixture-rfc.zh.md"
    area: ["compiler"]
    stability: "developer-facing"
""",
        encoding="utf-8",
    )
    (rfc_dir / "0000-template.zh.md").write_text(frontmatter("0000") + body(), encoding="utf-8")
    (rfc_dir / "0001-fixture-rfc.zh.md").write_text(frontmatter("0001") + body(), encoding="utf-8")


def run_checker(checker: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(checker), "--root", str(root)],
        check=False,
        capture_output=True,
        text=True,
    )


def assert_passes(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"expected success, got {result.returncode}: stdout={result.stdout!r} stderr={result.stderr!r}"
        )


def assert_fails(result: subprocess.CompletedProcess[str], needle: str) -> None:
    if result.returncode == 0:
        raise AssertionError(f"expected failure, got success: stdout={result.stdout!r}")
    combined = result.stdout + result.stderr
    if needle not in combined:
        raise AssertionError(f"expected {needle!r} in output: {combined!r}")


def test_index_updated_must_cover_newest_rfc(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_fixture(root, index_updated="2026-07-08")
        assert_fails(run_checker(checker, root), "newest canonical RFC updated date")


def test_index_updated_accepts_current_date(checker: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        write_fixture(root, index_updated="2026-07-09")
        assert_passes(run_checker(checker, root))


def test_live_rfc_registry_passes_with_root_arg(checker: Path) -> None:
    repo_root = checker.resolve().parents[1]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        shutil.copytree(repo_root / "docs", root / "docs")
        shutil.copytree(repo_root / "src", root / "src")
        assert_passes(run_checker(checker, root))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: rfc_check_smoke.py <check-rfc.py>", file=sys.stderr)
        return 2
    checker = Path(sys.argv[1]).resolve()
    test_index_updated_must_cover_newest_rfc(checker)
    test_index_updated_accepts_current_date(checker)
    test_live_rfc_registry_passes_with_root_arg(checker)
    print("rfc check smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
