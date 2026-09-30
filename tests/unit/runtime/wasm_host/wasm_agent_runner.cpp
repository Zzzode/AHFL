// RFC 0026 KR6.8 WH-4: KAT for the wasm3-backed agent runner.
//
// Drives REAL wasm3 AGENT modules end-to-end through run_wasm_agent and pins
// the observation data (states, capabilities, arguments, transition_count),
// the hook-firing discipline (step-walk state_entered on the effects-free
// instance, capability invoked/result on the canonical instance), and the
// result status + output.
//
// Three lanes:
//   1. WireJson identity agent (e1_identity_agent): no capabilities; the
//      step-walk drives Start -> Done and run2 returns the input unchanged.
//   2. P6-frame computed agent (v2b_computed_string): the input is packed
//      into the fixed P4-D regions, runv walks the state machine, and the
//      computed output is read from the authorized output base.
//   3. WireJson capability agent (e2_capability_agent): the Echo capability
//      on the opaque lane; capability_invoked_hook and
//      capability_result_observer fire LIVE at the canonical instance's
//      imports.

#include "runtime/wasm_host/wasm_agent_runner.hpp"
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
namespace conf = ahfl::conformance;

using ahfl::runtime::AgentId;
using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityInvocationContext;
using ahfl::runtime::Value;
using ahfl::runtime::value_from_json;
using ahfl::runtime::value_to_json;

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

// ==== 1. WireJson identity agent (no capabilities) ====

void test_wirejson_identity_agent(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e1_identity_agent.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "wirejson_id.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    check(!desc.is_workflow, "wirejson_id.not_workflow");
    check(desc.frame_contract ==
              ahfl::backends::CoreWasmFrameContract::WireJson,
          "wirejson_id.wirejson");
    check(desc.imports.empty(), "wirejson_id.no_imports");

    auto input = value_from_json(R"({"_type":"wasm::e1_identity::Frame","value":"hello"})");
    check(input.has_value(), "wirejson_id.input");
    if (!input.has_value()) {
        return;
    }

    std::vector<std::string> hook_states;
    wh::WasmRuntimeHooks hooks;
    hooks.state_entered_hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };

    auto invoker = [](const CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_wasm_agent(emitted->module_bytes, desc, *input,
                                     std::move(hooks), std::move(invoker));
    check(result.has_value(), "wirejson_id.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The step-walk should produce: Start -> Done (2 states).
    check(result->states.size() == 2, "wirejson_id.state_count");
    if (result->states.size() == 2) {
        check(result->states[0].state == "Start",
              "wirejson_id.state_0");
        check(result->states[1].state == "Done",
              "wirejson_id.state_1");
    }

    // The hook should have fired for both states.
    check(hook_states.size() == 2, "wirejson_id.hook_count");
    if (hook_states.size() == 2) {
        check(hook_states[0] == "Start", "wirejson_id.hook_0");
        check(hook_states[1] == "Done", "wirejson_id.hook_1");
    }

    // No capabilities were invoked.
    check(result->capabilities.empty(), "wirejson_id.no_capabilities");
    check(result->capability_arguments.empty(),
          "wirejson_id.no_cap_args");
    check(result->capability_failures.empty(),
          "wirejson_id.no_failures");

    // The run completed successfully.
    check(result->result.status() ==
              ahfl::runtime::WorkflowStatus::Completed,
          "wirejson_id.completed");

    // The output should equal the input (identity agent).
    const auto *output = result->result.output();
    check(output != nullptr, "wirejson_id.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e1_identity::Frame","value":"hello"})",
              "wirejson_id.output_value");
    }
}

// ==== 2. P6-frame computed agent ====

void test_p6frame_computed_agent(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/v2b_computed_string.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "p6_computed.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    check(!desc.is_workflow, "p6_computed.not_workflow");
    check(desc.frame_contract ==
              ahfl::backends::CoreWasmFrameContract::P6Frame,
          "p6_computed.p6_frame");
    check(desc.imports.empty(), "p6_computed.no_imports");

    auto input = value_from_json(R"({"_type":"wasm::v2b::computed_string::In","flag":true})");
    check(input.has_value(), "p6_computed.input");
    if (!input.has_value()) {
        return;
    }

    std::vector<std::string> hook_states;
    wh::WasmRuntimeHooks hooks;
    hooks.state_entered_hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };

    auto invoker = [](const CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_wasm_agent(emitted->module_bytes, desc, *input,
                                     std::move(hooks), std::move(invoker));
    check(result.has_value(), "p6_computed.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The step-walk should produce: Start -> Done (2 states).
    check(result->states.size() == 2, "p6_computed.state_count");
    if (result->states.size() == 2) {
        check(result->states[0].state == "Start",
              "p6_computed.state_0");
        check(result->states[1].state == "Done",
              "p6_computed.state_1");
    }

    // The hook should have fired for both states.
    check(hook_states.size() == 2, "p6_computed.hook_count");
    if (hook_states.size() == 2) {
        check(hook_states[0] == "Start", "p6_computed.hook_0");
        check(hook_states[1] == "Done", "p6_computed.hook_1");
    }

    // The run completed successfully.
    check(result->result.status() ==
              ahfl::runtime::WorkflowStatus::Completed,
          "p6_computed.completed");

    // The output should be Out { label: "some", value: 1 } (flag=true).
    const auto *output = result->result.output();
    check(output != nullptr, "p6_computed.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::v2b::computed_string::Out","label":"some","value":1})",
              "p6_computed.output_value");
    }
}

