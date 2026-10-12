# Runs `ahflc check --verify-data-contracts` with all solver environment variables unset and PATH
# pointing at a solver-free directory, then asserts the bundled Z3 SMT-BMC
# solver (shipped next to ahflc) was discovered and discharged the contract
# data-predicate goals.
#
# The fixture has provable SMT contract implications. Data verification is an
# explicit request; the default check command does not launch this solver.

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

# Strip every solver-discovery env var so the only path to a solver is the
# executable-directory lookup.
foreach(env_name IN ITEMS AHFL_SMV_CHECKER AHFL_NUXMV_PATH AHFL_NUSMV_PATH AHFL_Z3_PATH)
    unset(ENV{${env_name}})
endforeach()

# PATH with no solver binaries.
if(DEFINED EMPTY_PATH_DIR)
    set(ENV{PATH} "${EMPTY_PATH_DIR}")
endif()

execute_process(
    COMMAND "${AHFLC}" check --verify-data-contracts "${INPUT_FILE}"
    RESULT_VARIABLE ahflc_result
    OUTPUT_VARIABLE ahflc_stdout
    ERROR_VARIABLE ahflc_stderr
)

string(CONCAT ahflc_output "${ahflc_stdout}" "${ahflc_stderr}")

if(NOT ahflc_result EQUAL 0)
    message(FATAL_ERROR
        "expected ahflc check to succeed with bundled Z3, but it failed "
        "(exit ${ahflc_result})\n${ahflc_output}"
    )
endif()

# The explicit data-contract proof must have completed.
if(NOT ahflc_output MATCHES "data_contract_status: proved" AND
   NOT ahflc_output MATCHES "data_contract_status: bounded")
    message(FATAL_ERROR
        "expected proved or bounded data-contract status (bundled Z3 proven), "
        "got:\n${ahflc_output}"
    )
endif()

if(NOT ahflc_output MATCHES "smt_bmc_scope: contract_implication" OR
   NOT ahflc_output MATCHES "smt_bmc_assumptions_consistent: [1-9][0-9]*")
    message(FATAL_ERROR "expected consistent contract-implication proof scope:\n${ahflc_output}")
endif()

message(STATUS "bundled Z3 SMT-BMC solver discovered and goals proven")
