add_library(ahfl_tooling_incremental STATIC
    dependency_graph.cpp
    ir_cache.cpp
    incremental_compiler.cpp
)

target_link_libraries(ahfl_tooling_incremental
    PUBLIC
        ahfl_compiler_ir
)

ahfl_enable_cxx23(ahfl_tooling_incremental)
target_include_directories(ahfl_tooling_incremental PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_incremental)
