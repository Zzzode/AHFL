add_library(ahfl_base_toml STATIC
    toml.cpp
)

target_link_libraries(ahfl_base_toml
    PUBLIC
        ahfl_base_public
)

target_include_directories(ahfl_base_toml PUBLIC ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_base_toml)
