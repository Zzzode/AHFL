# CMake -P gate for the self-contained fallback mutation runner.
#
# Invoked by the ahfl.mutation.fallback_score ctest. It runs
# run_fallback_mutation.sh, then validates the emitted JSON score report
# (schema ahfl.mutation.fallback.v2) against the COMMITTED target specs. This
# is what turns "a config report + a score exists" into the release-blocking
# mutation signal KR7.3 names:
#
#   1. every committed target under <SPEC_ROOT>/*/ must be present in the
#      report (a dropped target is a coverage regression, not a silent pass);
#   2. every mutant's OUTCOME is pinned to the `expect` recorded in its target
#      spec — no strengthening test may quietly stop killing a mutant, and no
#      new survivor may appear without an explicit, reviewed spec edit;
#   3. each target's mutation_score must clear its committed `score_floor`.
#
# The target specs are the single source of truth: the gate reads `expect` and
# `score_floor` straight from them, so adding a target or recording an
# intentional survivor is a spec edit and never a gate edit.
#
# Required -D variables:
#   RUNNER       - absolute path to run_fallback_mutation.sh
#   REPORT_FILE  - path the runner should write its JSON report to
#   SPEC_ROOT    - absolute path to tests/mutation/fallback (holds */<name>.target.json)

cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0054 NEW)  # quoted operands to if() are never dereferenced

