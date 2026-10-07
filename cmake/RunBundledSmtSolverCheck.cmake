# Runs `ahflc check` with all solver environment variables unset and PATH
# pointing at a solver-free directory, then asserts the bundled Z3 SMT-BMC
# solver (shipped next to ahflc) was discovered and discharged the contract
# data-predicate goals.
#
# RFC 0028: a default AHFL installation always has a solver; the
# "missing solver" failure mode cannot occur without explicitly deleting the
# bundled binary. The input fixture (ok_bounded_data_semantics.ahfl) carries
# encodable, provable SMT goals: a missing Z3 would fail-closed with
# "SMT-BMC solver (Z3) not found" before any verdict is printed, so observing
# smt_bmc_status: safe/bounded_safe proves the bundled binary was found and ran.

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
    COMMAND "${AHFLC}" check "${INPUT_FILE}"
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

# The SMT-BMC path must have run and proven its goals. A missing Z3 would
# fail-closed with "solver (Z3) not found" and a non-zero exit before here.
if(NOT ahflc_output MATCHES "smt_bmc_status: safe" AND
   NOT ahflc_output MATCHES "smt_bmc_status: bounded_safe")
    message(FATAL_ERROR
        "expected smt_bmc_status: safe or bounded_safe (bundled Z3 proven), "
        "got:\n${ahflc_output}"
    )
endif()

# The fail-closed "solver not found" diagnostic must never appear.
if(ahflc_output MATCHES "SMT-BMC solver \\(Z3\\) not found")
    message(FATAL_ERROR
        "bundled Z3 was not discovered (solver-not-found diagnostic):\n"
        "${ahflc_output}"
    )
endif()

message(STATUS "bundled Z3 SMT-BMC solver discovered and goals proven")
