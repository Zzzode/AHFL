#!/usr/bin/env python3
"""REPL process smoke test.

Modes:
  baseline  (default) :help + Unit short-circuit + :quit. Runs in both
            WASM=ON and WASM=OFF builds (pure frontend + value, no engine).
  wasm      WASM=ON: eval 1+2 -> 3, "hello" -> "hello" (quoted), {} -> {}.
  wasm-off  WASM=OFF: eval refuses with the actionable diagnostic; :type and
            the Unit short-circuit still work.
"""
import subprocess
import sys


def run_repl(repl, inp):
    result = subprocess.run(
        [repl],
        input=inp,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode("utf-8", errors="replace"))
    return (
        result.stdout.decode("utf-8", errors="replace"),
        result.stderr.decode("utf-8", errors="replace"),
    )


def assert_contains(haystack, needle, label):
    if needle not in haystack:
        raise AssertionError(f"missing {label} in output: {haystack!r}")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: repl_smoke.py <ahfl-repl> [baseline|wasm|wasm-off]")

    repl = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "baseline"

    if mode == "wasm":
        stdout, stderr = run_repl(repl, b':help\n1+2\n1+2 // c\n"hello"\n{}\n:quit\n')
        assert_contains(stdout, "AHFL REPL Commands:", "help header")
        assert_contains(stdout, "3", "eval 1+2 -> 3")
        assert_contains(stdout, "3", "eval 1+2 // c -> 3 (comment delimiter)")
        assert_contains(stdout, '"hello"', "eval \"hello\" -> quoted string")
        assert_contains(stdout, "{}", "Unit short-circuit")
    elif mode == "wasm-off":
        stdout, stderr = run_repl(repl, b":help\n1+2\n{}\n:type 1 + 2\n:quit\n")
        assert_contains(stdout, "AHFL REPL Commands:", "help header")
        assert_contains(stdout, "requires the embedded wasm engine", "WASM=OFF refusal")
        assert_contains(stdout, "{}", "Unit short-circuit")
        assert_contains(stdout, "Int", ":type 1 + 2 -> Int")
    else:
        stdout, stderr = run_repl(repl, b":help\n{}\n:quit\n")
        assert_contains(stdout, "AHFL REPL Commands:", "help header")
        assert_contains(stdout, "{}", "Unit short-circuit")

    if stderr:
        raise AssertionError(f"unexpected stderr: {stderr!r}")


if __name__ == "__main__":
    main()
