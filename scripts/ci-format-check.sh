#!/usr/bin/env bash
# Blocking CI gate for lossless, idempotent .ahfl formatting.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

AHFLC="${AHFLC:-build/dev/src/tooling/cli/ahflc}"
if [[ ! -x "$AHFLC" ]]; then
    echo "error: ahflc not found or not executable at $AHFLC" >&2
    echo "       build first: cmake --preset dev && cmake --build --preset build-dev" >&2
    exit 2
fi

echo "checking canonical std package"
"$AHFLC" fmt --check std

fixture_root="tests/golden/formatter"
fixture_config="${fixture_root}/two_spaces.ahfl-format"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/ahfl-format-check.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT

fixture_files=0
idempotence_cases=0
while IFS= read -r -d '' fixture; do
    fixture_files=$((fixture_files + 1))
    fixture_name="$(basename "$fixture")"
    for profile in default two-spaces; do
        case_dir="${temp_dir}/${fixture_files}-${profile}"
        mkdir -p "$case_dir"
        cp "$fixture" "${case_dir}/${fixture_name}"
        if [[ "$profile" == "two-spaces" ]]; then
            cp "$fixture_config" "${case_dir}/.ahfl-format"
        fi
        "$AHFLC" fmt "${case_dir}/${fixture_name}" >/dev/null
        "$AHFLC" fmt --check "${case_dir}/${fixture_name}" >/dev/null
        idempotence_cases=$((idempotence_cases + 1))
    done
done < <(find "$fixture_root" -maxdepth 1 -name '*.ahfl' -print0 | sort -z)

echo "formatter fixture files: $fixture_files"
echo "formatter idempotence cases: $idempotence_cases"
echo "formatter second-pass changes: 0"
