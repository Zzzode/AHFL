add_library(ahfl_tooling_abi STATIC
    version_info.cpp
    compat_check.cpp
)

target_link_libraries(ahfl_tooling_abi PUBLIC ahfl_base_public)

ahfl_enable_cxx23(ahfl_tooling_abi)
target_include_directories(ahfl_tooling_abi PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_abi)
