// RFC 0026 KR6.8 WH-4: KAT for the workflow session.
//
// Drives REAL wasm3 workflow modules end-to-end through the workflow session
// and pins the observation data (states, capabilities, arguments, counts),
// the hook-firing discipline (import-boundary state_entered, capability
// invoked/result, post-run node_completed), and the result status + output.
//
// Every module is P6-frame after the WireJson lane elimination. Two
// representative shapes:
//   1. P6-frame workflow (wh4_trace_workflow): no capabilities; the trace ring
//      is the sole state-sequence evidence. Verifies state_entered_hook fires
//      for the full Decide -> High -> Done walk and the output is the computed
//      Frame.
//   2. P6-frame capability workflow (e3_capability_workflow): the Echo
//      capability served through the P6 bridge. Verifies
//      capability_invoked_hook, capability_result_observer,
//      capability_arguments collection, and node_completed_hook for both
//      nodes.

#include "runtime/wasm_host/workflow_session.hpp"
#include "runtime/wasm_host/wasm3_engine.hpp"

#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_schema_module.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/execution_report.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"

#include "common/project_input_support.hpp"
#include "unit/runtime/wasm_host/wasm_host_test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
    // The compiled program, retained for tests that inspect the IR shape
    // (capability ranges, program index, etc.).
    ir::Program program;
    // WH-5c.7 P1-2/P2-2: the codegen diagnostics, retained so a test can pin
    // the exact compile-time rejection code (kUnsupportedCapabilityFrame)
    // for a rich-type bridge parameter.
    std::vector<ahfl::backends::CoreWasmDiagnostic> diagnostics;
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
        .program = std::move(*program),
        .diagnostics = emitted.diagnostics,
    };
}

// WH-5c.7 P1-2: variant of emit_workflow that ALWAYS returns diagnostics,
// even when the codegen rejects the module (no artifact). Needed for Map/Set
// bridge pins whose REAL rejection layer is the P6 scalar codegen itself
// (kUnsupportedCapabilityFrame), not the runtime bridge_param_kind.
struct EmitOrDiag {
    std::optional<EmittedWorkflow> wf;
    std::vector<ahfl::backends::CoreWasmDiagnostic> diagnostics;
};

[[nodiscard]] EmitOrDiag
emit_workflow_or_diag(const std::filesystem::path &source_path) {
    EmitOrDiag result;
    std::string error;
    auto program = conf::compile_conformance_source(source_path, error);
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return result;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        std::cerr << "  core lower failed\n";
        for (const auto &d : core.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return result;
    }
    const auto layouts = irc::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        std::cerr << "  layout failed\n";
        return result;
    }
    const auto emitted = ahfl::backends::emit_core_wasm(
        core.program, *layouts.table,
        {irc::CoreWorkflowId{0}, ahfl::backends::WasmProfileKind::Wasi});
    result.diagnostics = emitted.diagnostics;
    if (!emitted.artifact.has_value()) {
        std::cerr << "  emit failed: no artifact\n";
        for (const auto &d : emitted.diagnostics) {
            std::cerr << "    [" << d.code << "] " << d.message << "\n";
        }
        return result;
    }
    if (!emitted.descriptor.has_value()) {
        std::cerr << "  emit failed: no descriptor\n";
        return result;
    }
    result.wf = EmittedWorkflow{
        .module_bytes = emitted.artifact->bytes,
        .descriptor = std::move(*emitted.descriptor),
        .program = std::move(*program),
        .diagnostics = emitted.diagnostics,
    };
    return result;
}

// WH-5c.6: build the SAME host-side range resolvers the facade builds
// (design 12.15.17.1, Option A), so the session tests exercise the
// range-resolver seam without constructing a WasmWorkflowRuntime.
struct SessionRangeResolvers {
    std::function<ir::SourceRangeOpt(std::uint32_t)> node;
    std::function<ir::SourceRangeOpt(std::uint64_t)> capability;
};

[[nodiscard]] SessionRangeResolvers
make_range_resolvers(const EmittedWorkflow &wf) {
    SessionRangeResolvers resolvers;
    ir::ProgramIndex idx(wf.program);

    // Source-order node ranges for the descriptor's workflow.
    std::vector<ir::SourceRangeOpt> source_ranges;
    for (const auto *decl : idx.workflows()) {
        if (decl != nullptr && decl->name == wf.descriptor.workflow_name) {
            source_ranges.reserve(decl->nodes.size());
            for (const auto &node : decl->nodes) {
                source_ranges.push_back(node.source_range);
            }
            break;
        }
    }
    // Schedule-order vector: descriptor.nodes[i].node_id indexes the
    // source-order vector (invariant nodes[i].schedule_pos == i).
    std::vector<ir::SourceRangeOpt> schedule_ranges;
    schedule_ranges.reserve(wf.descriptor.nodes.size());
    for (const auto &node_desc : wf.descriptor.nodes) {
        if (node_desc.node_id < source_ranges.size()) {
            schedule_ranges.push_back(source_ranges[node_desc.node_id]);
        } else {
            schedule_ranges.push_back(std::nullopt);
        }
    }
    resolvers.node =
        [schedule_ranges = std::move(schedule_ranges)](
            std::uint32_t schedule_pos) -> ir::SourceRangeOpt {
            if (schedule_pos < schedule_ranges.size()) {
                return schedule_ranges[schedule_pos];
            }
            return std::nullopt;
        };

    // Capability declaration ranges by source_symbol.
    std::unordered_map<std::uint64_t, ir::SourceRangeOpt> cap_ranges;
    for (const auto *cap : idx.capabilities()) {
        if (cap != nullptr && cap->symbol_ref.id.has_value()) {
            cap_ranges.emplace(
                static_cast<std::uint64_t>(*cap->symbol_ref.id),
                cap->provenance.source_range);
        }
    }
    resolvers.capability =
        [cap_ranges = std::move(cap_ranges)](
            std::uint64_t source_symbol) -> ir::SourceRangeOpt {
            auto it = cap_ranges.find(source_symbol);
            if (it != cap_ranges.end()) {
                return it->second;
            }
            return std::nullopt;
        };
    return resolvers;
}

// WH-5c.6: the first (and only) capability declaration's provenance range,
// for the expected-range assertions (the fixtures carry one capability).
[[nodiscard]] ir::SourceRangeOpt
first_capability_range(const ir::Program &program) {
    ir::ProgramIndex idx(program);
    for (const auto *cap : idx.capabilities()) {
        if (cap != nullptr) {
            return cap->provenance.source_range;
        }
    }
    return std::nullopt;
}

// WH-5c.6: the source-order node range for a workflow by node index.
[[nodiscard]] ir::SourceRangeOpt
node_source_range(const ir::Program &program,
                  const std::string &workflow_name, std::size_t node_index) {
    ir::ProgramIndex idx(program);
    for (const auto *decl : idx.workflows()) {
        if (decl != nullptr && decl->name == workflow_name &&
            node_index < decl->nodes.size()) {
            return decl->nodes[node_index].source_range;
        }
    }
    return std::nullopt;
}

// WH-5c.6: the NodeFailed / WorkflowFailed diagnostic ids from the event
// stream (each appears at most once on the wasm lane).
struct FailedEventIds {
    std::optional<ahfl::runtime::DiagnosticId> node_failed;
    std::optional<ahfl::runtime::DiagnosticId> workflow_failed;
};

[[nodiscard]] FailedEventIds
failed_event_ids(const ahfl::runtime::WorkflowResult &result) {
    FailedEventIds ids;
    for (const auto &event : result.events.events()) {
        if (const auto *nf =
                std::get_if<ahfl::runtime::NodeFailed>(&event.payload)) {
            ids.node_failed = nf->diagnostic;
        } else if (const auto *wf =
                       std::get_if<ahfl::runtime::WorkflowFailed>(
                           &event.payload)) {
            ids.workflow_failed = wf->diagnostic;
        }
    }
    return ids;
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

// ==== 2. P6-frame capability workflow (Echo, P6 lane) ====

void test_p6_capability_workflow(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "p6_cap.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "p6_cap.is_workflow");
    check(desc.workflow_node_count == 2, "p6_cap.two_nodes");
    check(desc.imports.size() == 1, "p6_cap.one_import");
    if (desc.imports.empty()) {
        return;
    }
    // The canonical name may include the module prefix; check it ends with
    // "Echo" rather than exact equality.
    const auto &echo_name = desc.imports[0].canonical_name;
    check(echo_name.size() >= 4 &&
              echo_name.substr(echo_name.size() - 4) == "Echo",
          "p6_cap.echo_name");

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})");
    check(input.has_value(), "p6_cap.input");
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
            check(name == "Echo", "p6_cap.hook_cap_name");
            // P1-6: the Echo capability is invoked by the `first` node,
            // whose runner is agent 0 (FirstAgent).
            check(agent_id.index() == 0, "p6_cap.hook_agent_id");
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
    check(result.has_value(), "p6_cap.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The Echo capability was invoked exactly once.
    check(cap_invoked_count == 1, "p6_cap.cap_invoked_count");
    check(cap_result_count == 1, "p6_cap.cap_result_count");
    check(result->capabilities.size() == 1, "p6_cap.capabilities_size");
    if (!result->capabilities.empty()) {
        check(result->capabilities[0] == "Echo",
              "p6_cap.capabilities[0]");
    }

    // The capability argument envelope should be the bare struct JSON.
    check(result->capability_arguments.size() == 1,
          "p6_cap.cap_args_size");
    if (!result->capability_arguments.empty()) {
        check(result->capability_arguments[0] ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})",
              "p6_cap.cap_args[0]");
    }

    // No failures.
    check(result->capability_failures.empty(),
          "p6_cap.no_failures");

    // node_completed_hook should have fired for both nodes.
    check(node_completed_count == 2, "p6_cap.node_completed_count");
    // P1-6: the hook receives the REAL node names (not agent names).
    check(completed_nodes.size() == 2, "p6_cap.completed_nodes_size");
    if (completed_nodes.size() == 2) {
        check(completed_nodes[0] == "first",
              "p6_cap.completed_nodes[0]");
        check(completed_nodes[1] == "second",
              "p6_cap.completed_nodes[1]");
    }

    // D-B + P1-6: the post-run state reconstruction fires state_entered_hook
    // with the REAL node names. Each node walks Start -> Done.
    check(hook_states.size() == 4, "p6_cap.hook_states_count");
    if (hook_states.size() == 4) {
        check(hook_states[0] == "Start", "p6_cap.hook_state_0");
        check(hook_states[1] == "Done", "p6_cap.hook_state_1");
        check(hook_states[2] == "Start", "p6_cap.hook_state_2");
        check(hook_states[3] == "Done", "p6_cap.hook_state_3");
    }
    check(hook_node_names.size() == 4,
          "p6_cap.hook_node_names_count");
    if (hook_node_names.size() == 4) {
        check(hook_node_names[0] == "first",
              "p6_cap.hook_node_name_0");
        check(hook_node_names[1] == "first",
              "p6_cap.hook_node_name_1");
        check(hook_node_names[2] == "second",
              "p6_cap.hook_node_name_2");
        check(hook_node_names[3] == "second",
              "p6_cap.hook_node_name_3");
    }

    // The workflow completed successfully.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "p6_cap.completed");

    // The output should equal the input (Echo echoes, SecondAgent is identity).
    const auto *output = result->result.output();
    check(output != nullptr, "p6_cap.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"identity"})",
              "p6_cap.output_value");
    }

    // workflow_completed_count should be 2 (two nodes).
    check(result->workflow_completed_count == 2,
          "p6_cap.completed_count");
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

    // P1-2: the wasm lane emits the expected event variant sequence + order:
    // RunStarted, WorkflowStarted, NodeScheduled (all, in
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
    // decoded and node outputs are populated (the actual decoded P6 output).
    config.node_completed_hook =
        [](AgentId, std::string_view, const Value &) {};

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "seq_cap.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // P1-2: same event sequence as expected, including
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

    // WH-5c.4 P2-D: the failed node (FirstAgent) is a capability-final node.
    // A failed node writes no node-event record (only successful completions
    // write them), so the post-run state reconstruction cannot recover its
    // walk from the trace ring. The P2-D fix populates the states from the
    // agent's static walk so the lifecycle hooks fire for the failed node.
    // FirstAgent's walk is [Start, Done] (initial Start, goto Done, Done is
    // final). The second node is Skipped and contributes no states.
    std::size_t state_entered_count = 0;
    for (const auto &event : result->result.events.events()) {
        if (event_type_name(event.payload) == "AgentStateEntered") {
            ++state_entered_count;
        }
    }
    check(state_entered_count == 2,
          "host_abort_wf.p2d_failed_opaque_node_states_from_walk");
}

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
    // failed). This is the expected per-node terminal semantics.
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

