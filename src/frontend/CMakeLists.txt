add_library(ahfl_syntax STATIC
    ast.cpp
    frontend.cpp
    project.cpp
)

target_link_libraries(ahfl_syntax
    PUBLIC
        ahfl_generated_parser
        ahfl_support
)

target_include_directories(ahfl_syntax PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_syntax)

add_library(ahfl_frontend_lib STATIC
    error_recovery.cpp
)

target_link_libraries(ahfl_frontend_lib
    PUBLIC
        ahfl_support
)

target_include_directories(ahfl_frontend_lib PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_frontend_lib)
