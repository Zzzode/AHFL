add_library(ahfl_support INTERFACE)
target_include_directories(ahfl_support
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)
ahfl_enable_cxx23(ahfl_support)

add_subdirectory(parser)
add_subdirectory(frontend)
add_subdirectory(semantics)
add_subdirectory(ir)
add_subdirectory(handoff)
add_subdirectory(dry_run)
add_subdirectory(runtime_session)
add_subdirectory(execution_journal)
add_subdirectory(replay_view)
add_subdirectory(audit_report)
add_subdirectory(scheduler_snapshot)
add_subdirectory(checkpoint_record)
add_subdirectory(persistence_descriptor)
add_subdirectory(persistence_export)
add_subdirectory(store_import)
add_subdirectory(durable_store_import)
add_subdirectory(evaluator)
add_subdirectory(runtime)
add_subdirectory(backends)

add_library(ahfl_compiler INTERFACE)
target_link_libraries(ahfl_compiler
    INTERFACE
        ahfl_handoff
        ahfl_dry_run
        ahfl_evaluator
        ahfl_runtime
        ahfl_runtime_session
        ahfl_execution_journal
        ahfl_replay_view
        ahfl_audit_report
        ahfl_scheduler_snapshot
        ahfl_checkpoint_record
        ahfl_persistence_descriptor
        ahfl_persistence_export
        ahfl_store_import
        ahfl_durable_store_import
        ahfl_backend
)

add_library(ahfl_frontend INTERFACE)
target_link_libraries(ahfl_frontend
    INTERFACE
        ahfl_compiler
)

add_subdirectory(cli)
