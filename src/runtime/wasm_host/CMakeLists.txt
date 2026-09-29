# RFC 0026 KR6.8 WH-1: the wasm3-backed CoreWasmResumeEngine port. This target
# exists only when AHFL_ENABLE_BACKEND_WASM is ON (the embedded host consumes
# emitted Core-IR wasm; WASM=OFF leaves the tree without it). It is a peer
# execution engine of src/runtime/engine, not a subfolder of it (decision doc
# section 5 tier-honesty), which is why it gets its own directory and target.
add_library(ahfl_runtime_wasm_host STATIC
    wasm3_engine.cpp
)

target_link_libraries(ahfl_runtime_wasm_host
    PRIVATE
        # The vendored wasm3 interpreter (C library; SYSTEM include, extern "C"
        # in the TU). Gated with this target by AHFL_ENABLE_BACKEND_WASM.
        wasm3
        # The engine port + the fixed-page capacity authority.
        ahfl_runtime_engine
        # The F1 ABI SSOT (kCoreWasmFixedLinearMemory* constants).
        ahfl_compiler_ir
)

# The engine header includes the engine-port header under src/
# ("runtime/engine/core_wasm_resume_engine.hpp"); propagate the build-tree src/
# dir to consumers. BUILD_INTERFACE only: these headers are NOT installed SDK
# surface (mirrors src/runtime/value/CMakeLists.txt).
target_include_directories(ahfl_runtime_wasm_host PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

ahfl_apply_project_warnings(ahfl_runtime_wasm_host)
