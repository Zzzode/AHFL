add_library(ahfl_telemetry STATIC
    trace.cpp
    metrics.cpp
    logging.cpp
    context.cpp
)

target_link_libraries(ahfl_telemetry
    PUBLIC
        ahfl_support
)

target_include_directories(ahfl_telemetry PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_telemetry)
