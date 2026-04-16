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
find_program(CLANG_FORMAT_BIN NAMES clang-format clang-format-21 clang-format-20)

add_subdirectory(third_party/antlr4)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

ahfl_enable_format_targets("${CMAKE_CURRENT_SOURCE_DIR}" "${CLANG_FORMAT_BIN}")
