add_library(ahfl_compiler_assurance STATIC
    assurance.cpp
)

target_link_libraries(ahfl_compiler_assurance
    PUBLIC
        ahfl_compiler_ir
)

target_include_directories(ahfl_compiler_assurance PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_assurance)