// ==== WH-5b.3: P6 capability workflow (P6-before-cap) ====
//
// FULL runtime: both nodes are P6. The compute node packs P4-D; the echo
// node calls Echo through the P6 bridge. The workflow return is the
// compute node's P4-D output.

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
    check(desc.workflow_node_count == 2, "hybrid_p6cap.two_nodes");
    check(desc.imports.size() == 1, "hybrid_p6cap.one_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.nodes.size() == 2, "hybrid_p6cap.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_p6cap.node0_p6_ordinal");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_p6cap.node1_p6_ordinal");
    }

    // P6 dense node_blocks: exactly 2 entries (one per node).
    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_p6cap.node_blocks_count");

    // WH-5b.3: full runtime execution on real wasm3.
    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_p6_before_cap::Frame","n":1})");
    check(input.has_value(), "hybrid_p6cap.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_p6cap.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
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
    check(result.has_value(), "hybrid_p6cap.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The Echo capability was invoked exactly once (by the opaque echo node).
    check(cap_invoked_count == 1, "hybrid_p6cap.cap_invoked_count");
    check(result->capabilities.size() == 1,
          "hybrid_p6cap.capabilities_size");

    // Both nodes completed.
    check(node_completed_count == 2, "hybrid_p6cap.node_completed_count");

    // The workflow completed successfully.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_p6cap.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_p6cap.completed_count");
}

// ==== WH-5b.3: P6 capability workflow (cap-before-P6) ====
//
// FULL runtime: both nodes are P6. The echo node calls Echo through the
// P6 bridge; the compute node packs P4-D. The workflow return is the
// compute node's P4-D output.

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
    check(desc.workflow_node_count == 2, "hybrid_capp6.two_nodes");
    check(desc.imports.size() == 1, "hybrid_capp6.one_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.nodes.size() == 2, "hybrid_capp6.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_capp6.node0_p6_ordinal");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_capp6.node1_p6_ordinal");
    }

    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_capp6.node_blocks_count");

    // WH-5b.3: full runtime execution on real wasm3.
    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_cap_before_p6::Frame","n":1})");
    check(input.has_value(), "hybrid_capp6.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_capp6.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_capp6.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_capp6.cap_invoked_count");
    check(result->capabilities.size() == 1,
          "hybrid_capp6.capabilities_size");
    check(node_completed_count == 2, "hybrid_capp6.node_completed_count");

    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_capp6.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_capp6.completed_count");
}

// ==== WH-5b.3: 3-node Kahn-reordered workflow ====
//
// FULL runtime: the Kahn schedule is [echo1, compute, echo2]. All nodes
// are P6. The workflow return is the compute node's P4-D output.

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
    check(desc.workflow_node_count == 3, "hybrid_kahn.three_nodes");
    check(desc.imports.size() == 1, "hybrid_kahn.one_import");

    // Kahn schedule is [echo1, compute, echo2]. The declaration order is
    // [echo2, compute, echo1]; the `after` edges force echo1 first.
    // All nodes are P6; p6_block_ordinal follows node-id order.
    check(desc.nodes.size() == 3, "hybrid_kahn.nodes_size");
    if (desc.nodes.size() == 3) {
        check(desc.nodes[0].name == "echo1", "hybrid_kahn.node0_name");
        check(desc.nodes[0].p6_block_ordinal == 2,
              "hybrid_kahn.node0_p6_ordinal");
        check(desc.nodes[1].name == "compute", "hybrid_kahn.node1_name");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_kahn.node1_p6_ordinal");
        check(desc.nodes[2].name == "echo2", "hybrid_kahn.node2_name");
        check(desc.nodes[2].p6_block_ordinal == 0,
              "hybrid_kahn.node2_p6_ordinal");
    }

    check(desc.frame_section.node_blocks.size() == 3,
          "hybrid_kahn.node_blocks_count");

    // WH-5b.3: full runtime execution on real wasm3.
    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_kahn_reordered::Frame","n":1})");
    check(input.has_value(), "hybrid_kahn.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_kahn.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_kahn.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // Echo was invoked twice (echo1 and echo2 both call Echo).
    check(cap_invoked_count == 2, "hybrid_kahn.cap_invoked_count");
    check(result->capabilities.size() == 2,
          "hybrid_kahn.capabilities_size");
    check(node_completed_count == 3, "hybrid_kahn.node_completed_count");

    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_kahn.completed");

    check(result->workflow_completed_count == 3,
          "hybrid_kahn.completed_count");
}

// ==== WH-5b.3 AC1(b): P6->opaque NodeOutput cross-lane ====
//
// FULL runtime: both nodes are P6. The echo node consumes the compute
// node's O_k (NodeOutput source) through the P6 bridge. The workflow
// return is the compute node (no workflow-output crossing).

void test_hybrid_p6_to_opaque(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_p6_to_opaque.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_p6opaque.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_p6opaque.is_workflow");
    check(desc.workflow_node_count == 2, "hybrid_p6opaque.two_nodes");
    check(desc.imports.size() == 1, "hybrid_p6opaque.one_cap_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.nodes.size() == 2, "hybrid_p6opaque.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].name == "compute",
              "hybrid_p6opaque.node0_name");
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_p6opaque.node0_p6_ordinal");
        check(desc.nodes[1].name == "echo", "hybrid_p6opaque.node1_name");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_p6opaque.node1_p6_ordinal");
    }

    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_p6opaque.node_blocks_count");

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_p6_to_opaque::Frame","n":1})");
    check(input.has_value(), "hybrid_p6opaque.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_p6opaque.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_p6opaque.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_p6opaque.cap_invoked_count");
    check(node_completed_count == 2,
          "hybrid_p6opaque.node_completed_count");
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_p6opaque.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_p6opaque.completed_count");
}

// ==== WH-5b.3 AC1(b): opaque->P6 NodeOutput cross-lane ====
//
// FULL runtime: the P6 compute node consumes the opaque echo node's
// wire-JSON output (NodeOutput source). The scheduler inserts a
// JSON_TO_P4D transcode that decodes the opaque result, packs INLINE P4-D
// into the reused shadow region, normalizes, and the materializer reads
// from the shadow to build the P6 node's I_k. The workflow return is the
// P6 node.

void test_hybrid_opaque_to_p6(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_opaque_to_p6.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_opaquep6.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_opaquep6.is_workflow");
    check(desc.workflow_node_count == 2, "hybrid_opaquep6.two_nodes");
    check(desc.imports.size() == 1, "hybrid_opaquep6.one_cap_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.nodes.size() == 2, "hybrid_opaquep6.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].name == "echo", "hybrid_opaquep6.node0_name");
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_opaquep6.node0_p6_ordinal");
        check(desc.nodes[1].name == "compute",
              "hybrid_opaquep6.node1_name");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_opaquep6.node1_p6_ordinal");
    }

    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_opaquep6.node_blocks_count");

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_opaque_to_p6::Frame","n":1})");
    check(input.has_value(), "hybrid_opaquep6.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_opaquep6.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_opaquep6.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_opaquep6.cap_invoked_count");
    check(node_completed_count == 2,
          "hybrid_opaquep6.node_completed_count");
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_opaquep6.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_opaquep6.completed_count");
}

// ==== WH-5b.3 AC1(b): capability-node workflow-return ====
//
// FULL runtime: the workflow return is the echo node (P6). The host
// post-run reads the echo node's O_k block directly.

