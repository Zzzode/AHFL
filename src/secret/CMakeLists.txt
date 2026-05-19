add_library(ahfl_secret STATIC
    secret_provider.cpp
    auth_provider.cpp
    vault_provider.cpp
    key_rotation.cpp
)

target_link_libraries(ahfl_secret
    PUBLIC
        ahfl_support
)

ahfl_apply_project_warnings(ahfl_secret)
