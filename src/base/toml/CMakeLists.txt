add_library(ahfl_base_toml STATIC
    toml.cpp
)

target_link_libraries(ahfl_base_toml
    PUBLIC
        ahfl_base_public
)

# Internal implementation header (src/base/toml/toml.hpp), included only by
# in-tree translation units via "base/toml/..."; never part of the installed
# SDK include surface. BUILD_INTERFACE keeps the build tree working while the
# install/export interface carries no source-tree path.
target_include_directories(ahfl_base_toml PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

ahfl_assert_no_external_toml_runtime(ahfl_base_toml)
ahfl_apply_project_warnings(ahfl_base_toml)
