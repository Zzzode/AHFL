// RFC 0026 KR6.8 WH-4 fix-forward D-A: the wasm3-backed agent runner facade
// implementation. One-shot compile + run.

#include "runtime/wasm_runner/wasm_agent_runner.hpp"

#include "compiler/backends/wasm/core_wasm_codegen.hpp"

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"

#include <string>

namespace ahfl::runtime::wasm_runner {

namespace irc = ahfl::ir::core;
namespace bd = ahfl::backends;

std::expected<wasm_host::WasmAgentRunResult, std::string>
run_wasm_agent(const ir::Program &program, std::string_view agent_name,
               const Value &input, WasmAgentRunnerConfig config) {
    // Lower AHFL-IR to Core-IR.
    auto core = irc::lower_ahfl_to_core(program);
    if (!core.ok()) {
        std::string msg = "wasm agent runner: core lowering failed";
        for (const auto &d : core.diagnostics) {
            msg += "\n  [" + d.code + "] " + d.message;
        }
        return std::unexpected(std::move(msg));
    }

    // Compute the wasm32 physical layout side artifact.
    auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::string msg = "wasm agent runner: layout computation failed";
        for (const auto &d : layouts.diagnostics) {
            msg += "\n  [" + d.code + "] " + d.message;
        }
        return std::unexpected(std::move(msg));
    }

    // Find the agent by name.
    std::optional<std::size_t> agent_index;
    for (std::size_t i = 0; i < core.program.agents.size(); ++i) {
        if (core.program.agents[i].name == agent_name) {
            agent_index = i;
            break;
        }
    }
    if (!agent_index.has_value()) {
        return std::unexpected(
            "wasm agent runner: agent '" + std::string(agent_name) +
            "' not found");
    }

    // Emit wasm for the agent.
    auto emitted = bd::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreAgentId{static_cast<std::uint32_t>(*agent_index)},
         bd::WasmProfileKind::Wasi});
    if (!emitted.artifact.has_value() || !emitted.descriptor.has_value()) {
        std::string msg = "wasm agent runner: wasm emission failed for agent '";
        msg += agent_name;
        msg += "'";
        for (const auto &d : emitted.diagnostics) {
            msg += "\n  [" + d.code + "] " + d.message;
        }
        return std::unexpected(std::move(msg));
    }

    // Drive the wasm_host engine-session layer.
    return wasm_host::run_wasm_agent(
        emitted.artifact->bytes, *emitted.descriptor, input,
        std::move(config.hooks), std::move(config.invoker),
        std::move(config.states_invoker));
}

} // namespace ahfl::runtime::wasm_runner
