add_library(ahfl_compiler_manifest STATIC
    manifest_canonical.cpp
    manifest.cpp
)

target_link_libraries(ahfl_compiler_manifest
    PUBLIC
        ahfl_base_public
        ahfl_base_toml
)

target_include_directories(ahfl_compiler_manifest PUBLIC ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_compiler_manifest)
