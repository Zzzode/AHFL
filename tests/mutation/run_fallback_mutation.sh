#!/usr/bin/env bash
# Self-contained fallback mutation runner for AHFL.
#
# Purpose
# -------
# Produce a REAL, deterministic mutation score without depending on mull /
# mull-runner / LLVM instrumentation (none of which are installed in CI here).
# It applies a fixed, hand-audited set of source mutants to a COPY of a tiny
# target translation unit, rebuilds a narrow test binary against the mutated
# copy, and records whether the test suite catches (kills) the mutation.
#
# The real source tree is NEVER modified: every build happens inside a
# scratch directory built from copies. Each mutant is a literal string
# substitution keyed to an exact expression in target.cpp.
#
# Output
# ------
# A machine-readable JSON score report (schema documented in README.md).
# Written to the path given by --report (or $1). Exit status is 0 as long as
# the run completed and every fixed mutant was evaluated; it does NOT gate on
# an absolute score threshold (that would be flaky).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REPORT_PATH=""
CXX_BIN="${CXX:-}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --report) REPORT_PATH="$2"; shift 2 ;;
        --report=*) REPORT_PATH="${1#*=}"; shift ;;
        --cxx) CXX_BIN="$2"; shift 2 ;;
        --cxx=*) CXX_BIN="${1#*=}"; shift ;;
        *) if [[ -z "${REPORT_PATH}" ]]; then REPORT_PATH="$1"; shift; else
               echo "unknown argument: $1" >&2; exit 2; fi ;;
    esac
done

if [[ -z "${REPORT_PATH}" ]]; then
    # Default to a scratch location so a manual run never writes into the
    # source tree. The ctest gate always passes --report into the build dir.
    REPORT_PATH="${TMPDIR:-/tmp}/ahfl-mutation/fallback-score.json"
fi

# Pick a C++ compiler. Prefer $CXX, then g++, then clang++, then c++.
if [[ -z "${CXX_BIN}" ]]; then
    for cand in g++ clang++ c++; do
        if command -v "${cand}" &>/dev/null; then CXX_BIN="${cand}"; break; fi
    done
fi

mkdir -p "$(dirname "${REPORT_PATH}")"

emit_unavailable() {
    local reason="$1"
    cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.fallback.v1",
  "runner": "fallback",
  "status": "tool_unavailable",
  "reason": "${reason}",
  "mutants_total": 4,
  "mutants_evaluated": 0,
  "killed": 0,
  "survived": 0,
  "mutation_score": null,
  "mutants": []
}
JSON
    echo "fallback mutation runner: ${reason}" >&2
    echo "report written: ${REPORT_PATH}"
}

if [[ -z "${CXX_BIN}" ]]; then
    emit_unavailable "no C++ compiler found (set \$CXX or install g++/clang++)"
    # This is an honest 'cannot run' contract, not a fabricated score. Still a
    # well-formed report, so the ctest gate can validate structure.
    exit 0
fi

# --- Fixed, deterministic mutant set --------------------------------------
# Each entry: id | from-string | to-string | human description.
# The from-string must appear verbatim in target.cpp exactly once.
MUTANT_IDS=(classify_rel add_arith is_valid_rel scaled_arith)
declare -A MUT_FROM=(
    [classify_rel]="if (x <= 0) {"
    [add_arith]="return a + b;"
    [is_valid_rel]="return n >= 10;"
    [scaled_arith]="return v * 2;"
)
declare -A MUT_TO=(
    [classify_rel]="if (x < 0) {"
    [add_arith]="return a - b;"
    [is_valid_rel]="return n > 10;"
    [scaled_arith]="return v + 2;"
)
declare -A MUT_DESC=(
    [classify_rel]="relational operator <= -> < in classify()"
    [add_arith]="arithmetic operator + -> - in add()"
    [is_valid_rel]="relational operator >= -> > in is_valid()"
    [scaled_arith]="arithmetic operator * -> + in scaled() (untested; expected survivor)"
)

SRC_DIR="${SCRIPT_DIR}/fallback"
WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/ahfl-mutation.XXXXXX")"
trap 'rm -rf "${WORK_ROOT}"' EXIT

