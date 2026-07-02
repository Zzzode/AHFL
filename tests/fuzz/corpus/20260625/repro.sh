#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-fuzz}"
DATE="$(basename "$(dirname "${BASH_SOURCE[0]}")")"
CORPUS_DIR="$ROOT/tests/fuzz/corpus/$DATE"

CRASHES=(
  # 格式: <target> <crash_file> <expected_exit>
  "fuzz_typecheck  fuzz_typecheck.timeout-d41c6b2a9e8f    nonzero"
)

echo "CORPUS_DATE=$DATE  BUILD_DIR=$BUILD_DIR  (use -timeout=30 for this batch)"
echo "------------------------------------------------------------"

any_fail=0
for row in "${CRASHES[@]}"; do
  read -r target file expected <<< "$row"
  bin="$BUILD_DIR/tests/fuzz/$target"
  input="$CORPUS_DIR/$file"
  if [ ! -x "$bin" ]; then
    echo "[SKIP] $target binary not built at $build_dir"
    continue
  fi
  if [ ! -f "$input" ]; then
    echo "[SKIP] placeholder: $input not present (binary-only sample; see $input.SAMPLE.txt)"
    continue
  fi
  echo ""
  echo "===== $file ====="
  set +e
  timeout 60 "$bin" "$input" -timeout=30 2>&1 | tail -40
  rc=${PIPESTATUS[0]}
  set -e
  case "$expected" in
    nonzero) if [ "$rc" -eq 0 ]; then echo "[FIXED?] exit=0 expected timeout/crash"; any_fail=1; fi
             else echo "[CONFIRMED] rc=$rc"; ;;
    zero)    if [ "$rc" -ne 0 ]; then echo "[REGRESSED] rc=$rc expected pass"; any_fail=1; fi
             else echo "[CONFIRMED] rc=0"; ;;
  esac
done

echo ""
echo "============================================================"
if [ "$any_fail" -eq 0 ]; then echo "ALL OK."; exit 0; else echo "CHANGED."; exit 1; fi
