add_library(ahfl_tooling_testing STATIC
    ast_generator.cpp
)

target_link_libraries(ahfl_tooling_testing
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_tooling_testing PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_testing)
