add_library(ahfl_incremental STATIC
    dependency_graph.cpp
    ir_cache.cpp
    incremental_compiler.cpp
)

target_link_libraries(ahfl_incremental
    PUBLIC
        ahfl_ir
)

ahfl_enable_cxx23(ahfl_incremental)
target_include_directories(ahfl_incremental PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_incremental)
