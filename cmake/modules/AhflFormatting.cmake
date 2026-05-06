# ─────────────────────────────────────────────────────────────────────────────
# AhflFormatting.cmake
# 格式化目标：format（就地修改）+ format-check（仅检查）
# 要求 clang-format 18.x，与 CI (Ubuntu 24.04) 保持一致
# ─────────────────────────────────────────────────────────────────────────────

set(AHFL_REQUIRED_CLANG_FORMAT_MAJOR 18)

function(ahfl_enable_format_targets root_dir clang_format_bin)
    if(NOT clang_format_bin)
        message(STATUS "clang-format not found; format targets are disabled")
        message(STATUS "  macOS: brew install llvm@18")
        message(STATUS "  Linux: pipx install clang-format==18.1.8")
        return()
    endif()

    # ── 版本校验 ──────────────────────────────────────────────────────────
    execute_process(
        COMMAND ${clang_format_bin} --version
        OUTPUT_VARIABLE _cf_version_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # 提取主版本号（"clang-format version 18.1.8" → "18"）
    string(REGEX MATCH "([0-9]+)\\." _cf_match "${_cf_version_output}")
    set(_cf_major "${CMAKE_MATCH_1}")

    if(NOT _cf_major STREQUAL "${AHFL_REQUIRED_CLANG_FORMAT_MAJOR}")
        message(WARNING
            "clang-format version mismatch!\n"
            "  Found:    ${_cf_version_output}\n"
            "  Required: ${AHFL_REQUIRED_CLANG_FORMAT_MAJOR}.x\n"
            "  CI uses clang-format-${AHFL_REQUIRED_CLANG_FORMAT_MAJOR} on Ubuntu 24.04.\n"
            "  Install:  brew install llvm@18  (macOS)\n"
            "            pipx install clang-format==18.1.8  (Linux)\n"
            "  Format targets are DISABLED to prevent version drift."
        )
        return()
    endif()

    message(STATUS "clang-format: ${clang_format_bin} (${_cf_version_output})")

    # ── 收集源文件 ────────────────────────────────────────────────────────
    file(GLOB_RECURSE AHFL_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${root_dir}/include/*.hpp"
        "${root_dir}/src/*.hpp"
        "${root_dir}/src/*.cpp"
    )
    list(FILTER AHFL_FORMAT_SOURCES EXCLUDE REGEX ".*/src/parser/generated/.*")

    # ── 格式化目标 ────────────────────────────────────────────────────────
    add_custom_target(format
        COMMAND ${clang_format_bin} -i --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${root_dir}
        COMMENT "Formatting AHFL C++ sources with clang-format-${AHFL_REQUIRED_CLANG_FORMAT_MAJOR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${clang_format_bin} --dry-run --Werror --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${root_dir}
        COMMENT "Checking AHFL C++ source formatting (clang-format-${AHFL_REQUIRED_CLANG_FORMAT_MAJOR})"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()
