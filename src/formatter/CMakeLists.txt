add_library(ahfl_formatter STATIC
    formatter.cpp
    format_config.cpp
)

target_link_libraries(ahfl_formatter
    PUBLIC
        ahfl_syntax
)

ahfl_apply_project_warnings(ahfl_formatter)
