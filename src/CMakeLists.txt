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
add_subdirectory(backends)

add_library(ahfl_compiler INTERFACE)
target_link_libraries(ahfl_compiler
    INTERFACE
        ahfl_handoff
        ahfl_dry_run
        ahfl_runtime_session
        ahfl_execution_journal
        ahfl_backend
)

add_library(ahfl_frontend INTERFACE)
target_link_libraries(ahfl_frontend
    INTERFACE
        ahfl_compiler
)

add_subdirectory(cli)
