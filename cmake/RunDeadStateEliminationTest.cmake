# Backend-effect regression for DeadStateEliminationPass (P2 3.6).
#
# WHAT THIS LOCKS, AND WHY IT IS SHAPED THIS WAY
# ----------------------------------------------
# DeadStateEliminationPass (src/compiler/passes/dead_state_elimination.cpp) runs
# a BFS from an agent's `initial` state over its transition graph and drops any
# state that is unreachable. In isolation it is a real, size-reducing transform.
#
# However, it is UNREACHABLE through the CLI on any well-formed program: the
# semantic validator (src/compiler/semantics/validate.cpp) performs the exact
# same reachability analysis and rejects an unreachable agent state with a hard
# `validation.INVALID_STATE` error BEFORE IR lowering. So no program that passes
# `check` can carry a dead state into the pass pipeline, and the dead-state
# effect cannot be locked as a before/after `emit ir` diff the way expr /
# temporal / workflow simplification are (see RunWorkflowSimplificationTest.cmake
# for the note this test makes executable).
#
# Rather than fabricate a diff that does not exist, this script LOCKS THE GATE
# that makes the pass unreachable: the fixture declares a genuinely unreachable
# state `Orphan`, and we assert that BOTH `emit ir` and `emit ir -O` reject it
# identically with `validation.INVALID_STATE`, naming `Orphan`. If a future
# change ever lets an unreachable state survive validation into IR lowering (the
# only situation in which the pass could fire), this assertion breaks loudly and
# forces the dead-state effect to be re-locked as a genuine before/after diff.

if(NOT DEFINED AHFLC)
    message(FATAL_ERROR "AHFLC is required")
endif()

if(NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

# Runs ahflc expecting a NON-zero exit, capturing combined stdout+stderr into
# `output_var`. Fails if the command unexpectedly succeeds.
function(run_ahflc_expect_failure output_var)
    execute_process(
        COMMAND "${AHFLC}" ${ARGN}
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE command_output
        ERROR_VARIABLE command_error
    )
    if(result_code EQUAL 0)
        message(FATAL_ERROR
            "expected command to fail but it succeeded: ${ARGN}\n"
            "stdout:\n${command_output}"
        )
    endif()
    set(${output_var} "${command_output}${command_error}" PARENT_SCOPE)
endfunction()

function(require_contains haystack needle description)
    string(FIND "${haystack}" "${needle}" needle_position)
    if(needle_position EQUAL -1)
        message(FATAL_ERROR "expected ${description} to contain:\n${needle}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The validator gate rejects the unreachable state identically with and without
# optimization. This is the property that makes DeadStateEliminationPass
# unreachable via the CLI; locking it guards the invariant documented above.
# ---------------------------------------------------------------------------
run_ahflc_expect_failure(ir_unopt emit ir "${INPUT_FILE}")
run_ahflc_expect_failure(ir_opt emit ir -O "${INPUT_FILE}")

require_contains(
    "${ir_unopt}"
    "state 'Orphan' is unreachable from initial state 'Init'"
    "unoptimized emit ir diagnostics"
)
require_contains(
    "${ir_unopt}"
    "validation.INVALID_STATE"
    "unoptimized emit ir diagnostics"
)
require_contains(
    "${ir_opt}"
    "state 'Orphan' is unreachable from initial state 'Init'"
    "optimized emit ir diagnostics"
)
require_contains(
    "${ir_opt}"
    "validation.INVALID_STATE"
    "optimized emit ir diagnostics"
)

message(STATUS
    "dead-state validator gate locked: 'Orphan' rejected as validation.INVALID_STATE "
    "with and without -O (DeadStateEliminationPass is unreachable via the CLI)"
)
