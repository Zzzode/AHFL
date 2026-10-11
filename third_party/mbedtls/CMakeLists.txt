# mbedTLS 3.6.4 — vendored TLS library (Apache-2.0).
#
# See VERSION for the exact upstream tarball and SHA-256, VENDORING.md for
# the patch manifest (none), LICENSE for the upstream licence text.
#
# Wrapper forces a static, library-only, warning-isolated build.
# The upstream CMakeLists.txt is used as-is via add_subdirectory(upstream).
# The upstream build creates MbedTLS::mbedtls / MbedTLS::mbedx509 /
# MbedTLS::mbedcrypto aliases that curl's custom FindMbedTLS.cmake resolves.

# --- Static library only ------------------------------------------------------
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)

# --- No programs, no tests ----------------------------------------------------
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)

# --- Don't treat warnings as errors in vendored code --------------------------
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

# --- Disable package config and install (subproject default) ------------------
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)

# --- Disable GEN_FILES (we ship pre-generated files) --------------------------
set(GEN_FILES OFF CACHE BOOL "" FORCE)

add_subdirectory(upstream)

# Isolate vendored code from -Werror.
foreach(_t mbedtls mbedx509 mbedcrypto everest p256m)
    if(TARGET ${_t})
        ahfl_apply_third_party_warnings(${_t})
    endif()
endforeach()
