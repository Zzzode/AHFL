add_library(ahfl_tooling_package STATIC
    registry.cpp
    resolver.cpp
)

target_link_libraries(ahfl_tooling_package
    PUBLIC
        ahfl_base_public
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

target_include_directories(ahfl_tooling_package PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_package)
