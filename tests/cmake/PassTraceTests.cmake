# PassTraceTests.cmake -- P2 pass-level trace export backlog item (issue-backlog §3.6).
#
# Validates the machine-readable optimization pass-level trace emitted by
# `ahflc emit ir -O --pass-trace-export <path>`. See
# tests/scripts/pass_trace_gate.py for the schema/structure assertions.
#
# The gate runs the compiler against a golden fixture known to trigger the
# default optimization pipeline under -O, then parses the emitted JSON,
# checks the schema is ahfl.pass_trace.v1, and asserts the recorded passes
# include the transformation passes that ran.
#
# NOTE: this file is included from tests/CMakeLists.txt (the orchestrator
# wires the include() line). It relies on AHFL_TESTS_DIR and
# Python3_EXECUTABLE being set up by the parent list file.

add_test(NAME ahflc.profile.pass_trace_export.emit_ir
    COMMAND ${Python3_EXECUTABLE}
            "${AHFL_TESTS_DIR}/scripts/pass_trace_gate.py"
            $<TARGET_FILE:ahflc>
            "${AHFL_TESTS_DIR}/golden/ir/ok_workflow_simplification.ahfl"
            "${CMAKE_CURRENT_BINARY_DIR}/pass_trace/ok_workflow_simplification.pass_trace.json"
)
set_tests_properties(ahflc.profile.pass_trace_export.emit_ir PROPERTIES
    PASS_REGULAR_EXPRESSION "pass trace OK:"
    FAIL_REGULAR_EXPRESSION "missing expected passes|not valid JSON|polluted"
    LABELS "profile;passes;golden;telemetry"
)
