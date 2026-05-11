add_library(ahfl_assurance STATIC
    assurance.cpp
)

target_link_libraries(ahfl_assurance
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_assurance)
