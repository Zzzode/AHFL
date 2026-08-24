#!/usr/bin/env bash
# tests/fuzz/crash_replay.sh — deterministic crash regression replay.
#
# Feeds every crash input under tests/fuzz/crashes/<target>/ to the matching
# libFuzzer binary as a single-run file argument and asserts a clean (exit 0)
# run. A crash input that still reproduces makes the binary exit non-zero and
# this script fails — that is the regression guard.
#
# Usage: crash_replay.sh <fuzzer_binary> <crashes_target_dir>
#   <fuzzer_binary>       path to a libFuzzer executable (accepts a file arg)
#   <crashes_target_dir>  tests/fuzz/crashes/<target>/
#
# With no crash inputs present the script is a no-op and exits 0.
#
# Files that are documentation, not inputs, are skipped: README.md, *.repro.md,
# and .gitkeep.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <fuzzer_binary> <crashes_target_dir>" >&2
  exit 2
fi

BIN="$1"
DIR="$2"

if [ ! -x "$BIN" ]; then
  echo "[SKIP] fuzzer binary not executable: $BIN" >&2
  # Not a regression: the binary only exists in a fuzzer-enabled build.
  exit 0
fi

if [ ! -d "$DIR" ]; then
  echo "[SKIP] crashes dir not found: $DIR" >&2
  exit 0
fi

count=0
fail=0
for input in "$DIR"/*; do
  [ -e "$input" ] || continue          # empty glob → no crash files yet
  base="$(basename "$input")"
  case "$base" in
    README.md|*.repro.md|.gitkeep) continue ;;
  esac
  [ -f "$input" ] || continue

  count=$((count + 1))
  echo "===== replay: $base ====="
  set +e
  "$BIN" "$input" >/dev/null 2>&1
  rc=$?
  set -e
  if [ "$rc" -ne 0 ]; then
    echo "[REGRESSED] $base -> exit $rc (crash reproduced)" >&2
    fail=1
  else
    echo "[OK] $base -> exit 0 (no crash)"
  fi
done

if [ "$count" -eq 0 ]; then
  echo "[NO-OP] no crash inputs in $DIR; nothing to replay."
fi

if [ "$fail" -ne 0 ]; then
  echo "CRASH REPLAY FAILED: at least one input still reproduces." >&2
  exit 1
fi

echo "CRASH REPLAY PASSED ($count input(s))."
exit 0
