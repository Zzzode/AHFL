// RFC 0026 KR6.8 WH-4 fix-forward D-A: KAT for the wasm_runner facade.
//
// Drives the WasmWorkflowRuntime and WasmAgentRunner facade end-to-end:
// compiles AHFL-IR to wasm in the constructor / one-shot call, then runs
// the workflow / agent and pins the result status, output, and hook firing.
//
// Three lanes:
//   1. WasmWorkflowRuntime with an identity workflow (no capabilities):
//      verifies compile-once + run(name, input) -> WorkflowResult.
//   2. WasmWorkflowRuntime with a capability workflow (Echo): verifies
//      the capability invoker is wired through the facade and the output
//      is the echoed input.
//   3. WasmAgentRunner one-shot compile+run: verifies the agent facade
//      compiles and runs an agent from ir::Program, and agent_input_hook
//      fires LIVE before the step-walk (D-D).

#include "runtime/wasm_runner/wasm_agent_runner.hpp"
#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

#include "ahfl/runtime/execution_renderer.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "conformance/compile_source.hpp"
#include "common/project_input_support.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace ir = ahfl::ir;
namespace wr = ahfl::runtime::wasm_runner;
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

// ==== 1. WasmWorkflowRuntime: identity workflow (no capabilities) ====

void test_identity_workflow(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_identity_workflow.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "identity_wf.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    std::vector<std::string> hook_states;
    std::vector<std::string> hook_node_names;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.state_entered_hook =
        [&hook_states, &hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
        };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})");
    check(input.has_value(), "identity_wf.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run("wasm::e3_workflow::IdentityPipeline",
                              std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "identity_wf.completed");

    const auto *output = result.output();
    check(output != nullptr, "identity_wf.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})",
              "identity_wf.output_value");
    }

    // P2-5: elementwise state_sequence for the WireJson reconstruction set.
    // Each node walks Start -> Done; the flat sequence is schedule-order.
    check(hook_states.size() == 4, "identity_wf.hook_states_count");
    if (hook_states.size() == 4) {
        check(hook_states[0] == "Start", "identity_wf.hook_state_0");
        check(hook_states[1] == "Done", "identity_wf.hook_state_1");
        check(hook_states[2] == "Start", "identity_wf.hook_state_2");
        check(hook_states[3] == "Done", "identity_wf.hook_state_3");
    }
    check(hook_node_names.size() == 4,
          "identity_wf.hook_node_names_count");
    if (hook_node_names.size() == 4) {
        check(hook_node_names[0] == "first",
              "identity_wf.hook_node_name_0");
        check(hook_node_names[1] == "first",
              "identity_wf.hook_node_name_1");
        check(hook_node_names[2] == "second",
              "identity_wf.hook_node_name_2");
        check(hook_node_names[3] == "second",
              "identity_wf.hook_node_name_3");
    }
}

// ==== 2. WasmWorkflowRuntime: capability workflow (Echo) ====

void test_capability_workflow(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "cap_wf.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int cap_invoked_count = 0;
    std::vector<std::string> hook_states;
    std::vector<std::string> hook_node_names;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "cap_wf.hook_cap_name");
        };
    config.hooks.state_entered_hook =
        [&hook_states, &hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
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

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "cap_wf.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run("wasm::e3_capability_workflow::CapabilityPipeline",
                              std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "cap_wf.completed");

    // The Echo capability was invoked exactly once.
    check(cap_invoked_count == 1, "cap_wf.cap_invoked_count");

    // The output should equal the input (Echo echoes, SecondAgent is identity).
    const auto *output = result.output();
    check(output != nullptr, "cap_wf.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})",
              "cap_wf.output_value");
    }

    // D-B: the WireJson state reconstruction fires state_entered_hook
    // post-run with the REAL node names. Each node walks Start -> Done.
    check(hook_states.size() == 4, "cap_wf.hook_states_count");
    if (hook_states.size() == 4) {
        check(hook_states[0] == "Start", "cap_wf.hook_state_0");
        check(hook_states[1] == "Done", "cap_wf.hook_state_1");
        check(hook_states[2] == "Start", "cap_wf.hook_state_2");
        check(hook_states[3] == "Done", "cap_wf.hook_state_3");
    }
    check(hook_node_names.size() == 4, "cap_wf.hook_node_names_count");
    if (hook_node_names.size() == 4) {
        check(hook_node_names[0] == "first", "cap_wf.hook_node_name_0");
        check(hook_node_names[1] == "first", "cap_wf.hook_node_name_1");
        check(hook_node_names[2] == "second", "cap_wf.hook_node_name_2");
        check(hook_node_names[3] == "second", "cap_wf.hook_node_name_3");
    }
}

// ==== 2a-facade. P2-10: session failure propagates through FACADE ====

void test_workflow_session_failure_facade(
    const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "wf_sess_fail.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    // The invoker returns Error; the workflow's capability node traps
    // (unreachable on non-zero status). The FACADE must map the session
    // failure to a failed WorkflowResult (wasm.session-failed), NOT crash.
    wr::WasmWorkflowRuntimeConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };
    config.name_resolver = [](std::uint64_t) -> std::optional<std::string> {
        return "Echo";
    };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})");
    check(input.has_value(), "wf_sess_fail.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run("wasm::e3_capability_workflow::CapabilityPipeline",
                              std::move(*input));
    // P2-10: the FACADE must return a failed WorkflowResult when the
    // underlying session fails (trap from Error invoker).
    check(result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "wf_sess_fail.node_failed");
}

