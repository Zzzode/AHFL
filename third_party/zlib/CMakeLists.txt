# zlib 1.3.1 — vendored compression library (zlib License).
#
# See VERSION for the exact upstream tarball and SHA-256, VENDORING.md for
# the patch manifest (none), LICENSE for the upstream licence text.
#
# Wrapper forces a static, minimal, warning-isolated build. The upstream
# CMakeLists.txt is used as-is via add_subdirectory(upstream).

set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(upstream)

# The upstream zlib CMake creates `zlib` (shared) and `zlibstatic` (static).
# Alias the static target to a namespaced name for consistent linking.
if(TARGET zlibstatic)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    ahfl_apply_third_party_warnings(zlibstatic)
    target_include_directories(zlibstatic SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream>
    )
endif()
