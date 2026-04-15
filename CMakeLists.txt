cmake_minimum_required(VERSION 3.22)

project(AHFL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(
    AHFL_EXPORT_COMPILE_COMMANDS
    CACHE CMAKE_EXPORT_COMPILE_COMMANDS
    PROPERTY VALUE
)

if(AHFL_EXPORT_COMPILE_COMMANDS)
    set(AHFL_ROOT_COMPILE_COMMANDS "${CMAKE_SOURCE_DIR}/compile_commands.json")

    if(IS_SYMLINK "${AHFL_ROOT_COMPILE_COMMANDS}")
        file(REMOVE "${AHFL_ROOT_COMPILE_COMMANDS}")
    endif()

    if(NOT EXISTS "${AHFL_ROOT_COMPILE_COMMANDS}")
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
    else()
        message(STATUS
            "keeping existing ${AHFL_ROOT_COMPILE_COMMANDS}; not replacing regular file"
        )
    endif()
endif()

find_package(Threads REQUIRED)
find_program(CLANG_FORMAT_BIN NAMES clang-format clang-format-21 clang-format-20)

file(GLOB_RECURSE ANTLR4_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/antlr4/runtime/src/*.cpp"
)

add_library(antlr4_runtime STATIC
    ${ANTLR4_RUNTIME_SOURCES}
)

target_include_directories(antlr4_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/antlr4/runtime/src
)

target_compile_features(antlr4_runtime PUBLIC cxx_std_23)
target_compile_definitions(antlr4_runtime PUBLIC ANTLR4CPP_STATIC)
target_link_libraries(antlr4_runtime PUBLIC Threads::Threads)

if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    if(COREFOUNDATION_LIBRARY)
        target_link_libraries(antlr4_runtime PUBLIC ${COREFOUNDATION_LIBRARY})
    endif()
endif()

if(MSVC)
    target_compile_options(antlr4_runtime PRIVATE /W0)
else()
    target_compile_options(antlr4_runtime PRIVATE
        -Wno-overloaded-virtual
        -Wno-dollar-in-identifier-extension
        -Wno-four-char-constants
    )
endif()

add_library(ahfl_generated_parser STATIC
    src/parser/generated/AHFLBaseVisitor.cpp
    src/parser/generated/AHFLLexer.cpp
    src/parser/generated/AHFLParser.cpp
    src/parser/generated/AHFLVisitor.cpp
)

target_include_directories(ahfl_generated_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/generated
)

target_compile_features(ahfl_generated_parser PUBLIC cxx_std_23)
target_link_libraries(ahfl_generated_parser PUBLIC antlr4_runtime)

add_library(ahfl_frontend STATIC
    src/ast.cpp
    src/frontend.cpp
)

target_include_directories(ahfl_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ahfl_frontend PUBLIC cxx_std_23)
target_link_libraries(ahfl_frontend PUBLIC ahfl_generated_parser)

if(MSVC)
    target_compile_options(ahfl_frontend PRIVATE /W4 /permissive-)
else()
    target_compile_options(ahfl_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ahflc
    src/tools/ahflc.cpp
)

target_link_libraries(ahflc PRIVATE ahfl_frontend)

if(CLANG_FORMAT_BIN)
    file(GLOB_RECURSE AHFL_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    )

    list(FILTER AHFL_FORMAT_SOURCES EXCLUDE REGEX ".*/src/parser/generated/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_BIN} -i --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting handwritten AHFL C++ sources with clang-format"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror --style=file ${AHFL_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking handwritten AHFL C++ source formatting"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; format targets are disabled")
endif()
