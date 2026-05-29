add_library(ahfl_tooling_telemetry STATIC
    trace.cpp
    metrics.cpp
    logging.cpp
    context.cpp
)

target_link_libraries(ahfl_tooling_telemetry
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_tooling_telemetry PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_telemetry)
