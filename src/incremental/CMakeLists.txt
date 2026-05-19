add_library(ahfl_incremental STATIC
    dependency_graph.cpp
    ir_cache.cpp
    incremental_compiler.cpp
)

target_include_directories(ahfl_incremental
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

ahfl_enable_cxx23(ahfl_incremental)
ahfl_apply_project_warnings(ahfl_incremental)