void test_hybrid_opaque_return(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_opaque_return.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_opaque_ret.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_opaque_ret.is_workflow");
    check(desc.workflow_node_count == 2, "hybrid_opaque_ret.two_nodes");
    check(desc.imports.size() == 1, "hybrid_opaque_ret.one_cap_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.nodes.size() == 2, "hybrid_opaque_ret.nodes_size");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].name == "compute",
              "hybrid_opaque_ret.node0_name");
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_opaque_ret.node0_p6_ordinal");
        check(desc.nodes[1].name == "echo", "hybrid_opaque_ret.node1_name");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_opaque_ret.node1_p6_ordinal");
    }

    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_opaque_ret.node_blocks_count");

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_opaque_return::Frame","n":1})");
    check(input.has_value(), "hybrid_opaque_ret.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_opaque_ret.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_opaque_ret.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_opaque_ret.cap_invoked_count");
    check(node_completed_count == 2,
          "hybrid_opaque_ret.node_completed_count");
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_opaque_ret.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_opaque_ret.completed_count");
}

// ==== WH-5b.3 P2-2: rich type fidelity across BOTH transcode directions ====
//
// Drives a TAG-ONLY enum, String, Int, and Bool through a P4D_TO_JSON ENTRY
// transcode and a JSON_TO_P4D workflow-output transcode on real wasm3, then
// compares the wasm output Value against the expected output
// structurally. The String fields exercise the transcode payload arena
// (JSON_TO_P4D bump-allocation) and the entry-payload arena (P4D_TO_JSON
// PtrLen read).
//
// Coverage boundary (established with file evidence, not silently skipped):
//   * Payload-bearing enums and nested structs in a workflow node input /
//     return frame are rejected by the scheduler materializer
//     (kUnsupportedWorkflowFrame), and a computed final cannot forward an
//     aggregate/enum field sourced from the host-packed INPUT frame. Those
//     are P6 workflow-lane limitations, not transcode codec limits; the
//     codec-level payload-enum pack/read arms are covered in
//     frame_packer_reader.cpp and core_json_round_trip.
//   * std::option::Option / std::result::Result cannot be imported in this
//     fixture because compile_conformance_source parses a single file
//     without the project/sysroot module graph (an `import std::option`
//     fixture needs the // @repo-std project parse used by
//     core_wasm_producer_probe). The tag-only enum exercises the
//     CoreWireSchemaEnum discriminant path; the Option null/payload and
//     payload-bearing Enum arms are covered by the synthetic
//     frame_packer_reader.cpp pins and core_json_round_trip.
void test_hybrid_rich_fidelity(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_rich_fidelity.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_rich.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_rich.is_workflow");
    check(desc.workflow_node_count == 2, "hybrid_rich.two_nodes");
    check(desc.imports.size() == 1, "hybrid_rich.one_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_rich.node_blocks_count");

    // Rich input: tag-only enum, String, Int, Bool. Payload-bearing enums
    // and nested structs are rejected by the scheduler materializer
    // (kUnsupportedWorkflowFrame); see the fixture header for details.
    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_rich_fidelity::Frame",)"
        R"("n":1,"flag":true,)"
        R"("color":{"_enum":"wasm::wh5b_hybrid_rich_fidelity::Color","_variant":"Green"},)"
        R"("label":"rich"})");
    check(input.has_value(), "hybrid_rich.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_rich.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
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
    check(result.has_value(), "hybrid_rich.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_rich.cap_invoked_count");
    check(node_completed_count == 2, "hybrid_rich.node_completed_count");
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_rich.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_rich.completed_count");
}

// ==== WH-5b.3 P2-2: rich fidelity on the P4D_TO_JSON NODE_OUTPUT path ====
//
// rich_fidelity above covers ENTRY + workflow-output crossings. This fixture
// pins the distinct NODE_OUTPUT source: the P6 compute node's O_k is the
// workflow return (host reads it directly, no scheduler materializer) and
// the opaque echo node consumes that O_k through a P4D_TO_JSON NODE_OUTPUT
// transcode. Fields are the same flat wire subset (tag-only enum, String,
// Int, Bool); the String PtrLen in O_k may point into the entry-payload
// arena or the rodata pool, so this is the path that requires the
// transcode String regions to admit both. A payload-bearing enum forwarded
// from the host-packed INPUT to a constructed P6 output stays blocked by
// the computed-final aggregate-forwarding gate (see the fixture header);
// payload-enum codec arms are covered in frame_packer_reader.cpp.
void test_hybrid_rich_p6_to_opaque(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_rich_p6_to_opaque.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "hybrid_rich_p6o.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "hybrid_rich_p6o.is_workflow");
    check(desc.workflow_node_count == 2, "hybrid_rich_p6o.two_nodes");
    check(desc.imports.size() == 1, "hybrid_rich_p6o.one_import");

    // All nodes are P6 (WireJson lane eliminated).
    check(desc.frame_section.node_blocks.size() == 2,
          "hybrid_rich_p6o.node_blocks_count");

    // Rich input: tag-only enum, String, Int, Bool. Payload-bearing enums
    // cannot be forwarded from the host-packed input to a constructed P6
    // output (see the fixture header for details).
    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_rich_p6_to_opaque::Frame",)"
        R"("n":1,"flag":true,)"
        R"("color":{"_enum":"wasm::wh5b_hybrid_rich_p6_to_opaque::Color","_variant":"Blue"},)"
        R"("label":"node-output"})");
    check(input.has_value(), "hybrid_rich_p6o.input");
    if (!input.has_value()) {
        return;
    }

    int cap_invoked_count = 0;
    int node_completed_count = 0;
    wh::WorkflowSessionConfig config;
    config.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "hybrid_rich_p6o.cap_name");
        };
    config.node_completed_hook =
        [&node_completed_count](AgentId, std::string_view, const Value &) {
            ++node_completed_count;
        };
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
    check(result.has_value(), "hybrid_rich_p6o.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(cap_invoked_count == 1, "hybrid_rich_p6o.cap_invoked_count");
    check(node_completed_count == 2, "hybrid_rich_p6o.node_completed_count");
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "hybrid_rich_p6o.completed");

    check(result->workflow_completed_count == 2,
          "hybrid_rich_p6o.completed_count");
}

// ==== WH-5b.3 P2-2: Decimal/Duration fail-closed pin ====
//
// Spec 12.13.6 claims "Decimal/Duration: P4-D scalar <-> wire JSON number/
// string" fidelity across the transcode boundary. The implementation does
// NOT support this: the P4-D packer (frame_packer.cpp:470-472) and reader
// (frame_reader.cpp:447-448) both fail closed on CoreWireSchemaDecimal /
// CoreWireSchemaDuration with ValueNotWireEncodable. The wire schema itself
// accepts them (core_wire_schema.cpp:268-273), and the P6 codegen can emit
// them as i64 constants (core_wasm_codegen.cpp:2057-2075), but the host-side
// pack/read path rejects them. This test pins the current safe behavior:
// the session fails closed at the entry pack step, before any node runs.
//
// Closure reachability note: Closure is rejected at wire-schema construction
// (core_wire_schema.cpp:304-308, ws_Closure lambda: "closure values are not
// supported by value_json"). No source-level AHFL program can produce a wire
// schema with a Closure type, so no transcode site can ever carry a Closure
// value. The transcode boundary is unreachable for Closure; the codec-level
// rejection is already pinned in core_json_round_trip and
// frame_packer_reader.cpp.
void test_hybrid_decimal_round_trip(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_decimal_round_trip.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "decimal_rt.emit");
    if (!wf.has_value()) {
        return;
    }

    // Construct a Frame input with a real DecimalValue. value_from_json
    // cannot produce a DecimalValue (it has no type information), so build
    // the struct directly.
    Value input{ahfl::runtime::StructValue{}};
    auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
    sv.type_name = "wasm::wh5b_hybrid_decimal_round_trip::Frame";
    sv.fields.set("n",
                  std::make_unique<Value>(Value{ahfl::runtime::IntValue{1}}));
    sv.fields.set(
        "amount",
        std::make_unique<Value>(Value{ahfl::runtime::DecimalValue{"1.25"}}));

    wh::WorkflowSessionConfig config;
    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           input, std::move(config));
    // WH-5c.7: the packer now round-trips Decimal(2) through the P6 frame
    // (parse_decimal -> i64 mantissa -> format_decimal_spelling). The session
    // must SUCCEED and the output Decimal must match the input.
    check(result.has_value(), "decimal_rt.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    const auto *output = result->result.output();
    check(output != nullptr, "decimal_rt.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == R"({"_type":"wasm::wh5b_hybrid_decimal_round_trip::Frame","amount":"1.25","n":1})",
              "decimal_rt.output_value");
    }
}

// ==== WH-5c.7 P1-1: Decimal spelling round-trip guard ====
//
// The packer rebuilds the canonical spelling from the i64 mantissa and
// compares it EXACTLY against the source spelling. A non-canonical spelling
// whose scale matches the wire schema (e.g. "01.20" for Decimal(2)) is
// rejected with ValueNotWireEncodable at entry pack. A spelling whose scale
// does not match (e.g. "1.2" or "1.200" for Decimal(2)) is rejected with
// ShapeMismatch by the existing scale check. Both rejections happen at ENTRY
// PACK before the module runs.

void test_decimal_spelling_guard(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_decimal_round_trip.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "dec_sg.emit");
    if (!wf.has_value()) {
        return;
    }

    auto make_input = [](const std::string &spelling) {
        Value input{ahfl::runtime::StructValue{}};
        auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
        sv.type_name = "wasm::wh5b_hybrid_decimal_round_trip::Frame";
        sv.fields.set("n",
                      std::make_unique<Value>(Value{ahfl::runtime::IntValue{1}}));
        sv.fields.set(
            "amount",
            std::make_unique<Value>(Value{ahfl::runtime::DecimalValue{spelling}}));
        return input;
    };

    // Accept: "1.20" is canonical for Decimal(2).
    {
        auto input = make_input("1.20");
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(result.has_value(), "dec_sg.accept_1.20");
        if (!result.has_value()) {
            std::cerr << "  error: " << result.error() << "\n";
        }
    }

    // Reject: "1.2" has scale 1, wire schema expects scale 2 -> ShapeMismatch.
    {
        auto input = make_input("1.2");
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(!result.has_value(), "dec_sg.reject_1.2");
        if (!result.has_value()) {
            check(result.error().find("ShapeMismatch") != std::string::npos,
                  "dec_sg.reject_1.2.code");
        }
    }

    // Reject: "1.200" has scale 3, wire schema expects scale 2 ->
    // ShapeMismatch.
    {
        auto input = make_input("1.200");
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(!result.has_value(), "dec_sg.reject_1.200");
        if (!result.has_value()) {
            check(result.error().find("ShapeMismatch") != std::string::npos,
                  "dec_sg.reject_1.200.code");
        }
    }

    // Reject: "01.20" has the right scale (2) but is non-canonical (leading
    // zero) -> ValueNotWireEncodable from the spelling guard.
    {
        auto input = make_input("01.20");
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(!result.has_value(), "dec_sg.reject_01.20");
        if (!result.has_value()) {
            check(result.error().find("ValueNotWireEncodable") !=
                      std::string::npos,
                  "dec_sg.reject_01.20.code");
        }
    }
}