// ==== 2b. P2-5: WireJson reconstruction set -- resume fixture ====

void test_capability_workflow_resume(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e3_capability_workflow_resume.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "resume_wf.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int cap_invoked_count = 0;
    std::vector<std::string> hook_states;
    std::vector<std::string> hook_node_names;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "resume_wf.hook_cap_name");
        };
    config.hooks.state_entered_hook =
        [&hook_states, &hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
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

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"resume-echo"})");
    check(input.has_value(), "resume_wf.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run(
        "wasm::e3_capability_workflow_resume::CapabilityPipeline",
        std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "resume_wf.completed");

    check(cap_invoked_count == 1, "resume_wf.cap_invoked_count");

    const auto *output = result.output();
    check(output != nullptr, "resume_wf.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::e3_capability_workflow_resume::Frame","value":"resume-echo"})",
              "resume_wf.output_value");
    }

    // P2-5: elementwise state_sequence for the WireJson reconstruction set.
    check(hook_states.size() == 4, "resume_wf.hook_states_count");
    if (hook_states.size() == 4) {
        check(hook_states[0] == "Start", "resume_wf.hook_state_0");
        check(hook_states[1] == "Done", "resume_wf.hook_state_1");
        check(hook_states[2] == "Start", "resume_wf.hook_state_2");
        check(hook_states[3] == "Done", "resume_wf.hook_state_3");
    }
    check(hook_node_names.size() == 4,
          "resume_wf.hook_node_names_count");
    if (hook_node_names.size() == 4) {
        check(hook_node_names[0] == "first",
              "resume_wf.hook_node_name_0");
        check(hook_node_names[1] == "first",
              "resume_wf.hook_node_name_1");
        check(hook_node_names[2] == "second",
              "resume_wf.hook_node_name_2");
        check(hook_node_names[3] == "second",
              "resume_wf.hook_node_name_3");
    }
}

// ==== 2c. P2-5: WireJson reconstruction set -- float output fixture ====

void test_float_output_e2e(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/runtime/float_output_e2e.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "float_wf.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    std::vector<std::string> hook_states;
    std::vector<std::string> hook_node_names;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.state_entered_hook =
        [&hook_states, &hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view state_name) {
            hook_states.emplace_back(state_name);
            hook_node_names.emplace_back(node_name);
        };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"runtime::float_output_e2e::Sample","ratio":2.0})");
    check(input.has_value(), "float_wf.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run(
        "runtime::float_output_e2e::FloatPipeline", std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "float_wf.completed");

    const auto *output = result.output();
    check(output != nullptr, "float_wf.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"runtime::float_output_e2e::Sample","ratio":2.0})",
              "float_wf.output_value");
    }

    // P2-5: elementwise state_sequence for the WireJson reconstruction set.
    // Single agent, single node: Start -> Done.
    check(hook_states.size() == 2, "float_wf.hook_states_count");
    if (hook_states.size() == 2) {
        check(hook_states[0] == "Start", "float_wf.hook_state_0");
        check(hook_states[1] == "Done", "float_wf.hook_state_1");
    }
    check(hook_node_names.size() == 2,
          "float_wf.hook_node_names_count");
    if (hook_node_names.size() == 2) {
        check(hook_node_names[0] == "sample",
              "float_wf.hook_node_name_0");
        check(hook_node_names[1] == "sample",
              "float_wf.hook_node_name_1");
    }
}

// ==== 2d. P2-8: e2e_multi_agent through the FACADE ====
//
// Drives the real e2e_multi_agent fixture (3 agents, 5 capabilities,
// branching routing) through WasmWorkflowRuntime, verifying the facade
// compiles and runs a multi-agent workflow with capability dispatch,
// constructed-input nodes, and branching.

