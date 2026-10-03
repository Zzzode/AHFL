add_library(ahfl_tooling_dap STATIC
    dap_server.cpp
    breakpoints.cpp
    debug_session.cpp
)

# WH-8 (kr68 section 12.9.8): the DAP is wasm-only. The wasm3-backed runtime
# facade is a peer target that exists only under AHFL_ENABLE_BACKEND_WASM;
# link it (PUBLIC) and propagate the compile definition so debug_session's
# #ifdef gate is active. Under WASM=OFF the launch verb refuses with a
# byte-pinned diagnostic. The evaluator-backed WorkflowRuntime is no longer
# linked (big-bang cutover, kr68 section 12.9.0).
if(AHFL_ENABLE_BACKEND_WASM)
    target_link_libraries(ahfl_tooling_dap PUBLIC ahfl_runtime_wasm_runner)
endif()
target_compile_definitions(ahfl_tooling_dap PUBLIC
    $<$<BOOL:${AHFL_ENABLE_BACKEND_WASM}>:AHFL_ENABLE_BACKEND_WASM=1>)

target_link_libraries(ahfl_tooling_dap
    PUBLIC
        ahfl_base_public
        ahfl_compiler_ir
        ahfl_runtime_value
    PRIVATE
        ahfl_base_json
        ahfl_compiler_syntax
        ahfl_compiler_semantics
)

target_include_directories(ahfl_tooling_dap PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_dap)

add_executable(ahfl-dap main.cpp)
target_link_libraries(ahfl-dap
    PRIVATE
        ahfl_tooling_dap
)
target_include_directories(ahfl-dap PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-dap)
