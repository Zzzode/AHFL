add_library(ahfl_dap STATIC
    dap_server.cpp
    breakpoints.cpp
    state_inspector.cpp
)

target_link_libraries(ahfl_dap
    PUBLIC
        ahfl_support
    PRIVATE
        ahfl_json
)

target_include_directories(ahfl_dap PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_dap)