foreach(required RUNNER REPORT_FILE SPEC_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

# Fresh report each run.
file(REMOVE "${REPORT_FILE}")
get_filename_component(report_dir "${REPORT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${report_dir}")

execute_process(
    COMMAND bash "${RUNNER}" --report "${REPORT_FILE}"
    RESULT_VARIABLE runner_rc
    OUTPUT_VARIABLE runner_out
    ERROR_VARIABLE runner_err
)
message(STATUS "fallback runner stdout:\n${runner_out}")
if(NOT runner_rc EQUAL 0)
    message(FATAL_ERROR
        "Fallback mutation runner exited ${runner_rc}\nstderr:\n${runner_err}")
endif()

if(NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Fallback mutation report not produced: ${REPORT_FILE}")
endif()

file(READ "${REPORT_FILE}" report)

# --- Structural validation via string(JSON ...) ---------------------------
string(JSON schema ERROR_VARIABLE e GET "${report}" schema)
if(e OR NOT schema STREQUAL "ahfl.mutation.fallback.v2")
    message(FATAL_ERROR "Report schema mismatch: '${schema}' (err='${e}')")
endif()

string(JSON status ERROR_VARIABLE e GET "${report}" status)
if(e)
    message(FATAL_ERROR "Report missing 'status' (err='${e}')")
endif()

# The gate accepts 'tool_unavailable' ONLY for an environment reason — no C++
# compiler, or no python3 to parse the specs. These mean "this machine cannot
# run mutation testing", which is honest and reproducible, and the artifacts
# still cannot regress. A target whose spec no longer matches its source is
# NOT an environment reason: it is fixture breakage that must fail, or a
# deleted target directory would silently shrink the gate. The runner
# distinguishes the two in `reason`; the sentinel below is that contract.
if(status STREQUAL "tool_unavailable")
    string(JSON reason ERROR_VARIABLE e GET "${report}" reason)
    if(e OR reason STREQUAL "")
        message(FATAL_ERROR "tool_unavailable report must carry a reason")
    endif()

    string(FIND "${reason}" "environment: " env_pos)
    if(NOT env_pos EQUAL 0)
        message(FATAL_ERROR
            "Mutation suite is broken, not unavailable: '${reason}'. "
            "A spec/target mismatch or a deleted target directory must fail the "
            "gate; only a missing compiler/python3 may yield tool_unavailable.")
    endif()

    # JSON null reads as an empty string through string(JSON), so validate the
    # literal absence of a score, not a "null" spelling.
    string(JSON score ERROR_VARIABLE e GET "${report}" mutation_score)
    if(NOT score STREQUAL "")
        message(FATAL_ERROR "tool_unavailable must report no score, got '${score}'")
    endif()
    string(JSON target_count ERROR_VARIABLE e LENGTH "${report}" targets)
    if(e OR NOT target_count EQUAL 0)
        message(FATAL_ERROR "tool_unavailable must report no targets")
    endif()
    message(STATUS
        "Mutation fallback score report ok: status=tool_unavailable reason=${reason}")
    return()
endif()

if(NOT status STREQUAL "ok")
    message(FATAL_ERROR "Unexpected report status: '${status}'")
endif()

# --- Numeric totals -------------------------------------------------------
foreach(field mutants_evaluated killed survived mutation_score mutants_total)
    string(JSON ${field} ERROR_VARIABLE e GET "${report}" ${field})
    if(e)
        message(FATAL_ERROR "Report missing '${field}' (err='${e}')")
    endif()
endforeach()

if(mutants_evaluated LESS 1)
    message(FATAL_ERROR "No mutants evaluated (${mutants_evaluated})")
endif()

math(EXPR sum "${killed} + ${survived}")
if(NOT sum EQUAL mutants_evaluated)
    message(FATAL_ERROR
        "killed(${killed}) + survived(${survived}) != evaluated(${mutants_evaluated})")
endif()

# --- Committed target specs: the source of truth --------------------------
# targets.json is the signed-off target SET. Reading it (rather than globbing
# the directories) is what makes a deleted target FAIL instead of silently
# shrinking the gate.
set(manifest_file "${SPEC_ROOT}/targets.json")
if(NOT EXISTS "${manifest_file}")
    message(FATAL_ERROR "Missing target manifest: ${manifest_file}")
endif()
file(READ "${manifest_file}" manifest_doc)
string(JSON manifest_count ERROR_VARIABLE e LENGTH "${manifest_doc}" targets)
if(e OR manifest_count LESS 1)
    message(FATAL_ERROR "Target manifest ${manifest_file} has no targets (err='${e}')")
endif()

string(JSON target_count ERROR_VARIABLE e LENGTH "${report}" targets)
if(e)
    message(FATAL_ERROR "Report missing 'targets' array (err='${e}')")
endif()
if(NOT target_count EQUAL manifest_count)
    message(FATAL_ERROR
        "Report carries ${target_count} target(s) but ${manifest_count} are "
        "declared in ${manifest_file} — a target was dropped from the run")
endif()

# Report totals must equal the sum over targets — a mismatch means the report
# is internally inconsistent.
math(EXPR total_mutants 0)
math(EXPR total_evaluated 0)
math(EXPR total_killed 0)
math(EXPR total_survived 0)

math(EXPR last_target "${target_count} - 1")
foreach(i RANGE ${last_target})
    string(JSON tname ERROR_VARIABLE e GET "${report}" targets ${i} target)
    if(e OR tname STREQUAL "")
        message(FATAL_ERROR "target[${i}] missing name (err='${e}')")
    endif()
    if(i GREATER manifest_count OR i LESS 0)
        message(FATAL_ERROR "target index ${i} out of range")
    endif()
    string(JSON manifest_name ERROR_VARIABLE e GET "${manifest_doc}" targets ${i} target)
    if(e OR manifest_name STREQUAL "")
        message(FATAL_ERROR "Manifest ${manifest_file} target[${i}] missing name")
    endif()
    if(NOT tname STREQUAL manifest_name)
        message(FATAL_ERROR
            "Report target[${i}] '${tname}' != manifest '${manifest_name}' — the "
            "run's target set drifted from the signed-off set in ${manifest_file}")
    endif()

    set(spec_file "${SPEC_ROOT}/${tname}/${tname}.target.json")
    if(NOT EXISTS "${spec_file}")
        message(FATAL_ERROR "Missing spec for target '${tname}': ${spec_file}")
    endif()

    file(READ "${spec_file}" spec_doc)
    string(JSON spec_floor ERROR_VARIABLE e GET "${spec_doc}" score_floor)
    if(e OR spec_floor STREQUAL "")
        message(FATAL_ERROR "Spec ${spec_file} missing 'score_floor'")
    endif()

    # The report echoes the spec's floor (recorded once in the spec, copied
    # into the artifact). Compare as numbers, not as JSON-typed strings: the
    # report truncates to 4 decimals, so a string compare would trip on
    # 0.88 vs 0.8800.
    string(JSON report_floor ERROR_VARIABLE e GET "${report}" targets ${i} score_floor)
    if(e)
        message(FATAL_ERROR "target[${i}] '${tname}' missing score_floor (err='${e}')")
    endif()
    if(NOT report_floor EQUAL spec_floor)
        message(FATAL_ERROR
            "target[${i}] '${tname}' score_floor ${report_floor} != spec ${spec_floor}")
    endif()

    # JSON null reads as an empty string through string(JSON), so "has a score"
    # is a non-empty test.
    string(JSON t_score ERROR_VARIABLE e GET "${report}" targets ${i} mutation_score)
    if(e OR t_score STREQUAL "")
        message(FATAL_ERROR "target[${i}] '${tname}' missing mutation_score")
    endif()

    # (3) The committed score floor is the release-blocking threshold.
    if(t_score LESS spec_floor)
        message(FATAL_ERROR
            "target '${tname}' mutation score ${t_score} below committed floor "
            "${spec_floor} — a covering test regressed to surviving")
    endif()

    # (2) Pin the per-mutant outcome list to the spec, by index (the runner
    # emits mutants in spec order, so index is identity AND order is checked).
    string(JSON spec_mutants ERROR_VARIABLE e LENGTH "${spec_doc}" mutants)
    if(e)
        message(FATAL_ERROR "Spec ${spec_file} missing 'mutants' (err='${e}')")
    endif()
    string(JSON manifest_mutants ERROR_VARIABLE e GET "${manifest_doc}" targets ${i} mutants)
    if(e OR NOT spec_mutants EQUAL manifest_mutants)
        message(FATAL_ERROR
            "target '${tname}': spec declares ${spec_mutants} mutants but "
            "${manifest_file} declares ${manifest_mutants} — deleting mutants "
            "from a spec must be a reviewed manifest edit too")
    endif()
    string(JSON report_mutants ERROR_VARIABLE e LENGTH "${report}" targets ${i} mutants)
    if(e OR NOT report_mutants EQUAL spec_mutants)
        message(FATAL_ERROR
            "target '${tname}': report has ${report_mutants} mutants, spec has "
            "${spec_mutants}")
    endif()

    math(EXPR last_mut "${spec_mutants} - 1")
    foreach(j RANGE ${last_mut})
        string(JSON spec_id ERROR_VARIABLE e GET "${spec_doc}" mutants ${j} id)
        string(JSON spec_expect ERROR_VARIABLE e GET "${spec_doc}" mutants ${j} expect)
        if(e OR spec_id STREQUAL "" OR spec_expect STREQUAL "")
            message(FATAL_ERROR
                "Spec ${spec_file} mutant[${j}] missing id/expect (err='${e}')")
        endif()

        string(JSON rep_id ERROR_VARIABLE e GET "${report}" targets ${i} mutants ${j} id)
        if(e OR NOT rep_id STREQUAL spec_id)
            message(FATAL_ERROR
                "target '${tname}' mutant[${j}] id '${rep_id}' != spec '${spec_id}' "
                "— the report and the committed spec disagree on the mutant set")
        endif()

        string(JSON rep_outcome ERROR_VARIABLE e GET
            "${report}" targets ${i} mutants ${j} outcome)
        if(e OR (NOT rep_outcome STREQUAL "killed" AND NOT rep_outcome STREQUAL "survived"))
            message(FATAL_ERROR
                "target '${tname}' mutant '${spec_id}' bad outcome '${rep_outcome}'")
        endif()

        if(NOT rep_outcome STREQUAL spec_expect)
            if(spec_expect STREQUAL "killed")
                message(FATAL_ERROR
                    "target '${tname}' mutant '${spec_id}' SURVIVED but its spec pins "
                    "it as killed — a covering test stopped killing this mutant "
                    "(update ${spec_file} only if the survivor is intentional)")
            else()
                message(FATAL_ERROR
                    "target '${tname}' mutant '${spec_id}' was killed but its spec pins "
                    "it as ${spec_expect} — the intentional survivor is now covered; "
                    "promote it to 'killed' in ${spec_file}")
            endif()
        endif()
    endforeach()

    string(JSON t_total ERROR_VARIABLE e GET "${report}" targets ${i} mutants_total)
    string(JSON t_eval ERROR_VARIABLE e GET "${report}" targets ${i} mutants_evaluated)
    string(JSON t_killed ERROR_VARIABLE e GET "${report}" targets ${i} killed)
    string(JSON t_survived ERROR_VARIABLE e GET "${report}" targets ${i} survived)
    math(EXPR t_sum "${t_killed} + ${t_survived}")
    if(NOT t_sum EQUAL t_eval)
        message(FATAL_ERROR
            "target '${tname}': killed(${t_killed}) + survived(${t_survived}) != "
            "evaluated(${t_eval})")
    endif()
    math(EXPR total_mutants "${total_mutants} + ${t_total}")
    math(EXPR total_evaluated "${total_evaluated} + ${t_eval}")
    math(EXPR total_killed "${total_killed} + ${t_killed}")
    math(EXPR total_survived "${total_survived} + ${t_survived}")

    message(STATUS
        "  target ${tname}: evaluated=${t_eval} killed=${t_killed} "
        "survived=${t_survived} score=${t_score} floor=${spec_floor} — all outcomes pinned")
endforeach()

if(NOT total_evaluated EQUAL mutants_evaluated
   OR NOT total_killed EQUAL killed
   OR NOT total_survived EQUAL survived
   OR NOT total_mutants EQUAL mutants_total)
    message(FATAL_ERROR
        "Report totals disagree with the per-target sums: "
        "totals(eval=${mutants_evaluated} killed=${killed} survived=${survived} "
        "mutants=${mutants_total}) vs sums(eval=${total_evaluated} "
        "killed=${total_killed} survived=${total_survived} mutants=${total_mutants})")
endif()

message(STATUS
    "Mutation fallback score report ok: targets=${target_count} "
    "evaluated=${mutants_evaluated} killed=${killed} survived=${survived} "
    "score=${mutation_score} report=${REPORT_FILE}")
