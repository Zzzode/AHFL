#pragma once

// RFC 0026 KR6.8 WH-4: the wasm3-backed workflow runtime facade.
//
// A thin adapter over the workflow_session that accepts the shared
// WasmRuntimeHooks (hook signatures IDENTICAL to WorkflowRuntimeConfig) and
// the ContextualCapabilityInvoker, and delegates to run_workflow_session.
// This is the production entry point for running a WORKFLOW module end-to-end
// on the wasm3 engine with the D1 hook-firing discipline.

#include "runtime/wasm_host/wasm_runtime_hooks.hpp"
#include "runtime/wasm_host/workflow_session.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace ahfl::runtime::wasm_host {

// Run a WORKFLOW module end-to-end on the wasm3 engine.
//
// `module_bytes` is the compiled wasm module. `descriptor` carries the
// workflow schedule, agents, frame section, and wire schema. `input` is the
// workflow input. `hooks` carry the debug/test hooks (same signatures as
// WorkflowRuntimeConfig). `invoker` dispatches capability calls.
// `name_resolver` resolves source_symbol to canonical name (needed by the
// WH-3 capability_import executor for the opaque/bridge lanes).
//
// Returns the session result (status, output, observation data, failure
// kinds), or a human-readable error string on failure.
[[nodiscard]] std::expected<WorkflowSessionResult, std::string>
run_wasm_workflow(std::span<const std::uint8_t> module_bytes,
                  const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
                  const Value &input, WasmRuntimeHooks hooks,
                  ContextualCapabilityInvoker invoker,
                  std::function<std::optional<std::string>(std::uint64_t)>
                      name_resolver);

} // namespace ahfl::runtime::wasm_host
