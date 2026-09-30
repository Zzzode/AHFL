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
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/execution_report.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"

#include "common/project_input_support.hpp"
#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
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

// P1-2: return the event type name for sequence assertions. Only the types
// the wasm lane emits are listed; any other type is "Other".
[[nodiscard]] std::string_view
event_type_name(const ahfl::runtime::ExecutionEventPayload &p) {
    return std::visit(
        [](const auto &e) -> std::string_view {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ahfl::runtime::RunStarted>)
                return "RunStarted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::WorkflowStarted>)
                return "WorkflowStarted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::NodeScheduled>)
                return "NodeScheduled";
            else if constexpr (std::is_same_v<T, ahfl::runtime::NodeStarted>)
                return "NodeStarted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::AgentStateEntered>)
                return "AgentStateEntered";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::CapabilityStarted>)
                return "CapabilityStarted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::CapabilityCompleted>)
                return "CapabilityCompleted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::NodeCompleted>)
                return "NodeCompleted";
            else if constexpr (std::is_same_v<T, ahfl::runtime::NodeFailed>)
                return "NodeFailed";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::NodeSkipped>)
                return "NodeSkipped";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::WorkflowCompleted>)
                return "WorkflowCompleted";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::WorkflowFailed>)
                return "WorkflowFailed";
            else if constexpr (std::is_same_v<
                                   T, ahfl::runtime::RunCompleted>)
                return "RunCompleted";
            else
                return "Other";
        },
        p);
}

