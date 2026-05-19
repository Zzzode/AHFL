add_library(ahfl_package STATIC
    registry.cpp
    resolver.cpp
)

target_link_libraries(ahfl_package
    PUBLIC
        ahfl_support
)

ahfl_apply_project_warnings(ahfl_package)
