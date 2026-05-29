add_library(ahfl_tooling_profiling STATIC
    pass_timing.cpp
    memory_tracker.cpp
    hotspot.cpp
)

target_link_libraries(ahfl_tooling_profiling
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_tooling_profiling PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_profiling)
