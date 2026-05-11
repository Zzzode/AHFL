cmake_minimum_required(VERSION 3.22)

project(AHFL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

include(AhflCompileCommands)
include(AhflCompiler)
include(AhflFormatting)
include(AhflTesting)

ahfl_enable_root_compile_commands_link()

find_package(Threads REQUIRED)
find_program(CLANG_FORMAT_BIN
    NAMES clang-format-18 clang-format
    HINTS /opt/homebrew/opt/llvm@18/bin   # macOS Homebrew
          /usr/lib/llvm-18/bin            # Ubuntu/Debian
    NO_DEFAULT_PATH
)
# 如果 HINTS 未命中，降级搜索系统 PATH（CI 上 clang-format-18 在 /usr/bin）
if(NOT CLANG_FORMAT_BIN)
    find_program(CLANG_FORMAT_BIN NAMES clang-format-18)
endif()

set(AHFL_SMV_CHECKER "" CACHE FILEPATH
    "Path to a nuXmv/NuSMV-compatible checker used by optional formal CTest tests"
)
if(NOT AHFL_SMV_CHECKER AND DEFINED ENV{AHFL_SMV_CHECKER})
    set(AHFL_SMV_CHECKER "$ENV{AHFL_SMV_CHECKER}" CACHE FILEPATH
        "Path to a nuXmv/NuSMV-compatible checker used by optional formal CTest tests"
        FORCE
    )
endif()
if(NOT AHFL_SMV_CHECKER)
    find_program(AHFL_DETECTED_SMV_CHECKER NAMES NuSMV nuXmv nuxmv)
    if(AHFL_DETECTED_SMV_CHECKER)
        set(AHFL_SMV_CHECKER "${AHFL_DETECTED_SMV_CHECKER}" CACHE FILEPATH
            "Path to a nuXmv/NuSMV-compatible checker used by optional formal CTest tests"
            FORCE
        )
    endif()
endif()
if(AHFL_SMV_CHECKER)
    message(STATUS "AHFL SMV checker for optional tests: ${AHFL_SMV_CHECKER}")
else()
    message(STATUS "AHFL SMV checker for optional tests: not found")
endif()

add_subdirectory(third_party/antlr4)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

ahfl_enable_format_targets("${CMAKE_CURRENT_SOURCE_DIR}" "${CLANG_FORMAT_BIN}")
