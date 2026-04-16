function(ahfl_enable_root_compile_commands_link)
    get_property(
        AHFL_EXPORT_COMPILE_COMMANDS
        CACHE CMAKE_EXPORT_COMPILE_COMMANDS
        PROPERTY VALUE
    )

    if(NOT AHFL_EXPORT_COMPILE_COMMANDS)
        return()
    endif()

    set(AHFL_ROOT_COMPILE_COMMANDS "${CMAKE_SOURCE_DIR}/compile_commands.json")

    if(IS_SYMLINK "${AHFL_ROOT_COMPILE_COMMANDS}")
        get_filename_component(
            AHFL_EXISTING_COMPILE_COMMANDS_TARGET
            "${AHFL_ROOT_COMPILE_COMMANDS}"
            REALPATH
            BASE_DIR "${CMAKE_SOURCE_DIR}"
        )

        if(AHFL_EXISTING_COMPILE_COMMANDS_TARGET STREQUAL
           "${CMAKE_BINARY_DIR}/compile_commands.json")
            return()
        endif()

        file(REMOVE "${AHFL_ROOT_COMPILE_COMMANDS}")
    elseif(EXISTS "${AHFL_ROOT_COMPILE_COMMANDS}")
        message(STATUS
            "keeping existing ${AHFL_ROOT_COMPILE_COMMANDS}; not replacing regular file"
        )
        return()
    endif()

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E create_symlink
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${AHFL_ROOT_COMPILE_COMMANDS}"
        RESULT_VARIABLE AHFL_CREATE_COMPILE_COMMANDS_LINK_RESULT
        ERROR_VARIABLE AHFL_CREATE_COMPILE_COMMANDS_LINK_ERROR
    )

    if(NOT AHFL_CREATE_COMPILE_COMMANDS_LINK_RESULT EQUAL 0)
        message(WARNING
            "failed to create root compile_commands.json symlink: "
            "${AHFL_CREATE_COMPILE_COMMANDS_LINK_ERROR}"
        )
    endif()
endfunction()