// P1-2: assert the exact event variant-type sequence (and order) emitted by
// the wasm lane. The wasm lane emits a fixed subset of the 24 payload types.
void check_event_sequence(const ahfl::runtime::WorkflowResult &result,
                          std::span<const std::string_view> expected,
                          std::string_view label) {
    const auto events = result.events.events();
    check(events.size() == expected.size(),
          std::string(label) + ": event count " +
              std::to_string(events.size()) + " != expected " +
              std::to_string(expected.size()));
    if (events.size() != expected.size()) {
        std::cerr << "  actual sequence:\n";
        for (std::size_t i = 0; i < events.size(); ++i) {
            std::cerr << "    [" << i << "] "
                      << event_type_name(events[i].payload) << "\n";
        }
        return;
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto name = event_type_name(events[i].payload);
        check(name == expected[i],
              std::string(label) + ": event[" + std::to_string(i) + "] " +
                  std::string(name) + " != expected " +
                  std::string(expected[i]));
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
    std::vector<std::string> hook_node_names;
    wh::WorkflowSessionConfig config;
    config.state_entered_hook =
        [&hook_states, &hook_node_names](AgentId, std::string_view,
                                         std::string_view node_name,
                                         std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
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

    // P2-7: the hook must receive the real node name (not empty).
    check(hook_node_names.size() == 3, "p6_trace.hook_node_count");
    for (const auto &name : hook_node_names) {
        check(name == "route", "p6_trace.hook_node_name");
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
    std::vector<std::string> hook_states;
    std::vector<std::string> hook_node_names;

    wh::WorkflowSessionConfig config;
    config.state_entered_hook =
        [&hook_states, &hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
        };
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId agent_id, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "wirejson_cap.hook_cap_name");
            // P1-6: the Echo capability is invoked by the `first` node,
            // whose runner is agent 0 (FirstAgent).
            check(agent_id.index() == 0, "wirejson_cap.hook_agent_id");
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
    // P1-6: the hook receives the REAL node names (not agent names).
    check(completed_nodes.size() == 2, "wirejson_cap.completed_nodes_size");
    if (completed_nodes.size() == 2) {
        check(completed_nodes[0] == "first",
              "wirejson_cap.completed_nodes[0]");
        check(completed_nodes[1] == "second",
              "wirejson_cap.completed_nodes[1]");
    }

    // D-B + P1-6: the WireJson state reconstruction fires state_entered_hook
    // post-run with the REAL node names. Each node walks Start -> Done.
    check(hook_states.size() == 4, "wirejson_cap.hook_states_count");
    if (hook_states.size() == 4) {
        check(hook_states[0] == "Start", "wirejson_cap.hook_state_0");
        check(hook_states[1] == "Done", "wirejson_cap.hook_state_1");
        check(hook_states[2] == "Start", "wirejson_cap.hook_state_2");
        check(hook_states[3] == "Done", "wirejson_cap.hook_state_3");
    }
    check(hook_node_names.size() == 4,
          "wirejson_cap.hook_node_names_count");
    if (hook_node_names.size() == 4) {
        check(hook_node_names[0] == "first",
              "wirejson_cap.hook_node_name_0");
        check(hook_node_names[1] == "first",
              "wirejson_cap.hook_node_name_1");
        check(hook_node_names[2] == "second",
              "wirejson_cap.hook_node_name_2");
        check(hook_node_names[3] == "second",
              "wirejson_cap.hook_node_name_3");
    }

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

// ==== 4. P1-2: event variant sequence + order on linear workflow ====

void test_event_sequence_identity(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_identity_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "seq_identity.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})");
    check(input.has_value(), "seq_identity.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "seq_identity.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // P1-2: the wasm lane emits the SAME event variant sequence + order as
    // the evaluator: RunStarted, WorkflowStarted, NodeScheduled (all, in
    // schedule order), per node: NodeStarted/AgentStateEntered/NodeCompleted,
    // WorkflowCompleted, RunCompleted.
    static constexpr std::string_view expected[] = {
        "RunStarted",
        "WorkflowStarted",
        "NodeScheduled", // first, deps=[], slot=0
        "NodeScheduled", // second, deps=[first], slot=1
        "NodeStarted",   // first
        "AgentStateEntered", // first: Start
        "AgentStateEntered", // first: Done
        "NodeCompleted",  // first
        "NodeStarted",    // second
        "AgentStateEntered", // second: Start
        "AgentStateEntered", // second: Done
        "NodeCompleted",  // second
        "WorkflowCompleted",
        "RunCompleted",
    };
    check_event_sequence(result->result, expected, "seq_identity");
}

// ==== 5. P1-2/P1-3: event sequence + report fields on capability workflow ====

void test_event_sequence_and_report_fields(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "seq_cap.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "seq_cap.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
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
    // P1-3: node_completed_hook must be set so the node-event buffer is
    // decoded and node outputs are populated (WireJson: NoneValue, the
    // documented observability gap; P6: the actual decoded output).
    config.node_completed_hook =
        [](AgentId, std::string_view, const Value &) {};

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "seq_cap.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // P1-2: same event sequence as the evaluator, including
    // CapabilityStarted/CapabilityCompleted for node 0 (which calls Echo).
    static constexpr std::string_view expected[] = {
        "RunStarted",
        "WorkflowStarted",
        "NodeScheduled",
        "NodeScheduled",
        "NodeStarted",
        "AgentStateEntered",
        "AgentStateEntered",
        "CapabilityStarted",
        "CapabilityCompleted",
        "NodeCompleted",
        "NodeStarted",
        "AgentStateEntered",
        "AgentStateEntered",
        "NodeCompleted",
        "WorkflowCompleted",
        "RunCompleted",
    };
    check_event_sequence(result->result, expected, "seq_cap");

    // P1-3: assert all ExecutionNodeReport fields.
    const auto &report = result->result.report;
    check(report.status == ahfl::runtime::RunTerminalStatus::Completed,
          "seq_cap.report_completed");
    check(report.nodes.size() == 2, "seq_cap.report_node_count");
    if (report.nodes.size() == 2) {
        // Node 0: first, execution_slot=0, no dependencies, has output.
        const auto &n0 = report.nodes[0];
        check(n0.status == ahfl::runtime::NodeReportStatus::Completed,
              "seq_cap.n0_status");
        check(n0.execution_slot == 0, "seq_cap.n0_slot");
        check(n0.dependencies.empty(), "seq_cap.n0_deps");
        check(n0.output.has_value(), "seq_cap.n0_has_output");
        check(n0.agent != ahfl::runtime::AgentId{}, "seq_cap.n0_has_agent");

        // Node 1: second, execution_slot=1, depends on first, has output.
        const auto &n1 = report.nodes[1];
        check(n1.status == ahfl::runtime::NodeReportStatus::Completed,
              "seq_cap.n1_status");
        check(n1.execution_slot == 1, "seq_cap.n1_slot");
        check(n1.dependencies.size() == 1, "seq_cap.n1_deps_count");
        if (n1.dependencies.size() == 1) {
            check(n1.dependencies[0] == n0.node,
                  "seq_cap.n1_deps[0]_is_n0");
        }
        check(n1.output.has_value(), "seq_cap.n1_has_output");
        check(n1.agent != ahfl::runtime::AgentId{}, "seq_cap.n1_has_agent");
        // The two nodes have DIFFERENT agents (FirstAgent vs SecondAgent).
        check(n0.agent != n1.agent, "seq_cap.agents_differ");
    }
    // The workflow output is the second node's output.
    check(report.output.has_value(), "seq_cap.report_has_output");
}

// ==== 6. P1-4: host-abort on workflow lane (wrong capability name) ====

void test_host_abort_workflow(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "host_abort_wf.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "host_abort_wf.input");
    if (!input.has_value()) {
        return;
    }

    // Resolve the capability to a name NOT in the wire schema. The
    // capability_import executor sets last_error and host-aborts.
    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "WrongCapability";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    // P1-4: host-abort maps to a FAILED WorkflowResult, NOT std::unexpected.
    check(result.has_value(), "host_abort_wf.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "host_abort_wf.node_failed");

    // The report should show the first node Failed, second Skipped.
    const auto &report = result->result.report;
    check(report.status == ahfl::runtime::RunTerminalStatus::Failed,
          "host_abort_wf.report_failed");
    check(report.nodes.size() == 2, "host_abort_wf.report_node_count");
    if (report.nodes.size() == 2) {
        check(report.nodes[0].status == ahfl::runtime::NodeReportStatus::Failed,
              "host_abort_wf.n0_failed");
        check(report.nodes[1].status ==
                  ahfl::runtime::NodeReportStatus::Skipped,
              "host_abort_wf.n1_skipped");
    }

    // P1-4: the diagnostic code is wasm.host-abort.
    bool found_host_abort = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == "wasm.host-abort") {
            found_host_abort = true;
            // P2-1: the message carries the CapabilityImportError ENUM NAME.
            check(diag.message.find("CapabilityImportError=") !=
                      std::string::npos,
                  "host_abort_wf.enum_name_in_message");
            // The enum name should be a known identifier, not a raw integer.
            check(diag.message.find("CapabilityImportError=0") ==
                      std::string::npos,
                  "host_abort_wf.no_raw_int");
            break;
        }
    }
    check(found_host_abort, "host_abort_wf.found_diagnostic");
}

// ==== 7. P1-4: trap on workflow lane (divide by zero) ====

void test_trap_workflow(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh4_trap_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "trap_wf.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(R"({"_type":"wasm::wh4_trap_workflow::Frame","n":1})");
    check(input.has_value(), "trap_wf.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    // P1-4: trap maps to a FAILED WorkflowResult with wasm.trap, NOT
    // std::unexpected (which is reserved for pre-run setup failures).
    check(result.has_value(), "trap_wf.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "trap_wf.node_failed");

    const auto &report = result->result.report;
    check(report.status == ahfl::runtime::RunTerminalStatus::Failed,
          "trap_wf.report_failed");

    // P2-1: per-node terminal. The single node trapped, so it MUST be
    // Failed (not Completed). The old identity-workflow fallback marked a
    // node with states as Completed even when it trapped.
    check(report.nodes.size() == 1, "trap_wf.report_node_count");
    if (report.nodes.size() == 1) {
        check(report.nodes[0].status ==
                  ahfl::runtime::NodeReportStatus::Failed,
              "trap_wf.n0_failed");
    }

    // P1-4: the diagnostic code is wasm.trap.
    bool found_trap = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == "wasm.trap") {
            found_trap = true;
            break;
        }
    }
    check(found_trap, "trap_wf.found_diagnostic");
}

// ==== 7b. P2-1: trap on a multi-node identity workflow (per-node Failed +
//         Skipped successors) ====

void test_trap_pipeline(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh4_trap_pipeline.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "trap_pipe.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input =
        value_from_json(R"({"_type":"wasm::wh4_trap_pipeline::Frame","n":1})");
    check(input.has_value(), "trap_pipe.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "trap_pipe.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "trap_pipe.node_failed");

    const auto &report = result->result.report;
    check(report.status == ahfl::runtime::RunTerminalStatus::Failed,
          "trap_pipe.report_failed");

    // P2-1: the first node Completed, the second (trapping) node Failed,
    // the third node Skipped (never executed because its dependency
    // failed). This is the evaluator's per-node terminal semantics.
    check(report.nodes.size() == 3, "trap_pipe.report_node_count");
    if (report.nodes.size() == 3) {
        check(report.nodes[0].status ==
                  ahfl::runtime::NodeReportStatus::Completed,
              "trap_pipe.n0_completed");
        check(report.nodes[1].status ==
                  ahfl::runtime::NodeReportStatus::Failed,
              "trap_pipe.n1_failed");
        check(report.nodes[2].status ==
                  ahfl::runtime::NodeReportStatus::Skipped,
              "trap_pipe.n2_skipped");
    }

    // The trapping node's diagnostic is wasm.trap.
    bool found_trap = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == "wasm.trap") {
            found_trap = true;
            break;
        }
    }
    check(found_trap, "trap_pipe.found_diagnostic");
}

