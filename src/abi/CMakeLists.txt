add_library(ahfl_abi STATIC
    version_info.cpp
    compat_check.cpp
)

target_include_directories(ahfl_abi
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

ahfl_enable_cxx23(ahfl_abi)
ahfl_apply_project_warnings(ahfl_abi)
