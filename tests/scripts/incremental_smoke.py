#!/usr/bin/env python3
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: incremental_smoke.py <ahfl-incremental> <input.ahfl>")

    tool = sys.argv[1]
    source = sys.argv[2]
    result = subprocess.run([tool, source], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))

    stdout = result.stdout.decode("utf-8", errors="replace")
    if "ahfl.incremental.v1" not in stdout:
        raise AssertionError(f"missing incremental header: {stdout!r}")
    if " recompiled" not in stdout:
        raise AssertionError(f"missing recompiled module result: {stdout!r}")
    if "stats modules_checked 1" not in stdout:
        raise AssertionError(f"missing modules_checked stat: {stdout!r}")
    if "stats cache_misses 1" not in stdout:
        raise AssertionError(f"missing cache_misses stat: {stdout!r}")

    stderr = result.stderr.decode("utf-8", errors="replace")
    if stderr:
        raise AssertionError(f"unexpected stderr: {stderr!r}")


if __name__ == "__main__":
    main()
