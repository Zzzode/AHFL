// RFC 0026 KR6.8 WH-4: KAT for the agent step-walk session.
//
// Drives the step-walk on REAL wasm3 agent modules and pins the walked state
// sequence against the descriptor's state table. Two lanes:
//   1. WireJson agent (e1_identity_agent): no capabilities, no input packing;
//      the walk drives Start -> Done on the effects-free instance.
//   2. P6-frame agent (v2b_computed_string): the input is packed into the
//      fixed P4-D regions before the walk (computed handlers branch on packed
//      input); the walk drives Start -> Done.
//
// Also verifies the state_entered_hook fires per transition (including the
// initial state) and the transition_count is correct.

#include "runtime/wasm_host/agent_session.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"

#include "common/project_input_support.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ir = ahfl::ir;
namespace irc = ahfl::ir::core;
namespace wh = ahfl::runtime::wasm_host;
namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace conf = ahfl::conformance;

using ahfl::runtime::AgentId;
using ahfl::runtime::Value;
using ahfl::runtime::value_from_json;

int g_checks = 0;

void check(bool condition, std::string_view label) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        std::exit(1);
    }
}

// Emit a real wasm AGENT module from a source .ahfl file.
struct EmittedAgent {
    std::vector<std::uint8_t> module_bytes;
    ahfl::backends::CoreWasmExecutionDescriptor descriptor;
};

[[nodiscard]] std::optional<EmittedAgent>
emit_agent(const std::filesystem::path &source_path) {
    std::string error;
    auto program = conf::compile_conformance_source(source_path, error);
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return std::nullopt;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  core lower failed\n";
        for (const auto &d : core.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return std::nullopt;
    }
    const auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  layout failed\n";
        return std::nullopt;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreAgentId{0}, ahfl::backends::WasmProfileKind::Wasi});
    if (!emitted.artifact.has_value()) {
        std::cerr << "  emit failed: no artifact\n";
        for (const auto &d : emitted.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return std::nullopt;
    }
    if (!emitted.descriptor.has_value()) {
        std::cerr << "  emit failed: no descriptor\n";
        return std::nullopt;
    }
    return EmittedAgent{
        .module_bytes = emitted.artifact->bytes,
        .descriptor = std::move(*emitted.descriptor),
    };
}

// A no-op import callback for capability-free agents (never called).
eng::ImportCallbackResult noop_import(const eng::ImportObservation &) {
    return eng::ImportReply{};
}

// ==== 1. WireJson agent step-walk ====

void test_wirejson_agent(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/e1_identity_agent.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "wirejson.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    check(!desc.is_workflow, "wirejson.not_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::WireJson,
          "wirejson.frame_contract");
    check(desc.states.size() == 2, "wirejson.state_count");
    check(desc.initial_state == 1, "wirejson.initial_state"); // Start = id 1

    wh::AgentWalkDescriptor walk_desc;
    walk_desc.agent_name = desc.agent_name;
    walk_desc.states = desc.states;
    walk_desc.initial_state = desc.initial_state;
    walk_desc.is_p6_frame = false;

    std::vector<std::string> hook_states;
    wh::StateEnteredHook hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };

    auto input = value_from_json(R"({"value":"hello"})");
    check(input.has_value(), "wirejson.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wh::run_agent_step_walk(
        emitted->module_bytes, walk_desc, *input, noop_import, hook,
        AgentId{0}, "node");
    check(result.has_value(), "wirejson.step_walk");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The walk should produce: Start -> Done (2 states, 1 transition).
    check(result->states.size() == 2, "wirejson.walk_length");
    check(result->states[0] == "Start", "wirejson.state_0");
    check(result->states[1] == "Done", "wirejson.state_1");
    check(result->transition_count == 1, "wirejson.transition_count");

    // The hook should have fired for both states.
    check(hook_states.size() == 2, "wirejson.hook_count");
    check(hook_states[0] == "Start", "wirejson.hook_0");
    check(hook_states[1] == "Done", "wirejson.hook_1");
}

// ==== 2. P6-frame agent step-walk ====

void test_p6frame_agent(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/v2b_computed_string.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "p6frame.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    check(!desc.is_workflow, "p6frame.not_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "p6frame.frame_contract");
    check(desc.frame_section.has_value(), "p6frame.has_frame_section");
    check(desc.wire_schema.has_value(), "p6frame.has_wire_schema");
    if (!desc.frame_section.has_value() || !desc.wire_schema.has_value()) {
        return;
    }
    check(desc.states.size() == 2, "p6frame.state_count");
    check(desc.initial_state == 1, "p6frame.initial_state"); // Start = id 1

    wh::AgentWalkDescriptor walk_desc;
    walk_desc.agent_name = desc.agent_name;
    walk_desc.states = desc.states;
    walk_desc.initial_state = desc.initial_state;
    walk_desc.is_p6_frame = true;
    walk_desc.frame_section = &*desc.frame_section;
    walk_desc.wire_schema = &*desc.wire_schema;

    std::vector<std::string> hook_states;
    wh::StateEnteredHook hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };

    auto input = value_from_json(R"({"flag":true})");
    check(input.has_value(), "p6frame.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wh::run_agent_step_walk(
        emitted->module_bytes, walk_desc, *input, noop_import, hook,
        AgentId{0}, "node");
    check(result.has_value(), "p6frame.step_walk");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The walk should produce: Start -> Done (2 states, 1 transition).
    check(result->states.size() == 2, "p6frame.walk_length");
    check(result->states[0] == "Start", "p6frame.state_0");
    check(result->states[1] == "Done", "p6frame.state_1");
    check(result->transition_count == 1, "p6frame.transition_count");

    // The hook should have fired for both states.
    check(hook_states.size() == 2, "p6frame.hook_count");
    check(hook_states[0] == "Start", "p6frame.hook_0");
    check(hook_states[1] == "Done", "p6frame.hook_1");
}

// ==== 3. Hook liveness: the hook fires BEFORE the walk returns ====

void test_hook_liveness(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/e1_identity_agent.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "liveness.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    wh::AgentWalkDescriptor walk_desc;
    walk_desc.agent_name = desc.agent_name;
    walk_desc.states = desc.states;
    walk_desc.initial_state = desc.initial_state;
    walk_desc.is_p6_frame = false;

    // The hook mutates a counter; we verify it fired during the walk (not
    // after) by checking the counter is non-zero when the walk returns.
    int hook_fired = 0;
    wh::StateEnteredHook hook =
        [&hook_fired](AgentId, std::string_view, std::string_view,
                      std::string_view) { ++hook_fired; };

    auto input = value_from_json(R"({"value":"test"})");
    check(input.has_value(), "liveness.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wh::run_agent_step_walk(
        emitted->module_bytes, walk_desc, *input, noop_import, hook,
        AgentId{0}, "node");
    check(result.has_value(), "liveness.step_walk");
    if (!result.has_value()) {
        return;
    }
    check(hook_fired == 2, "liveness.hook_fired_during_walk");
}

} // namespace

int main() {
    const auto repo_root = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_wirejson_agent(repo_root);
    test_p6frame_agent(repo_root);
    test_hook_liveness(repo_root);

    std::cout << "agent_session: " << g_checks << " checks passed\n";
    return 0;
}
