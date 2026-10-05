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
# Targets
# -------
# The runner is target-agnostic. It discovers every target under fallback/*/
# that carries a `<name>.target.json` spec next to its source, so adding a
# representative target never edits this script. Each spec pins the mutant set
# (literal match/replace pairs), the human description, and the *expected*
# outcome (killed|survived), plus the committed score floor for the target.
#
# Three targets ship today:
#   arithmetic/         - integer arithmetic/predicate operators (target 1)
#   structured_writer/  - a canonical JSON-ish writer (target 2)
#   state_machine/      - agent-style state machine transitions (target 3)
# Each has exactly one deliberate, expected survivor, so the reported score is
# an honest sub-100% in all three cases, never a rigged 100%.
#
# Output
# ------
# A machine-readable JSON score report (schema ahfl.mutation.fallback.v2,
# documented in README.md). Written to the path given by --report. Exit status
# is 0 as long as the run completed and every fixed mutant was evaluated; the
# score *floor* and the per-mutant outcome *pinning* are enforced by the CTest
# gate (RunFallbackMutationGate.cmake), which reads the committed specs
# directly. This keeps the runner a pure measurement and the gate the policy.
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

# JSON string escaper: backslash, double-quote, and the two ASCII control
# characters a spec's description could carry. Emitted as a quoted JSON string.
json_string() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\t'/\\t}"
    printf '"%s"' "${s}"
}

# Discover every target under fallback/*/ that has a <name>.target.json spec.
TARGET_SPECS=()
for spec in "${SCRIPT_DIR}"/fallback/*/*.target.json; do
    [[ -e "${spec}" ]] || continue
    TARGET_SPECS+=("${spec}")
done

emit_unavailable() {
    local reason="$1"
    cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.fallback.v2",
  "runner": "fallback",
  "status": "tool_unavailable",
  "reason": $(json_string "${reason}"),
  "mutants_total": 0,
  "mutants_evaluated": 0,
  "killed": 0,
  "survived": 0,
  "mutation_score": null,
  "targets": []
}
JSON
    echo "fallback mutation runner: ${reason}" >&2
    echo "report written: ${REPORT_PATH}"
}

# Environment reasons ("this machine cannot run mutation testing") are the ONLY
# ones the gate treats as unavailable. Fixture breakage (a spec that no longer
# matches its source, a missing spec) must fail the gate, so those call
# fail_fixture below and exit non-zero.
emit_environment_unavailable() {
    emit_unavailable "environment: $1"
}

fail_fixture() {
    echo "fallback mutation runner: FIXTURE BROKEN: $1" >&2
    exit 3
}

if [[ ${#TARGET_SPECS[@]} -eq 0 ]]; then
    fail_fixture "no target specs found under ${SCRIPT_DIR}/fallback/*/*.target.json"
fi

# --- Spec parsing helpers -------------------------------------------------
# The spec is a small, fixed JSON document. We read the scalars we need with a
# tiny python3 one-liner (python3 is a hard dependency of the AHFL test tree
# already, e.g. tests/scripts/*.py). Keeping the parse in one place means the
# shell never has to reason about JSON.
PY=python3
if ! command -v "${PY}" &>/dev/null; then
    emit_environment_unavailable "python3 not found; cannot parse target specs"
    exit 0
fi

if [[ -z "${CXX_BIN}" ]]; then
    emit_environment_unavailable "no C++ compiler found (set \$CXX or install g++/clang++)"
    # This is an honest 'cannot run' contract, not a fabricated score. Still a
    # well-formed report, so the ctest gate can validate structure.
    exit 0
fi

# Probe the compiler once, up front. A compiler that is named but not runnable
# (bad $CXX, a broken toolchain) is an environment fault; a baseline compile
# failure AFTER this probe is genuine fixture breakage (target and test
# disagree) and must fail the gate instead.
if ! "${CXX_BIN}" --version >/dev/null 2>&1; then
    emit_environment_unavailable "C++ compiler '${CXX_BIN}' is not runnable"
    exit 0
fi

spec_get() {  # spec_get <spec> <dotted-path>
    "${PY}" - "$1" "$2" <<'PYEOF'
import json, sys
doc = json.load(open(sys.argv[1]))
node = doc
for part in sys.argv[2].split("."):
    node = node[int(part)] if part.isdigit() else node[part]
print(node)
PYEOF
}

spec_len() {  # spec_len <spec> <array-path>
    "${PY}" - "$1" "$2" <<'PYEOF'
import json, sys
doc = json.load(open(sys.argv[1]))
node = doc
for part in sys.argv[2].split("."):
    node = node[int(part)] if part.isdigit() else node[part]
print(len(node))
PYEOF
}

# --- Scratch workspace ----------------------------------------------------
WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/ahfl-mutation.XXXXXX")"
trap 'rm -rf "${WORK_ROOT}"' EXIT

build_and_test() {
    # $1 = directory holding the target source + test copies.
    # $2 = source basename, $3 = test basename.
    local dir="$1" src="$2" test="$3"
    local bin="${dir}/test_bin"
    if ! "${CXX_BIN}" -std=c++17 -O0 -o "${bin}" \
            "${dir}/${src}" "${dir}/${test}" >/dev/null 2>&1; then
        # Compilation failure counts as "killed" (the mutant broke the build).
        return 2
    fi
    if "${bin}" >/dev/null 2>&1; then
        return 0    # tests passed
    fi
    return 1        # tests failed
}

