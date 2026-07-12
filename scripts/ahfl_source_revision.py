"""Canonical source revision identity for AHFL release evidence."""

from __future__ import annotations

import hashlib
import subprocess
from pathlib import Path


def run_git(repo: Path, *args: str, text: bool = False) -> subprocess.CompletedProcess:
    result = subprocess.run(
        ["git", *args],
        cwd=repo,
        check=False,
        capture_output=True,
        text=text,
    )
    if result.returncode != 0:
        stderr = result.stderr if text else result.stderr.decode(errors="replace")
        raise RuntimeError(f"git {' '.join(args)} failed: {stderr.strip()}")
    return result


def compute_source_revision(repo: Path) -> str:
    """Return HEAD plus a deterministic digest of tracked and untracked changes."""

    root = repo.resolve()
    head = run_git(root, "rev-parse", "HEAD", text=True).stdout.strip()
    diff = run_git(root, "diff", "--binary", "HEAD").stdout
    untracked = run_git(
        root,
        "ls-files",
        "--others",
        "--exclude-standard",
        "-z",
    ).stdout.split(b"\0")

    digest = hashlib.sha256()
    digest.update(diff)
    paths = sorted(path for path in untracked if path)
    for encoded_relative in paths:
        relative = encoded_relative.decode("utf-8", errors="surrogateescape")
        path = root / relative
        if not path.is_file():
            continue
        digest.update(b"\0path\0")
        digest.update(encoded_relative)
        digest.update(b"\0content\0")
        digest.update(path.read_bytes())

    return head + ("+dirty:" + digest.hexdigest() if diff or paths else "")
