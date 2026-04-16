cmake_minimum_required(VERSION 3.22)

project(AHFL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(
    AHFL_EXPORT_COMPILE_COMMANDS
    CACHE CMAKE_EXPORT_COMPILE_COMMANDS
    PROPERTY VALUE
)

if(AHFL_EXPORT_COMPILE_COMMANDS)
    set(AHFL_ROOT_COMPILE_COMMANDS "${CMAKE_SOURCE_DIR}/compile_commands.json")

    if(IS_SYMLINK "${AHFL_ROOT_COMPILE_COMMANDS}")
        file(REMOVE "${AHFL_ROOT_COMPILE_COMMANDS}")
    endif()

    if(NOT EXISTS "${AHFL_ROOT_COMPILE_COMMANDS}")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E create_symlink
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${AHFL_ROOT_COMPILE_COMMANDS}"
            RESULT_VARIABLE AHFL_CREATE_COMPILE_COMMANDS_LINK_RESULT
            ERROR_VARIABLE AHFL_CREATE_COMPILE_COMMANDS_LINK_ERROR
        )

        if(NOT AHFL_CREATE_COMPILE_COMMANDS_LINK_RESULT EQUAL 0)
            message(WARNING
                "failed to create root compile_commands.json symlink: "
                "${AHFL_CREATE_COMPILE_COMMANDS_LINK_ERROR}"
            )
        endif()
    else()
        message(STATUS
            "keeping existing ${AHFL_ROOT_COMPILE_COMMANDS}; not replacing regular file"
        )
    endif()
endif()

find_package(Threads REQUIRED)
find_program(CLANG_FORMAT_BIN NAMES clang-format clang-format-21 clang-format-20)

file(GLOB_RECURSE ANTLR4_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/antlr4/runtime/src/*.cpp"
)

add_library(antlr4_runtime STATIC
    ${ANTLR4_RUNTIME_SOURCES}
)

target_include_directories(antlr4_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/antlr4/runtime/src
)

target_compile_features(antlr4_runtime PUBLIC cxx_std_23)
target_compile_definitions(antlr4_runtime PUBLIC ANTLR4CPP_STATIC)
target_link_libraries(antlr4_runtime PUBLIC Threads::Threads)

if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    if(COREFOUNDATION_LIBRARY)
        target_link_libraries(antlr4_runtime PUBLIC ${COREFOUNDATION_LIBRARY})
    endif()
endif()

if(MSVC)
    target_compile_options(antlr4_runtime PRIVATE /W0)
else()
    target_compile_options(antlr4_runtime PRIVATE
        -Wno-overloaded-virtual
        -Wno-dollar-in-identifier-extension
        -Wno-four-char-constants
    )
endif()

add_library(ahfl_generated_parser STATIC
    src/parser/generated/AHFLBaseVisitor.cpp
    src/parser/generated/AHFLLexer.cpp
    src/parser/generated/AHFLParser.cpp
    src/parser/generated/AHFLVisitor.cpp
)

target_include_directories(ahfl_generated_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/generated
)

target_compile_features(ahfl_generated_parser PUBLIC cxx_std_23)
target_link_libraries(ahfl_generated_parser PUBLIC antlr4_runtime)

add_library(ahfl_frontend STATIC
    src/ast.cpp
    src/frontend.cpp
    src/ir.cpp
    src/ir_json.cpp
    src/resolver.cpp
    src/typecheck.cpp
    src/validate.cpp
)

target_include_directories(ahfl_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ahfl_frontend PUBLIC cxx_std_23)
target_link_libraries(ahfl_frontend PUBLIC ahfl_generated_parser)

if(MSVC)
    target_compile_options(ahfl_frontend PRIVATE /W4 /permissive-)
else()
    target_compile_options(ahfl_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ahflc
    src/tools/ahflc.cpp
)

target_link_libraries(ahflc PRIVATE ahfl_frontend)

enable_testing()

function(add_ahflc_check_test name source_file)
    add_test(NAME ${name}
        COMMAND $<TARGET_FILE:ahflc> check "${source_file}"
    )
endfunction()

function(add_ahflc_check_fail_test name source_file expected_pattern)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DAHFLC=$<TARGET_FILE:ahflc>
            -DINPUT_FILE=${source_file}
            -DEXPECTED_REGEX=${expected_pattern}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExpectedFailure.cmake"
    )
endfunction()

function(add_ahflc_output_test name subcommand source_file expected_file)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DAHFLC=$<TARGET_FILE:ahflc>
            -DSUBCOMMAND=${subcommand}
            -DINPUT_FILE=${source_file}
            -DEXPECTED_FILE=${expected_file}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExpectedOutput.cmake"
    )
endfunction()

add_ahflc_check_test(
    ahflc.check.example
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/refund_audit_core_v0_1.ahfl"
)

add_test(NAME ahflc.dump_ast.example
    COMMAND $<TARGET_FILE:ahflc> dump-ast
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_ast.example PROPERTIES
    PASS_REGULAR_EXPRESSION "program .*/examples/refund_audit_core_v0_1\\.ahfl"
)

add_test(NAME ahflc.dump_types.example
    COMMAND $<TARGET_FILE:ahflc> dump-types
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/refund_audit_core_v0_1.ahfl"
)
set_tests_properties(ahflc.dump_types.example PROPERTIES
    PASS_REGULAR_EXPRESSION "workflow refund::audit::RefundAuditWorkflow"
)

