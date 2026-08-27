cmake_minimum_required(VERSION 3.22)

project(AHFL VERSION 0.59.0 LANGUAGES CXX)

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

# RFC 0017: optional Z3 SMT solver for the BMC contract-semantics integration
# tests. Detection mirrors AHFL_SMV_CHECKER: explicit cache var, then
# AHFL_Z3_PATH env, then a PATH lookup for `z3`.
set(AHFL_Z3_SOLVER "" CACHE FILEPATH
    "Path to a Z3 SMT solver used by optional RFC 0017 formal CTest tests"
)
if(NOT AHFL_Z3_SOLVER AND DEFINED ENV{AHFL_Z3_PATH})
    set(AHFL_Z3_SOLVER "$ENV{AHFL_Z3_PATH}" CACHE FILEPATH
        "Path to a Z3 SMT solver used by optional RFC 0017 formal CTest tests"
        FORCE
    )
endif()
if(NOT AHFL_Z3_SOLVER)
    find_program(AHFL_DETECTED_Z3_SOLVER NAMES z3)
    if(AHFL_DETECTED_Z3_SOLVER)
        set(AHFL_Z3_SOLVER "${AHFL_DETECTED_Z3_SOLVER}" CACHE FILEPATH
            "Path to a Z3 SMT solver used by optional RFC 0017 formal CTest tests"
            FORCE
        )
    endif()
endif()
if(AHFL_Z3_SOLVER)
    message(STATUS "AHFL Z3 solver for optional tests: ${AHFL_Z3_SOLVER}")
else()
    message(STATUS "AHFL Z3 solver for optional tests: not found")
endif()

# KR4.5 / RFC 0012 stabilized: optional real (non-stub) local LLM engine for the
# real-LLM run evidence CTest. Detection mirrors AHFL_Z3_SOLVER: explicit cache
# var, then AHFL_LLAMA_SERVER env, then a PATH lookup for `llama-server`. When
# present, the evidence test drives genuine llama.cpp autoregressive inference
# through AHFL's production provider path (see tests/scripts/real_llm_run_evidence.py).
set(AHFL_LLAMA_SERVER "" CACHE FILEPATH
    "Path to a llama.cpp llama-server used by the optional real-LLM run evidence test"
)
if(NOT AHFL_LLAMA_SERVER AND DEFINED ENV{AHFL_LLAMA_SERVER})
    set(AHFL_LLAMA_SERVER "$ENV{AHFL_LLAMA_SERVER}" CACHE FILEPATH
        "Path to a llama.cpp llama-server used by the optional real-LLM run evidence test"
        FORCE
    )
endif()
if(NOT AHFL_LLAMA_SERVER)
    find_program(AHFL_DETECTED_LLAMA_SERVER NAMES llama-server)
    if(AHFL_DETECTED_LLAMA_SERVER)
        set(AHFL_LLAMA_SERVER "${AHFL_DETECTED_LLAMA_SERVER}" CACHE FILEPATH
            "Path to a llama.cpp llama-server used by the optional real-LLM run evidence test"
            FORCE
        )
    endif()
endif()
if(AHFL_LLAMA_SERVER)
    message(STATUS "AHFL llama-server for optional real-LLM test: ${AHFL_LLAMA_SERVER}")
else()
    message(STATUS "AHFL llama-server for optional real-LLM test: not found")
endif()

option(AHFL_ENABLE_BACKEND_FORMAL "Build formal verification backend (SMV)" ON)
option(AHFL_ENABLE_BACKEND_INFRA "Build infrastructure backends (WASM, K8s, OpenAPI, Terraform)" ON)
option(AHFL_ENABLE_BACKEND_PIPELINE "Build pipeline observation backends" ON)

add_subdirectory(third_party/antlr4)
add_subdirectory(third_party/doctest)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

ahfl_enable_format_targets("${CMAKE_CURRENT_SOURCE_DIR}" "${CLANG_FORMAT_BIN}")

option(AHFL_INSTALL "Generate install/export rules" OFF)
if(AHFL_INSTALL)
    include(cmake/modules/AhflInstall.cmake)
endif()