// ==== WH-5c.7 P1-1: Duration spelling round-trip guard ====
//
// The packer rebuilds the canonical source-unit spelling from the i64 millis
// and compares it EXACTLY against the source spelling. A non-canonical unit
// form ("60s" instead of "1m", "5000ms" instead of "5s") is rejected with
// ValueNotWireEncodable at entry pack before the module runs. Canonical
// forms ("1m", "5s", "250ms") round-trip successfully.

void test_duration_spelling_guard(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_duration_round_trip.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "dur_sg.emit");
    if (!wf.has_value()) {
        return;
    }

    auto make_input = [](const std::string &spelling) {
        Value input{ahfl::runtime::StructValue{}};
        auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
        sv.type_name = "wasm::wh5c7_duration_round_trip::Frame";
        sv.fields.set("n",
                      std::make_unique<Value>(Value{ahfl::runtime::IntValue{1}}));
        sv.fields.set(
            "dur",
            std::make_unique<Value>(Value{ahfl::runtime::DurationValue{spelling}}));
        return input;
    };

    // Accept: canonical forms.
    for (const char *spelling : {"1m", "5s", "250ms"}) {
        auto input = make_input(spelling);
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(result.has_value(),
              std::string("dur_sg.accept_") + spelling);
        if (!result.has_value()) {
            std::cerr << "  error: " << result.error() << "\n";
        }
    }

    // Reject: non-canonical unit forms -> ValueNotWireEncodable.
    for (const char *spelling : {"60s", "5000ms"}) {
        auto input = make_input(spelling);
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                               input, std::move(config));
        check(!result.has_value(),
              std::string("dur_sg.reject_") + spelling);
        if (!result.has_value()) {
            check(result.error().find("ValueNotWireEncodable") !=
                      std::string::npos,
                  std::string("dur_sg.reject_") + spelling + ".code");
        }
    }
}

// ==== WH-5c.7: f64-bridge fail-closed pin ====
//
// The P6 frame packer/reader now round-trips Float (f64) through the P6
// frame, but f64 across a CAPABILITY bridge stays rejected. The capability
// call is in a RETURN position (`return Echo(input)`), which
// region_contains_capability does not detect (it scans for
// CoreCapabilityCallStmt, not a capability call expression inside a
// return). The bridge registry is never populated, so the P6 runner's
// CapabilityAction arm is not emitted; the node fails at runtime when the
// capability call cannot be served.

void test_f64_bridge_fail_closed(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_f64_bridge_fail_closed.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "f64_fc.emit");
    if (!wf.has_value()) {
        return;
    }

    // P2-2: the true rejection layer. The capability call is in a RETURN
    // position, which region_contains_capability does not detect, so the
    // bridge registry is never populated and the node fails at runtime.

    auto input = value_from_json(
        R"({"_type":"wasm::wh5c7_f64_bridge_fail_closed::Frame","value":"x","ratio":1.5})");
    check(input.has_value(), "f64_fc.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    // The session runs on the P6 lane; the node fails because the
    // capability call cannot be served (bridge registry not populated).
    check(result.has_value(), "f64_fc.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "f64_fc.node_failed");
}

// ==== WH-5c.7 f64-bridge positive pin ====
//
// The f64 bridge ladder: a Float argument spills into the 8-byte spill slot
// (f64.store), the host reads it as f64 via the wire schema's Float leaf, and
// the f64 result is loaded from the result placement area (f64.load). The
// capability call is in a STATEMENT position (`let reply = Echo(input.ratio)`)
// so region_contains_capability detects it, the bridge registry is populated,
// and the module stays on the P6 lane. The echoed f64 is carried through the
// context so the Done state can materialize it in the output.
void test_f64_bridge(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_f64_bridge.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "f64_bridge.emit");
    if (!wf.has_value()) {
        return;
    }

    // The capability call is in statement position, so the bridge registry
    // is populated and the node can serve the capability at runtime (the
    // return-position fail-closed fixture exercises the undetected case).

    auto input = value_from_json(
        R"({"_type":"wasm::wh5c7_f64_bridge::Frame","ratio":1.5})");
    check(input.has_value(), "f64_bridge.input");
    if (!input.has_value()) {
        return;
    }

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &args) -> CapabilityCallResult {
        // Echo the f64 argument back as the f64 result. The host packs it
        // into the result placement area; the guest loads it with f64.load.
        // Value is non-copyable (FieldMap holds unique_ptr), so extract the
        // double and construct a fresh FloatValue.
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        double echoed = 1.5;
        if (!args.empty()) {
            if (const auto *fv = std::get_if<ahfl::runtime::FloatValue>(
                    &args.front().node)) {
                echoed = fv->value;
            }
        }
        r.value = ahfl::runtime::make_float(echoed);
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "f64_bridge.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    // The f64 value crossed the bridge (argument spill + result load) and the
    // workflow completes successfully.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "f64_bridge.completed");

    // The output carries the echoed f64 value through the context.
    const auto *output = result->result.output();
    check(output != nullptr, "f64_bridge.has_output");
    if (output != nullptr) {
        const auto *sv =
            std::get_if<ahfl::runtime::StructValue>(&output->node);
        check(sv != nullptr, "f64_bridge.output_is_struct");
        if (sv != nullptr) {
            const auto &echoed_field = sv->fields.get("echoed");
            check(echoed_field != nullptr, "f64_bridge.has_echoed_field");
            if (echoed_field != nullptr) {
                const auto *fv = std::get_if<ahfl::runtime::FloatValue>(
                    &echoed_field->node);
                check(fv != nullptr, "f64_bridge.echoed_is_float");
                if (fv != nullptr) {
                    // The echoed f64 must be exactly 1.5 (bit-identical round
                    // trip through the spill slot and result placement).
                    check(fv->value == 1.5, "f64_bridge.echoed_value");
                }
            }
        }
    }
}

// ==== WH-5c.7 P1-2: rich-type bridge-rejection pins ====
//
// Four dedicated pins for Decimal/Duration/Set/Map across a capability
// bridge. The capability call is in a STATEMENT position (`let reply =
// Echo(input)`) so region_contains_capability detects it and the bridge
// registry is populated.
//
// Decimal/Duration: the workflow frame-section builder does not call
// verify_frame_bridge_sites, so there is NO compile-time diagnostic. The
// module is emitted on the P6 lane; the runtime bridge_param_kind returns
// Reject for the rich-type wire shape, the host's argument decoder aborts
// the import, and the node fails.
//
// Set/Map: the P6 scalar codegen rejects the rich-type bridge parameter at
// COMPILE TIME with kUnsupportedCapabilityFrame and no wasm module is
// produced.

namespace {

[[nodiscard]] bool
has_diagnostic(const EmittedWorkflow &wf, std::string_view code) {
    return std::any_of(wf.diagnostics.begin(), wf.diagnostics.end(),
                       [code](const ahfl::backends::CoreWasmDiagnostic &d) {
                           return d.code == code;
                       });
}

[[nodiscard]] bool
has_diagnostic(const std::vector<ahfl::backends::CoreWasmDiagnostic> &diags,
               std::string_view code) {
    return std::any_of(diags.begin(), diags.end(),
                       [code](const ahfl::backends::CoreWasmDiagnostic &d) {
                           return d.code == code;
                       });
}

} // namespace

void test_decimal_bridge_fail_closed(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_decimal_bridge_fail_closed.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "dec_bfc.emit");
    if (!wf.has_value()) {
        return;
    }
    // The workflow frame-section builder does not call
    // verify_frame_bridge_sites, so there is NO compile-time diagnostic. The
    // capability call is in statement position so region_contains_capability
    // detects it, the bridge registry is populated, and the module stays on
    // the P6 lane.
    check(!has_diagnostic(*wf,
                          ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame),
          "dec_bfc.no_compile_time_rejection");

    Value input{ahfl::runtime::StructValue{}};
    auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
    sv.type_name = "wasm::wh5c7_decimal_bridge_fail_closed::Frame";
    sv.fields.set(
        "amount",
        std::make_unique<Value>(Value{ahfl::runtime::DecimalValue{"1.25"}}));

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           input, std::move(config));
    check(result.has_value(), "dec_bfc.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    // The runtime bridge_param_kind returns Reject for a Decimal wire shape,
    // causing the host's argument decoder to abort the import, which traps
    // the wasm module and fails the node.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "dec_bfc.node_failed");
}

void test_duration_bridge_fail_closed(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_duration_bridge_fail_closed.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "dur_bfc.emit");
    if (!wf.has_value()) {
        return;
    }
    // No compile-time diagnostic (workflow builder skips
    // verify_frame_bridge_sites). P6 lane; runtime bridge_param_kind returns
    // Reject for Duration.
    check(!has_diagnostic(*wf,
                          ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame),
          "dur_bfc.no_compile_time_rejection");

    Value input{ahfl::runtime::StructValue{}};
    auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
    sv.type_name = "wasm::wh5c7_duration_bridge_fail_closed::Frame";
    sv.fields.set(
        "dur",
        std::make_unique<Value>(Value{ahfl::runtime::DurationValue{"5s"}}));

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           input, std::move(config));
    check(result.has_value(), "dur_bfc.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "dur_bfc.node_failed");
}

void test_map_bridge_fail_closed(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_map_bridge_fail_closed.ahfl";
    auto outcome = emit_workflow_or_diag(source);
    // Map's REAL rejection layer is the P6 scalar codegen itself:
    // kUnsupportedCapabilityFrame is emitted because a Map bridge argument is
    // not a frame-walkable P6 value in this rung. No wasm module is produced.
    check(!outcome.wf.has_value(), "map_bfc.emit_rejected");
    check(has_diagnostic(outcome.diagnostics,
                         ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame),
          "map_bfc.unsupported_capability_frame");
}

