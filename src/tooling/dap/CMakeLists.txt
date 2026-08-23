add_library(ahfl_tooling_dap STATIC
    dap_server.cpp
    breakpoints.cpp
    state_inspector.cpp
    debug_session.cpp
)

target_link_libraries(ahfl_tooling_dap
    PUBLIC
        ahfl_base_public
        ahfl_compiler_ir
        ahfl_runtime_engine
    PRIVATE
        ahfl_base_json
        ahfl_compiler_syntax
        ahfl_compiler_semantics
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
