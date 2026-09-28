add_library(ahfl_compiler_project_discovery STATIC
    discovery.cpp
)

target_link_libraries(ahfl_compiler_project_discovery
    PUBLIC
        ahfl_compiler_package_graph
)

# Internal src/ headers ("compiler/project_discovery/..."), build-tree usage
# only; see src/base/toml/CMakeLists.txt for the BUILD_INTERFACE rationale.
target_include_directories(ahfl_compiler_project_discovery PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_compile_definitions(ahfl_compiler_project_discovery
    PRIVATE
        AHFL_DEFAULT_SYSROOT="${PROJECT_SOURCE_DIR}"
)

ahfl_apply_project_warnings(ahfl_compiler_project_discovery)
