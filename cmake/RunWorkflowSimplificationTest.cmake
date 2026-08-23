# Backend-effect regression for the workflow-simplification optimization pass
# (P2 §3.6). Complements RunPassProductizationTest.cmake, which locks expr
# canonicalization and temporal collapse. This script proves that
# WorkflowSimplificationPass produces a reviewable, size-reducing change:
#
#   * IR: a transitively-redundant `after` dependency edge is removed
#     (`after [first, second]` -> `after [second]`), and the optimized IR is
#     strictly smaller in bytes.
#   * SMV: the workflow-ordering LTLSPEC drops the implied `first__completed`
#     conjunct, and the optimized SMV is strictly smaller in bytes.
#
# The fixture also carries the expr/temporal constructs so the shared expr and
# temporal simplifications are re-locked here on an independent input.
#
# Note on dead-state elimination: the semantic validator rejects unreachable
# agent states as a hard `validation.INVALID_STATE` error before IR lowering,
# so DeadStateEliminationPass cannot fire on any program that passes `check`.
# It is therefore not regression-lockable through `emit ir`; see the P2 §3.6
# report for details.

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

# Asserts optimized output is strictly smaller than unoptimized, and by at
# least `min_delta` bytes so the assertion fails loudly if the pass regresses
# into a no-op.
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
# IR: after-edge reduction + expr/temporal simplification
# ---------------------------------------------------------------------------
run_ahflc(ir_before emit ir "${INPUT_FILE}")
run_ahflc(ir_after emit ir -O "${INPUT_FILE}")

# Workflow simplification: `third` depends on both `first` and `second`, but
# `second` already depends on `first`, so the `first` edge on `third` is
# transitively implied and must be dropped.
require_contains(
    "${ir_before}"
    "node third: ir::workflow_simplification::StageAgent(input) after [first, second]"
    "unoptimized IR"
)
require_contains(
    "${ir_after}"
    "node third: ir::workflow_simplification::StageAgent(input) after [second]"
    "optimized IR"
)
require_not_contains(
    "${ir_after}"
    "node third: ir::workflow_simplification::StageAgent(input) after [first, second]"
    "optimized IR"
)

# Expr canonicalization: `(true && ...)` collapses to `...`.
require_contains(
    "${ir_before}"
    "requires: (true && ir::workflow_simplification::ready(input.value))"
    "unoptimized IR"
)
require_contains(
    "${ir_after}"
    "requires: ir::workflow_simplification::ready(input.value)"
    "optimized IR"
)
require_not_contains(
    "${ir_after}"
    "requires: (true && ir::workflow_simplification::ready(input.value))"
    "optimized IR"
)

# Temporal simplification: `G(G(...))` collapses to `G(...)`.
require_contains(
    "${ir_before}"
    "safety: G(G((!(running(third)) || completed(third))))"
    "unoptimized IR"
)
require_contains(
    "${ir_after}"
    "safety: G((!(running(third)) || completed(third)))"
    "optimized IR"
)

require_smaller("${ir_before}" "${ir_after}" 20 "IR")

# ---------------------------------------------------------------------------
# SMV: ordering-LTLSPEC pruning + temporal collapse
# ---------------------------------------------------------------------------
run_ahflc(smv_before emit smv "${INPUT_FILE}")
run_ahflc(smv_after emit smv -O "${INPUT_FILE}")

# The ordering constraint for `third` requires all its dependencies complete.
# After edge reduction the redundant `first__completed` conjunct disappears.
require_contains(
    "${smv_before}"
    "(workflow__ir_workflow_simplification_StageWorkflow__node__third__running | workflow__ir_workflow_simplification_StageWorkflow__node__third__completed) -> (workflow__ir_workflow_simplification_StageWorkflow__node__first__completed & workflow__ir_workflow_simplification_StageWorkflow__node__second__completed)"
    "unoptimized SMV"
)
require_contains(
    "${smv_after}"
    "(workflow__ir_workflow_simplification_StageWorkflow__node__third__running | workflow__ir_workflow_simplification_StageWorkflow__node__third__completed) -> (workflow__ir_workflow_simplification_StageWorkflow__node__second__completed)"
    "optimized SMV"
)
require_not_contains(
    "${smv_after}"
    "(workflow__ir_workflow_simplification_StageWorkflow__node__third__running | workflow__ir_workflow_simplification_StageWorkflow__node__third__completed) -> (workflow__ir_workflow_simplification_StageWorkflow__node__first__completed & workflow__ir_workflow_simplification_StageWorkflow__node__second__completed)"
    "optimized SMV"
)

# Temporal collapse on the agent-called invariant.
require_contains(
    "${smv_before}"
    "LTLSPEC G (G (agent__ir_workflow_simplification_StageAgent__called__ir_workflow_simplification_Decide))"
    "unoptimized SMV"
)
require_contains(
    "${smv_after}"
    "LTLSPEC G (agent__ir_workflow_simplification_StageAgent__called__ir_workflow_simplification_Decide)"
    "optimized SMV"
)
require_not_contains(
    "${smv_after}"
    "LTLSPEC G (G (agent__ir_workflow_simplification_StageAgent__called__ir_workflow_simplification_Decide))"
    "optimized SMV"
)

require_smaller("${smv_before}" "${smv_after}" 40 "SMV")
