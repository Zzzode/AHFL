add_executable(ahflc
    ahflc.cpp
    cli_pipeline_artifacts.cpp
    command_catalog.cpp
    pipeline_core_commands.cpp
    pipeline_durable_store_import.cpp
    pipeline_durable_store_import_provider.cpp
    durable_store_import_provider_pipeline/runtime.cpp
    durable_store_import_provider_pipeline/sdk.cpp
    durable_store_import_provider_pipeline/execution.cpp
    durable_store_import_provider_pipeline/configuration.cpp
    durable_store_import_provider_pipeline/reliability.cpp
    durable_store_import_provider_pipeline/governance.cpp
    durable_store_import_provider_pipeline/production.cpp
    pipeline_runner.cpp
)

target_link_libraries(ahflc
    PRIVATE
        ahfl_compiler
)

ahfl_apply_project_warnings(ahflc)

add_executable(ahfl-run
    ahfl_run.cpp
)

target_link_libraries(ahfl-run
    PRIVATE
        ahfl_compiler
)

ahfl_apply_project_warnings(ahfl-run)
