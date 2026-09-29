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
            # GCC 12's -O2 analysis emits well-known false positives for
            # -Wrestrict (inlined libstdc++ char_traits::copy on plain
            # std::string builders) and -Wmaybe-uninitialized (std::optional /
            # std::variant internals). These are fixed in newer GCC and never
            # fire under clang — the dev/CI compiler — which keeps full -Werror
            # coverage including its own uninitialized-use analysis. Keep the
            # diagnostics ON (still warn) but stop them from failing the
            # Release/-O2 GCC build (the clean-install evidence gate), instead
            # of scattering per-translation-unit pragmas across the tree.
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                target_compile_options(${target_name} PRIVATE
                    -Wno-error=restrict
                    -Wno-error=maybe-uninitialized
                )
            endif()
        endif()
    endif()
endfunction()

function(ahfl_apply_third_party_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W0)
        return()
    endif()

    # The downstream flags are C++-only. A C target (e.g. the vendored wasm3
    # interpreter) would otherwise emit a diagnostic about the unknown option
    # itself -- "valid for C++/ObjC++ but not for C" is a warning in GCC but a
    # hard error in Clang, so under -Werror it breaks the build. Decide from the
    # target's own sources, so every call site stays identical (antlr4
    # precedent) and a C++ target keeps exactly its previous profile. The
    # LINKER_LANGUAGE property is not readable for STATIC libraries, hence the
    # extension scan.
    get_target_property(AHFL_THIRD_PARTY_SOURCES ${target_name} SOURCES)
    set(AHFL_THIRD_PARTY_IS_C ON)
    if(AHFL_THIRD_PARTY_SOURCES)
        foreach(AHFL_THIRD_PARTY_SOURCE IN LISTS AHFL_THIRD_PARTY_SOURCES)
            if(AHFL_THIRD_PARTY_SOURCE MATCHES "\\.(cpp|cc|cxx|c\\+\\+|mm|ixx)$")
                set(AHFL_THIRD_PARTY_IS_C OFF)
                break()
            endif()
        endforeach()
    else()
        set(AHFL_THIRD_PARTY_IS_C OFF)
    endif()

    if(AHFL_THIRD_PARTY_IS_C)
        # Silences the C++-only-flag diagnostics above; the vendored C has its
        # own warning posture and is not ours to keep clean.
        target_compile_options(${target_name} PRIVATE -w)
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
