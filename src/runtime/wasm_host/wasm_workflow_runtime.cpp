// RFC 0026 KR6.8 WH-4: the wasm3-backed workflow runtime facade implementation.
//
// Thin adapter: converts the shared WasmRuntimeHooks + ContextualCapabilityInvoker
// into a WorkflowSessionConfig and delegates to run_workflow_session.

#include "runtime/wasm_host/wasm_workflow_runtime.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ahfl::runtime::wasm_host {

std::expected<WorkflowSessionResult, std::string>
run_wasm_workflow(std::span<const std::uint8_t> module_bytes,
                  const ahfl::backends::CoreWasmExecutionDescriptor &descriptor,
                  const Value &input, WasmRuntimeHooks hooks,
                  ContextualCapabilityInvoker invoker,
                  std::function<std::optional<std::string>(std::uint64_t)>
                      name_resolver) {
    WorkflowSessionConfig config;
    config.state_entered_hook = std::move(hooks.state_entered_hook);
    config.capability_invoked_hook = std::move(hooks.capability_invoked_hook);
    config.capability_result_observer =
        std::move(hooks.capability_result_observer);
    config.node_completed_hook = std::move(hooks.node_completed_hook);
    config.invoker = std::move(invoker);
    config.name_resolver = std::move(name_resolver);

    return run_workflow_session(module_bytes, descriptor, input,
                                std::move(config));
}

} // namespace ahfl::runtime::wasm_host
