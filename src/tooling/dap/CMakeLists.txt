add_library(ahfl_tooling_dap STATIC
    dap_server.cpp
    breakpoints.cpp
    state_inspector.cpp
)

target_link_libraries(ahfl_tooling_dap
    PUBLIC
        ahfl_base_public
    PRIVATE
        ahfl_base_json
)

target_include_directories(ahfl_tooling_dap PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_dap)

add_executable(ahfl-dap main.cpp)
target_link_libraries(ahfl-dap
    PRIVATE
        ahfl_tooling_dap
)
target_include_directories(ahfl-dap PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-dap)