// ==== 7c. P2-2: corrupted WireJson output fails closed ====

void test_corrupted_output_workflow() {
    namespace whts = ahfl::runtime::wasm_host_test_support;
    // A hand-built module whose run2 reports success but returns a pointer
    // to invalid JSON bytes.
    const auto module_bytes = whts::corrupted_output_module();

    // Minimal 1-node WireJson identity-workflow descriptor.
    ahfl::backends::CoreWasmExecutionDescriptor descriptor;
    descriptor.is_workflow = true;
    descriptor.frame_contract =
        ahfl::backends::CoreWasmFrameContract::WireJson;
    descriptor.workflow_name = "corrupt_output";
    descriptor.workflow_node_count = 1;
    descriptor.agents.push_back(
        ahfl::backends::CoreWasmStateWalk{
            .agent = "A", .walk = {"Done"}, .all_states = {"Done"}});
    ahfl::backends::CoreWasmNodeDescriptor node_desc;
    node_desc.node_id = 0;
    node_desc.schedule_pos = 0;
    node_desc.runner = 0;
    node_desc.name = "n0";
    descriptor.nodes.push_back(std::move(node_desc));

    auto input = value_from_json("42");
    check(input.has_value(), "corrupt_out.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(module_bytes, descriptor, *input,
                                           std::move(config));
    // The session itself ran (no pre-run setup failure); the run must be
    // Failed, not Completed-with-null.
    check(result.has_value(), "corrupt_out.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::EvalError,
          "corrupt_out.eval_error");

    const auto &report = result->result.report;
    check(report.status == ahfl::runtime::RunTerminalStatus::Failed,
          "corrupt_out.report_failed");

    // The diagnostic code is wasm.output-decode-failed.
    bool found_decode_failed = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() &&
            *diag.code == "wasm.output-decode-failed") {
            found_decode_failed = true;
            break;
        }
    }
    check(found_decode_failed, "corrupt_out.found_diagnostic");

    // The output must NOT be present (never Completed-with-null).
    check(!report.output.has_value(), "corrupt_out.no_output");
}

