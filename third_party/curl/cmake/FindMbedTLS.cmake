# FindMbedTLS.cmake — vendored mbedTLS resolver for curl.
#
# When mbedTLS is built from source via add_subdirectory(third_party/mbedtls),
# the MbedTLS::mbedtls / MbedTLS::mbedx509 / MbedTLS::mbedcrypto targets
# already exist. This module creates the CURL::mbedtls interface target that
# curl's CMakeLists.txt expects, bypassing find_library / find_path which
# cannot see libraries that have not been built yet at configure time.

if(TARGET MbedTLS::mbedtls AND TARGET MbedTLS::mbedx509 AND TARGET MbedTLS::mbedcrypto)
    set(MbedTLS_FOUND TRUE)
    set(MBEDTLS_FOUND TRUE)
    set(MBEDTLS_VERSION "3.6.4")

    if(NOT TARGET CURL::mbedtls)
        add_library(CURL::mbedtls INTERFACE IMPORTED)
        set_target_properties(CURL::mbedtls PROPERTIES
            INTERFACE_LINK_LIBRARIES "MbedTLS::mbedtls;MbedTLS::mbedx509;MbedTLS::mbedcrypto"
        )
    endif()
else()
    set(MbedTLS_FOUND FALSE)
    set(MBEDTLS_FOUND FALSE)
endif()
