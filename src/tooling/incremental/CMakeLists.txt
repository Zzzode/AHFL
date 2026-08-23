add_library(ahfl_tooling_incremental STATIC
    cache_core.cpp
    daemon.cpp
    dependency_graph.cpp
    import_graph_discovery.cpp
    ir_cache.cpp
    incremental_compiler.cpp
)

target_link_libraries(ahfl_tooling_incremental
    PUBLIC
        ahfl_compiler_ir
        ahfl_compiler_project_discovery
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

ahfl_enable_cxx23(ahfl_tooling_incremental)
target_include_directories(ahfl_tooling_incremental PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_incremental)

add_executable(ahfl-incremental main.cpp)
target_link_libraries(ahfl-incremental
    PRIVATE
        ahfl_tooling_incremental
)
target_include_directories(ahfl-incremental PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-incremental)
