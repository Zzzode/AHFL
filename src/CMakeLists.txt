add_library(ahfl_support INTERFACE)
target_include_directories(ahfl_support
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
ahfl_enable_cxx23(ahfl_support)

add_subdirectory(support)
add_subdirectory(json)
add_subdirectory(parser)
add_subdirectory(frontend)
add_subdirectory(semantics)
add_subdirectory(ir)
add_subdirectory(assurance)
add_subdirectory(handoff)
add_library(ahfl_execution_pipeline STATIC
    dry_run/mock.cpp
    dry_run/runner.cpp
    runtime_session/session.cpp
    execution_journal/journal.cpp
    replay_view/replay.cpp
)
target_link_libraries(ahfl_execution_pipeline PUBLIC ahfl_handoff)
target_include_directories(ahfl_execution_pipeline PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_execution_pipeline)

add_library(ahfl_observation STATIC
    audit_report/report.cpp
    scheduler_snapshot/snapshot.cpp
    scheduler_snapshot/review.cpp
    checkpoint_record/record.cpp
    checkpoint_record/review.cpp
)
target_link_libraries(ahfl_observation PUBLIC ahfl_execution_pipeline)
target_include_directories(ahfl_observation PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_observation)

add_library(ahfl_persistence STATIC
    persistence_descriptor/descriptor.cpp
    persistence_descriptor/review.cpp
    persistence_export/manifest.cpp
    persistence_export/review.cpp
    store_import/descriptor.cpp
    store_import/review.cpp
)
target_link_libraries(ahfl_persistence PUBLIC ahfl_observation)
target_include_directories(ahfl_persistence PRIVATE ${PROJECT_SOURCE_DIR}/src)
ahfl_apply_project_warnings(ahfl_persistence)

add_subdirectory(durable_store_import)
add_subdirectory(evaluator)
add_subdirectory(runtime)
add_subdirectory(llm_provider)
add_subdirectory(backends)
add_subdirectory(formal)
add_subdirectory(secret)
add_subdirectory(passes)
add_subdirectory(lsp)
add_subdirectory(formatter)
add_subdirectory(repl)
add_subdirectory(dap)
add_subdirectory(telemetry)
add_subdirectory(profiling)
add_subdirectory(abi)
add_subdirectory(incremental)
add_subdirectory(testing)
add_subdirectory(package)

add_library(ahfl_compiler_core INTERFACE)
target_link_libraries(ahfl_compiler_core
    INTERFACE
        ahfl_support_lib
        ahfl_syntax
        ahfl_semantics
        ahfl_ir
        ahfl_assurance
        ahfl_backend
        ahfl_json
        ahfl_passes
)

add_library(ahfl_runtime_stack INTERFACE)
target_link_libraries(ahfl_runtime_stack
    INTERFACE
        ahfl_handoff
        ahfl_execution_pipeline
        ahfl_evaluator
        ahfl_runtime
        ahfl_observation
        ahfl_persistence
        ahfl_durable_store_import
        ahfl_formal
        ahfl_llm_provider
        ahfl_secret
)

add_library(ahfl_tooling_stack INTERFACE)
target_link_libraries(ahfl_tooling_stack
    INTERFACE
        ahfl_lsp
        ahfl_formatter
        ahfl_repl
        ahfl_dap
        ahfl_telemetry
        ahfl_profiling
        ahfl_abi
        ahfl_incremental
        ahfl_testing
        ahfl_package
)

add_library(ahfl_compiler INTERFACE)
target_link_libraries(ahfl_compiler
    INTERFACE
        ahfl_compiler_core
        ahfl_runtime_stack
        ahfl_tooling_stack
)

add_library(ahfl_frontend INTERFACE)
target_link_libraries(ahfl_frontend
    INTERFACE
        ahfl_syntax
        ahfl_frontend_lib
)

add_subdirectory(cli)
