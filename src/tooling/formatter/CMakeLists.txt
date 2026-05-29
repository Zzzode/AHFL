add_library(ahfl_tooling_formatter STATIC
    formatter.cpp
    format_config.cpp
)

target_link_libraries(ahfl_tooling_formatter
    PUBLIC
        ahfl_compiler_syntax
)

target_include_directories(ahfl_tooling_formatter PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_formatter)
