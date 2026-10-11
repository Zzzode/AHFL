# curl 8.19.0 — vendored HTTP client (curl License).
#
# See VERSION for the exact upstream tarball and SHA-256, VENDORING.md for
# the patch manifest (3 Find-module replacements in upstream/CMake/),
# LICENSE for the upstream licence text.
#
# Wrapper forces a static, HTTP-only, mbedTLS-backed, warning-isolated build.
# The upstream CMakeLists.txt is used as-is via add_subdirectory(upstream).
# Custom Find modules in upstream/CMake/ resolve mbedTLS, nghttp2, and zlib
# to the vendored targets (libraries don't exist at configure time, so
# find_library cannot see them).

# --- Protocol lock: HTTP/HTTPS only -----------------------------------------
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

# --- Library only, no curl executable ---------------------------------------
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)

# --- TLS backend: mbedTLS only -----------------------------------------------
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_WOLFSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_RUSTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
set(CURL_USE_MESALINK OFF CACHE BOOL "" FORCE)

# --- HTTP/2 via nghttp2, compression via zlib --------------------------------
set(USE_NGHTTP2 ON CACHE BOOL "" FORCE)
set(CURL_ZLIB ON CACHE BOOL "" FORCE)

# --- Static library -----------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)

# --- Disable features we don't need ------------------------------------------
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(ENABLE_UNICODE OFF CACHE BOOL "" FORCE)

# --- Disable optional dependencies (hermetic build) ---------------------------
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBUV OFF CACHE BOOL "" FORCE)
set(USE_LIBRTMP OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGTCP2 OFF CACHE BOOL "" FORCE)
set(USE_QUICHE OFF CACHE BOOL "" FORCE)
set(USE_HTTPSRR OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBGSASL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBIDN OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBRTMP OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_MESALINK OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_RUSTLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
set(CURL_USE_WOLFSSL OFF CACHE BOOL "" FORCE)

# --- Disable pkg-config (we use vendored targets, not system libraries) ------
set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)

# --- Pre-set mbedTLS feature checks -------------------------------------------
# curl's CMakeLists.txt calls check_function_exists("mbedtls_des_crypt_ecb")
# with CMAKE_REQUIRED_LIBRARIES=CURL::mbedtls. That creates a try_compile
# project where the MbedTLS::* ALIAS targets are not visible, so the check
# fails. mbedTLS 3.6.4 has mbedtls_des_crypt_ecb (removed in 4.0), so pre-set
# the result to skip the check entirely.
set(HAVE_MBEDTLS_DES_CRYPT_ECB 1 CACHE INTERNAL "")

add_subdirectory(upstream)

# Isolate vendored code from -Werror. The actual static target is
# libcurl_static (libcurl and CURL::libcurl are ALIAS targets created by
# curl's own CMake, which cannot receive compile options).
if(TARGET libcurl_static)
    ahfl_apply_third_party_warnings(libcurl_static)
endif()
