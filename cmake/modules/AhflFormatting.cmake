function(ahfl_enable_format_targets root_dir clang_format_bin)
    if(NOT clang_format_bin)
        message(STATUS "clang-format not found; format targets are disabled")
        return()
    endif()

    file(GLOB_RECURSE AHFL_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${root_dir}/include/*.hpp"
        "${root_dir}/src/*.hpp"
        "${root_dir}/src/*.cpp"
    )

    list(FILTER AHFL_FORMAT_SOURCES EXCLUDE REGEX ".*/src/parser/generated/.*")

    add_custom_target(format
        COMMAND ${clang_format_bin} -i --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${root_dir}
        COMMENT "Formatting handwritten AHFL C++ sources with clang-format"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${clang_format_bin} --dry-run --Werror --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${root_dir}
        COMMENT "Checking handwritten AHFL C++ source formatting"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()