# --- Baseline: every unmutated target MUST pass ---------------------------
for spec in "${TARGET_SPECS[@]}"; do
    tname="$(spec_get "${spec}" target)"
    tsrc="$(spec_get "${spec}" source)"
    ttest="$(spec_get "${spec}" test)"
    tdir="$(dirname "${spec}")"
    base="${WORK_ROOT}/baseline_${tname}"
    mkdir -p "${base}"
    cp "${tdir}/"*.hpp "${tdir}/${tsrc}" "${tdir}/${ttest}" "${base}/"
    set +e
    build_and_test "${base}" "${tsrc}" "${ttest}"
    rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        fail_fixture "baseline (unmutated) build/test failed for target '${tname}' rc=${rc}"
    fi
done

# --- Apply each mutant of each target to a fresh copy ---------------------
TOTAL=0
EVALUATED=0
KILLED=0
SURVIVED=0
TARGET_JSON=""

for spec in "${TARGET_SPECS[@]}"; do
    tname="$(spec_get "${spec}" target)"
    tsrc="$(spec_get "${spec}" source)"
    ttest="$(spec_get "${spec}" test)"
    floor="$(spec_get "${spec}" score_floor)"
    tdir="$(dirname "${spec}")"
    nmut="$(spec_len "${spec}" mutants)"

    t_killed=0
    t_survived=0
    t_evaluated=0
    t_mut_json=""

    for ((mi = 0; mi < nmut; ++mi)); do
        mid="$(spec_get "${spec}" "mutants.${mi}.id")"
        mfrom="$(spec_get "${spec}" "mutants.${mi}.match")"
        mto="$(spec_get "${spec}" "mutants.${mi}.replace")"
        mdesc="$(spec_get "${spec}" "mutants.${mi}.description")"
        mexp="$(spec_get "${spec}" "mutants.${mi}.expect")"

        mdir="${WORK_ROOT}/mut_${tname}_${mid}"
        mkdir -p "${mdir}"
        cp "${tdir}/"*.hpp "${tdir}/${tsrc}" "${tdir}/${ttest}" "${mdir}/"

        # Verify the target expression is present exactly once, then
        # substitute. A spec that no longer matches its source is drift, not a
        # silent zero-mutant pass.
        count="$(grep -F -c -- "${mfrom}" "${mdir}/${tsrc}" || true)"
        if [[ "${count}" != "1" ]]; then
            fail_fixture "target '${tname}' mutant '${mid}' pattern not found exactly once (count=${count}); source or spec drifted"
        fi
        # Literal, delimiter-safe replacement via index()/substr() (no regex,
        # so metacharacters like ( ) + * in the patterns are matched verbatim).
        # The patterns are passed through the ENVIRONMENT, not awk -v: -v runs
        # escape processing, which would silently mangle a backslash-bearing
        # pattern (e.g. a C string escape) into a no-op substitution.
        FROM="${mfrom}" TO="${mto}" awk '
            { p = index($0, ENVIRON["FROM"]);
              if (p > 0) { $0 = substr($0, 1, p - 1) ENVIRON["TO"] substr($0, p + length(ENVIRON["FROM"])); }
              print }
        ' "${mdir}/${tsrc}" > "${mdir}/${tsrc}.new"
        mv "${mdir}/${tsrc}.new" "${mdir}/${tsrc}"

        set +e
        build_and_test "${mdir}" "${tsrc}" "${ttest}"
        rc=$?
        set -e

        t_evaluated=$((t_evaluated + 1))
        if [[ "${rc}" -eq 0 ]]; then
            outcome="survived"; t_survived=$((t_survived + 1))
        else
            outcome="killed"; t_killed=$((t_killed + 1))
        fi

        [[ -n "${t_mut_json}" ]] && t_mut_json="${t_mut_json},"
        t_mut_json="${t_mut_json}
      {\"id\": $(json_string "${mid}"), \"description\": $(json_string "${mdesc}"), \"expect\": $(json_string "${mexp}"), \"outcome\": $(json_string "${outcome}")}"
    done

    t_score="$(awk -v k="${t_killed}" -v e="${t_evaluated}" 'BEGIN{ if (e==0) print "null"; else printf "%.4f", k/e }')"
    TOTAL=$((TOTAL + nmut))
    EVALUATED=$((EVALUATED + t_evaluated))
    KILLED=$((KILLED + t_killed))
    SURVIVED=$((SURVIVED + t_survived))

    [[ -n "${TARGET_JSON}" ]] && TARGET_JSON="${TARGET_JSON},"
    TARGET_JSON="${TARGET_JSON}
  {
    \"target\": $(json_string "${tname}"),
    \"source\": $(json_string "${tsrc}"),
    \"score_floor\": ${floor},
    \"mutants_total\": ${nmut},
    \"mutants_evaluated\": ${t_evaluated},
    \"killed\": ${t_killed},
    \"survived\": ${t_survived},
    \"mutation_score\": ${t_score},
    \"mutants\": [${t_mut_json}
    ]
  }"
done

# Mutation score = killed / evaluated, rounded to 4 decimals via awk.
SCORE="$(awk -v k="${KILLED}" -v e="${EVALUATED}" 'BEGIN{ if (e==0) print "null"; else printf "%.4f", k/e }')"

cat > "${REPORT_PATH}" <<JSON
{
  "schema": "ahfl.mutation.fallback.v2",
  "runner": "fallback",
  "status": "ok",
  "compiler": $(json_string "${CXX_BIN}"),
  "mutants_total": ${TOTAL},
  "mutants_evaluated": ${EVALUATED},
  "killed": ${KILLED},
  "survived": ${SURVIVED},
  "mutation_score": ${SCORE},
  "targets": [${TARGET_JSON}
  ]
}
JSON

echo "fallback mutation runner: targets=${#TARGET_SPECS[@]} killed=${KILLED} survived=${SURVIVED} evaluated=${EVALUATED} score=${SCORE}"
echo "report written: ${REPORT_PATH}"
