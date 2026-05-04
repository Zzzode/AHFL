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
