// RFC 0026 KR6.8 WH-4: KAT for the workflow session.
//
// Drives REAL wasm3 workflow modules end-to-end through the workflow session
// and pins the observation data (states, capabilities, arguments, counts),
// the hook-firing discipline (import-boundary state_entered, capability
// invoked/result, post-run node_completed), and the result status + output.
//
// Two lanes:
//   1. P6-frame workflow (wh4_trace_workflow): no capabilities; the trace ring
//      is the sole state-sequence evidence. Verifies state_entered_hook fires
//      for the full Decide -> High -> Done walk and the output is the computed
//      Frame.
//   2. WireJson capability workflow (e3_capability_workflow): the Echo
//      capability on the opaque lane. Verifies capability_invoked_hook,
//      capability_result_observer, capability_arguments collection, and
//      node_completed_hook for both nodes.

#include "runtime/wasm_host/workflow_session.hpp"
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

// Emit a real wasm WORKFLOW module from a source .ahfl file.
struct EmittedWorkflow {
    std::vector<std::uint8_t> module_bytes;
    ahfl::backends::CoreWasmExecutionDescriptor descriptor;
};

[[nodiscard]] std::optional<EmittedWorkflow>
emit_workflow(const std::filesystem::path &source_path) {
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
        {irc::CoreWorkflowId{0}, ahfl::backends::WasmProfileKind::Wasi});
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
    return EmittedWorkflow{
        .module_bytes = emitted.artifact->bytes,
        .descriptor = std::move(*emitted.descriptor),
    };
}

// ==== 1. P6-frame workflow (trace ring, no capabilities) ====

void test_p6_trace_workflow(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/wh4_trace_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "p6_trace.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "p6_trace.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "p6_trace.p6_frame");
    check(desc.workflow_node_count == 1, "p6_trace.one_node");
    check(desc.imports.empty(), "p6_trace.no_imports");

    auto input = value_from_json(R"({"_type":"wasm::wh4_trace::Frame","n":1})");
    check(input.has_value(), "p6_trace.input");
    if (!input.has_value()) {
        return;
    }

    std::vector<std::string> hook_states;
    wh::WorkflowSessionConfig config;
    config.state_entered_hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };
    config.invoker = [](const CapabilityInvocationContext &, const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, desc, *input,
                                           std::move(config));
    check(result.has_value(), "p6_trace.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The trace ring should produce: Decide -> High -> Done (3 states).
    check(result->states.size() == 3, "p6_trace.state_count");
    if (result->states.size() == 3) {
        check(result->states[0].state == "Decide", "p6_trace.state_0");
        check(result->states[1].state == "High", "p6_trace.state_1");
        check(result->states[2].state == "Done", "p6_trace.state_2");
    }

    // The hook should have fired for all 3 states.
    check(hook_states.size() == 3, "p6_trace.hook_count");
    if (hook_states.size() == 3) {
        check(hook_states[0] == "Decide", "p6_trace.hook_0");
        check(hook_states[1] == "High", "p6_trace.hook_1");
        check(hook_states[2] == "Done", "p6_trace.hook_2");
    }

    // No capabilities were invoked.
    check(result->capabilities.empty(), "p6_trace.no_capabilities");
    check(result->capability_arguments.empty(), "p6_trace.no_cap_args");
    check(result->capability_failures.empty(), "p6_trace.no_failures");

    // The workflow completed successfully.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "p6_trace.completed");

    // The output should be Frame { n: 1 }.
    const auto *output = result->result.output();
    check(output != nullptr, "p6_trace.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == R"({"_type":"wasm::wh4_trace::Frame","n":1})",
              "p6_trace.output_value");
    }

    // workflow_completed_count should be 1 (one node).
    check(result->workflow_completed_count == 1,
          "p6_trace.completed_count");
}

// ==== 2. WireJson capability workflow (Echo, opaque lane) ====

