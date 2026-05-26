add_library(ahfl_passes STATIC
    pass_manager.cpp
    dead_state_elimination.cpp
    capability_reachability.cpp
    contract_redundancy.cpp
    workflow_simplification.cpp
    expr_canonicalization.cpp
    temporal_simplification.cpp
)

target_link_libraries(ahfl_passes
    PUBLIC
        ahfl_ir
)

target_include_directories(ahfl_passes PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_passes)