// ==== 3. WireJson capability agent (Echo, opaque lane) ====

void test_wirejson_capability_agent(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e2_capability_agent.ahfl";
    auto emitted = emit_agent(source);
    check(emitted.has_value(), "wirejson_cap.emit");
    if (!emitted.has_value()) {
        return;
    }

    const auto &desc = emitted->descriptor;
    check(!desc.is_workflow, "wirejson_cap.not_workflow");
    check(desc.frame_contract ==
              ahfl::backends::CoreWasmFrameContract::WireJson,
          "wirejson_cap.wirejson");
    check(desc.imports.size() == 1, "wirejson_cap.one_import");
    if (desc.imports.empty()) {
        return;
    }
    // WH-4 fix-forward D-C: the hook and invoker receive the FULLY-QUALIFIED
    // canonical capability name (no local-name stripping). The descriptor's
    // imports table carries it; the assertions below compare against it.
    const auto &echo_name = desc.imports[0].canonical_name;
    check(echo_name == "wasm::e2_capability::Echo",
          "wirejson_cap.echo_name");

    auto input = value_from_json(
        R"({"_type":"wasm::e2_capability::InputFrame","value":"hello"})");
    check(input.has_value(), "wirejson_cap.input");
    if (!input.has_value()) {
        return;
    }

    // Track hook firings.
    int cap_invoked_count = 0;
    int cap_result_count = 0;
    std::vector<std::string> hook_states;

    wh::WasmRuntimeHooks hooks;
    hooks.state_entered_hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };
    hooks.capability_invoked_hook =
        [&cap_invoked_count, echo_name](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == echo_name, "wirejson_cap.hook_cap_name");
        };
    hooks.capability_result_observer =
        [&cap_result_count](const CapabilityInvocationContext &,
                            const CapabilityCallResult &) {
            ++cap_result_count;
        };

    // Echo mock: returns an OutputFrame with the same `value` field as the
    // InputFrame. The WH-3 executor validates the result against the
    // OutputFrame schema binding, so the type tag must match (the old
    // simplified handler skipped schema-bound validation).
    auto invoker = [](const CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<Value> &args) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        if (!args.empty()) {
            auto cloned = ahfl::runtime::clone_value(args[0]);
            if (auto *sv = std::get_if<ahfl::runtime::StructValue>(&cloned.node)) {
                sv->type_name = "wasm::e2_capability::OutputFrame";
            }
            r.value = std::move(cloned);
        }
        return r;
    };

    auto result = wh::run_wasm_agent(emitted->module_bytes, desc, *input,
                                     std::move(hooks), std::move(invoker));
    check(result.has_value(), "wirejson_cap.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The step-walk should produce: Start -> Done (2 states).
    check(result->states.size() == 2, "wirejson_cap.state_count");
    if (result->states.size() == 2) {
        check(result->states[0].state == "Start",
              "wirejson_cap.state_0");
        check(result->states[1].state == "Done",
              "wirejson_cap.state_1");
    }

    // The Echo capability was invoked exactly once.
    check(cap_invoked_count == 1, "wirejson_cap.cap_invoked_count");
    check(cap_result_count == 1, "wirejson_cap.cap_result_count");
    check(result->capabilities.size() == 1,
          "wirejson_cap.capabilities_size");
    if (!result->capabilities.empty()) {
        check(result->capabilities[0] == echo_name,
              "wirejson_cap.capabilities[0]");
    }

    // No failures.
    check(result->capability_failures.empty(),
          "wirejson_cap.no_failures");

    // The run completed successfully.
    check(result->result.status() ==
              ahfl::runtime::WorkflowStatus::Completed,
          "wirejson_cap.completed");

    // The output should be the OutputFrame-typed echo of the input value
    // (the agent's output type is OutputFrame; the WH-3 executor validates
    // the mock's result against the OutputFrame schema binding).
    const auto *output = result->result.output();
    check(output != nullptr, "wirejson_cap.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e2_capability::OutputFrame","value":"hello"})",
              "wirejson_cap.output_value");
    }
}

} // namespace

int main() {
    const auto repo_root =
        ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_wirejson_identity_agent(repo_root);
    test_p6frame_computed_agent(repo_root);
    test_wirejson_capability_agent(repo_root);

    std::cout << "wasm_agent_runner: " << g_checks << " checks passed\n";
    return 0;
}
