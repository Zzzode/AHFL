add_library(ahfl_testing STATIC
    ast_generator.cpp
)

target_link_libraries(ahfl_testing
    PUBLIC
        ahfl_support
)

target_include_directories(ahfl_testing PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_testing)
