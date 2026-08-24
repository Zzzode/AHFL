# Backend-effect regression for the runtime/execution plan under -O (P2 3.6).
#
# `emit execution-plan` lowers the (optionally optimized) IR program into the
# runtime execution-plan JSON (src/tooling/cli/pipeline_core_commands.cpp ->
# build_execution_plan_for_cli). The CLI runs the semantic optimization
# pipeline on the IR program BEFORE the plan is built (see cli_driver.cpp:
# run_requested_semantic_optimization_pipeline is invoked ahead of
# emit_core_backend), so pass effects that touch workflow structure propagate
# into the plan.
#
# This locks the WorkflowSimplificationPass effect as observed in the runtime
# plan: the fixture's `gamma` node depends on both `alpha` and `beta`, but
# `beta` already depends on `alpha`, so the `alpha -> gamma` edge is
# transitively redundant. Under -O it is dropped, so:
#   * the plan's `dependency_edges` loses the {alpha -> gamma} edge, and
#   * `gamma`'s `after` list shrinks from [alpha, beta] to [beta], and
#   * the optimized plan JSON is strictly smaller.
# All three are verified by running ahflc.

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

function(run_ahflc output_var)
    execute_process(
        COMMAND "${AHFLC}" ${ARGN}
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE command_output
        ERROR_VARIABLE command_error
    )
    if(NOT result_code EQUAL 0)
        message(FATAL_ERROR
            "command failed with exit code ${result_code}: ${ARGN}\n"
            "stderr:\n${command_error}"
        )
    endif()
    set(${output_var} "${command_output}" PARENT_SCOPE)
endfunction()

function(require_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "expected ${description} to contain:\n${needle}")
    endif()
endfunction()

function(require_not_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" needle_position)
    if(NOT needle_position EQUAL -1)
        message(FATAL_ERROR "expected ${description} not to contain:\n${needle}")
    endif()
endfunction()

# Asserts optimized output is strictly smaller than unoptimized, by at least
# `min_delta` bytes, so the assertion fails loudly if the effect regresses.
function(require_smaller before after min_delta description)
    string(LENGTH "${before}" before_bytes)
    string(LENGTH "${after}" after_bytes)
    if(NOT after_bytes LESS before_bytes)
        message(FATAL_ERROR
            "expected optimized ${description} to be smaller: "
            "before=${before_bytes} bytes, after=${after_bytes} bytes"
        )
    endif()
    math(EXPR actual_delta "${before_bytes} - ${after_bytes}")
    if(actual_delta LESS min_delta)
        message(FATAL_ERROR
            "optimized ${description} shrank by only ${actual_delta} bytes, "
            "expected at least ${min_delta} (before=${before_bytes}, after=${after_bytes})"
        )
    endif()
    message(STATUS
        "${description} size delta ok: ${before_bytes} -> ${after_bytes} bytes "
        "(-${actual_delta})"
    )
endfunction()

# ---------------------------------------------------------------------------
# Execution plan: redundant dependency edge is pruned under -O.
# ---------------------------------------------------------------------------
run_ahflc(plan_before emit execution-plan "${INPUT_FILE}")
run_ahflc(plan_after emit execution-plan -O "${INPUT_FILE}")

# The transitively-redundant alpha -> gamma dependency edge is present without
# -O and dropped with -O.
require_contains(
    "${plan_before}"
    "\"from_node\": \"alpha\",\n          \"to_node\": \"gamma\""
    "unoptimized execution plan"
)
require_not_contains(
    "${plan_after}"
    "\"from_node\": \"alpha\",\n          \"to_node\": \"gamma\""
    "optimized execution plan"
)

# The alpha -> beta and beta -> gamma edges (genuine dependencies) survive.
require_contains(
    "${plan_after}"
    "\"from_node\": \"alpha\",\n          \"to_node\": \"beta\""
    "optimized execution plan"
)
require_contains(
    "${plan_after}"
    "\"from_node\": \"beta\",\n          \"to_node\": \"gamma\""
    "optimized execution plan"
)

# gamma's `after` list shrinks from [alpha, beta] to [beta].
require_contains(
    "${plan_before}"
    "\"after\": [\n            \"alpha\",\n            \"beta\"\n          ]"
    "unoptimized execution plan"
)
require_contains(
    "${plan_after}"
    "\"after\": [\n            \"beta\"\n          ]"
    "optimized execution plan"
)
require_not_contains(
    "${plan_after}"
    "\"after\": [\n            \"alpha\",\n            \"beta\"\n          ]"
    "optimized execution plan"
)

require_smaller("${plan_before}" "${plan_after}" 40 "execution plan")
