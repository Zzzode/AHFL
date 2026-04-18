add_library(ahfl_runtime_session STATIC
    session.cpp
)

target_link_libraries(ahfl_runtime_session
    PUBLIC
        ahfl_dry_run
)

ahfl_apply_project_warnings(ahfl_runtime_session)
