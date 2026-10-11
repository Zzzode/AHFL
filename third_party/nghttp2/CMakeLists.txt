# nghttp2 1.70.0 — vendored HTTP/2 framing library (MIT).
#
# See VERSION for the exact upstream tarball and SHA-256, VENDORING.md for
# the patch manifest (none), LICENSE for the upstream licence text.
#
# Wrapper forces a static, library-only, warning-isolated build.
# The upstream CMakeLists.txt is used as-is via add_subdirectory(upstream).

# --- Library only (disables apps, hpack tools, examples) ----------------------
set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)

# --- Static library only ------------------------------------------------------
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# --- Disable docs --------------------------------------------------------------
set(ENABLE_DOC OFF CACHE BOOL "" FORCE)

# --- Disable optional dependencies ---------------------------------------------
set(WITH_JEMALLOC OFF CACHE BOOL "" FORCE)
set(WITH_JANSSON OFF CACHE BOOL "" FORCE)
set(WITH_LIBXML2 OFF CACHE BOOL "" FORCE)
set(WITH_MRUBY OFF CACHE BOOL "" FORCE)
set(WITH_NEVERBLEED OFF CACHE BOOL "" FORCE)
set(WITH_SPDYLAY OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEMD OFF CACHE BOOL "" FORCE)
set(WITH_LIBBPF OFF CACHE BOOL "" FORCE)
set(WITH_CARES OFF CACHE BOOL "" FORCE)
set(WITH_TCMALLOC OFF CACHE BOOL "" FORCE)

# --- Disable tests --------------------------------------------------------------
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_FAILMALLOC OFF CACHE BOOL "" FORCE)

add_subdirectory(upstream)

# Isolate vendored code from -Werror.
if(TARGET nghttp2_static)
    ahfl_apply_third_party_warnings(nghttp2_static)
    add_library(NGHTTP2::nghttp2 ALIAS nghttp2_static)
    target_include_directories(nghttp2_static SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/upstream/lib/includes>
    )
endif()
