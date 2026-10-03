add_library(ahfl_cli_command_catalog STATIC
    command_registry.cpp
    command_help.cpp
)

target_link_libraries(ahfl_cli_command_catalog
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_cli_command_catalog PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_cli_command_catalog)

add_executable(ahflc
    ahflc.cpp
    cli_analysis_helpers.cpp
    cli_driver.cpp
    diagnostic_consumer.cpp
    option_table.cpp
    cli_pipeline_artifacts.cpp
    pipeline_core_commands.cpp
    pipeline_runner.cpp
    public_api_artifact.cpp
    workflow_run.cpp
)

target_link_libraries(ahflc
    PRIVATE
        ahfl_compiler
        ahfl_compiler_ir_opt
        ahfl_runtime
        ahfl_tooling_package
        ahfl_tooling_formatter
        ahfl_tooling_telemetry
        ahfl_tooling_profiling
        ahfl_cli_command_catalog
)

# RFC 0026 KR6.8 WH-6 (kr68 §12.7.1/§12.7.3): `ahflc run` is wasm-only. The
# wasm3-backed runtime facade is a peer target that exists only under
# AHFL_ENABLE_BACKEND_WASM; link it (PRIVATE) and propagate the compile
# definition so workflow_run.cpp's single #ifdef gate is active. Under
# WASM=OFF the run verb refuses with an actionable diagnostic; the rest of
# the compiler/verifier CLI stays fully functional (no link breakage).
if(AHFL_ENABLE_BACKEND_WASM)
    target_link_libraries(ahflc PRIVATE ahfl_runtime_wasm_runner)
endif()
target_compile_definitions(ahflc PRIVATE
    $<$<BOOL:${AHFL_ENABLE_BACKEND_WASM}>:AHFL_ENABLE_BACKEND_WASM=1>)

target_include_directories(ahflc PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahflc)