// ==== 8. P1-5/P2-4: workflow_completed_count fail-closed ====

void test_completed_count_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_identity_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "count_fc.emit");
    if (!wf.has_value()) {
        return;
    }

    // Corrupt the descriptor: claim fewer nodes than the module actually
    // has. The module will complete 2 nodes, but the descriptor says 1.
    // The P2-4 cross-check must fail closed.
    auto corrupted_desc = wf->descriptor;
    corrupted_desc.workflow_node_count = 1;

    auto input = value_from_json(
        R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})");
    check(input.has_value(), "count_fc.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto result = wh::run_workflow_session(wf->module_bytes, corrupted_desc,
                                           *input, std::move(config));
    // P2-4: the session must fail closed (std::unexpected) when
    // workflow_completed_count disagrees with descriptor.workflow_node_count.
    check(!result.has_value(), "count_fc.fail_closed");
    if (result.has_value()) {
        std::cerr << "  unexpected success with corrupted descriptor\n";
    }
}

// ==== 9. P1-5/P2-3: node-event capability kind fail-closed ====

void test_capability_kind_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "cap_fc.emit");
    if (!wf.has_value()) {
        return;
    }

    // Corrupt the descriptor: claim the first node has NO capability, but
    // the module actually invokes Echo on it. The P2-3 cross-check must
    // fail closed when the node-event record says Capability but the
    // descriptor says no capability.
    auto corrupted_desc = wf->descriptor;
    if (!corrupted_desc.nodes.empty()) {
        corrupted_desc.nodes[0].has_capability = false;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "cap_fc.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
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

    auto result = wh::run_workflow_session(wf->module_bytes, corrupted_desc,
                                           *input, std::move(config));
    // P2-3: the session must fail closed (std::unexpected) when the
    // node-event record's capability kind disagrees with the descriptor.
    check(!result.has_value(), "cap_fc.fail_closed");
    if (result.has_value()) {
        std::cerr << "  unexpected success with corrupted descriptor\n";
    }
}

// ==== 10. P2-10: capability_ordinal out-of-range fail-closed ====

