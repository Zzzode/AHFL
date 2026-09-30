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

#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "conformance/compile_source.hpp"
#include "common/project_input_support.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
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

    wr::WasmWorkflowRuntimeConfig config;
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

} // namespace

int main() {
    const auto repo_root =
        ahfl::test_support::repo_root_from_source_file(__FILE__);

    test_identity_workflow(repo_root);
    test_capability_workflow(repo_root);
    test_agent_runner(repo_root);
    test_agent_trap(repo_root);
    test_agent_host_abort(repo_root);
    test_states_invoker_separation(repo_root);

    std::cout << "wasm_runner: " << g_checks << " checks passed\n";
    return 0;
}
