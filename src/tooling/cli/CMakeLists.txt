add_library(ahfl_cli_command_catalog STATIC
    command_registry.cpp
    command_help.cpp
    provider/provider_artifact_catalog.cpp
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
    workflow_run.cpp
)

target_link_libraries(ahflc
    PRIVATE
        ahfl_compiler
        ahfl_runtime
        ahfl_tooling_package
        ahfl_cli_command_catalog
)

target_include_directories(ahflc PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahflc)