void test_capability_ordinal_oor_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "ord_oor.emit");
    if (!wf.has_value()) {
        return;
    }

    // Corrupt the descriptor: set the first node's capability ordinal beyond
    // the imports table. The P2-10 production check must fail closed.
    auto corrupted_desc = wf->descriptor;
    if (!corrupted_desc.nodes.empty()) {
        auto &node = corrupted_desc.nodes[0];
        if (!node.all_capabilities.empty()) {
            // P2-8 multi-capability path: corrupt the first ordinal.
            node.all_capabilities[0].first =
                static_cast<std::uint32_t>(corrupted_desc.imports.size() + 1);
        } else {
            // Legacy single-capability path.
            node.capability_ordinal =
                static_cast<std::uint32_t>(corrupted_desc.imports.size() + 1);
        }
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "ord_oor.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
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

    auto result = wh::run_workflow_session(wf->module_bytes, corrupted_desc,
                                           *input, std::move(config));
    // P2-10: the session must fail closed when capability_ordinal is out of
    // range for the imports table.
    check(!result.has_value(), "ord_oor.fail_closed");
    if (result.has_value()) {
        std::cerr << "  unexpected success with corrupted ordinal\n";
    }
}

// ==== WH-5b.1: hybrid P6 + opaque capability workflow (P6-before-cap) ====
//
// Emission/descriptor-only: the control-plane contract (P6-only dense
// node_blocks, per-node is_p6 + p6_block_ordinal, tag-0 identity event
// records for P6 nodes in capability-bearing mixed modules). Runtime
// execution of hybrid workflows is WH-5b.3 scope (host transcode).

void test_hybrid_p6_before_cap(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_before_cap.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_p6cap.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_p6cap.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "hybrid_p6cap.p6_frame");
    check(desc.workflow_node_count == 2, "hybrid_p6cap.two_nodes");
    check(desc.imports.size() == 1, "hybrid_p6cap.one_import");

    // Per-node is_p6 flags: node 0 (compute) is P6, node 1 (echo) is opaque.
    check(desc.nodes.size() == 2, "hybrid_p6cap.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].is_p6, "hybrid_p6cap.node0_is_p6");
        check(!desc.nodes[1].is_p6, "hybrid_p6cap.node1_is_opaque");
        // The P6 node's dense node_blocks ordinal is 0 (only P6 runner).
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_p6cap.node0_p6_ordinal");
    }

    // P6-only dense node_blocks: exactly 1 entry (the P6 runner).
    check(desc.frame_section.has_value(), "hybrid_p6cap.frame_section");
    if (desc.frame_section.has_value()) {
        check(desc.frame_section->node_blocks.size() == 1,
              "hybrid_p6cap.node_blocks_count");
    }
    check(desc.wire_schema.has_value(), "hybrid_p6cap.wire_schema");
}

// ==== WH-5b.1: hybrid P6 + opaque capability workflow (cap-before-P6) ====

void test_hybrid_cap_before_p6(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_cap_before_p6.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_capp6.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_capp6.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "hybrid_capp6.p6_frame");
    check(desc.workflow_node_count == 2, "hybrid_capp6.two_nodes");
    check(desc.imports.size() == 1, "hybrid_capp6.one_import");

    // Per-node is_p6 flags: node 0 (echo) is opaque, node 1 (compute) is P6.
    check(desc.nodes.size() == 2, "hybrid_capp6.nodes_size");
    if (desc.nodes.size() == 2) {
        check(!desc.nodes[0].is_p6, "hybrid_capp6.node0_is_opaque");
        check(desc.nodes[1].is_p6, "hybrid_capp6.node1_is_p6");
        check(desc.nodes[1].p6_block_ordinal == 0,
              "hybrid_capp6.node1_p6_ordinal");
    }

    check(desc.frame_section.has_value(), "hybrid_capp6.frame_section");
    if (desc.frame_section.has_value()) {
        check(desc.frame_section->node_blocks.size() == 1,
              "hybrid_capp6.node_blocks_count");
    }
    check(desc.wire_schema.has_value(), "hybrid_capp6.wire_schema");
}

// ==== WH-5b.1: hybrid 3-node Kahn-reordered workflow ====

