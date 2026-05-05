add_executable(ahflc
    ahflc.cpp
    cli_pipeline_artifacts.cpp
    command_catalog.cpp
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
        ahfl_llm_provider
)

ahfl_apply_project_warnings(ahfl-run)