void test_set_bridge_fail_closed(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_set_bridge_fail_closed.ahfl";
    auto outcome = emit_workflow_or_diag(source);
    // Set's REAL rejection layer is the P6 scalar codegen itself:
    // kUnsupportedCapabilityFrame is emitted because a Set (bounded
    // collection) bridge argument is not a frame-walkable P6 value in this
    // rung. No wasm module is produced.
    check(!outcome.wf.has_value(), "set_bfc.emit_rejected");
    check(has_diagnostic(outcome.diagnostics,
                         ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame),
          "set_bfc.unsupported_capability_frame");
}

// WH-5c.7 P2-3: the workflow-level kInvalidLayout diagnostic must carry the
// workflow declaration's SourceRange. This test verifies the prerequisite:
// the CoreWorkflowDecl's source_range is populated from the AHFL-IR
// WorkflowDecl's provenance during lowering, and the range lands on the
// `workflow` keyword in the source.
void test_workflow_decl_source_range(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c7_duration_round_trip.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wf_sr.compile");
    if (!program.has_value()) {
        std::cerr << "  error: " << error << "\n";
        return;
    }
    const auto core = irc::lower_ahfl_to_core(*program);
    check(core.ok(), "wf_sr.lower");
    if (!core.ok()) {
        return;
    }
    check(!core.program.workflows.empty(), "wf_sr.has_workflow");
    if (core.program.workflows.empty()) {
        return;
    }
    const auto &wf = core.program.workflows.front();
    check(wf.source_range.has_value(), "wf_sr.range_present");
    if (!wf.source_range.has_value()) {
        return;
    }
    check(!wf.source_range->empty(), "wf_sr.range_non_empty");
    // Read the source file and verify the range lands on the `workflow`
    // keyword (the workflow declaration start).
    std::ifstream in(source, std::ios::binary);
    check(in.good(), "wf_sr.open_source");
    if (!in.good()) {
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    const auto begin = wf.source_range->begin_offset;
    check(begin < content.size(), "wf_sr.offset_in_bounds");
    if (begin >= content.size()) {
        return;
    }
    const auto head = content.substr(begin, std::min<std::size_t>(8, content.size() - begin));
    check(head.rfind("workflow", 0) == 0, "wf_sr.lands_on_workflow_keyword");
}


// ==== WH-5b.1 fix-forward: hybrid P6-bridge + opaque cap manifest test ====
//
// Emission/descriptor-only: the exec-manifest v2 (WH-5b.2) carries a per-node
// bridge-site table. P6 bridge nodes emit cap_call_count=0 + empty capabilities
// + one bridge site (the in-handler Bridge import); the opaque cap node emits
// cap_call_count=1 (Echo cap) + an empty bridge-site table. The A2 admission
// set is the UNION of opaque + bridge-site capabilities, which equals the
// wire-schema/import set {Bridge, Echo}. Also covers the multi-P6 ordinal case
// (two P6 runners -> p6_block_ordinal 0 and 1, node_blocks count == 2).
//
// WH-5b.2 CLOSED the former tag-0-vs-cap-byte latent gap: the bridge-site
// table is the separate in-runner site authority (design 12.14), and the
// resume classifier now accepts a P6 bridge node as a legal frontier via
// bridge_sites (not cap_call_count).

// One decoded exec-manifest node (the subset the test asserts).
// WH-5b.2 (manifest v2): one in-runner bridge site decoded from the per-node
// bridge-site table.
struct ManifestBridgeSite {
    std::uint8_t ordinal = 0;
    std::uint32_t call_site_id = 0;
    std::uint32_t capability = 0;
    std::uint64_t source_symbol = 0;
};

struct ManifestNode {
    std::uint32_t node_id = 0;
    std::uint32_t schedule_pos = 0;
    std::uint8_t cap_call_count = 0;
    // WH-5b.2: in-runner bridge sites (manifest v2). A P6 bridge node carries
    // cap_call_count == 0 plus a non-empty bridge_sites list.
    std::vector<ManifestBridgeSite> bridge_sites;
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
// capabilities[] { cap_id(4) + source_symbol(8) } + bridge_site_count(1) +
// bridge_sites[] { ordinal(1) + call_site_id(4) + capability(4) +
// source_symbol(8) } }). WH-5b.2: version 2; the per-node bridge-site table is
// appended AFTER the capabilities array.
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
    if (!version.has_value() || *version != 2) {
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
        // WH-5b.2 (manifest v2): per-node bridge-site table.
        const auto bridge_site_count = r.byte();
        if (!bridge_site_count.has_value()) {
            return std::nullopt;
        }
        node.bridge_sites.reserve(*bridge_site_count);
        for (std::uint8_t b = 0; b < *bridge_site_count; ++b) {
            ManifestBridgeSite site;
            const auto ordinal = r.byte();
            if (!ordinal.has_value() || *ordinal != b) {
                return std::nullopt; // dense 0..N-1
            }
            site.ordinal = *ordinal;
            const auto call_site_id = r.u32();
            if (!call_site_id.has_value()) {
                return std::nullopt;
            }
            site.call_site_id = *call_site_id;
            const auto capability = r.u32();
            if (!capability.has_value()) {
                return std::nullopt;
            }
            site.capability = *capability;
            const auto source_symbol = r.u64();
            if (!source_symbol.has_value()) {
                return std::nullopt;
            }
            site.source_symbol = *source_symbol;
            node.bridge_sites.push_back(site);
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
    check(desc.workflow_node_count == 3, "hybrid_bridge.three_nodes");
    check(desc.imports.size() == 2, "hybrid_bridge.two_imports");

    // P2-5: P6 dense ordinals. All nodes are P6 (WireJson lane eliminated);
    // the Kahn schedule is declaration order [bridge_a, bridge_b, echo].
    check(desc.nodes.size() == 3, "hybrid_bridge.nodes_size");
    if (desc.nodes.size() == 3) {
        check(desc.nodes[0].name == "bridge_a",
              "hybrid_bridge.node0_name");
        check(desc.nodes[0].p6_block_ordinal == 0,
              "hybrid_bridge.node0_p6_ordinal");
        check(desc.nodes[1].name == "bridge_b",
              "hybrid_bridge.node1_name");
        check(desc.nodes[1].p6_block_ordinal == 1,
              "hybrid_bridge.node1_p6_ordinal");
        check(desc.nodes[2].name == "echo", "hybrid_bridge.node2_name");
        check(desc.nodes[2].p6_block_ordinal == 2,
              "hybrid_bridge.node2_p6_ordinal");
    }

    // P6 dense node_blocks: exactly 3 entries (one per node).
    check(desc.frame_section.node_blocks.size() == 3,
          "hybrid_bridge.node_blocks_count");

    // Decode the exec-manifest and assert the WH-5b.2 v2 byte shape: all
    // nodes are P6 bridge nodes with cap_call_count=0 + a non-empty
    // bridge-site table (one cap each: Bridge, Bridge, Echo). The A2
    // admission set is the UNION of all bridge-site capabilities, which
    // equals the wire-schema/import set {Bridge, Echo}.
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
        // All nodes are P6 bridge nodes: cap_call_count 0 + one bridge site.
        check((*nodes)[0].cap_call_count == 0,
              "hybrid_bridge.manifest_node0_p6_zero_cap_call");
        check((*nodes)[0].bridge_sites.size() == 1,
              "hybrid_bridge.manifest_node0_bridge_site_count");
        check((*nodes)[1].cap_call_count == 0,
              "hybrid_bridge.manifest_node1_p6_zero_cap_call");
        check((*nodes)[1].bridge_sites.size() == 1,
              "hybrid_bridge.manifest_node1_bridge_site_count");
        check((*nodes)[2].cap_call_count == 0,
              "hybrid_bridge.manifest_node2_p6_zero_cap_call");
        check((*nodes)[2].bridge_sites.size() == 1,
              "hybrid_bridge.manifest_node2_bridge_site_count");
    }

    // A2 admission SUCCEEDS: the union of opaque + bridge-site capabilities
    // equals the wire-schema/import set {Bridge, Echo}.
    auto admitted = ahfl::runtime::core_wasm_schema_module::
        make_verified_core_wasm_schema_module(
            std::span<const std::uint8_t>(wf->module_bytes));
    check(admitted.ok(),
          "hybrid_bridge.a2_admission_succeeds_bridge_plus_opaque_union");
}

// ==== WH-5b.2 §12.14.9 case 4: unknown call_site_id host trust check ====
//
// The manifest's bridge site carries a call_site_id that joins to the frame
// section's bridge_call_sites. On resume, the replay path resolves the
// manifest site from (node, ordinal), then finds the frame-section site by
// call_site_id. If the manifest was corrupted (call_site_id changed to a
// value not in the frame section), find_bridge_site_by_id returns nullptr
// and the replay fails closed. This test corrupts the manifest in the module
// bytes between origination and resume, proving the host trust check is
// live on REAL wasm3.

void test_wh5b2_case4_unknown_callsite_id(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b2_two_bridge_calls.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "case4.emit");
    if (!wf.has_value()) {
        return;
    }

    // Origination: suspend at ordinal 1 (second Bridge call returns Pending).
    int bridge_call_count = 0;
    wh::WorkflowSessionConfig config;
    config.invoker = [&bridge_call_count](
        const CapabilityInvocationContext &,
        const std::string &,
        const std::vector<Value> &args) -> CapabilityCallResult {
        CapabilityCallResult r;
        ++bridge_call_count;
        if (bridge_call_count == 1) {
            r.status = CapabilityCallStatus::Success;
            if (!args.empty()) {
                if (const auto *int_val =
                        std::get_if<ahfl::runtime::IntValue>(
                            &args[0].node)) {
                    std::unordered_map<std::string, Value> fields;
                    fields.emplace("n",
                                   ahfl::runtime::make_int(int_val->value));
                    r.value = ahfl::runtime::make_struct(
                        "wasm::wh5b2_two_bridge_calls::Frame",
                        std::move(fields));
                }
            }
        } else {
            r.status = CapabilityCallStatus::Pending;
        }
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Bridge";
    };

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b2_two_bridge_calls::Frame","n":42})");
    check(input.has_value(), "case4.input");
    if (!input.has_value()) {
        return;
    }

    auto suspended = wh::run_workflow_session(
        wf->module_bytes, wf->descriptor, *input, std::move(config));
    check(suspended.has_value(), "case4.suspend");
    if (!suspended.has_value()) {
        std::cerr << "  suspend failed: " << suspended.error() << "\n";
        return;
    }
    check(suspended->result.suspended.has_value(), "case4.has_snapshot");
    if (!suspended->result.suspended.has_value()) {
        return;
    }
    check(bridge_call_count == 2, "case4.two_bridge_calls");
    auto snapshot = std::move(*suspended->result.suspended);

    // Corrupt the manifest: change the first bridge site's call_site_id
    // from 0 to 99. The frame section has no site with call_site_id 99,
    // so find_bridge_site_by_id returns nullptr on resume.
    auto corrupted_bytes = wf->module_bytes;
    static constexpr std::array<char, 6> kMagic = {
        'A', 'H', 'F', 'L', 'X', 'M'};
    auto magic_it = std::search(
        corrupted_bytes.begin(), corrupted_bytes.end(),
        kMagic.begin(), kMagic.end());
    check(magic_it != corrupted_bytes.end(), "case4.found_magic");
    if (magic_it == corrupted_bytes.end()) {
        return;
    }
    const auto manifest_offset =
        static_cast<std::size_t>(magic_it - corrupted_bytes.begin());
    UlebReader r{std::span<const std::uint8_t>(
        corrupted_bytes.data() + manifest_offset,
        corrupted_bytes.size() - manifest_offset)};
    r.pos = 6; // magic
    const auto version = r.byte();
    const auto entry_kind = r.byte();
    const auto entry_id = r.u32();
    const auto node_count = r.u32();
    const auto nid = r.u32();
    const auto spos = r.u32();
    const auto ccc = r.byte();
    const auto bridge_site_count = r.byte();
    const auto ordinal = r.byte();
    const auto callsite_id_offset = r.pos;
    const auto callsite_id = r.u32();
    check(version.has_value() && *version == 2, "case4.version");
    check(entry_kind.has_value() && *entry_kind == 0, "case4.entry_kind");
    check(entry_id.has_value(), "case4.entry_id");
    check(node_count.has_value() && *node_count == 1, "case4.node_count");
    check(nid.has_value(), "case4.node_id");
    check(spos.has_value(), "case4.schedule_pos");
    check(ccc.has_value() && *ccc == 0, "case4.cap_call_count");
    check(bridge_site_count.has_value() && *bridge_site_count == 2,
          "case4.bridge_site_count");
    check(ordinal.has_value() && *ordinal == 0, "case4.ordinal");
    check(callsite_id.has_value() && *callsite_id == 0,
          "case4.callsite_id_original");
    if (!callsite_id.has_value() || *callsite_id != 0) {
        return;
    }
    // The ULEB encoding of 0 is a single byte. Change it to 99.
    corrupted_bytes[manifest_offset + callsite_id_offset] = 99;

    // Resume with the corrupted module bytes. The replay path must fail
    // closed: the manifest's bridge site call_site_id (99) is not found in
    // the frame section.
    wh::WorkflowSessionConfig resume_config;
    resume_config.invoker = [](const CapabilityInvocationContext &,
                               const std::string &,
                               const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };
    resume_config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Bridge";
    };
    resume_config.recovery_snapshot = std::move(snapshot);
    resume_config.resume_pending_result_wire_json =
        R"({"_type":"wasm::wh5b2_two_bridge_calls::Frame","n":42})";

    auto resumed = wh::run_workflow_session(
        corrupted_bytes, wf->descriptor, *input, std::move(resume_config));
    // The admission-time bridge-site trust check (workflow_session.cpp
    // manifest<->frame-section cross-check) rejects the corrupted manifest
    // before the replay classifier runs: the manifest's bridge site
    // call_site_id (99) is not found in the frame section.
    check(!resumed.has_value(), "case4.rejected");
    if (!resumed.has_value()) {
        check(resumed.error().find(
                  "manifest bridge site call_site_id not found in frame "
                  "section") != std::string::npos,
              "case4.message_pin");
    }
}

