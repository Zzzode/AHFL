function(ahfl_enable_cxx23 target_name)
    get_target_property(AHFL_TARGET_TYPE ${target_name} TYPE)

    if(AHFL_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target_name} INTERFACE cxx_std_23)
    else()
        target_compile_features(${target_name} PUBLIC cxx_std_23)
    endif()
endfunction()

option(AHFL_WARNINGS_AS_ERRORS "Treat AHFL project warnings as errors." ON)

function(ahfl_apply_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        if(AHFL_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(AHFL_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(ahfl_apply_third_party_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W0)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wno-overloaded-virtual
            -Wno-dollar-in-identifier-extension
            -Wno-four-char-constants
        )
    endif()
endfunction()

function(ahfl_assert_no_external_toml_runtime target_name)
    get_target_property(AHFL_TOML_POLICY_LINK_LIBRARIES "${target_name}" LINK_LIBRARIES)
    if(NOT AHFL_TOML_POLICY_LINK_LIBRARIES)
        return()
    endif()

    foreach(AHFL_TOML_POLICY_DEP IN LISTS AHFL_TOML_POLICY_LINK_LIBRARIES)
        if(AHFL_TOML_POLICY_DEP STREQUAL "ahfl_base_toml")
            continue()
        endif()

        string(TOLOWER "${AHFL_TOML_POLICY_DEP}" AHFL_TOML_POLICY_DEP_LOWER)
        if(AHFL_TOML_POLICY_DEP_LOWER MATCHES
           "(^|::|[-_])toml($|[-_+:.])|tomlplusplus|toml11|cpptoml")
            message(FATAL_ERROR
                "RFC 0005 forbids non-vendored TOML runtime dependencies on "
                "${target_name}: ${AHFL_TOML_POLICY_DEP}"
            )
        endif()
    endforeach()
endfunction()
