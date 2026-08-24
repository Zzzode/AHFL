#!/usr/bin/env bash
# AHFL mull-based mutation runner.
#
# This is the "real tool" path: it drives mull (mull-runner + the
# mull-ir-frontend pass plugin) when that toolchain is installed. Regardless
# of whether the tool is present, it ALWAYS emits a machine-readable JSON
# score report (schema ahfl.mutation.mull.v1, documented in README.md):
#   - tool present  -> parses mull's JSON report and computes killed/
#                       survived/score.
#   - tool absent   -> emits status "tool_unavailable" with a clear reason and
#                       mutation_score: null. It does NOT fabricate a score.
#
# For a real mutation score that does NOT depend on mull, use
# run_fallback_mutation.sh instead.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/mutation"

REPORT_PATH=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --report) REPORT_PATH="$2"; shift 2 ;;
        --report=*) REPORT_PATH="${1#*=}"; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ -z "${REPORT_PATH}" ]]; then
    REPORT_PATH="${BUILD_DIR}/mutation_reports/mull-score.json"
fi
mkdir -p "$(dirname "${REPORT_PATH}")"

echo "=== AHFL Mutation Testing (mull) ==="
echo "Project root: ${PROJECT_ROOT}"
echo "Build dir: ${BUILD_DIR}"
echo "Report: ${REPORT_PATH}"

emit_unavailable() {
    local reason="$1"
    cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.mull.v1",
  "runner": "mull",
  "status": "tool_unavailable",
  "reason": "${reason}",
  "mutation_score": null,
  "killed": null,
  "survived": null,
  "targets": ["ahfl_compiler_syntax", "ahfl_compiler_ir", "ahfl_verification_formal"],
  "test_suites": ["ahfl_project_parse_tests", "ahfl_project_check_tests", "ahfl_bmc_tests"]
}
JSON
    echo "mull unavailable: ${reason}"
    echo "report written: ${REPORT_PATH}"
}

# Check for mull-runner.
if ! command -v mull-runner &> /dev/null; then
    emit_unavailable "mull-runner not found on PATH; install from https://github.com/mull-project/mull (brew install mull on macOS)"
    # Honest 'cannot run' contract, not a fabricated score. Exit 0 so this
    # path stays usable in environments without mull.
    exit 0
fi

# Build with the mull IR pass plugin.
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fpass-plugin=mull-ir-frontend" \
    -G Ninja

cmake --build "${BUILD_DIR}"

# Run mutation testing. mull writes an IDE/JSON report into report-dir.
MULL_REPORT_DIR="${BUILD_DIR}/mutation_reports"
mkdir -p "${MULL_REPORT_DIR}"
echo "Running mull-runner..."
mull-runner \
    --config "${SCRIPT_DIR}/mutation_config.json" \
    --report-dir "${MULL_REPORT_DIR}" \
    --reporters IDEReporter Elements \
    "${BUILD_DIR}/tests/ahfl_project_parse_tests"

# Parse mull's JSON output to compute killed/survived/score. mull's Elements
# reporter emits a mutation-testing-elements JSON; locate the newest one.
MULL_JSON="$(ls -t "${MULL_REPORT_DIR}"/*.json 2>/dev/null | grep -v "$(basename "${REPORT_PATH}")" | head -n1 || true)"
if [[ -z "${MULL_JSON}" || ! -f "${MULL_JSON}" ]]; then
    emit_unavailable "mull-runner completed but produced no JSON report in ${MULL_REPORT_DIR}"
    exit 0
fi

if command -v jq &>/dev/null; then
    # mutation-testing-elements schema: files[].mutants[].status
    # "Killed"/"Timeout"/"CompileError" == killed; "Survived"/"NoCoverage" == survived.
    read -r KILLED SURVIVED < <(jq -r '
        [.files[].mutants[].status] as $s
        | (($s | map(select(. == "Killed" or . == "Timeout" or . == "CompileError")) | length)),
          (($s | map(select(. == "Survived" or . == "NoCoverage")) | length))
        | @tsv' "${MULL_JSON}" | paste -sd' ' -)
    KILLED="${KILLED:-0}"; SURVIVED="${SURVIVED:-0}"
    TOTAL=$((KILLED + SURVIVED))
    SCORE="$(awk -v k="${KILLED}" -v t="${TOTAL}" 'BEGIN{ if (t==0) print "null"; else printf "%.4f", k/t }')"
    cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.mull.v1",
  "runner": "mull",
  "status": "ok",
  "source_report": "${MULL_JSON}",
  "mutants_total": ${TOTAL},
  "killed": ${KILLED},
  "survived": ${SURVIVED},
  "mutation_score": ${SCORE}
}
JSON
    echo "mull mutation: killed=${KILLED} survived=${SURVIVED} score=${SCORE}"
else
    emit_unavailable "mull-runner produced ${MULL_JSON} but jq is not installed to parse it"
    exit 0
fi

echo "=== Mutation testing complete ==="
echo "report written: ${REPORT_PATH}"
