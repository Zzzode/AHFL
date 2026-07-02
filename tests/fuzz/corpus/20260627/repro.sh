#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-fuzz}"
DATE="$(basename "$(dirname "${BASH_SOURCE[0]}")")"
CORPUS_DIR="$ROOT/tests/fuzz/corpus/$DATE"

CRASHES=(
  # <target> <crash_file> <expected_exit>
  "fuzz_smv_emitter  fuzz_smv_emitter.oom-c3f9e1a7b5d2  nonzero"
)

echo "CORPUS_DATE=$DATE  BUILD_DIR=$BUILD_DIR  (OOM batch: rss_limit_mb=1024 recommended)"
echo "------------------------------------------------------------"

any_fail=0
for row in "${CRASHES[@]}"; do
  read -r target file expected <<< "$row"
  bin="$BUILD_DIR/tests/fuzz/$target"
  input="$CORPUS_DIR/$file"
  if [ ! -x "$bin" ]; then
    echo "[SKIP] $target binary not built at $BUILD_DIR"
    continue
  fi
  if [ ! -f "$input" ]; then
    echo "[SKIP] placeholder: $input not present (binary-only sample; see $input.SAMPLE.txt)"
    continue
  fi
  echo ""
  echo "===== $file ====="
  set +e
  # Limit RSS to 1 GB so the OOM doesn't thrash the local machine.
  (ulimit -Sv 1048576; "$bin" "$input" -rss_limit_mb=1024) 2>&1 | tail -30
  rc=${PIPESTATUS[0]}
  set -e
  case "$expected" in
    nonzero) if [ "$rc" -eq 0 ]; then echo "[FIXED?] exit=0 expected oom/crash"; any_fail=1; fi
             else echo "[CONFIRMED] rc=$rc"; ;;
    zero)    if [ "$rc" -ne 0 ]; then echo "[REGRESSED] rc=$rc expected pass"; any_fail=1; fi
             else echo "[CONFIRMED] rc=0"; ;;
  esac
done

echo ""
echo "============================================================"
if [ "$any_fail" -eq 0 ]; then echo "ALL OK."; exit 0; else echo "CHANGED."; exit 1; fi
