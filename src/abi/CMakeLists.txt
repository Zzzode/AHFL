add_library(ahfl_abi STATIC
    version_info.cpp
    compat_check.cpp
)

target_link_libraries(ahfl_abi PUBLIC ahfl_support)

ahfl_enable_cxx23(ahfl_abi)
target_include_directories(ahfl_abi PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_abi)
