add_library(ahfl_compiler_project_discovery STATIC
    discovery.cpp
)

target_link_libraries(ahfl_compiler_project_discovery
    PUBLIC
        ahfl_compiler_package_graph
)

target_include_directories(ahfl_compiler_project_discovery PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(ahfl_compiler_project_discovery
    PRIVATE
        AHFL_DEFAULT_SYSROOT="${PROJECT_SOURCE_DIR}"
)

ahfl_apply_project_warnings(ahfl_compiler_project_discovery)