void test_e2e_multi_agent_facade(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/runtime/e2e_multi_agent.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "e2e_multi.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int cap_invoked_count = 0;
    std::vector<std::string> invoked_caps;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.capability_invoked_hook =
        [&cap_invoked_count, &invoked_caps](AgentId,
                                            std::string_view name) {
            ++cap_invoked_count;
            invoked_caps.emplace_back(name);
        };
    // Mock invoker: returns the canned result for each capability,
    // matching the conformance case manifest.
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &name,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        if (name == "runtime::e2e_multi_agent::ClassifyMessage") {
            auto v = value_from_json(
                R"({"_type":"runtime::e2e_multi_agent::ClassifyResult","category":{"_enum":"runtime::e2e_multi_agent::Category","_variant":"Technical"},"confidence":"high"})");
            if (v.has_value()) {
                r.value = std::move(*v);
            }
        } else if (name == "runtime::e2e_multi_agent::HandleGeneral") {
            auto v = value_from_json(
                R"({"_type":"runtime::e2e_multi_agent::SupportResult","resolved":true,"response":"Your issue has been resolved"})");
            if (v.has_value()) {
                r.value = std::move(*v);
            }
        } else if (name == "runtime::e2e_multi_agent::HandleTechnical") {
            auto v = value_from_json(
                R"({"_type":"runtime::e2e_multi_agent::SupportResult","resolved":true,"response":"Escalated to senior engineer"})");
            if (v.has_value()) {
                r.value = std::move(*v);
            }
        } else if (name == "runtime::e2e_multi_agent::GenerateSummary") {
            auto v = value_from_json(
                R"({"_type":"runtime::e2e_multi_agent::SummaryResult","category":{"_enum":"runtime::e2e_multi_agent::Category","_variant":"Technical"},"resolved":true,"summary":"Case resolved successfully"})");
            if (v.has_value()) {
                r.value = std::move(*v);
            }
        } else {
            r.status = CapabilityCallStatus::Error;
            r.error_message = "unknown capability: " + name;
        }
        return r;
    };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    // priority_low scenario: SupportAgent routes to Handling (not
    // Escalated), so HandleGeneral is called (not HandleTechnical).
    auto input = value_from_json(
        R"({"_type":"runtime::e2e_multi_agent::SupportRequest","message":"My server is crashing","priority":{"_enum":"runtime::e2e_multi_agent::Priority","_variant":"Low"},"user_id":"user_123"})");
    check(input.has_value(), "e2e_multi.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run(
        "runtime::e2e_multi_agent::CustomerSupportWorkflow",
        std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "e2e_multi.completed");

    // 3 capabilities invoked: ClassifyMessage, HandleGeneral,
    // GenerateSummary (priority_low routes to Handling).
    check(cap_invoked_count == 3, "e2e_multi.cap_invoked_count");
    if (invoked_caps.size() == 3) {
        check(invoked_caps[0] == "runtime::e2e_multi_agent::ClassifyMessage",
              "e2e_multi.cap_0");
        check(invoked_caps[1] == "runtime::e2e_multi_agent::HandleGeneral",
              "e2e_multi.cap_1");
        check(invoked_caps[2] == "runtime::e2e_multi_agent::GenerateSummary",
              "e2e_multi.cap_2");
    }

    // The output should be the SummaryResult from GenerateSummary.
    const auto *output = result.output();
    check(output != nullptr, "e2e_multi.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"runtime::e2e_multi_agent::SummaryResult","category":{"_enum":"runtime::e2e_multi_agent::Category","_variant":"Technical"},"resolved":true,"summary":"Case resolved successfully"})",
              "e2e_multi.output_value");
    }
}

// ==== 3. WasmAgentRunner: one-shot compile+run + agent_input_hook ====

void test_agent_runner(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e1_identity_agent.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "agent.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int agent_input_count = 0;
    std::vector<std::string> hook_states;

    wr::WasmAgentRunnerConfig config;
    config.hooks.agent_input_hook =
        [&agent_input_count](AgentId, std::string_view agent_name,
                             std::string_view node_name, const Value &) {
            ++agent_input_count;
            check(agent_name == "wasm::e1_identity::IdentityAgent",
                  "agent.hook_agent_name");
            // P1-1: signature parity with WorkflowRuntimeConfig. The wasm
            // agent lane has no workflow node, so node_name is always empty.
            check(node_name.empty(), "agent.hook_node_name_empty");
        };
    config.hooks.state_entered_hook =
        [&hook_states](AgentId, std::string_view, std::string_view,
                       std::string_view state_name) {
            hook_states.emplace_back(state_name);
        };

    auto input = value_from_json(
        R"({"_type":"wasm::e1_identity::Frame","value":"agent"})");
    check(input.has_value(), "agent.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(*program, "wasm::e1_identity::IdentityAgent",
                                     *input, std::move(config));
    check(result.has_value(), "agent.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // D-D: agent_input_hook fired LIVE before the step-walk.
    check(agent_input_count == 1, "agent.agent_input_hook_count");

    // The agent walked Start -> Done.
    check(hook_states.size() == 2, "agent.hook_states_count");
    if (hook_states.size() == 2) {
        check(hook_states[0] == "Start", "agent.hook_state_0");
        check(hook_states[1] == "Done", "agent.hook_state_1");
    }

    // The output should equal the input (identity agent).
    check(result->result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "agent.completed");
    const auto *output = result->result.output();
    check(output != nullptr, "agent.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == R"({"_type":"wasm::e1_identity::Frame","value":"agent"})",
              "agent.output_value");
    }
}

// ==== 4. P1-4: trap on agent lane (bridge lane non-zero status) ====

void test_agent_trap(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/v2c_single_arg_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "agent_trap.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    // The states_invoker (effects-free step-walk) returns Success so the
    // step-walk completes. The canonical invoker returns Error, which the
    // bridge lane maps to AHFL_CAP_ERROR; the guest's bridge code hits
    // unreachable on any non-zero status, trapping the canonical run.
    wr::WasmAgentRunnerConfig config;
    config.states_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(
            R"({"_type":"wasm::v2c_single_arg_bridge::RoutingDecision","owner":"agent-1"})");
        return r;
    };
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Error;
        return r;
    };

    auto input = value_from_json(
        R"({"_type":"wasm::v2c_single_arg_bridge::TicketRequest","ticket_id":"T-123"})");
    check(input.has_value(), "agent_trap.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(
        *program, "wasm::v2c_single_arg_bridge::RoutingAgent", *input,
        std::move(config));
    // P1-4: trap maps to a FAILED WorkflowResult with wasm.trap, NOT
    // std::unexpected (which is reserved for pre-run setup failures).
    check(result.has_value(), "agent_trap.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "agent_trap.node_failed");

    // P1-4: the diagnostic code is wasm.trap.
    bool found_trap = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == "wasm.trap") {
            found_trap = true;
            break;
        }
    }
    check(found_trap, "agent_trap.found_diagnostic");
}

