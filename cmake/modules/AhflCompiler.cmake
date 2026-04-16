function(ahfl_enable_cxx23 target_name)
    get_target_property(AHFL_TARGET_TYPE ${target_name} TYPE)

    if(AHFL_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target_name} INTERFACE cxx_std_23)
    else()
        target_compile_features(${target_name} PUBLIC cxx_std_23)
    endif()
endfunction()

function(ahfl_apply_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
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