// ==== WH-5b.2 §12.14.9 case 3: bridge result region out of page ====
//
// The frame section travels in the host descriptor and sessions admit it
// without the agent-driver verifier gate, so its bridge-site placements are
// trusted only after the admission-time cross-check in run_workflow_session.
// A forged section that places a bridge result frame (or its String payload
// arena) beyond the fixed 64 KiB page must be rejected before run2, with the
// module's manifest left valid so only the frame-section check can fire.

void test_wh5b2_case3_bridge_site_oob_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b2_two_bridge_calls.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "case3.emit");
    if (!wf.has_value()) {
        return;
    }
    check(!wf->descriptor.frame_section.bridge_call_sites.empty(),
          "case3.has_bridge_sites");
    if (wf->descriptor.frame_section.bridge_call_sites.empty()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b2_two_bridge_calls::Frame","n":42})");
    check(input.has_value(), "case3.input");
    if (!input.has_value()) {
        return;
    }

    const auto make_config = [] {
        wh::WorkflowSessionConfig config;
        config.invoker = [](const CapabilityInvocationContext &,
                            const std::string &,
                            const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            return r;
        };
        config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
            return "Bridge";
        };
        return config;
    };

    constexpr std::uint32_t kPageEnd =
        irc::kCoreWasmFixedLinearMemoryCapacityBytes;

    // Forged result FRAME: ends four bytes past the page end.
    auto frame_oob_desc = wf->descriptor;
    auto &frame_site = frame_oob_desc.frame_section.bridge_call_sites.front();
    frame_site.result_base = kPageEnd - 4u;
    frame_site.result_extent = 8u;

    auto frame_rejected = wh::run_workflow_session(
        wf->module_bytes, frame_oob_desc, *input, make_config());
    check(!frame_rejected.has_value(), "case3.frame_oob_rejected");
    if (!frame_rejected.has_value()) {
        check(frame_rejected.error().find(
                  "bridge result frame out of bounds") != std::string::npos,
              "case3.frame_oob_message_pin");
    }

    // Forged String payload ARENA: ends four bytes past the page end while
    // the result frame stays at its honest placement.
    auto payload_oob_desc = wf->descriptor;
    auto &payload_site =
        payload_oob_desc.frame_section.bridge_call_sites.front();
    payload_site.result_payload_base = kPageEnd - 4u;
    payload_site.result_payload_capacity = 8u;

    auto payload_rejected = wh::run_workflow_session(
        wf->module_bytes, payload_oob_desc, *input, make_config());
    check(!payload_rejected.has_value(), "case3.payload_oob_rejected");
    if (!payload_rejected.has_value()) {
        check(payload_rejected.error().find(
                  "bridge result payload arena out of bounds") !=
                  std::string::npos,
              "case3.payload_oob_message_pin");
    }
}

// ==== WH-5c.5 GAP 4 pin (d): P6 O_k corruption fails closed ====
//
// The per-node output join (design 12.15.19.6) reads a P6 node's output
// from its runner's O_k block via read_value_at. A corrupt O_k block MUST
// fail closed with kOutputDecodeFailed -- never a silent NoneValue. This
// pin corrupts the P6 compute node's O_k bytes via the
// post_run2_memory_mutator test seam and asserts the host decodes the
// corruption as a hard failure.
//
// Fixture: wh5b_hybrid_rich_fidelity (P6 compute + opaque echo). The P6
// node's Frame carries a Bool field; writing 0xFF to the O_k block makes
// the Bool word 0xFFFFFFFF (not 0/1) -> BoolWordInvalid -> decode failure.
void test_gap4_p6_ok_corruption_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_rich_fidelity.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "gap4_p6.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(desc.is_workflow, "gap4_p6.is_workflow");
    check(desc.workflow_node_count == 2, "gap4_p6.two_nodes");
    // Node 0 (compute) is the P6 node; its O_k block is node_blocks[0].
    check(!desc.nodes.empty(), "gap4_p6.nodes_nonempty");
    check(desc.frame_section.node_blocks.size() >= 1,
          "gap4_p6.node_blocks_nonempty");
    if (desc.frame_section.node_blocks.empty()) {
        return;
    }
    const auto ok_base = desc.frame_section.node_blocks[0].output_base;
    const auto ok_size = desc.frame_section.node_blocks[0].output_size;
    check(ok_base != 0, "gap4_p6.ok_base_nonzero");
    check(ok_size > 0, "gap4_p6.ok_size_positive");

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_rich_fidelity::Frame",)"
        R"("n":1,"flag":true,)"
        R"("color":{"_enum":"wasm::wh5b_hybrid_rich_fidelity::Color","_variant":"Green"},)"
        R"("label":"rich"})");
    check(input.has_value(), "gap4_p6.input");
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
    // Corrupt the P6 node's O_k block after run2 returns. The host reads
    // the O_k block in the post-run per-node output join; 0xFF bytes make
    // the Bool field's word 0xFFFFFFFF (not 0/1) -> BoolWordInvalid.
    config.post_run2_memory_mutator =
        [ok_base, ok_size](std::span<std::uint8_t> mem) {
            if (ok_base < mem.size()) {
                const auto n =
                    std::min<std::size_t>(ok_size, mem.size() - ok_base);
                std::memset(mem.data() + ok_base, 0xFF, n);
            }
        };

    auto result = wh::run_workflow_session(wf->module_bytes, desc, *input,
                                           std::move(config));
    check(result.has_value(), "gap4_p6.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The session ran (no pre-run setup failure); the run MUST be Failed
    // with kOutputDecodeFailed, never Completed-with-null.
    check(result->result.status() == ahfl::runtime::WorkflowStatus::EvalError,
          "gap4_p6.eval_error");
    check(result->result.report.status ==
              ahfl::runtime::RunTerminalStatus::Failed,
          "gap4_p6.report_failed");
    check(!result->result.report.output.has_value(), "gap4_p6.no_output");

    bool found_decode_failed = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() &&
            *diag.code == "wasm.output-decode-failed") {
            found_decode_failed = true;
            break;
        }
    }
    check(found_decode_failed, "gap4_p6.found_diagnostic");
}

// ==== WH-5c.6: failure-diagnostic SourceRange + single-id parity ====