// ==== 5. P1-4/P2-1: host-abort on agent lane (wrong result type) ====

void test_agent_host_abort(const std::filesystem::path &repo_root) {
    const auto source =
        repo_root / "tests/golden/wasm/e2_capability_agent.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "agent_host_abort.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    // The invoker returns a Value of the WRONG type (Int instead of
    // OutputFrame). The capability_import executor validates the result
    // against the result binding, fails with ResultSchemaInvalid, and
    // host-aborts.
    wr::WasmAgentRunnerConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = ahfl::runtime::Value{ahfl::runtime::IntValue{42}};
        return r;
    };

    auto input = value_from_json(
        R"({"_type":"wasm::e2_capability::InputFrame","value":"echo"})");
    check(input.has_value(), "agent_host_abort.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(
        *program, "wasm::e2_capability::CapabilityAgent", *input,
        std::move(config));
    // P1-4: host-abort maps to a FAILED WorkflowResult with
    // wasm.host-abort, NOT std::unexpected.
    check(result.has_value(), "agent_host_abort.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    check(result->result.status() == ahfl::runtime::WorkflowStatus::NodeFailed,
          "agent_host_abort.node_failed");

    // P1-4: the diagnostic code is wasm.host-abort.
    bool found_host_abort = false;
    for (const auto &diag : result->result.diagnostics.entries()) {
        if (diag.code.has_value() && *diag.code == "wasm.host-abort") {
            found_host_abort = true;
            // P2-1: the message carries the CapabilityImportError ENUM NAME.
            check(diag.message.find("CapabilityImportError=") !=
                      std::string::npos,
                  "agent_host_abort.enum_name_in_message");
            check(diag.message.find("ResultSchemaInvalid") !=
                      std::string::npos,
                  "agent_host_abort.enum_name_is_ResultSchemaInvalid");
            break;
        }
    }
    check(found_host_abort, "agent_host_abort.found_diagnostic");
}

// ==== 6. P1-6: states_invoker scripted-replay separation ====

void test_states_invoker_separation(const std::filesystem::path &repo_root) {
    // Use the v2c_single_arg_bridge P6-frame agent: on the bridge lane, the
    // step function executes the capability call in the Routing state, so
    // the states_invoker IS called during the effects-free step-walk (unlike
    // the WireJson opaque lane, where step() only transitions).
    const auto source =
        repo_root / "tests/golden/wasm/v2c_single_arg_bridge.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "sep.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    // Track which invoker was called and how many times.
    int walk_calls = 0;
    int canonical_calls = 0;

    // The states_invoker (effects-free step-walk) returns a SCRIPTED result.
    // It must be DISTINCT from the canonical invoker.
    wr::WasmAgentRunnerConfig config;
    config.states_invoker =
        [&walk_calls](const CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++walk_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(
            R"({"_type":"wasm::v2c_single_arg_bridge::RoutingDecision","owner":"scripted"})");
        return r;
    };
    // The canonical invoker returns a DIFFERENT result.
    config.invoker =
        [&canonical_calls](const CapabilityInvocationContext &,
                           const std::string &,
                           const std::vector<Value> &) -> CapabilityCallResult {
        ++canonical_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(
            R"({"_type":"wasm::v2c_single_arg_bridge::RoutingDecision","owner":"canonical"})");
        return r;
    };

    auto input = value_from_json(
        R"({"_type":"wasm::v2c_single_arg_bridge::TicketRequest","ticket_id":"T-123"})");
    check(input.has_value(), "sep.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(
        *program, "wasm::v2c_single_arg_bridge::RoutingAgent", *input,
        std::move(config));
    check(result.has_value(), "sep.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The step-walk called the states_invoker exactly once (one capability
    // call in the Routing state).
    check(walk_calls == 1, "sep.walk_calls");
    // The canonical run called the canonical invoker exactly once.
    check(canonical_calls == 1, "sep.canonical_calls");

    // The output reflects the CANONICAL invoker's result, not the scripted
    // walk result.
    const auto *output = result->result.output();
    check(output != nullptr, "sep.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::v2c_single_arg_bridge::RoutingDecision","owner":"canonical"})",
              "sep.output_is_canonical");
    }

    // The step-walk collected the state sequence (Init -> Routing -> Done).
    check(result->states.size() == 3, "sep.state_count");
}

// ==== 6b. P2-11: states_invoker multi-call (two calls in two states) ====

