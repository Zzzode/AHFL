#include "runtime/wasm_host/agent_session.hpp"

#include "runtime/wasm_host/frame_packer.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/core_wire_migration.hpp"

namespace ahfl::runtime::wasm_host {

namespace {

namespace eng = ::ahfl::runtime::core_wasm_resume_engine;
namespace irc = ::ahfl::ir::core;

// Pack the P6-frame input into the engine's live mutable memory. Returns
// nullopt on success, or an error string on failure.
[[nodiscard]] std::optional<std::string>
pack_p6_input_for_walk(Wasm3ResumeEngine &engine,
                       const AgentWalkDescriptor &descriptor, const Value &input) {
    if (!descriptor.is_p6_frame || descriptor.frame_section == nullptr ||
        descriptor.wire_schema == nullptr) {
        return "P6-frame descriptor missing frame_section or wire_schema";
    }

    // Mint the verified table + input binding from the descriptor's wire
    // schema (the same authorities the p6_frame_driver uses).
    auto verified = irc::make_verified_wire_schema_table(*descriptor.wire_schema);
    if (!verified.table.has_value()) {
        return "failed to verify wire schema for P6-frame input";
    }
    std::vector<irc::CoreLowerDiagnostic> diagnostics;
    auto binding = irc::make_frame_binding_from_verified_table(
        *verified.table, {irc::CoreWireFrameRootKind::Input}, diagnostics);
    if (!binding.has_value()) {
        return "failed to mint input binding for P6-frame walk";
    }

    auto page = engine.mutable_whole_memory();
    if (!page.has_value()) {
        return "failed to acquire mutable memory for P6-frame packing";
    }
    auto packed = pack_p6_input(*page, *descriptor.frame_section, *binding, input);
    if (!packed.has_value()) {
        return "failed to pack P6-frame input for step-walk";
    }
    return std::nullopt;
}

} // namespace

std::expected<AgentWalkResult, std::string>
run_agent_step_walk(std::span<const std::uint8_t> module_bytes,
                    const AgentWalkDescriptor &descriptor, const Value &input,
                    std::function<eng::ImportCallback(
                        Wasm3ResumeEngine &)> import_callback_factory,
                    StateEnteredHook hook,
                    AgentId agent_id, std::string_view node_name) {
    // 1. Create the effects-free instance. The import callback factory builds
    //    the callback that serves capability imports; its results drive the
    //    state walk but its events are discarded (the canonical instance
    //    fires the real hooks). The factory is invoked with the engine
    //    reference because the WH-3 capability_import executor needs an
    //    engine& to construct.
    Wasm3ResumeEngine engine;
    auto import_callback = import_callback_factory(engine);
    auto inst = engine.fresh_instance(module_bytes, std::move(import_callback));
    if (!inst.has_value()) {
        return std::unexpected("failed to instantiate effects-free wasm3 instance");
    }

    // 2. Pack the P6-frame input (computed handlers branch on packed input).
    //    WireJson agents need no packing: step() drives the state machine
    //    without the input, which run2 consumes separately.
    if (descriptor.is_p6_frame) {
        if (auto err = pack_p6_input_for_walk(engine, descriptor, input);
            err.has_value()) {
            return std::unexpected(std::move(*err));
        }
    }

    // 3. Verify the initial state matches the descriptor.
    auto initial = engine.invoke_current_state();
    if (!initial.has_value()) {
        return std::unexpected("failed to read initial current_state");
    }
    if (*initial != descriptor.initial_state) {
        return std::unexpected("initial state mismatch");
    }

    // 4. Bounded step-walk, mirroring collectStatesViaStep exactly:
    //    guard = states.size() + 2; walk starts at the initial state; each
    //    step() that changes the state must bump transition_count exactly
    //    once and current_state() must agree with step()'s return.
    AgentWalkResult result;
    if (descriptor.initial_state >= descriptor.states.size()) {
        return std::unexpected("initial_state id out of range");
    }
    result.states.push_back(descriptor.states[descriptor.initial_state]);
    if (hook) {
        hook(agent_id, descriptor.agent_name, node_name,
             descriptor.states[descriptor.initial_state]);
    }

    std::uint32_t previous = descriptor.initial_state;
    auto guard = static_cast<std::int32_t>(descriptor.states.size() + 2);
    bool stabilized = false;

    while (guard-- > 0) {
        auto before = engine.read_transition_count();
        if (!before.has_value()) {
            return std::unexpected("failed to read transition_count before step");
        }
        auto next = engine.invoke_step();
        if (!next.has_value()) {
            return std::unexpected("step() invocation failed");
        }
        if (*next != previous) {
            // Transition: current_state must agree and transition_count must
            // bump exactly once.
            auto current = engine.invoke_current_state();
            if (!current.has_value()) {
                return std::unexpected("failed to read current_state after step");
            }
            if (*current != *next) {
                return std::unexpected("current_state does not match step()");
            }
            auto after = engine.read_transition_count();
            if (!after.has_value()) {
                return std::unexpected("failed to read transition_count after step");
            }
            if (*after != *before + 1) {
                return std::unexpected("transition_count not bumped exactly once");
            }
            if (*next >= descriptor.states.size()) {
                return std::unexpected("step() returned out-of-range state id");
            }
            result.states.push_back(descriptor.states[*next]);
            if (hook) {
                hook(agent_id, descriptor.agent_name, node_name,
                     descriptor.states[*next]);
            }
            previous = *next;
        } else {
            // Stable: the final state does not transition on step().
            auto current = engine.invoke_current_state();
            if (!current.has_value()) {
                return std::unexpected("failed to read current_state at stable");
            }
            if (*current != *next) {
                return std::unexpected("final state is not stable");
            }
            stabilized = true;
            break;
        }
    }

    if (!stabilized) {
        return std::unexpected("step() bounded walk guard expired without a stable final state");
    }

    auto final_count = engine.read_transition_count();
    if (!final_count.has_value()) {
        return std::unexpected("failed to read final transition_count");
    }
    result.transition_count = *final_count;
    return result;
}

} // namespace ahfl::runtime::wasm_host