// Shared assertions for a failed run with the range resolvers installed:
// NodeFailed and WorkflowFailed reference the SAME DiagnosticId, the bag
// holds exactly one entry with `expected_code`, and that entry's range is
// present (when `expect_range`) and equals `expected_range`.
void check_wh5c6_failure_diagnostic(
    const ahfl::runtime::WorkflowResult &wr, std::string_view expected_code,
    bool expect_range, ir::SourceRangeOpt expected_range,
    std::string_view label) {
    const auto ids = failed_event_ids(wr);
    check(ids.node_failed.has_value(),
          std::string(label) + ".node_failed_event");
    check(ids.workflow_failed.has_value(),
          std::string(label) + ".wf_failed_event");
    check(ids.node_failed.has_value() && ids.workflow_failed.has_value() &&
              *ids.node_failed == *ids.workflow_failed,
          std::string(label) + ".shared_diagnostic_id");

    std::size_t code_count = 0;
    const ahfl::Diagnostic *found = nullptr;
    for (const auto &diag : wr.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == expected_code) {
            ++code_count;
            found = &diag;
        }
    }
    check(code_count == 1, std::string(label) + ".single_entry");
    check(found != nullptr, std::string(label) + ".entry_present");
    if (found == nullptr) {
        return;
    }
    check(!found->message.empty(), std::string(label) + ".message_present");
    if (expect_range) {
        check(found->range.has_value(),
              std::string(label) + ".range_present");
        if (expected_range.has_value()) {
            check(found->range.has_value() && *found->range == *expected_range,
                  std::string(label) + ".range_value");
        }
    } else {
        check(!found->range.has_value(),
              std::string(label) + ".range_absent");
    }
}

// Workflow-level failure check (EvaluationFailed: every node completed during
// run2 but the workflow still failed -- e.g. the P6 O_k output-decode
// fail-closed path). No NodeFailed event is emitted; the WorkflowFailed event
// references a single ranged bag entry.
void check_wh5c6_workflow_only_failure(
    const ahfl::runtime::WorkflowResult &wr, std::string_view expected_code,
    ir::SourceRangeOpt expected_range, std::string_view label) {
    const auto ids = failed_event_ids(wr);
    check(!ids.node_failed.has_value(),
          std::string(label) + ".no_node_failed");
    check(ids.workflow_failed.has_value(),
          std::string(label) + ".wf_failed_event");

    std::size_t code_count = 0;
    const ahfl::Diagnostic *found = nullptr;
    for (const auto &diag : wr.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == expected_code) {
            ++code_count;
            found = &diag;
        }
    }
    check(code_count == 1, std::string(label) + ".single_entry");
    check(found != nullptr, std::string(label) + ".entry_present");
    if (found == nullptr) {
        return;
    }
    check(!found->message.empty(), std::string(label) + ".message_present");
    check(found->range.has_value(), std::string(label) + ".range_present");
    if (expected_range.has_value()) {
        check(found->range.has_value() && *found->range == *expected_range,
              std::string(label) + ".range_value");
    }
    // The WorkflowFailed event references the bag entry's diagnostic id.
    if (ids.workflow_failed.has_value()) {
        const auto idx = ids.workflow_failed->index();
        check(idx < wr.diagnostics.entries().size(),
              std::string(label) + ".diag_idx_oob");
        if (idx < wr.diagnostics.entries().size()) {
            const auto &diag = wr.diagnostics.entries()[idx];
            check(diag.code.has_value() && *diag.code == expected_code,
                  std::string(label) + ".wf_event_code");
        }
    }
}
// AHFL_CAP_ERROR reply traps the guest by design (the bridge runner has a
// graceful PENDING arm only; ERROR and any other non-OK status traps). The
// trap diagnostic (wasm.trap) carries the capability declaration's provenance
// range (the section-13 resolution fires on last_capability_error).
void test_wh5c6_capability_error_range(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_cap_err.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "wh5c6_cap_err.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_cap_range = first_capability_range(wf->program);
    check(expected_cap_range.has_value(), "wh5c6_cap_err.cap_range");

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        r.error_message = "boom";
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_cap_err.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.trap", true,
                                   expected_cap_range, "wh5c6_cap_err");
}

// (2) A host-abort (capability import failure): the diagnostic carries the
// capability declaration's provenance range.
void test_wh5c6_host_abort_capability_range(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_host_abort.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "wh5c6_host_abort.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_cap_range = first_capability_range(wf->program);
    check(expected_cap_range.has_value(), "wh5c6_host_abort.cap_range");

    // Resolve to a name NOT in the wire schema; the import executor
    // host-aborts (ResultSchemaInvalid on the NoneValue reply).
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
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_host_abort.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.host-abort", true,
                                   expected_cap_range, "wh5c6_host_abort");
}

// (3) A generic trap (no capability context): the diagnostic carries the
// failed node's WorkflowNode::source_range.
void test_wh5c6_trap_node_range(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh4_trap_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_trap_node.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input =
        value_from_json(R"({"_type":"wasm::wh4_trap_workflow::Frame","n":1})");
    check(input.has_value(), "wh5c6_trap_node.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_node_range = node_source_range(
        wf->program, wf->descriptor.workflow_name, 0);
    check(expected_node_range.has_value(), "wh5c6_trap_node.node_range");

    wh::WorkflowSessionConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_trap_node.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.trap", true,
                                   expected_node_range, "wh5c6_trap_node");
}

// (4) Resolver safety: with neither resolver installed, the same failing run
// still produces code+message diagnostics with a null range and no crash.
void test_wh5c6_no_resolvers_safe(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh4_trap_workflow.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_no_res.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input =
        value_from_json(R"({"_type":"wasm::wh4_trap_workflow::Frame","n":1})");
    check(input.has_value(), "wh5c6_no_res.input");
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
    // Deliberately NO node_range_resolver / capability_range_resolver.

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_no_res.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.trap", false,
                                   std::nullopt, "wh5c6_no_res");
}

// (5) Output-decode fail-closed: the kOutputDecodeFailed entry carries the
// decoded node's range (the P6 O_k corruption pin).
void test_wh5c6_decode_node_range(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5b_hybrid_rich_fidelity.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_decode.emit");
    if (!wf.has_value()) {
        return;
    }

    const auto &desc = wf->descriptor;
    check(!desc.nodes.empty(), "wh5c6_decode.nodes_nonempty");
    if (desc.frame_section.node_blocks.empty()) {
        return;
    }
    const auto ok_base = desc.frame_section.node_blocks[0].output_base;
    const auto ok_size = desc.frame_section.node_blocks[0].output_size;

    auto input = value_from_json(
        R"({"_type":"wasm::wh5b_hybrid_rich_fidelity::Frame",)"
        R"("n":1,"flag":true,)"
        R"("color":{"_enum":"wasm::wh5b_hybrid_rich_fidelity::Color","_variant":"Green"},)"
        R"("label":"rich"})");
    check(input.has_value(), "wh5c6_decode.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_node_range = node_source_range(
        wf->program, desc.workflow_name, 0);
    check(expected_node_range.has_value(), "wh5c6_decode.node_range");

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
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;
    config.post_run2_memory_mutator =
        [ok_base, ok_size](std::span<std::uint8_t> mem) {
            if (ok_base < mem.size()) {
                const auto n =
                    std::min<std::size_t>(ok_size, mem.size() - ok_base);
                std::memset(mem.data() + ok_base, 0xFF, n);
            }
        };

    auto result = wh::run_workflow_session(wf->module_bytes, desc, *input,
                                           std::move(config));
    check(result.has_value(), "wh5c6_decode.session");
    if (!result.has_value()) {
        return;
    }
    // The O_k corruption is a workflow-level EvaluationFailed (all nodes
    // completed during run2; the decode fail-closed fires afterward), so no
    // NodeFailed event is emitted -- the WorkflowFailed references the single
    // ranged wasm.output-decode-failed bag entry.
    check_wh5c6_workflow_only_failure(result->result,
                                      "wasm.output-decode-failed",
                                      expected_node_range, "wh5c6_decode");
}

// (6) Capability-range precedence: a SUCCESSFUL capability call on node 0
// followed by a generic trap on node 1 must attach node 1's range, NOT the
// successful capability's declaration range (encodes the last_error /
// last_capability_error guard in the section-13 resolution).
void test_wh5c6_capability_precedence(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c6_cap_then_trap.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_prec.emit");
    if (!wf.has_value()) {
        return;
    }

    auto input = value_from_json(
        R"({"_type":"wasm::wh5c6_cap_then_trap::Frame",)"
        R"("n":1,"value":"echo"})");
    check(input.has_value(), "wh5c6_prec.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_node_range = node_source_range(
        wf->program, wf->descriptor.workflow_name, 1);
    const auto cap_range = first_capability_range(wf->program);
    check(expected_node_range.has_value(), "wh5c6_prec.node_range");
    check(cap_range.has_value(), "wh5c6_prec.cap_range");

    wh::WorkflowSessionConfig config;
    // Echo SUCCEEDS on node 0; node 1 then divides by zero (generic trap).
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
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;

    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_prec.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.trap", true,
                                   expected_node_range, "wh5c6_prec");
    // The diagnostic must NOT carry the successful capability's range.
    const auto ids = failed_event_ids(result->result);
    if (ids.node_failed.has_value() &&
        ids.node_failed->index() < result->result.diagnostics.entries().size()) {
        const auto &diag =
            result->result.diagnostics.entries()[ids.node_failed->index()];
        check(diag.range.has_value(), "wh5c6_prec.diag_range_present");
        if (diag.range.has_value() && cap_range.has_value()) {
            check(!(*diag.range == *cap_range),
                  "wh5c6_prec.not_capability_range");
        }
    }
}

