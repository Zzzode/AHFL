add_library(ahfl_profiling STATIC
    pass_timing.cpp
    memory_tracker.cpp
    hotspot.cpp
)

target_link_libraries(ahfl_profiling
    PUBLIC
        ahfl_support
)

target_include_directories(ahfl_profiling PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_profiling)