void test_hybrid_kahn_reordered(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_kahn_reordered.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_kahn.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_kahn.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "hybrid_kahn.p6_frame");
    check(desc.workflow_node_count == 3, "hybrid_kahn.three_nodes");
    check(desc.imports.size() == 1, "hybrid_kahn.one_import");

    // Kahn schedule is [echo1, compute, echo2]: opaque, P6, opaque.
    // The declaration order is [echo2, compute, echo1]; the `after` edges
    // force echo1 first. Verify both the lane flags and the schedule order.
    check(desc.nodes.size() == 3, "hybrid_kahn.nodes_size");
    if (desc.nodes.size() == 3) {
        check(desc.nodes[0].name == "echo1", "hybrid_kahn.node0_name");
        check(!desc.nodes[0].is_p6, "hybrid_kahn.node0_is_opaque");
        check(desc.nodes[1].name == "compute", "hybrid_kahn.node1_name");
        check(desc.nodes[1].is_p6, "hybrid_kahn.node1_is_p6");
        check(desc.nodes[1].p6_block_ordinal == 0,
              "hybrid_kahn.node1_p6_ordinal");
        check(desc.nodes[2].name == "echo2", "hybrid_kahn.node2_name");
        check(!desc.nodes[2].is_p6, "hybrid_kahn.node2_is_opaque");
    }

    check(desc.frame_section.has_value(), "hybrid_kahn.frame_section");
    if (desc.frame_section.has_value()) {
        check(desc.frame_section->node_blocks.size() == 1,
              "hybrid_kahn.node_blocks_count");
    }
    check(desc.wire_schema.has_value(), "hybrid_kahn.wire_schema");
}

// ==== WH-5b.1 fix-forward: hybrid P6-bridge + opaque cap manifest test ====
//
// Emission/descriptor-only: the exec-manifest references ALL imported
// capabilities for every node (including in-handler bridge imports of P6
// nodes), because the manifest cap set doubles as the A2 import/admission
// authority and run_workflow_session admits unconditionally through it.
// Both P6 bridge nodes and the opaque cap node therefore encode
// cap_call_count 1. Also covers the multi-P6 ordinal case (two P6 runners
// -> p6_block_ordinal 0 and 1, node_blocks count == 2).
//
// KNOWN latent gap (WH-5b.2): a bridge P6 node writes a tag-0 identity
// event record (zero capability fields) yet its manifest cap byte counts
// its in-handler bridge import. This tag-0-vs-cap-byte disagreement is a
// resume-coordinate inconsistency owned by WH-5b.2 (separate in-runner
// site table + admission-set semantics + resume classifier, design
// 12.12.5/12.12.10). The resume gates are unreachable for this module
// today; this test does NOT assert they accept it.
//
// End-to-end suspend/resume of this configuration is WH-5b.2/5b.3 scope.

// One decoded exec-manifest node (the subset the test asserts).
struct ManifestNode {
    std::uint32_t node_id = 0;
    std::uint32_t schedule_pos = 0;
    std::uint8_t cap_call_count = 0;
};

// Minimal ULEB128 reader for the manifest grammar.
struct UlebReader {
    std::span<const std::uint8_t> bytes;
    std::size_t pos = 0;

    [[nodiscard]] std::optional<std::uint64_t> u64() {
        std::uint64_t result = 0;
        unsigned shift = 0;
        while (pos < bytes.size()) {
            const auto b = bytes[pos++];
            result |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) {
                return result;
            }
            shift += 7;
            if (shift >= 64) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> u32() {
        auto v = u64();
        if (!v.has_value() || *v > 0xFFFFFFFFull) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*v);
    }

    [[nodiscard]] std::optional<std::uint8_t> byte() {
        if (pos >= bytes.size()) {
            return std::nullopt;
        }
        return bytes[pos++];
    }
};

// Extract the `ahfl.wasm-exec-manifest.v1` custom section payload from a wasm
// module. Mirrors the A2 framing: skip the 8-byte header, then walk sections
// (id + ULEB size + payload); a custom section (id 0) carries a name-length +
// name + data. Returns the data of the first matching section.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
extract_exec_manifest(std::span<const std::uint8_t> module_bytes) {
    static constexpr std::string_view kManifestName = "ahfl.wasm-exec-manifest.v1";
    if (module_bytes.size() < 8) {
        return std::nullopt;
    }
    std::size_t pos = 8; // skip magic + version
    while (pos < module_bytes.size()) {
        const auto section_id = module_bytes[pos++];
        UlebReader size_reader{module_bytes.subspan(pos)};
        auto section_size = size_reader.u64();
        if (!section_size.has_value()) {
            return std::nullopt;
        }
        pos += size_reader.pos; // advance past the size ULEB
        if (pos + *section_size > module_bytes.size()) {
            return std::nullopt;
        }
        const auto payload = module_bytes.subspan(pos, static_cast<std::size_t>(*section_size));
        pos += static_cast<std::size_t>(*section_size);
        if (section_id != 0) {
            continue; // not a custom section
        }
        UlebReader name_reader{payload};
        auto name_len = name_reader.u64();
        if (!name_len.has_value() ||
            name_reader.pos + *name_len > payload.size()) {
            return std::nullopt;
        }
        const auto name = std::string_view{
            reinterpret_cast<const char *>(payload.data() + name_reader.pos),
            static_cast<std::size_t>(*name_len)};
        if (name == kManifestName) {
            return std::vector<std::uint8_t>{
                payload.begin() + static_cast<std::ptrdiff_t>(name_reader.pos + *name_len),
                payload.end()};
        }
    }
    return std::nullopt;
}

