add_executable(ahflc
    ahflc.cpp
    cli_analysis_helpers.cpp
    cli_driver.cpp
    diagnostic_consumer.cpp
    option_table.cpp
    cli_pipeline_artifacts.cpp
    command_registry.cpp
    command_help.cpp
    pipeline_core_commands.cpp
    provider/provider_pipeline_cache.cpp
    provider/pipeline_durable_store_import.cpp
    provider/pipeline_durable_store_import_provider.cpp
    provider/steps/runtime.cpp
    provider/steps/sdk.cpp
    provider/steps/execution.cpp
    provider/steps/configuration.cpp
    provider/steps/reliability.cpp
    provider/steps/governance.cpp
    provider/steps/production.cpp
    pipeline_runner.cpp
)

target_link_libraries(ahflc
    PRIVATE
        ahfl_compiler
)

target_include_directories(ahflc PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahflc)

add_executable(ahfl-run
    ahfl_run.cpp
    cli_analysis_helpers.cpp
    command_registry.cpp
    command_help.cpp
)

target_link_libraries(ahfl-run
    PRIVATE
        ahfl_compiler
)

target_include_directories(ahfl-run PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-run)
