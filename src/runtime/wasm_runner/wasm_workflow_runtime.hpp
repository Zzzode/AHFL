#pragma once

// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed workflow runtime
// FACADE. Compiles every workflow in an AHFL-IR program to wasm once in the
// constructor, then run(name, input) drives the wasm_host engine-session
// layer. This is the ONLY runtime-tier component that links a compiler
// backend (ahfl_compiler_backend_wasm); the wasm_host free functions stay
// backend-free.
//
// The surface mirrors the evaluator-backed WorkflowRuntime
// (src/runtime/engine/workflow_runtime.hpp): same constructor shape
// (const ir::Program& + config), same run(name, input) -> WorkflowResult.
// A host that already drives the evaluator-backed runtime through the shared
// WasmRuntimeHooks can switch to this facade without changing its hook
// wiring.

#include "runtime/wasm_host/wasm_runtime_hooks.hpp"

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/compiler/ir/program.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::runtime::wasm_runner {

// Configuration for the wasm3-backed workflow runtime.
struct WasmWorkflowRuntimeConfig {
    // The shared hook set (identical signatures to WorkflowRuntimeConfig).
    wasm_host::WasmRuntimeHooks hooks;
    // The contextual capability invoker (production host capability dispatch).
    ContextualCapabilityInvoker invoker;
    // Resolves a capability's source_symbol to its canonical name.
    std::function<std::optional<std::string>(std::uint64_t)> name_resolver;
};

// The wasm3-backed workflow runtime. Compiles every workflow in the program
// to wasm in the constructor; run(name, input) looks up the compiled module
// by workflow name and drives the wasm_host workflow session.
//
// If compilation fails (lowering / layout / emission), the constructor
// records the error and every run() call returns a failed WorkflowResult
// carrying the compilation diagnostic (wasm.compile-failed).
class WasmWorkflowRuntime {
  public:
    WasmWorkflowRuntime(const ir::Program &program,
                        WasmWorkflowRuntimeConfig config);

    // Execute the named workflow.
    [[nodiscard]] WorkflowResult run(const std::string &workflow_name,
                                     Value input);

  private:
    struct CompiledWorkflow {
        std::vector<std::uint8_t> module_bytes;
        ahfl::backends::CoreWasmExecutionDescriptor descriptor;
    };

    // Populated by the constructor. Empty when compilation failed.
    std::unordered_map<std::string, CompiledWorkflow> workflows_;
    // Set when compilation fails; every run() returns a failed result.
    std::optional<std::string> compile_error_;
    WasmWorkflowRuntimeConfig config_;
};

} // namespace ahfl::runtime::wasm_runner
