add_library(ahfl_testing STATIC
    ast_generator.cpp
)

target_link_libraries(ahfl_testing
    PUBLIC
        ahfl_support
)

ahfl_apply_project_warnings(ahfl_testing)
