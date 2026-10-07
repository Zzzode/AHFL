find_package(JlCxx REQUIRED)

add_library(z3jl SHARED z3jl.cpp)
target_link_libraries(z3jl PRIVATE JlCxx::cxxwrap_julia libz3)
target_include_directories(z3jl PRIVATE
    "${PROJECT_SOURCE_DIR}/src/api"
    "${PROJECT_BINARY_DIR}/src/api"
    "${PROJECT_SOURCE_DIR}/src/api/c++"
)

option(Z3_INSTALL_JULIA_BINDINGS "Install Julia bindings when invoking install target" ON)
if(Z3_INSTALL_JULIA_BINDINGS)
    # AHFL vendoring patch: removed install(TARGETS z3jl ...) — bindings are
    # not built (Z3_BUILD_JULIA_BINDINGS=OFF) and AHFL installs only the z3
    # executable.
endif()