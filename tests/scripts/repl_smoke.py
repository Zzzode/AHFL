#!/usr/bin/env python3
import subprocess
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: repl_smoke.py <ahfl-repl>")

    repl = sys.argv[1]
    result = subprocess.run(
        [repl],
        input=b":help\n:quit\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))

    stdout = result.stdout.decode("utf-8", errors="replace")
    if "AHFL REPL Commands:" not in stdout:
        raise AssertionError(f"missing help header in output: {stdout!r}")
    if ":type <expr>" not in stdout:
        raise AssertionError(f"missing type command in output: {stdout!r}")

    stderr = result.stderr.decode("utf-8", errors="replace")
    if stderr:
        raise AssertionError(f"unexpected stderr: {stderr!r}")


if __name__ == "__main__":
    main()
