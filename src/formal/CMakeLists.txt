add_library(ahfl_formal STATIC
    checker.cpp
)

target_link_libraries(ahfl_formal
    PUBLIC
        ahfl_backend_smv
)

ahfl_apply_project_warnings(ahfl_formal)
