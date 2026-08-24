# CMake -P gate for the self-contained fallback mutation runner.
#
# Invoked by the ahfl.mutation.fallback_score ctest. It runs
# run_fallback_mutation.sh, then validates the emitted JSON score report is
# present and structurally well-formed. It deliberately does NOT assert an
# absolute mutation-score threshold (that would be flaky across compilers);
# it asserts structure + that at least the fixed mutant set was evaluated.
#
# Required -D variables:
#   RUNNER       - absolute path to run_fallback_mutation.sh
#   REPORT_FILE  - path the runner should write its JSON report to
#   MIN_MUTANTS  - minimum number of mutants that must be evaluated (default 4)

cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0054 NEW)  # quoted operands to if() are never dereferenced

if(NOT DEFINED RUNNER)
    message(FATAL_ERROR "RUNNER is required")
endif()
if(NOT DEFINED REPORT_FILE)
    message(FATAL_ERROR "REPORT_FILE is required")
endif()
if(NOT DEFINED MIN_MUTANTS)
    set(MIN_MUTANTS 4)
endif()

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
if(e OR NOT schema STREQUAL "ahfl.mutation.fallback.v1")
    message(FATAL_ERROR "Report schema mismatch: '${schema}' (err='${e}')")
endif()

string(JSON status ERROR_VARIABLE e GET "${report}" status)
if(e)
    message(FATAL_ERROR "Report missing 'status' (err='${e}')")
endif()

# The gate accepts either a real 'ok' run or the honest 'tool_unavailable'
# contract (e.g. no C++ compiler). It never accepts a malformed report.
if(status STREQUAL "tool_unavailable")
    string(JSON reason ERROR_VARIABLE e GET "${report}" reason)
    if(e OR reason STREQUAL "")
        message(FATAL_ERROR "tool_unavailable report must carry a reason")
    endif()
    string(JSON score ERROR_VARIABLE e GET "${report}" mutation_score)
    if(NOT score STREQUAL "null")
        message(FATAL_ERROR "tool_unavailable must report null score, got '${score}'")
    endif()
    message(STATUS "Mutation fallback score report ok: status=tool_unavailable reason=${reason}")
    return()
endif()

if(NOT status STREQUAL "ok")
    message(FATAL_ERROR "Unexpected report status: '${status}'")
endif()

# status == ok: validate the numeric fields and mutant array.
string(JSON evaluated ERROR_VARIABLE e GET "${report}" mutants_evaluated)
if(e)
    message(FATAL_ERROR "Report missing 'mutants_evaluated' (err='${e}')")
endif()
string(JSON killed ERROR_VARIABLE e GET "${report}" killed)
if(e)
    message(FATAL_ERROR "Report missing 'killed' (err='${e}')")
endif()
string(JSON survived ERROR_VARIABLE e GET "${report}" survived)
if(e)
    message(FATAL_ERROR "Report missing 'survived' (err='${e}')")
endif()
string(JSON score ERROR_VARIABLE e GET "${report}" mutation_score)
if(e)
    message(FATAL_ERROR "Report missing 'mutation_score' (err='${e}')")
endif()

if(evaluated LESS MIN_MUTANTS)
    message(FATAL_ERROR
        "Too few mutants evaluated: ${evaluated} < ${MIN_MUTANTS}")
endif()

math(EXPR sum "${killed} + ${survived}")
if(NOT sum EQUAL evaluated)
    message(FATAL_ERROR
        "killed(${killed}) + survived(${survived}) != evaluated(${evaluated})")
endif()

string(JSON mutant_count ERROR_VARIABLE e LENGTH "${report}" mutants)
if(e OR mutant_count LESS MIN_MUTANTS)
    message(FATAL_ERROR
        "mutants array too short: ${mutant_count} < ${MIN_MUTANTS} (err='${e}')")
endif()

# Each mutant entry must carry id + outcome in {killed, survived}.
math(EXPR last "${mutant_count} - 1")
foreach(i RANGE ${last})
    string(JSON mid ERROR_VARIABLE e GET "${report}" mutants ${i} id)
    if(e OR mid STREQUAL "")
        message(FATAL_ERROR "mutant[${i}] missing id (err='${e}')")
    endif()
    string(JSON outcome ERROR_VARIABLE e GET "${report}" mutants ${i} outcome)
    if(e OR (NOT outcome STREQUAL "killed" AND NOT outcome STREQUAL "survived"))
        message(FATAL_ERROR "mutant[${i}] '${mid}' bad outcome '${outcome}'")
    endif()
endforeach()

message(STATUS
    "Mutation fallback score report ok: evaluated=${evaluated} "
    "killed=${killed} survived=${survived} score=${score} report=${REPORT_FILE}")
