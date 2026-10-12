# Runs `ahflc check` with all solver environment variables unset and PATH
# pointing at a solver-free directory, then asserts the bundled NuSMV binary
# (shipped next to ahflc) was discovered and verification succeeded.
#
# RFC 0028: a default AHFL installation always has a solver; the
# "missing_binary" failure mode cannot occur without explicitly deleting the
# bundled binary.

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

# Strip every solver-discovery env var so the only path to a checker is the
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
        "expected ahflc check to succeed with bundled solver, but it failed "
        "(exit ${ahflc_result})\n${ahflc_output}"
    )
endif()

if(NOT ahflc_output MATCHES "checker_status: available")
    message(FATAL_ERROR
        "expected checker_status: available (bundled solver), got:\n${ahflc_output}"
    )
endif()

if(NOT ahflc_output MATCHES "control_status: passed" OR
   NOT ahflc_output MATCHES "data_contract_status: not_requested" OR
   ahflc_output MATCHES "smt_bmc_status:|NOT_IN_VERIFIED_SUBSET")
    message(FATAL_ERROR "expected default control proof without requested data verification:\n${ahflc_output}")
endif()

# The checker path must be the binary next to ahflc, not a PATH lookup.
get_filename_component(_ahflc_dir "${AHFLC}" DIRECTORY)
if(NOT ahflc_output MATCHES "checker: ${_ahflc_dir}/NuSMV")
    message(FATAL_ERROR
        "expected checker path '${_ahflc_dir}/NuSMV' (bundled), got:\n${ahflc_output}"
    )
endif()

message(STATUS "bundled solver discovered: ${_ahflc_dir}/NuSMV")
