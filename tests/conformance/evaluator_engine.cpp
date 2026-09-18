#include "conformance/evaluator_engine.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/program_view.hpp"
#include "base/json/json_value.hpp"
#include "conformance/compile_source.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

namespace ahfl::conformance {

namespace {

using ahfl::ir::Program;
using ahfl::evaluator::Value;
using ahfl::runtime::AgentId;
using ahfl::runtime::AgentResult;
using ahfl::runtime::AgentRuntime;
using ahfl::runtime::AgentStatus;
using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityRegistry;
using ahfl::runtime::WorkflowResult;
using ahfl::runtime::WorkflowRuntime;
using ahfl::runtime::WorkflowRuntimeConfig;
using ahfl::runtime::WorkflowStatus;
namespace json = ahfl::json;

// Builds the mock capability table declared by the case. `ok` outcomes are
// materialized ONCE from canonical wire JSON (value_from_json) and cloned per
// invocation through register_function; `error` / `pending` outcomes return
// their terminal status directly. This deliberately mirrors the canonical
// native-value path rather than the CLI's string-wrapping LLM-tool seam.
[[nodiscard]] std::optional<CapabilityRegistry>
build_mock_registry(const ConformanceCase &manifest, std::string &error_out) {
    CapabilityRegistry registry;
    for (const auto &capability : manifest.capabilities) {
        switch (capability.status) {
        case CapabilityOutcomeStatus::Ok: {
            if (!capability.result_json.has_value()) {
                error_out = "manifest capability '" + capability.name +
                            "' is 'ok' without a result frame";
                return std::nullopt;
            }
            auto canned = evaluator::value_from_json(*capability.result_json);
            if (!canned.has_value()) {
                error_out = "failed to decode result_json for capability '" + capability.name +
                            "'";
                return std::nullopt;
            }
            auto shared = std::make_shared<Value>(std::move(*canned));
            registry.register_function(
                capability.name,
                [shared](const std::vector<Value> &) -> Value {
                    return evaluator::clone_value(*shared);
                });
            break;
        }
        case CapabilityOutcomeStatus::Error: {
            // Register directly so the mock can return a non-Success terminal
            // (register_function only models an `ok` outcome).
            const std::string cap_name = capability.name;
            const std::string detail = capability.result_json.value_or("");
            ahfl::runtime::CapabilityBinding binding;
            binding.name = cap_name;
            binding.handler = [cap_name, detail](const std::vector<Value> &) -> CapabilityCallResult {
                CapabilityCallResult result;
                result.status = CapabilityCallStatus::Error;
                result.error_message =
                    "conformance mock error for capability '" + cap_name + "'" +
                    (detail.empty() ? std::string{} : ": " + detail);
                return result;
            };
            registry.register_capability(std::move(binding));
            break;
        }
        case CapabilityOutcomeStatus::Pending: {
            ahfl::runtime::CapabilityBinding binding;
            binding.name = capability.name;
            binding.handler = [](const std::vector<Value> &) -> CapabilityCallResult {
                CapabilityCallResult result;
                result.status = CapabilityCallStatus::Pending;
                return result;
            };
            registry.register_capability(std::move(binding));
            break;
        }
        }
    }
    return registry;
}

[[nodiscard]] const char *status_name_workflow(WorkflowStatus status) {
    switch (status) {
    case WorkflowStatus::Completed:
        return "completed";
    case WorkflowStatus::Suspended:
        return "suspended";
    case WorkflowStatus::NodeFailed:
    case WorkflowStatus::DependencyFailed:
    case WorkflowStatus::EvalError:
        return "failed";
    }
    return "failed";
}

[[nodiscard]] const char *status_name_agent(AgentStatus status) {
    switch (status) {
    case AgentStatus::Completed:
        return "completed";
    case AgentStatus::Suspended:
        return "suspended";
    case AgentStatus::Running:
    case AgentStatus::Failed:
    case AgentStatus::QuotaExceeded:
    case AgentStatus::InvalidTransition:
    case AgentStatus::InfiniteLoop:
        return "failed";
    }
    return "failed";
}

// A fixed monotonic clock: every run reads the same time point, so execution
// events carry zero offsets and observations never depend on wall time.
[[nodiscard]] std::chrono::steady_clock::time_point fixed_clock() {
    return std::chrono::steady_clock::time_point{};
}

[[nodiscard]] std::unique_ptr<json::JsonValue> make_string_node(std::string value) {
    return json::JsonValue::make_string(std::move(value));
}

// Serializes one engine run into the canonical observation DOM. `states` are
// declaration-order (agent, state) pairs; `capabilities` the canonical
// capability-name sequence.
[[nodiscard]] std::string render_observation(const ConformanceCase &manifest,
                                             const ConformanceScenario &scenario,
                                             const char *status,
                                             const std::vector<std::pair<std::string, std::string>>
                                                 &states,
                                             const std::vector<std::string> &capabilities,
                                             const Value *output) {
    auto root = json::JsonValue::make_object();
    root->set("schema", make_string_node("ahfl.evaluator-observation.v1"));
    root->set("case", make_string_node(manifest.source));
    root->set("scenario", make_string_node(scenario.name));
    root->set("status", make_string_node(status));

    auto state_array = json::JsonValue::make_array();
    for (const auto &[agent_name, state_name] : states) {
        auto entry = json::JsonValue::make_object();
        entry->set("agent", make_string_node(agent_name));
        entry->set("state", make_string_node(state_name));
        state_array->push(std::move(entry));
    }
    root->set("state_sequence", std::move(state_array));

    auto capability_array = json::JsonValue::make_array();
    for (const auto &capability : capabilities) {
        capability_array->push(make_string_node(capability));
    }
    root->set("capability_sequence", std::move(capability_array));

    if (output != nullptr) {
        // value_to_json is canonical compact wire JSON; re-parse only to embed
        // the value subtree (never as a string) into the observation DOM.
        auto output_dom = json::parse_json(evaluator::value_to_json(*output));
        if (output_dom.has_value() && *output_dom) {
            root->set("output_json", std::move(*output_dom));
        }
    }

    // Emit the envelope through the SAME canonical emitter the manifest
    // expectation gate uses (detail::canonical_json): `_type` stays first,
    // remaining keys sort deterministically, and the embedded output subtree
    // reproduces value_to_json byte-for-byte for every kind. Serializing with
    // json::serialize_json instead would diverge on integral-valued floats,
    // whose float syntax (e.g. "2.0") it would collapse to a bare integer
    // ("2") - the non-canonical spelling the wire codec's "no int widening"
    // contract rejects - making such a case un-blessable and any cross-engine
    // adapter that emits canonical bytes fail the byte gate falsely.
    return detail::canonical_json(*root);
}

[[nodiscard]] EvaluatorScenarioResult
run_workflow(const Program &program,
             const ConformanceCase &manifest,
             const ConformanceScenario &scenario,
             CapabilityRegistry &registry,
             Value input) {
    std::vector<std::pair<std::string, std::string>> states;
    std::vector<std::string> capabilities;

    WorkflowRuntimeConfig config;
    config.capability_invoker = registry.as_invoker();
    config.monotonic_clock = fixed_clock;
    config.state_entered_hook =
        [&states](AgentId,
                  std::string_view agent_name,
                  std::string_view,
                  std::string_view state_name) {
            states.emplace_back(std::string{agent_name}, std::string{state_name});
        };
    config.capability_invoked_hook = [&capabilities](AgentId, std::string_view name) {
        capabilities.emplace_back(name);
    };

    WorkflowRuntime runtime(program, std::move(config));
    const WorkflowResult result = runtime.run(manifest.entry, std::move(input));

    EvaluatorScenarioResult outcome;
    outcome.ok = true;
    outcome.observation_json =
        render_observation(manifest, scenario, status_name_workflow(result.status()), states,
                           capabilities, result.output());
    return outcome;
}

[[nodiscard]] EvaluatorScenarioResult
run_agent(const Program &program,
          const ConformanceCase &manifest,
          const ConformanceScenario &scenario,
          CapabilityRegistry &registry,
          Value input) {
    const ahfl::ir::ProgramIndex index(program);
    const auto *agent_decl = index.find_agent(manifest.entry);
    const auto *flow_decl = index.find_flow_for_agent(manifest.entry);
    if (agent_decl == nullptr || flow_decl == nullptr) {
        EvaluatorScenarioResult outcome;
        outcome.error = "agent entry '" + manifest.entry + "' not found in compiled program";
        return outcome;
    }

    std::vector<std::pair<std::string, std::string>> states;
    std::vector<std::string> capabilities;

    // Wrap the registry invoker so capability invocations are observed with
    // their canonical callee name (AgentRuntime has no workflow-style invoked
    // hook; the non-contextual invoker receives the canonical callee).
    ahfl::runtime::CapabilityInvoker recording_invoker =
        [&registry, &capabilities](const std::string &name,
                                   const std::vector<Value> &args) -> CapabilityCallResult {
        capabilities.push_back(name);
        return registry.invoke(name, args);
    };

    AgentRuntime runtime(*agent_decl, *flow_decl);
    runtime.set_capability_invoker(std::move(recording_invoker));
    ahfl::runtime::CapabilityInvocationContext invocation_context;
    invocation_context.agent_id = AgentId{0};
    runtime.set_invocation_context(invocation_context);
    runtime.set_state_entered_observer(
        [&states, agent_name = manifest.entry](AgentId,
                                               std::string_view state_name) -> ahfl::runtime::AgentStateId {
            states.emplace_back(agent_name, std::string{state_name});
            return ahfl::runtime::AgentStateId{};
        });

    const AgentResult result = runtime.run(std::move(input));

    EvaluatorScenarioResult outcome;
    outcome.ok = true;
    const Value *output = result.output.has_value() ? &*result.output : nullptr;
    outcome.observation_json =
        render_observation(manifest, scenario, status_name_agent(result.status), states,
                           capabilities, output);
    return outcome;
}

} // namespace

EvaluatorScenarioResult run_evaluator_scenario(const LoadedConformanceCase &loaded,
                                               const ConformanceScenario &scenario) {
    const ConformanceCase &manifest = loaded.manifest;

    std::string error;
    auto program = compile_conformance_source(loaded.source_path, error);
    if (!program.has_value()) {
        return EvaluatorScenarioResult{.ok = false, .observation_json = {}, .error = error};
    }

    auto registry = build_mock_registry(manifest, error);
    if (!registry.has_value()) {
        return EvaluatorScenarioResult{.ok = false, .observation_json = {}, .error = error};
    }

    auto input = evaluator::value_from_json(scenario.input_json);
    if (!input.has_value()) {
        return EvaluatorScenarioResult{
            .ok = false,
            .observation_json = {},
            .error = "failed to decode scenario '" + scenario.name + "' input from wire JSON",
        };
    }

    if (manifest.kind == CaseKind::Workflow) {
        return run_workflow(*program, manifest, scenario, *registry, std::move(*input));
    }
    return run_agent(*program, manifest, scenario, *registry, std::move(*input));
}

std::optional<std::string> observation_matches_expectations(const ConformanceCase &manifest,
                                                            const ConformanceScenario &scenario,
                                                            std::string_view observation_json) {
    auto dom = json::parse_json(observation_json);
    if (!dom.has_value() || !*dom || !(*dom)->is_object()) {
        return "observation is not a JSON object";
    }
    const json::JsonValue &root = **dom;

    const char *expected_status = "failed";
    switch (scenario.expect.run_status) {
    case ExpectedRunStatus::Completed:
        expected_status = "completed";
        break;
    case ExpectedRunStatus::Suspended:
        expected_status = "suspended";
        break;
    case ExpectedRunStatus::Failed:
        expected_status = "failed";
        break;
    }
    const auto *status = root.get("status");
    if (status == nullptr || status->as_string() != expected_status) {
        return "run status diverged: expected '" + std::string{expected_status} + "'";
    }

    // Canonical capability-name order.
    const auto *caps = root.get("capability_sequence");
    if (caps == nullptr || !caps->is_array()) {
        return "observation is missing a capability_sequence array";
    }
    if (caps->array_items.size() != scenario.expect.capability_sequence.size()) {
        return "capability_sequence length diverged (expected " +
               std::to_string(scenario.expect.capability_sequence.size()) + ", observed " +
               std::to_string(caps->array_items.size()) + ")";
    }
    for (std::size_t i = 0; i < caps->array_items.size(); ++i) {
        const auto observed_name = caps->array_items[i]->as_string();
        if (!observed_name.has_value() || *observed_name != scenario.expect.capability_sequence[i]) {
            return "capability_sequence[" + std::to_string(i) + "] diverged: expected '" +
                   scenario.expect.capability_sequence[i] + "'";
        }
    }

    // Agent cases pin the declaration-order state-name sequence. Workflow
    // cases declare no flat state sequence (fan-out makes it ambiguous), so
    // only the presence/shape of the array is checked there.
    const auto *states = root.get("state_sequence");
    if (states == nullptr || !states->is_array()) {
        return "observation is missing a state_sequence array";
    }
    if (manifest.kind == CaseKind::Agent) {
        if (states->array_items.size() != scenario.expect.state_sequence.size()) {
            return "state_sequence length diverged (expected " +
                   std::to_string(scenario.expect.state_sequence.size()) + ", observed " +
                   std::to_string(states->array_items.size()) + ")";
        }
        for (std::size_t i = 0; i < states->array_items.size(); ++i) {
            const auto *entry = states->array_items[i].get();
            const auto *state = entry != nullptr ? entry->get("state") : nullptr;
            const auto observed_state = state != nullptr ? state->as_string() : std::nullopt;
            if (!observed_state.has_value() ||
                *observed_state != scenario.expect.state_sequence[i]) {
                return "state_sequence[" + std::to_string(i) + "] diverged: expected '" +
                       scenario.expect.state_sequence[i] + "'";
            }
        }
    }

    // Canonical output wire bytes. The observation embeds output_json as a
    // value subtree; re-serialize it through the SAME canonical emitter the
    // manifest gate uses, so comparison is byte-exact regardless of the
    // observation's own formatting.
    const json::JsonValue *output = root.get("output_json");
    if (scenario.expect.output_json.has_value()) {
        if (output == nullptr) {
            return "observation is missing the expected output_json";
        }
        const std::string observed_bytes = detail::canonical_json(*output);
        if (observed_bytes != *scenario.expect.output_json) {
            return "output_json diverged: expected " + *scenario.expect.output_json;
        }
    } else if (output != nullptr) {
        return "observation carries an unexpected output_json";
    }

    return std::nullopt;
}

} // namespace ahfl::conformance