void test_states_invoker_multi_call(const std::filesystem::path &repo_root) {
    // v2c_bridge_chain calls RouteTicket in BOTH Routing and Routing2 states.
    // The states_invoker must fire once per call site (twice total) during
    // the effects-free step-walk, and the canonical invoker twice during
    // the canonical run.
    const auto source =
        repo_root / "tests/golden/wasm/v2c_bridge_chain.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "multi.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int walk_calls = 0;
    int canonical_calls = 0;

    wr::WasmAgentRunnerConfig config;
    config.states_invoker =
        [&walk_calls](const CapabilityInvocationContext &,
                      const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++walk_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(
            R"({"_type":"wasm::v2c_bridge_chain::RoutingDecision","owner":"scripted","route":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Web"}})");
        return r;
    };
    config.invoker =
        [&canonical_calls](const CapabilityInvocationContext &,
                           const std::string &,
                           const std::vector<Value> &) -> CapabilityCallResult {
        ++canonical_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(
            R"({"_type":"wasm::v2c_bridge_chain::RoutingDecision","owner":"canonical","route":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"}})");
        return r;
    };

    auto input = value_from_json(
        R"({"_type":"wasm::v2c_bridge_chain::TicketRequest","channel":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"},"ticket_id":"T-999","urgent":true})");
    check(input.has_value(), "multi.input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(
        *program, "wasm::v2c_bridge_chain::RoutingAgent", *input,
        std::move(config));
    check(result.has_value(), "multi.run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // The step-walk called the states_invoker exactly twice (one per call
    // site in Routing and Routing2).
    check(walk_calls == 2, "multi.walk_calls");
    // The canonical run called the canonical invoker exactly twice.
    check(canonical_calls == 2, "multi.canonical_calls");

    // The output reflects the CANONICAL invoker's result.
    const auto *output = result->result.output();
    check(output != nullptr, "multi.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json.find("\"owner\":\"canonical\"") != std::string::npos,
              "multi.output_is_canonical");
    }

    // The step-walk collected the full state sequence
    // (Init -> Routing -> Routing2 -> Done).
    check(result->states.size() == 4, "multi.state_count");
}

// ==== 6c. P2-12: shared-import attribution (two nodes, one capability) ====

void test_shared_import_attribution(const std::filesystem::path &repo_root) {
    // Two workflow nodes share the SAME Echo capability import. The
    // symbol_to_runner map uses last-wins attribution; this test verifies
    // the workflow runs correctly and both invocations are observed.
    const auto source =
        repo_root / "tests/golden/wasm/p2_12_shared_import.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "shared.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    int cap_invoked_count = 0;
    std::vector<std::string> hook_node_names;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.capability_invoked_hook =
        [&cap_invoked_count](AgentId, std::string_view name) {
            ++cap_invoked_count;
            check(name == "Echo", "shared.hook_cap_name");
        };
    config.hooks.state_entered_hook =
        [&hook_node_names](
            AgentId, std::string_view, std::string_view node_name,
            std::string_view) {
            hook_node_names.emplace_back(node_name);
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

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::p2_12_shared_import::Frame","value":"hello"})");
    check(input.has_value(), "shared.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run(
        "wasm::p2_12_shared_import::SharedEchoPipeline", std::move(*input));
    check(result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "shared.completed");

    // The Echo capability was invoked exactly twice (once per node).
    check(cap_invoked_count == 2, "shared.cap_invoked_count");

    // The output should equal the input (Echo echoes through both nodes).
    const auto *output = result.output();
    check(output != nullptr, "shared.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json ==
                  R"({"_type":"wasm::p2_12_shared_import::Frame","value":"hello"})",
              "shared.output_value");
    }

    // Each node walks Start -> Done (4 state entries total).
    check(hook_node_names.size() == 4, "shared.hook_node_names_count");
}

// ==== 6d. P2-13: branching-WireJson rejection (computed-goto terminal) ====

void test_branching_wirejson_rejection(
    const std::filesystem::path &repo_root) {
    // A WireJson agent whose walk terminal is a ComputedGotoAction must be
    // rejected at codegen time with wasm.UNSUPPORTED_ORCHESTRATION.
    const auto source =
        repo_root / "tests/golden/wasm/p2_13_branching_wirejson.ahfl";
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "branch.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    wr::WasmWorkflowRuntimeConfig config;
    config.invoker = [](const CapabilityInvocationContext &,
                        const std::string &,
                        const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        return r;
    };

    wr::WasmWorkflowRuntime runtime(*program, std::move(config));

    auto input = value_from_json(
        R"({"_type":"wasm::p2_13_branching_wirejson::Frame","value":"test","urgent":true})");
    check(input.has_value(), "branch.input");
    if (!input.has_value()) {
        return;
    }

    auto result = runtime.run(
        "wasm::p2_13_branching_wirejson::BranchWorkflow", std::move(*input));
    // The workflow must be rejected (not Completed) because the agent's
    // walk terminal is a ComputedGotoAction.
    check(result.status() != ahfl::runtime::WorkflowStatus::Completed,
          "branch.rejected");

    // The diagnostic must carry wasm.compile-failed (the facade maps
    // codegen rejection to a compile error).
    bool found_compile_failed = false;
    for (const auto &diag : result.diagnostics.entries()) {
        if (diag.code.has_value() &&
            *diag.code == "wasm.compile-failed") {
            found_compile_failed = true;
            break;
        }
    }
    check(found_compile_failed, "branch.found_diagnostic");
}

// ==== 7. P1-7: v2c bridge fixtures through WH-3 executor on wasm3 ====

