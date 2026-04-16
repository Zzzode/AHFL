add_library(ahfl_backend_smv STATIC
    smv.cpp
)

target_link_libraries(ahfl_backend_smv
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_smv)

add_library(ahfl_backend STATIC
    driver.cpp
)

target_link_libraries(ahfl_backend
    PUBLIC
        ahfl_ir
        ahfl_backend_smv
)

ahfl_apply_project_warnings(ahfl_backend)
