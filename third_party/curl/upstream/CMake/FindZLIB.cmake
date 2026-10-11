# FindZLIB.cmake — vendored zlib resolver for curl.
#
# When zlib is built from source via add_subdirectory(third_party/zlib), the
# ZLIB::ZLIB alias target already exists. This module short-circuits CMake's
# built-in FindZLIB so that find_package(ZLIB) succeeds without searching for
# library files that do not exist yet at configure time.

if(TARGET ZLIB::ZLIB)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARY "$<TARGET_FILE:zlibstatic>")
    get_target_property(_zlib_inc zlibstatic INTERFACE_INCLUDE_DIRECTORIES)
    if(_zlib_inc)
        set(ZLIB_INCLUDE_DIR "${_zlib_inc}")
    else()
        set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/zlib/upstream")
    endif()
    set(ZLIB_VERSION_STRING "1.3.1")
    set(ZLIB_VERSION_MAJOR 1)
    set(ZLIB_VERSION_MINOR 3)
    set(ZLIB_VERSION_PATCH 1)
else()
    set(ZLIB_FOUND FALSE)
endif()