// Helper: drive a v2c bridge agent through the facade (which uses the WH-3
// capability_import executor on real wasm3) and assert elementwise:
// state_sequence, capability_sequence, capability_arguments, output.
// Every fixture must compile and run; a compile or run failure is a test
// failure, not a silent skip.
void run_v2c_bridge_fixture(
    const std::filesystem::path &repo_root, std::string_view fixture_name,
    std::string_view agent_name, std::string_view input_json,
    std::string_view capability_result_json,
    std::span<const std::string_view> expected_states,
    std::span<const std::string_view> expected_capabilities,
    std::span<const std::string_view> expected_cap_args,
    std::string_view expected_output_json) {
    const auto source =
        repo_root / "tests/golden/wasm" /
        (std::string(fixture_name) + ".ahfl");
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(),
          std::string(fixture_name) + ": compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    wr::WasmAgentRunnerConfig config;
    config.invoker =
        [&capability_result_json](
            const CapabilityInvocationContext &, const std::string &,
            const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = value_from_json(std::string(capability_result_json));
        return r;
    };

    auto input = value_from_json(std::string(input_json));
    check(input.has_value(), std::string(fixture_name) + ": input");
    if (!input.has_value()) {
        return;
    }

    auto result = wr::run_wasm_agent(
        *program, std::string(agent_name), *input, std::move(config));
    check(result.has_value(), std::string(fixture_name) + ": run");
    if (!result.has_value()) {
        std::cerr << "  error: " << result.error() << "\n";
        return;
    }

    // Elementwise: state_sequence.
    check(result->states.size() == expected_states.size(),
          std::string(fixture_name) + ": state_count");
    for (std::size_t i = 0; i < expected_states.size() &&
                           i < result->states.size();
         ++i) {
        check(result->states[i].state == expected_states[i],
              std::string(fixture_name) + ": state[" + std::to_string(i) +
                  "]");
    }

    // Elementwise: capability_sequence.
    check(result->capabilities.size() == expected_capabilities.size(),
          std::string(fixture_name) + ": cap_count");
    for (std::size_t i = 0; i < expected_capabilities.size() &&
                           i < result->capabilities.size();
         ++i) {
        check(result->capabilities[i] == expected_capabilities[i],
              std::string(fixture_name) + ": cap[" + std::to_string(i) +
                  "]");
    }

    // Elementwise: capability_arguments envelope (BYTE comparison).
    check(result->capability_arguments.size() == expected_cap_args.size(),
          std::string(fixture_name) + ": cap_args_count");
    for (std::size_t i = 0; i < expected_cap_args.size() &&
                           i < result->capability_arguments.size();
         ++i) {
        check(result->capability_arguments[i] == expected_cap_args[i],
              std::string(fixture_name) + ": cap_args[" + std::to_string(i) +
                  "]");
    }

    // Elementwise: output.
    const auto *output = result->result.output();
    check(output != nullptr, std::string(fixture_name) + ": has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == expected_output_json,
              std::string(fixture_name) + ": output");
    }
}