void test_wirejson_capability_workflow(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wirejson_cap.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "wirejson_cap.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::WireJson,
          "wirejson_cap.wirejson");
    check(desc.workflow_node_count == 2, "wirejson_cap.two_nodes");
    check(desc.imports.size() == 1, "wirejson_cap.one_import");
    if (desc.imports.empty()) {
        return;
    }
    // The canonical name may include the module prefix; check it ends with
    // "Echo" rather than exact equality.
    const auto &echo_name = desc.imports[0].canonical_name;
    check(echo_name.size() >= 4 &&
              echo_name.substr(echo_name.size() - 4) == "Echo",
          "wirejson_cap.echo_name");

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})");
    check(input.has_value(), "wirejson_cap.input");
    if (!input.has_value()) {
        return;
    }

    // Track hook firings.
    int cap_invoked_count = 0;
    int cap_result_count = 0;
    int node_completed_count = 0;
    std::vector<std::string> completed_nodes;

    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "wirejson_cap.hook_cap_name");
        };
    config.capability_result_observer =
        [&cap_result_count](const CapabilityInvocationContext &,
                            const CapabilityCallResult &) {
            ++cap_result_count;
        };
    config.node_completed_hook =
        [&node_completed_count, &completed_nodes](
            AgentId, std::string_view node_name, const Value &) {
            ++node_completed_count;
            completed_nodes.emplace_back(node_name);
        };

    // Echo mock: returns the input unchanged.
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &args) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        if (!args.empty()) {
            r.value = ahfl::runtime::clone_value(args[0]);
        }
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, desc, *input,
                                           std::move(config));
    check(result.has_value(), "wirejson_cap.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The Echo capability was invoked exactly once.
    check(cap_invoked_count == 1, "wirejson_cap.cap_invoked_count");
    check(cap_result_count == 1, "wirejson_cap.cap_result_count");
    check(result->capabilities.size() == 1, "wirejson_cap.capabilities_size");
    if (!result->capabilities.empty()) {
        check(result->capabilities[0] == "Echo",
              "wirejson_cap.capabilities[0]");
    }

    // The capability argument envelope should be the bare struct JSON.
    check(result->capability_arguments.size() == 1,
          "wirejson_cap.cap_args_size");
    if (!result->capability_arguments.empty()) {
        check(result->capability_arguments[0] ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})",
              "wirejson_cap.cap_args[0]");
    }

    // No failures.
    check(result->capability_failures.empty(),
          "wirejson_cap.no_failures");

    // node_completed_hook should have fired for both nodes.
    check(node_completed_count == 2, "wirejson_cap.node_completed_count");

    // The workflow completed successfully.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "wirejson_cap.completed");

    // The output should equal the input (Echo echoes, SecondAgent is identity).
    const auto *output = result->result.output();
    check(output != nullptr, "wirejson_cap.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})",
              "wirejson_cap.output_value");
    }

    // workflow_completed_count should be 2 (two nodes).
    check(result->workflow_completed_count == 2,
          "wirejson_cap.completed_count");
}

// ==== 3. P6-frame workflow with n=0 (Low branch) ====

void test_p6_trace_low_branch(const std::filesystem::path &repo_root) {
    const auto source = repo_root / "tests/golden/wasm/wh4_trace_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "p6_low.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(R"({"_type":"wasm::wh4_trace::Frame","n":0})");
    check(input.has_value(), "p6_low.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &, const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "p6_low.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The trace ring should produce: Decide -> Low -> Done (3 states).
    check(result->states.size() == 3, "p6_low.state_count");
    if (result->states.size() == 3) {
        check(result->states[0].state == "Decide", "p6_low.state_0");
        check(result->states[1].state == "Low", "p6_low.state_1");
        check(result->states[2].state == "Done", "p6_low.state_2");
    }

    // The output should be Frame { n: 0 }.
    const auto *output = result->result.output();
    check(output != nullptr, "p6_low.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == R"({"_type":"wasm::wh4_trace::Frame","n":0})",
              "p6_low.output_value");
    }
}

} // namespace

int main() {
    const auto repo_root = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_p6_trace_workflow(repo_root);
    test_wirejson_capability_workflow(repo_root);
    test_p6_trace_low_branch(repo_root);

    std::cout << "workflow_session: " << g_checks << " checks passed\n";
    return 0;
}