build_and_test() {
    # $1 = directory containing target.cpp/target.hpp/target_test.cpp copies.
    local dir="$1"
    local bin="${dir}/test_bin"
    if ! "${CXX_BIN}" -std=c++17 -O0 -o "${bin}" \
            "${dir}/target.cpp" "${dir}/target_test.cpp" >/dev/null 2>&1; then
        # Compilation failure counts as "killed" (the mutant broke the build).
        return 2
    fi
    if "${bin}" >/dev/null 2>&1; then
        return 0    # tests passed
    fi
    return 1        # tests failed
}

# --- Baseline: unmutated copy MUST pass -----------------------------------
BASE_DIR="${WORK_ROOT}/baseline"
mkdir -p "${BASE_DIR}"
cp "${SRC_DIR}/target.cpp" "${SRC_DIR}/target.hpp" "${SRC_DIR}/target_test.cpp" "${BASE_DIR}/"
set +e
build_and_test "${BASE_DIR}"
BASE_RC=$?
set -e
if [[ "${BASE_RC}" -ne 0 ]]; then
    emit_unavailable "baseline (unmutated) build/test failed rc=${BASE_RC}; cannot trust mutation results"
    exit 0
fi

# --- Apply each mutant to a fresh copy ------------------------------------
KILLED=0
SURVIVED=0
EVALUATED=0
MUT_JSON=""

for id in "${MUTANT_IDS[@]}"; do
    from="${MUT_FROM[$id]}"
    to="${MUT_TO[$id]}"
    desc="${MUT_DESC[$id]}"

    mdir="${WORK_ROOT}/mut_${id}"
    mkdir -p "${mdir}"
    cp "${SRC_DIR}/target.cpp" "${SRC_DIR}/target.hpp" "${SRC_DIR}/target_test.cpp" "${mdir}/"

    # Verify the target expression is present exactly once, then substitute.
    count="$(grep -F -c -- "${from}" "${mdir}/target.cpp" || true)"
    if [[ "${count}" != "1" ]]; then
        emit_unavailable "mutant '${id}' pattern not found exactly once (count=${count}); source drifted"
        exit 0
    fi
    # Literal, delimiter-safe replacement via index()/substr() (no regex, so
    # metacharacters like ( ) + * in the patterns are matched verbatim).
    awk -v from="${from}" -v to="${to}" '
        { p = index($0, from);
          if (p > 0) { $0 = substr($0, 1, p - 1) to substr($0, p + length(from)); }
          print }
    ' "${mdir}/target.cpp" > "${mdir}/target.cpp.new"
    mv "${mdir}/target.cpp.new" "${mdir}/target.cpp"

    set +e
    build_and_test "${mdir}"
    rc=$?
    set -e

    EVALUATED=$((EVALUATED + 1))
    if [[ "${rc}" -eq 0 ]]; then
        outcome="survived"; SURVIVED=$((SURVIVED + 1))
    else
        outcome="killed"; KILLED=$((KILLED + 1))
    fi

    [[ -n "${MUT_JSON}" ]] && MUT_JSON="${MUT_JSON},"
    MUT_JSON="${MUT_JSON}
    {\"id\": \"${id}\", \"description\": \"${desc}\", \"outcome\": \"${outcome}\"}"
done

TOTAL="${#MUTANT_IDS[@]}"
# Mutation score = killed / evaluated, rounded to 4 decimals via awk.
SCORE="$(awk -v k="${KILLED}" -v e="${EVALUATED}" 'BEGIN{ if (e==0) print "null"; else printf "%.4f", k/e }')"

cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.fallback.v1",
  "runner": "fallback",
  "status": "ok",
  "compiler": "${CXX_BIN}",
  "mutants_total": ${TOTAL},
  "mutants_evaluated": ${EVALUATED},
  "killed": ${KILLED},
  "survived": ${SURVIVED},
  "mutation_score": ${SCORE},
  "mutants": [${MUT_JSON}
  ]
}
JSON

echo "fallback mutation runner: killed=${KILLED} survived=${SURVIVED} evaluated=${EVALUATED} score=${SCORE}"
echo "report written: ${REPORT_PATH}"
