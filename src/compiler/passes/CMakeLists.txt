add_library(ahfl_compiler_passes STATIC
    pass_manager.cpp
    dead_state_elimination.cpp
    capability_reachability.cpp
    contract_redundancy.cpp
    workflow_simplification.cpp
    expr_canonicalization.cpp
    temporal_simplification.cpp
)

target_link_libraries(ahfl_compiler_passes
    PUBLIC
        ahfl_compiler_ir
)

target_include_directories(ahfl_compiler_passes PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_passes)
