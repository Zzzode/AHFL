# BackendEffectExtraTests.cmake -- backend-effect regression for the
# dead-state-elimination gate and the runtime/execution-plan under -O
# (issue-backlog Sec 3.6). Complements ahflc.passes.workflow_simplification_*.
#
# Both entries are cmake -P harnesses driven against the built ahflc:
#   * RunDeadStateEliminationTest.cmake locks the validator gate that makes
#     DeadStateEliminationPass unreachable through the CLI (an unreachable
#     agent state is rejected as validation.INVALID_STATE with and without -O).
#   * RunRuntimePlanEffectTest.cmake asserts a real -O effect on the emitted
#     execution plan (a transitive dependency edge is dropped and the plan
#     JSON shrinks).
#
# Included from tests/CMakeLists.txt after SingleFileCliTests.cmake so the
# AHFLC target and AHFL_TESTS_DIR are available.

add_test(NAME ahflc.passes.dead_state_elimination_gate
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_dead_state_elimination.ahfl"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunDeadStateEliminationTest.cmake"
)

add_test(NAME ahflc.passes.runtime_plan_backend_effect
    COMMAND ${CMAKE_COMMAND}
            "-DAHFLC=$<TARGET_FILE:ahflc>"
            "-DINPUT_FILE=${AHFL_TESTS_DIR}/golden/ir/ok_runtime_plan_effect.ahfl"
            -P "${PROJECT_SOURCE_DIR}/cmake/RunRuntimePlanEffectTest.cmake"
)

ahfl_label_tests(
    LABELS ahfl-v0.59 v0.59-passes
    TESTS
        ahflc.passes.dead_state_elimination_gate
        ahflc.passes.runtime_plan_backend_effect
)
