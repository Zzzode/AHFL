add_library(ahfl_dry_run STATIC
    mock.cpp
    runner.cpp
)

target_link_libraries(ahfl_dry_run
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_dry_run)