void test_v2c_bridge_fixtures(const std::filesystem::path &repo_root) {
    // v2c_single_enum_bridge: Init -> Classify -> Done, one ClassifyChannel
    // call with a tag-only-enum argument. The envelope wraps the non-Struct
    // single argument as {"value":..}.
    {
        static constexpr std::string_view states[] = {"Init", "Classify",
                                                      "Done"};
        static constexpr std::string_view caps[] = {
            "wasm::v2c_single_enum_bridge::ClassifyChannel"};
        static constexpr std::string_view cap_args[] = {
            R"({"value":{"_enum":"wasm::v2c_single_enum_bridge::Channel","_variant":"Phone"}})"};
        run_v2c_bridge_fixture(
            repo_root, "v2c_single_enum_bridge",
            "wasm::v2c_single_enum_bridge::RoutingAgent",
            R"({"_type":"wasm::v2c_single_enum_bridge::TicketRequest","channel":{"_enum":"wasm::v2c_single_enum_bridge::Channel","_variant":"Phone"}})",
            R"({"_type":"wasm::v2c_single_enum_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_single_enum_bridge::Channel","_variant":"Phone"}})",
            states, caps, cap_args,
            R"({"_type":"wasm::v2c_single_enum_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_single_enum_bridge::Channel","_variant":"Phone"}})");
    }

    // v2c_route_then_bridge: Init -> Routing -> Done, one RouteTicket call
    // with (String, Int, Bool) args. The envelope is the multi-arg
    // {"args":[...]} form.
    {
        static constexpr std::string_view states[] = {"Init", "Routing",
                                                      "Done"};
        static constexpr std::string_view caps[] = {
            "wasm::v2c_route_then_bridge::RouteTicket"};
        static constexpr std::string_view cap_args[] = {
            R"({"args":["TKT-42",1,true]})"};
        run_v2c_bridge_fixture(
            repo_root, "v2c_route_then_bridge",
            "wasm::v2c_route_then_bridge::RoutingAgent",
            R"({"_type":"wasm::v2c_route_then_bridge::TicketRequest","channel":{"_enum":"wasm::v2c_route_then_bridge::Channel","_variant":"Phone"},"ticket_id":"TKT-42","urgent":true})",
            R"({"_type":"wasm::v2c_route_then_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_route_then_bridge::Channel","_variant":"Phone"}})",
            states, caps, cap_args,
            R"({"_type":"wasm::v2c_route_then_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_route_then_bridge::Channel","_variant":"Phone"}})");
    }

    // v2c_bridge_chain: Init -> Routing -> Routing2 -> Done, TWO RouteTicket
    // calls (same capability, chained handlers). Both calls carry identical
    // argument envelopes.
    {
        static constexpr std::string_view states[] = {
            "Init", "Routing", "Routing2", "Done"};
        static constexpr std::string_view caps[] = {
            "wasm::v2c_bridge_chain::RouteTicket",
            "wasm::v2c_bridge_chain::RouteTicket"};
        static constexpr std::string_view cap_args[] = {
            R"({"args":["TKT-42",{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"},true]})",
            R"({"args":["TKT-42",{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"},true]})"};
        run_v2c_bridge_fixture(
            repo_root, "v2c_bridge_chain",
            "wasm::v2c_bridge_chain::RoutingAgent",
            R"({"_type":"wasm::v2c_bridge_chain::TicketRequest","channel":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"},"ticket_id":"TKT-42","urgent":true})",
            R"({"_type":"wasm::v2c_bridge_chain::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"}})",
            states, caps, cap_args,
            R"({"_type":"wasm::v2c_bridge_chain::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_bridge_chain::Channel","_variant":"Phone"}})");
    }

    // v2c_multi_arg_bridge: Init -> Routing -> Done, one RouteTicket call
    // with (String, Enum, Bool) args. The envelope is the multi-arg
    // {"args":[...]} form. This fixture was previously rejected by A2
    // (params != 1); the P2-8 bridge-source-symbol relaxation admits it.
    {
        static constexpr std::string_view states[] = {"Init", "Routing",
                                                      "Done"};
        static constexpr std::string_view caps[] = {
            "wasm::v2c_multi_arg_bridge::RouteTicket"};
        static constexpr std::string_view cap_args[] = {
            R"({"args":["TKT-42",{"_enum":"wasm::v2c_multi_arg_bridge::Channel","_variant":"Phone"},true]})"};
        run_v2c_bridge_fixture(
            repo_root, "v2c_multi_arg_bridge",
            "wasm::v2c_multi_arg_bridge::RoutingAgent",
            R"({"_type":"wasm::v2c_multi_arg_bridge::TicketRequest","channel":{"_enum":"wasm::v2c_multi_arg_bridge::Channel","_variant":"Phone"},"ticket_id":"TKT-42","urgent":true})",
            R"({"_type":"wasm::v2c_multi_arg_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_multi_arg_bridge::Channel","_variant":"Phone"}})",
            states, caps, cap_args,
            R"({"_type":"wasm::v2c_multi_arg_bridge::RoutingDecision","owner":"senior","route":{"_enum":"wasm::v2c_multi_arg_bridge::Channel","_variant":"Phone"}})");
    }
}

// ==== 8. P1-2: byte-compare ahfl.run-report JSON wasm vs evaluator ====

// Render a WorkflowResult as ahfl.run-report JSON and return the string.
[[nodiscard]] std::string render_report_json(
    const ahfl::runtime::WorkflowResult &result) {
    ahfl::runtime::ExecutionOutputOptions opts;
    opts.format = ahfl::runtime::ExecutionOutputFormat::Json;
    std::ostringstream out;
    auto rendered = ahfl::runtime::render_execution_result(result, opts, out);
    check(rendered.has_value(), "byte_compare.render_ok");
    return out.str();
}

// For WireJson workflows the wasm lane cannot observe individual node outputs
// (the module passes them through its own heap; only the workflow output is
// host-observable via run2). The evaluator stores node outputs in its value
// store, so output_value_id differs. This helper strips that field from both
// JSON strings so the rest of the report can be byte-compared.
[[nodiscard]] std::string strip_output_value_id(std::string json) {
    const std::string key = "\"output_value_id\":";
    std::size_t pos = 0;
    while ((pos = json.find(key, pos)) != std::string::npos) {
        // Find the value start (skip the key and any whitespace).
        std::size_t val_start = pos + key.size();
        // Skip the value (null, number, ...) up to the next comma or brace.
        std::size_t val_end = val_start;
        while (val_end < json.size() && json[val_end] != ',' &&
               json[val_end] != '}') {
            ++val_end;
        }
        json.replace(pos, val_end - pos, "\"output_value_id\":null");
        pos = pos + key.size() + 4; // skip past the replaced null
    }
    return json;
}

