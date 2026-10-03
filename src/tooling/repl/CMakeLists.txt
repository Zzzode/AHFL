add_library(ahfl_tooling_repl STATIC
    repl.cpp
)

# RFC 0026 KR6.8 WH-7 (kr68 §12.8.5): the REPL no longer links the tree-walking
# evaluator. Expression evaluation goes through the wasm3-backed agent runner
# (gated below); the compiler bundle provides the frontend/IR/semantics, and
# ahfl_runtime_value provides print_value + the Value type.
target_link_libraries(ahfl_tooling_repl
    PUBLIC
        ahfl_compiler
        ahfl_verification_formal
        ahfl_runtime_value
)

# WH-7 (kr68 §12.8.5): the wasm eval path is a single #ifdef gate in
# default_eval_handler. Under WASM=OFF the eval verb refuses with an actionable
# diagnostic; :type/:verify/:simulate/:help and the Unit short-circuit stay
# fully functional (pure compiler/value code, no engine dependency).
if(AHFL_ENABLE_BACKEND_WASM)
    target_link_libraries(ahfl_tooling_repl PUBLIC ahfl_runtime_wasm_runner)
endif()
target_compile_definitions(ahfl_tooling_repl PUBLIC
    $<$<BOOL:${AHFL_ENABLE_BACKEND_WASM}>:AHFL_ENABLE_BACKEND_WASM=1>)

target_include_directories(ahfl_tooling_repl PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl_tooling_repl)

add_executable(ahfl-repl main.cpp)
target_link_libraries(ahfl-repl
    PRIVATE
        ahfl_tooling_repl
)
target_include_directories(ahfl-repl PRIVATE ${PROJECT_SOURCE_DIR}/src)

ahfl_apply_project_warnings(ahfl-repl)
