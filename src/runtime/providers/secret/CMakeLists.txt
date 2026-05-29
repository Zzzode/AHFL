add_library(ahfl_runtime_provider_secret STATIC
    secret_provider.cpp
    auth_provider.cpp
    vault_provider.cpp
    key_rotation.cpp
)

target_link_libraries(ahfl_runtime_provider_secret
    PUBLIC
        ahfl_base_public
    PRIVATE
        ahfl_base_json
        ahfl_base_support
)

target_include_directories(ahfl_runtime_provider_secret PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_runtime_provider_secret)
