add_library(ahfl_assurance STATIC
    assurance.cpp
)

target_link_libraries(ahfl_assurance
    PUBLIC
        ahfl_ir
)

target_include_directories(ahfl_assurance PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_assurance)
