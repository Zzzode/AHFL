#!/usr/bin/env bash
# Wave-20 QW-1: minimal repro script for 20260622 crash batch.
# Usage: ./tests/fuzz/corpus/20260622/repro.sh [BUILD_DIR]
#   BUILD_DIR defaults to AHFL_ROOT/build-fuzz (libFuzzer mode).
# Exit code 0 = all samples in expected state; non-zero = at least one changed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-fuzz}"
DATE="$(basename "$(dirname "${BASH_SOURCE[0]}")")"
CORPUS_DIR="$ROOT/tests/fuzz/corpus/$DATE"

CRASHES=(
  # <target>       <crash_file>                              <expected>
  "fuzz_parser     fuzz_parser.crash-a7b3c1f9e8d2            nonzero"
)

echo "CORPUS_DATE=$DATE  BUILD_DIR=$BUILD_DIR"
echo "------------------------------------------------------------"

any_fail=0
for row in "${CRASHES[@]}"; do
  read -r target file expected <<< "$row"
  bin="$BUILD_DIR/tests/fuzz/$target"
  input="$CORPUS_DIR/$file"
  if [ ! -x "$bin" ]; then
    echo "[SKIP] $target binary not built at $bin (need libFuzzer mode build)"
    continue
  fi
  if [ ! -f "$input" ]; then
    echo "[SKIP] sample file not found: $input (corpus sample placeholder; binary only)"
    continue
  fi
  echo ""
  echo "===== $file ====="
  set +e
  "$bin" "$input" 2>&1 | head -60
  rc=${PIPESTATUS[0]}
  set -e
  case "$expected" in
    nonzero) if [ "$rc" -eq 0 ]; then echo "[FIXED?] exit=0 expected crash"; any_fail=1; fi
             else echo "[CONFIRMED] rc=$rc (crashed as expected)" ;;
    zero)    if [ "$rc" -ne 0 ]; then echo "[REGRESSED] rc=$rc expected pass"; any_fail=1; fi
             else echo "[CONFIRMED] rc=0 (passed as expected)" ;;
  esac
done

echo ""
echo "============================================================"
if [ "$any_fail" -eq 0 ]; then
  echo "ALL SAMPLES IN EXPECTED STATE."
  exit 0
else
  echo "SOME SAMPLES CHANGED STATE (see above)."
  exit 1
fi