add_ahflc_output_test(
    ahflc.emit_ir.example
    emit-ir
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/refund_audit_core_v0_1.ahfl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/refund_audit_core_v0_1.ir"
)

add_ahflc_output_test(
    ahflc.emit_ir.alias_const
    emit-ir
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_alias_const.ahfl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_alias_const.ir"
)

add_ahflc_output_test(
    ahflc.emit_ir.expr_temporal
    emit-ir
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_expr_temporal.ahfl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_expr_temporal.ir"
)

add_ahflc_output_test(
    ahflc.emit_ir_json.expr_temporal
    emit-ir-json
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_expr_temporal.ahfl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ir/ok_expr_temporal.json"
)

add_ahflc_check_test(
    ahflc.check.import_alias
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/ok_import_alias_self.ahfl"
)

add_test(NAME ahflc.dump_types.alias_schema
    COMMAND $<TARGET_FILE:ahflc> dump-types
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/ok_agent_schema_alias.ahfl"
)
set_tests_properties(ahflc.dump_types.alias_schema PROPERTIES
    PASS_REGULAR_EXPRESSION "input: types::alias::Request"
)

add_ahflc_check_fail_test(
    ahflc.fail.duplicate_type
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/duplicate_top_level_type.ahfl"
    "duplicate type"
)

add_ahflc_check_fail_test(
    ahflc.fail.unknown_references
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/unknown_references.ahfl"
    "unknown type"
)

add_ahflc_check_fail_test(
    ahflc.fail.type_alias_cycle
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/type_alias_cycle.ahfl"
    "type alias cycle detected"
)

add_ahflc_check_fail_test(
    ahflc.fail.duplicate_import_alias
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/duplicate_import_alias.ahfl"
    "duplicate import alias"
)

add_ahflc_check_fail_test(
    ahflc.fail.ambiguous_callable
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/resolver/ambiguous_callable.ahfl"
    "ambiguous callable"
)

add_ahflc_check_fail_test(
    ahflc.fail.agent_schema_not_struct
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/agent_schema_not_struct.ahfl"
    "agent input type must resolve to a struct type"
)

add_ahflc_check_fail_test(
    ahflc.fail.if_condition_not_bool
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/if_condition_not_bool.ahfl"
    "if condition must have type Bool"
)

add_ahflc_check_fail_test(
    ahflc.fail.assert_condition_not_bool
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/assert_condition_not_bool.ahfl"
    "assert condition must have type Bool"
)

add_ahflc_check_fail_test(
    ahflc.fail.contract_capability_call
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/contract_capability_call.ahfl"
    "capability call 'Validate' is not allowed in this context"
)

add_ahflc_check_fail_test(
    ahflc.fail.contract_invariant_unknown_state
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/contract_invariant_unknown_state.ahfl"
    "unknown state 'Missing'"
)

add_ahflc_check_fail_test(
    ahflc.fail.contract_invariant_running_not_allowed
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/contract_invariant_running_not_allowed.ahfl"
    "running\\(\\.\\.\\.\\) is only valid in workflow safety/liveness formulas"
)

add_ahflc_check_fail_test(
    ahflc.fail.contract_invariant_output_not_allowed
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/contract_invariant_output_not_allowed.ahfl"
    "unknown value 'output'"
)

add_ahflc_check_fail_test(
    ahflc.fail.agent_unreachable_state
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/agent_unreachable_state.ahfl"
    "state 'Dead' is unreachable"
)

add_ahflc_check_fail_test(
    ahflc.fail.flow_missing_handler
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/flow_missing_handler.ahfl"
    "missing non-final-state handler for 'Middle'"
)

add_ahflc_check_fail_test(
    ahflc.fail.flow_illegal_goto
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/flow_illegal_goto.ahfl"
    "illegal goto from state 'Init' to 'Init'"
)

add_ahflc_check_fail_test(
    ahflc.fail.flow_return_in_non_final
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/flow_return_in_non_final.ahfl"
    "return is only allowed in final state handlers"
)

add_ahflc_check_fail_test(
    ahflc.fail.workflow_after_missing
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/workflow_after_missing.ahfl"
    "unknown workflow dependency 'missing'"
)

add_ahflc_check_fail_test(
    ahflc.fail.workflow_dependency_cycle
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/workflow_dependency_cycle.ahfl"
    "workflow dependency cycle detected"
)

add_ahflc_check_fail_test(
    ahflc.fail.workflow_dependency_not_visible
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/workflow_dependency_not_visible.ahfl"
    "unknown value 'first'"
)

add_ahflc_check_fail_test(
    ahflc.fail.workflow_completed_invalid_state
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/workflow_completed_invalid_state.ahfl"
    "state 'Init' is not a final state of node 'run'"
)

add_ahflc_check_fail_test(
    ahflc.fail.workflow_node_input_mismatch
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/typecheck/workflow_node_input_mismatch.ahfl"
    "type mismatch in workflow node input"
)

if(CLANG_FORMAT_BIN)
    file(GLOB_RECURSE AHFL_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    )

    list(FILTER AHFL_FORMAT_SOURCES EXCLUDE REGEX ".*/src/parser/generated/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_BIN} -i --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting handwritten AHFL C++ sources with clang-format"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking handwritten AHFL C++ source formatting"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; format targets are disabled")
endif()
