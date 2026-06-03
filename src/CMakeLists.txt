add_library(ahfl_base_public INTERFACE)
target_include_directories(ahfl_base_public
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
ahfl_enable_cxx23(ahfl_base_public)

add_subdirectory(base/support)
add_subdirectory(base/json)
add_subdirectory(compiler/syntax/parser)
add_subdirectory(compiler/syntax/frontend)
add_subdirectory(compiler/semantics)
add_subdirectory(compiler/ir)
add_subdirectory(compiler/assurance)
add_subdirectory(compiler/handoff)
add_library(ahfl_pipeline_validation INTERFACE)
target_include_directories(ahfl_pipeline_validation INTERFACE ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(pipeline/execution)
add_subdirectory(pipeline/observation)
add_subdirectory(pipeline/persistence)
add_subdirectory(runtime/evaluator)
add_subdirectory(runtime/engine)
add_subdirectory(runtime/providers/llm)
add_subdirectory(compiler/backends)
add_subdirectory(verification/formal)
add_subdirectory(runtime/providers/secret)
add_subdirectory(compiler/passes)
add_subdirectory(tooling/lsp)
add_subdirectory(tooling/formatter)
add_subdirectory(tooling/repl)
add_subdirectory(tooling/dap)
add_subdirectory(tooling/telemetry)
add_subdirectory(tooling/profiling)
add_subdirectory(tooling/abi)
add_subdirectory(tooling/incremental)
add_subdirectory(tooling/testing)
add_subdirectory(tooling/package)

add_library(ahfl_compiler_core INTERFACE)
target_link_libraries(ahfl_compiler_core
    INTERFACE
        ahfl_base_support
        ahfl_compiler_syntax
        ahfl_compiler_semantics
        ahfl_compiler_ir
        ahfl_compiler_assurance
        ahfl_compiler_backends
        ahfl_base_json
        ahfl_compiler_passes
)

add_library(ahfl_runtime_bundle INTERFACE)
target_link_libraries(ahfl_runtime_bundle
    INTERFACE
        ahfl_compiler_handoff
        ahfl_pipeline_execution
        ahfl_runtime_evaluator
        ahfl_runtime_engine
        ahfl_pipeline_observation
        ahfl_pipeline_persistence
        ahfl_pipeline_durable_store_import
        ahfl_verification_formal
        ahfl_runtime_provider_llm
        ahfl_runtime_provider_secret
)

add_library(ahfl_tooling_bundle INTERFACE)
target_link_libraries(ahfl_tooling_bundle
    INTERFACE
        ahfl_tooling_lsp
        ahfl_tooling_formatter
        ahfl_tooling_repl
        ahfl_tooling_dap
        ahfl_tooling_telemetry
        ahfl_tooling_profiling
        ahfl_tooling_abi
        ahfl_tooling_incremental
        ahfl_tooling_testing
        ahfl_tooling_package
)

add_library(ahfl_compiler INTERFACE)
target_link_libraries(ahfl_compiler
    INTERFACE
        ahfl_compiler_core
)

add_library(ahfl_runtime INTERFACE)
target_link_libraries(ahfl_runtime
    INTERFACE
        ahfl_runtime_bundle
)

add_library(ahfl_tooling INTERFACE)
target_link_libraries(ahfl_tooling
    INTERFACE
        ahfl_tooling_bundle
)

add_library(ahfl_compiler_frontend INTERFACE)
target_link_libraries(ahfl_compiler_frontend
    INTERFACE
        ahfl_compiler_syntax
        ahfl_compiler_syntax_recovery
)

add_subdirectory(tooling/cli)
