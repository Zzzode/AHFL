# FindNGHTTP2.cmake — vendored nghttp2 resolver for curl.
#
# When nghttp2 is built from source via add_subdirectory(third_party/nghttp2),
# the NGHTTP2::nghttp2 alias target already exists. This module creates the
# CURL::nghttp2 interface target that curl's CMakeLists.txt expects,
# bypassing find_library / find_path which cannot see libraries that have not
# been built yet at configure time.

if(TARGET NGHTTP2::nghttp2)
    set(NGHTTP2_FOUND TRUE)
    set(NGHTTP2_VERSION "1.70.0")

    if(NOT TARGET CURL::nghttp2)
        add_library(CURL::nghttp2 INTERFACE IMPORTED)
        set_target_properties(CURL::nghttp2 PROPERTIES
            INTERFACE_LINK_LIBRARIES "NGHTTP2::nghttp2"
        )
    endif()
else()
    set(NGHTTP2_FOUND FALSE)
endif()
