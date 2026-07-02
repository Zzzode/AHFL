add_library(ahfl_compiler_package_graph STATIC
    lockfile.cpp
    package_graph.cpp
    package_graph_json.cpp
)

target_link_libraries(ahfl_compiler_package_graph
    PUBLIC
        ahfl_base_json
        ahfl_compiler_manifest
    PRIVATE
        ahfl_base_support
)

target_include_directories(ahfl_compiler_package_graph PUBLIC ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_package_graph)