// (7) Schedule/source remap: the trap node is source id 0 but schedule
// position 1. The failure range MUST resolve through the descriptor
// node_id remap to the trap node's source range; indexing the source-order
// vector by schedule position would blame the successful echo node.
void test_wh5c6_reordered_schedule_range(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c6_reorder_trap.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c6_reorder.emit");
    if (!wf.has_value()) {
        return;
    }

    // Pin the precondition non-vacuously: descriptor nodes are in schedule
    // order and the trap node b (source id 0) is at schedule position 1.
    const auto &desc = wf->descriptor;
    check(desc.nodes.size() == 2, "wh5c6_reorder.two_nodes");
    if (desc.nodes.size() == 2) {
        check(desc.nodes[0].name == "a" && desc.nodes[0].node_id == 1,
              "wh5c6_reorder.schedule0_is_a");
        check(desc.nodes[1].name == "b" && desc.nodes[1].node_id == 0,
              "wh5c6_reorder.schedule1_is_b");
    }

    auto input = value_from_json(
        R"({"_type":"wasm::wh5c6_reorder_trap::Frame",)"
        R"("n":1,"value":"echo"})");
    check(input.has_value(), "wh5c6_reorder.input");
    if (!input.has_value()) {
        return;
    }

    auto resolvers = make_range_resolvers(*wf);
    const auto expected_trap_range = node_source_range(
        wf->program, desc.workflow_name, 0);
    const auto echo_node_range = node_source_range(
        wf->program, desc.workflow_name, 1);
    check(expected_trap_range.has_value(), "wh5c6_reorder.trap_range");
    check(echo_node_range.has_value(), "wh5c6_reorder.echo_range");

    wh::WorkflowSessionConfig config;
    // Echo SUCCEEDS (schedule position 0); the trap then fires at schedule
    // position 1 with no capability context.
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
    config.node_range_resolver = resolvers.node;
    config.capability_range_resolver = resolvers.capability;

    auto result = wh::run_workflow_session(wf->module_bytes, desc,
                                           *input, std::move(config));
    check(result.has_value(), "wh5c6_reorder.session");
    if (!result.has_value()) {
        return;
    }
    check_wh5c6_failure_diagnostic(result->result, "wasm.trap", true,
                                   expected_trap_range, "wh5c6_reorder");
    // The range must not be the echo node's source range (the value a
    // schedule-position-as-source-index bug would attach).
    const auto ids = failed_event_ids(result->result);
    if (ids.node_failed.has_value() &&
        ids.node_failed->index() <
            result->result.diagnostics.entries().size()) {
        const auto &diag =
            result->result.diagnostics.entries()[ids.node_failed->index()];
        if (diag.range.has_value() && echo_node_range.has_value()) {
            check(!(*diag.range == *echo_node_range),
                  "wh5c6_reorder.not_echo_node_range");
        }
    }
}

// ==== WH-5c.9 (GAP 9): non-final String PtrLen carry ====
//
// A non-final goto handler copies a String PtrLen from the input frame into a
// context slot (I_k -> C_k); a later computed-final handler reads it back.
// The 8-byte PtrLen header is copied (two i32 stores); the payload bytes
// borrow immutably from the entry payload arena. This pins the read-gate and
// store-gate lifts and the C_k persistence (zero-fill is once-per-node, not
// per-handler), for both an empty string and a long string spanning the
// payload arena.

namespace {

[[nodiscard]] Value make_string_frame(const std::string &type_name,
                                      const std::string &s) {
    Value input{ahfl::runtime::StructValue{}};
    auto &sv = std::get<ahfl::runtime::StructValue>(input.node);
    sv.type_name = type_name;
    sv.fields.set("s",
                  std::make_unique<Value>(Value{ahfl::runtime::StringValue{s}}));
    return input;
}

} // namespace

void test_wh5c9_string_carry(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c9_string_carry.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c9_carry.emit");
    if (!wf.has_value()) {
        return;
    }

    const std::string long_string(2048, 'x'); // spans the entry payload arena
    for (const auto &s : {std::string{""}, long_string}) {
        auto input =
            make_string_frame("wasm::wh5c9_string_carry::Frame", s);
        wh::WorkflowSessionConfig config;
        auto result = wh::run_workflow_session(wf->module_bytes,
                                               wf->descriptor, input,
                                               std::move(config));
        check(result.has_value(),
              std::string("wh5c9_carry.session.") +
                  (s.empty() ? "empty" : "long"));
        if (!result.has_value()) {
            std::cerr << "  error: " << result.error() << "\n";
            continue;
        }
        check(result->result.status() ==
                  ahfl::runtime::WorkflowStatus::Completed,
              std::string("wh5c9_carry.completed.") +
                  (s.empty() ? "empty" : "long"));
        const auto *output = result->result.output();
        check(output != nullptr,
              std::string("wh5c9_carry.has_output.") +
                  (s.empty() ? "empty" : "long"));
        if (output != nullptr) {
            const auto json = value_to_json(*output);
            const std::string expected =
                R"({"_type":"wasm::wh5c9_string_carry::Frame","s":")" + s +
                R"("})";
            check(json == expected,
                  std::string("wh5c9_carry.output_byte_parity.") +
                      (s.empty() ? "empty" : "long"));
        }
    }
}

// WH-5c.9 two-node edge pin: node `first` outputs a String, node `second`
// receives it as I_k (scheduler-materialized from the upstream O_k block),
// copies I_k -> C_k in a non-final handler, and returns it. The payload
// borrows immutably from the upstream O_k block, which is never zero-filled.

void test_wh5c9_string_edge(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c9_string_edge.ahfl";
    auto wf = emit_workflow(source);
    check(wf.has_value(), "wh5c9_edge.emit");
    if (!wf.has_value()) {
        return;
    }
    check(wf->descriptor.is_workflow, "wh5c9_edge.is_workflow");

    auto input = make_string_frame("wasm::wh5c9_string_edge::Frame",
                                   "edge-carried-string");
    wh::WorkflowSessionConfig config;
    auto result = wh::run_workflow_session(wf->module_bytes, wf->descriptor,
                                           input, std::move(config));
    check(result.has_value(), "wh5c9_edge.session");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "wh5c9_edge.completed");
    const auto *output = result->result.output();
    check(output != nullptr, "wh5c9_edge.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::wh5c9_string_edge::Frame","s":"edge-carried-string"})",
              "wh5c9_edge.output_byte_parity");
    }
}

// WH-5c.9 fail-closed pin: a String projected out of a scratch-CONSTRUCTED
// aggregate loses its input-frame provenance (the construct is not a
// path/alias edge the provenance walk follows), so the context store stays
// rejected with the updated diagnostic naming all three authorized
// provenances and carrying a SourceRange.

void test_wh5c9_string_construct_fail_closed(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/wh5c9_string_construct_fail_closed.ahfl";
    auto outcome = emit_workflow_or_diag(source);
    check(!outcome.wf.has_value(), "wh5c9_fc.rejected");
    bool found = false;
    for (const auto &d : outcome.diagnostics) {
        if (d.code !=
            ahfl::backends::core_wasm_diag::kUnsupportedCapabilityFrame) {
            continue;
        }
        found = true;
        check(d.message.find("rodata") != std::string::npos,
              "wh5c9_fc.names_rodata");
        check(d.message.find("capability bridge result") !=
                  std::string::npos,
              "wh5c9_fc.names_bridge");
        check(d.message.find("input frame") != std::string::npos,
              "wh5c9_fc.names_input_frame");
        check(d.source_range.has_value(), "wh5c9_fc.has_source_range");
    }
    check(found, "wh5c9_fc.diagnostic_present");
}

} // namespace

int main() {
    const auto repo_root = ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_p6_trace_workflow(repo_root);
    test_p6_capability_workflow(repo_root);
    test_p6_trace_low_branch(repo_root);
    test_event_sequence_identity(repo_root);
    test_event_sequence_and_report_fields(repo_root);
    test_host_abort_workflow(repo_root);
    test_trap_workflow(repo_root);
    test_trap_pipeline(repo_root);
    test_completed_count_fail_closed(repo_root);
    test_capability_kind_fail_closed(repo_root);
    test_capability_ordinal_oor_fail_closed(repo_root);
    // WH-5b.1: hybrid P6 + opaque emission/descriptor-only tests.
    test_hybrid_p6_before_cap(repo_root);
    test_hybrid_cap_before_p6(repo_root);
    test_hybrid_kahn_reordered(repo_root);
    // WH-5b.3 AC1(b): NodeOutput cross-lane + opaque-return fixtures.
    test_hybrid_p6_to_opaque(repo_root);
    test_hybrid_opaque_to_p6(repo_root);
    test_hybrid_opaque_return(repo_root);
    // WH-5b.3 P2-2: rich type fidelity across both transcode directions.
    test_hybrid_rich_fidelity(repo_root);
    test_hybrid_rich_p6_to_opaque(repo_root);
    // WH-5c.7: Decimal round-trip through the P6 frame (replaces the
    // WH-5b.3 fail-closed pin).
    test_hybrid_decimal_round_trip(repo_root);
    test_f64_bridge_fail_closed(repo_root);
    test_f64_bridge(repo_root);
    // WH-5c.7 P1-2: rich-type bridge-rejection pins.
    test_decimal_bridge_fail_closed(repo_root);
    test_duration_bridge_fail_closed(repo_root);
    test_map_bridge_fail_closed(repo_root);
    test_set_bridge_fail_closed(repo_root);
    // WH-5c.7 P1-1: Decimal/Duration spelling round-trip guard.
    test_decimal_spelling_guard(repo_root);
    test_duration_spelling_guard(repo_root);
    // WH-5c.7 P2-3: workflow declaration SourceRange on kInvalidLayout.
    test_workflow_decl_source_range(repo_root);
    // WH-5b.1 fix-forward: P6-bridge manifest byte shape + multi-P6 ordinals.
    test_hybrid_p6_bridge_manifest(repo_root);
    // WH-5b.2 §12.14.9 case 4: unknown call_site_id host trust check.
    test_wh5b2_case4_unknown_callsite_id(repo_root);
    // WH-5b.2 §12.14.9 case 3: forged frame-section bridge result regions
    // beyond the fixed 64 KiB page are rejected at session admission.
    test_wh5b2_case3_bridge_site_oob_fail_closed(repo_root);
    // WH-5c.5 GAP 4 pin (d): P6 O_k corruption fails closed.
    test_gap4_p6_ok_corruption_fail_closed(repo_root);
    // WH-5c.6: failure-diagnostic SourceRange + single-id parity.
    test_wh5c6_capability_error_range(repo_root);
    test_wh5c6_host_abort_capability_range(repo_root);
    test_wh5c6_trap_node_range(repo_root);
    test_wh5c6_no_resolvers_safe(repo_root);
    test_wh5c6_decode_node_range(repo_root);
    test_wh5c6_capability_precedence(repo_root);
    test_wh5c6_reordered_schedule_range(repo_root);

    test_wh5c9_string_carry(repo_root);
    test_wh5c9_string_edge(repo_root);
    test_wh5c9_string_construct_fail_closed(repo_root);

    std::cout << "workflow_session: " << g_checks << " checks passed\n";
    return 0;
}
