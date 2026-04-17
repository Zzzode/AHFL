add_library(ahfl_backend_smv STATIC
    smv.cpp
)

target_link_libraries(ahfl_backend_smv
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_smv)

add_library(ahfl_backend_summary STATIC
    summary.cpp
)

target_link_libraries(ahfl_backend_summary
    PUBLIC
        ahfl_ir
)

ahfl_apply_project_warnings(ahfl_backend_summary)

add_library(ahfl_backend_native_json STATIC
    native_json.cpp
)

target_link_libraries(ahfl_backend_native_json
    PUBLIC
        ahfl_handoff
)

ahfl_apply_project_warnings(ahfl_backend_native_json)

add_library(ahfl_backend STATIC
    driver.cpp
)

target_link_libraries(ahfl_backend
    PUBLIC
        ahfl_ir
        ahfl_backend_native_json
        ahfl_backend_summary
        ahfl_backend_smv
)

ahfl_apply_project_warnings(ahfl_backend)
