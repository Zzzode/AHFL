add_library(ahfl_tooling_package STATIC
    registry.cpp
    registry_package_input.cpp
    resolver.cpp
    source_archive.cpp
)

target_link_libraries(ahfl_tooling_package
    PUBLIC
        ahfl_base_public
        ahfl_compiler_package_graph
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

target_include_directories(ahfl_tooling_package PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_package)