// Decode the workflow-arm exec-manifest payload (mirrors the A2 grammar:
// magic(6) + version(1) + entry_kind=0 + workflow_id(4) + node_count(4) +
// nodes[] { node_id(4) + schedule_pos(4) + cap_call_count(1) +
// capabilities[] { cap_id(4) + source_symbol(8) } }).
[[nodiscard]] std::optional<std::vector<ManifestNode>>
decode_workflow_manifest(std::span<const std::uint8_t> payload) {
    static constexpr std::array<std::uint8_t, 6> kMagic = {
        'A', 'H', 'F', 'L', 'X', 'M'};
    if (payload.size() < 8 ||
        !std::equal(kMagic.begin(), kMagic.end(), payload.begin())) {
        return std::nullopt;
    }
    UlebReader r{payload};
    r.pos = 6; // magic
    const auto version = r.byte();
    if (!version.has_value() || *version != 1) {
        return std::nullopt;
    }
    const auto entry_kind = r.byte();
    if (!entry_kind.has_value() || *entry_kind != 0) {
        return std::nullopt; // workflow arm only
    }
    const auto workflow_id = r.u32();
    if (!workflow_id.has_value()) {
        return std::nullopt;
    }
    const auto node_count = r.u32();
    if (!node_count.has_value()) {
        return std::nullopt;
    }
    std::vector<ManifestNode> nodes;
    nodes.reserve(*node_count);
    for (std::uint32_t i = 0; i < *node_count; ++i) {
        ManifestNode node;
        const auto nid = r.u32();
        if (!nid.has_value()) {
            return std::nullopt;
        }
        node.node_id = *nid;
        const auto spos = r.u32();
        if (!spos.has_value() || *spos != i) {
            return std::nullopt;
        }
        node.schedule_pos = *spos;
        const auto ccc = r.byte();
        if (!ccc.has_value()) {
            return std::nullopt;
        }
        node.cap_call_count = *ccc;
        for (std::uint8_t c = 0; c < *ccc; ++c) {
            const auto cap = r.u32();
            if (!cap.has_value()) {
                return std::nullopt;
            }
            const auto sym = r.u64();
            if (!sym.has_value()) {
                return std::nullopt;
            }
        }
        nodes.push_back(node);
    }
    return nodes;
}