// Byte-compare the ahfl.run-report JSON from the evaluator-backed
// WorkflowRuntime and the wasm-backed WasmWorkflowRuntime on a shared
// fixture. The evaluator gets a deterministic monotonic clock (all offsets
// 0, matching the wasm lane's std::chrono::nanoseconds{0}).
//
// For P6-frame fixtures node outputs ARE host-observable (O_k blocks), so
// the full report byte-compares. For WireJson fixtures node outputs are not
// host-observable, so output_value_id is stripped before comparison.
void byte_compare_fixture(
    const std::filesystem::path &repo_root, std::string_view fixture_path,
    std::string_view workflow_name, std::string_view input_json,
    std::string_view expected_output_json, bool has_capability,
    bool is_p6_frame) {
    const auto source = repo_root / fixture_path;
    std::string error;
    auto program = conf::compile_conformance_source(source, error);
    check(program.has_value(), "byte_compare.compile");
    if (!program.has_value()) {
        std::cerr << "  compile failed: " << error << "\n";
        return;
    }

    auto input = value_from_json(std::string(input_json));
    check(input.has_value(), "byte_compare.input");
    if (!input.has_value()) {
        return;
    }

    // --- Evaluator lane ---
    ahfl::runtime::WorkflowRuntimeConfig eval_config;
    // Deterministic clock: always return the same time point so all offsets
    // are 0 (matching the wasm lane).
    eval_config.monotonic_clock = []() -> std::chrono::steady_clock::time_point {
        return std::chrono::steady_clock::time_point{};
    };
    if (has_capability) {
        eval_config.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &,
               const std::vector<Value> &args) -> CapabilityCallResult {
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            if (!args.empty()) {
                r.value = ahfl::runtime::clone_value(args[0]);
            }
            return r;
        };
    }
    ahfl::runtime::WorkflowRuntime eval_runtime(*program, std::move(eval_config));
    auto eval_result = eval_runtime.run(std::string(workflow_name),
                                        ahfl::runtime::clone_value(*input));
    check(eval_result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "byte_compare.eval_completed");

    // --- Wasm lane ---
    wr::WasmWorkflowRuntimeConfig wasm_config;
    // The wasm lane decodes P6-frame node outputs only when
    // node_completed_hook is set (the decode is gated on the hook to avoid
    // reading guest memory when no consumer wants it). Set a no-op hook so
    // the lifecycle events carry real output_value_ids for P6-frame fixtures.
    int hook_count = 0;
    if (is_p6_frame) {
        wasm_config.hooks.node_completed_hook =
            [&hook_count](AgentId, std::string_view, const Value &) {
                ++hook_count;
            };
    }
    if (has_capability) {
        wasm_config.invoker =
            [](const CapabilityInvocationContext &, const std::string &,
               const std::vector<Value> &args) -> CapabilityCallResult {
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            if (!args.empty()) {
                r.value = ahfl::runtime::clone_value(args[0]);
            }
            return r;
        };
        wasm_config.name_resolver =
            [](std::uint64_t) -> std::optional<std::string> {
            return "Echo";
        };
    }
    wr::WasmWorkflowRuntime wasm_runtime(*program, std::move(wasm_config));
    auto wasm_result = wasm_runtime.run(std::string(workflow_name),
                                        ahfl::runtime::clone_value(*input));
    check(wasm_result.status() == ahfl::runtime::WorkflowStatus::Completed,
          "byte_compare.wasm_completed");
    if (is_p6_frame) {
        check(hook_count > 0, "byte_compare.node_completed_hook_fired");
    }

    // --- Byte-compare ---
    auto eval_json = render_report_json(eval_result);
    auto wasm_json = render_report_json(wasm_result);
    if (!is_p6_frame) {
        // WireJson: node outputs are not host-observable on the wasm lane.
        eval_json = strip_output_value_id(std::move(eval_json));
        wasm_json = strip_output_value_id(std::move(wasm_json));
    }
    if (eval_json != wasm_json) {
        std::cerr << "  EVAL: " << eval_json << "\n";
        std::cerr << "  WASM: " << wasm_json << "\n";
    }
    check(eval_json == wasm_json,
          std::string("byte_compare.parity: ") + std::string(fixture_path));

    // Also verify the output value matches the expected.
    const auto *output = wasm_result.output();
    check(output != nullptr, "byte_compare.has_output");
    if (output != nullptr) {
        const auto json = value_to_json(*output);
        check(json == expected_output_json, "byte_compare.output_value");
    }
}

void test_byte_compare(const std::filesystem::path &repo_root) {
    // Agent-direct (WireJson, two identity agents, no capabilities):
    // e3_identity_workflow. Node outputs are not host-observable on the
    // WireJson lane, so output_value_id is stripped before comparison.
    byte_compare_fixture(
        repo_root, "tests/golden/wasm/e3_identity_workflow.ahfl",
        "wasm::e3_workflow::IdentityPipeline",
        R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})",
        R"({"_type":"wasm::e3_workflow::Frame","value":"identity"})",
        /*has_capability=*/false,
        /*is_p6_frame=*/false);

    // Workflow with capability (WireJson, two agents, one Echo):
    // e3_capability_workflow. Same output_value_id stripping.
    byte_compare_fixture(
        repo_root, "tests/golden/wasm/e3_capability_workflow.ahfl",
        "wasm::e3_capability_workflow::CapabilityPipeline",
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})",
        R"({"_type":"wasm::e3_capability_workflow::Frame","value":"echo"})",
        /*has_capability=*/true,
        /*is_p6_frame=*/false);
}

} // namespace

int main() {
    const auto repo_root =
        ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_identity_workflow(repo_root);
    test_capability_workflow(repo_root);
    test_workflow_session_failure_facade(repo_root);
    test_capability_workflow_resume(repo_root);
    test_float_output_e2e(repo_root);
    test_e2e_multi_agent_facade(repo_root);
    test_agent_runner(repo_root);
    test_agent_trap(repo_root);
    test_agent_host_abort(repo_root);
    test_states_invoker_separation(repo_root);
    test_states_invoker_multi_call(repo_root);
    test_shared_import_attribution(repo_root);
    test_branching_wirejson_rejection(repo_root);
    test_v2c_bridge_fixtures(repo_root);
    test_byte_compare(repo_root);

    std::cout << "wasm_runner: " << g_checks << " checks passed\n";
    return 0;
}
