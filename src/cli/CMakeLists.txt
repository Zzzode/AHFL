add_executable(ahflc
    ahflc.cpp
    cli_driver.cpp
    diagnostic_consumer.cpp
    option_table.cpp
    cli_pipeline_artifacts.cpp
    command_catalog.cpp
    pipeline_core_commands.cpp
    provider_pipeline_cache.cpp
    pipeline_durable_store_import.cpp
    pipeline_durable_store_import_provider.cpp
    provider_steps/runtime.cpp
    provider_steps/sdk.cpp
    provider_steps/execution.cpp
    provider_steps/configuration.cpp
    provider_steps/reliability.cpp
    provider_steps/governance.cpp
    provider_steps/production.cpp
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
)

target_link_libraries(ahfl-run
    PRIVATE
        ahfl_compiler
)

target_include_directories(ahfl-run PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-run)