void test_hybrid_p6_bridge_manifest(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_bridge.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_bridge.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_bridge.is_workflow");
    check(desc.frame_contract == ahfl::backends::CoreWasmFrameContract::P6Frame,
          "hybrid_bridge.p6_frame");
    check(desc.workflow_node_count == 3, "hybrid_bridge.three_nodes");
    check(desc.imports.size() == 2, "hybrid_bridge.two_imports");

    // P2-5: per-node is_p6 flags + P6-only dense ordinals. The Kahn schedule
    // is declaration order [bridge_a, bridge_b, echo]: P6, P6, opaque.
    check(desc.nodes.size() == 3, "hybrid_bridge.nodes_size");
    if (desc.nodes.size() == 3) {
        check(desc.nodes[0].name == "bridge_a",
              "hybrid_bridge.node0_name");
        check(desc.nodes[0].is_p6, "hybrid_bridge.node0_is_p6");
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_bridge.node0_p6_ordinal");
        check(desc.nodes[1].name == "bridge_b",
              "hybrid_bridge.node1_name");
        check(desc.nodes[1].is_p6, "hybrid_bridge.node1_is_p6");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_bridge.node1_p6_ordinal");
        check(desc.nodes[2].name == "echo", "hybrid_bridge.node2_name");
        check(!desc.nodes[2].is_p6, "hybrid_bridge.node2_is_opaque");
    }

    // P2-5: P6-only dense node_blocks: exactly 2 entries (the two P6 runners).
    check(desc.frame_section.has_value(), "hybrid_bridge.frame_section");
    if (desc.frame_section.has_value()) {
        check(desc.frame_section->node_blocks.size() == 2,
              "hybrid_bridge.node_blocks_count");
    }
    check(desc.wire_schema.has_value(), "hybrid_bridge.wire_schema");

    // Decode the exec-manifest and assert the current byte shape: ALL nodes
    // reference their capabilities (including P6 nodes' in-handler bridge
    // imports) so the manifest-referenced set equals the import set for A2
    // admission. Both P6 bridge nodes have 1 Bridge cap; the opaque echo node
    // has 1 Echo cap. The tag-0-record-vs-cap-byte disagreement for P6 nodes
    // is the WH-5b.2 latent gap (see header comment).
    auto manifest_payload =
        extract_exec_manifest(std::span<const std::uint8_t>(wf->module_bytes));
    check(manifest_payload.has_value(), "hybrid_bridge.manifest_extracted");
    if (!manifest_payload.has_value()) {
        return;
    }
    auto nodes = decode_workflow_manifest(*manifest_payload);
    check(nodes.has_value(), "hybrid_bridge.manifest_decoded");
    if (!nodes.has_value()) {
        return;
    }
    check(nodes->size() == 3, "hybrid_bridge.manifest_node_count");
    if (nodes->size() == 3) {
        // Node ids + schedule_pos order match the descriptor.
        check((*nodes)[0].node_id == desc.nodes[0].node_id,
              "hybrid_bridge.manifest_node0_id");
        check((*nodes)[0].schedule_pos == 0,
              "hybrid_bridge.manifest_node0_pos");
        check((*nodes)[1].node_id == desc.nodes[1].node_id,
              "hybrid_bridge.manifest_node1_id");
        check((*nodes)[1].schedule_pos == 1,
              "hybrid_bridge.manifest_node1_pos");
        check((*nodes)[2].node_id == desc.nodes[2].node_id,
              "hybrid_bridge.manifest_node2_id");
        check((*nodes)[2].schedule_pos == 2,
              "hybrid_bridge.manifest_node2_pos");
        // P6 bridge nodes: cap_call_count 1 (bridge cap referenced for A2
        // admission; the tag-0 record disagreement is WH-5b.2 latent gap).
        check((*nodes)[0].cap_call_count == 1,
              "hybrid_bridge.manifest_node0_bridge_cap");
        check((*nodes)[1].cap_call_count == 1,
              "hybrid_bridge.manifest_node1_bridge_cap");
        // Opaque cap node: cap_call_count 1 (Echo cap).
        check((*nodes)[2].cap_call_count == 1,
              "hybrid_bridge.manifest_node2_capability");
    }

    // A2 admission SUCCEEDS: the manifest references all imported caps
    // (including the P6 nodes' in-handler bridge imports), so the
    // manifest-referenced set equals the wire-schema/import set. The
    // tag-0-record-vs-cap-byte disagreement for P6 nodes is the WH-5b.2
    // latent gap (see header comment); the resume classifier is unreachable
    // for this module today.
    auto admitted = ahfl::runtime::core_wasm_schema_module::
        make_verified_core_wasm_schema_module(
            std::span<const std::uint8_t>(wf->module_bytes));
    check(admitted.ok(),
          "hybrid_bridge.a2_admission_succeeds_all_caps_referenced");
}

} // namespace

int main() {
    const auto repo_root = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_p6_trace_workflow(repo_root);
    test_wirejson_capability_workflow(repo_root);
    test_p6_trace_low_branch(repo_root);
    test_event_sequence_identity(repo_root);
    test_event_sequence_and_report_fields(repo_root);
    test_host_abort_workflow(repo_root);
    test_trap_workflow(repo_root);
    test_trap_pipeline(repo_root);
    test_corrupted_output_workflow();
    test_completed_count_fail_closed(repo_root);
    test_capability_kind_fail_closed(repo_root);
    test_capability_ordinal_oor_fail_closed(repo_root);
    // WH-5b.1: hybrid P6 + opaque emission/descriptor-only tests.
    test_hybrid_p6_before_cap(repo_root);
    test_hybrid_cap_before_p6(repo_root);
    test_hybrid_kahn_reordered(repo_root);
    // WH-5b.1 fix-forward: P6-bridge manifest byte shape + multi-P6 ordinals.
    test_hybrid_p6_bridge_manifest(repo_root);

    std::cout << "workflow_session: " << g_checks << " checks passed\n";
    return 0;
}
