#pragma once

// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed agent runner FACADE.
// One-shot compile + run: takes an AHFL-IR program and an agent name, compiles
// the agent to wasm, and drives the wasm_host engine-session layer. This is
// the ONLY runtime-tier component that links a compiler backend
// (ahfl_compiler_backend_wasm); the wasm_host free functions stay backend-free.

#include "runtime/wasm_host/wasm_agent_runner.hpp"
#include "runtime/wasm_host/wasm_runtime_hooks.hpp"

#include "runtime/engine/capability_bridge.hpp"
#include "runtime/value/value.hpp"

#include "ahfl/compiler/ir/program.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace ahfl::runtime::wasm_runner {

// Configuration for the one-shot wasm agent runner.
struct WasmAgentRunnerConfig {
    // The shared hook set (identical signatures to WorkflowRuntimeConfig).
    wasm_host::WasmRuntimeHooks hooks;
    // The contextual capability invoker (canonical instance, real effects).
    ContextualCapabilityInvoker invoker;
    // Optional: the invoker for the effects-free step-walk instance. When
    // empty, the canonical invoker is used for the step-walk too (the
    // conformance case, where the harness supplies a side-effect-free mock
    // as both).
    ContextualCapabilityInvoker states_invoker;
};

// One-shot compile + run: compile the named agent from the program to wasm
// and drive it end-to-end on the wasm3 engine.
//
// Returns the run result (status, output, observation data, failure kinds),
// or a human-readable error string on compilation / setup failure.
[[nodiscard]] std::expected<wasm_host::WasmAgentRunResult, std::string>
run_wasm_agent(const ir::Program &program, std::string_view agent_name,
               const Value &input, WasmAgentRunnerConfig config);

} // namespace ahfl::runtime::wasm_runner
