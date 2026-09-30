#include "conformance/native_engine.hpp"

#include <string>
#include <utility>
#include <vector>

#include "conformance/compile_source.hpp"
#include "conformance/conformance_mock_registry.hpp"
#include "conformance/observation_document.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/wire_value.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"
#include "runtime/wasm_runner/wasm_agent_runner.hpp"
#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"

namespace ahfl::conformance {

namespace {

using ahfl::runtime::AgentId;
using ahfl::runtime::CapabilityCallResult;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityInvocationContext;
using ahfl::runtime::CapabilityRegistry;
using ahfl::runtime::Value;
using ahfl::runtime::WorkflowResult;
using ahfl::runtime::WorkflowStatus;
namespace wr = ahfl::runtime::wasm_runner;

// Wraps the registry's contextual invoker so BOTH the canonical capability
// name and the canonical wire argument envelope are recorded per call (the
// differential comparator diffs the envelopes, not just the capability
// order). Mirrors the evaluator engine's recording wrapper exactly.
[[nodiscard]] ahfl::runtime::ContextualCapabilityInvoker
make_recording_invoker(CapabilityRegistry &registry,
                       std::vector<std::string> &capabilities,
                       std::vector<std::string> &argument_envelopes) {
    auto base = registry.as_contextual_invoker();
    return [base = std::move(base), &capabilities, &argument_envelopes](
               const CapabilityInvocationContext &ctx, const std::string &name,
               const std::vector<Value> &args) -> CapabilityCallResult {
        capabilities.emplace_back(name);
        // A non-wire argument (a closure) is recorded with a sentinel rather
        // than as empty bytes: no lane can legitimately produce it, so the
        // observation comparator fails loudly instead of comparing two
        // malformed/empty frames.
        argument_envelopes.push_back(
            ahfl::runtime::serialize_args_for_wire_json(args)
                .value_or("__non_wire_argument__"));
        return base(ctx, name, args);
    };
}

// Drives a WORKFLOW case through WasmWorkflowRuntime. The facade compiles
// every workflow in the program to wasm in its constructor, then run(name,
// input) drives the wasm_host workflow session.
[[nodiscard]] NativeScenarioResult
run_workflow_native(const ir::Program &program, const ConformanceCase &manifest,
                    const ConformanceScenario &scenario,
                    CapabilityRegistry &registry, Value input) {
    std::vector<std::pair<std::string, std::string>> states;
    std::vector<std::string> capabilities;
    std::vector<std::string> argument_envelopes;

    wr::WasmWorkflowRuntimeConfig config;
    config.hooks.state_entered_hook =
        [&states](AgentId, std::string_view agent_name, std::string_view,
                  std::string_view state_name) {
            states.emplace_back(std::string{agent_name}, std::string{state_name});
        };
    config.invoker = make_recording_invoker(registry, capabilities, argument_envelopes);
    // name_resolver: the workflow session builds a fallback from the
    // descriptor when the caller does not supply one. The invoker already
    // receives the canonical name, so the resolver is not needed for
    // observation.

    wr::WasmWorkflowRuntime runtime(program, std::move(config));
    const WorkflowResult result = runtime.run(manifest.entry, std::move(input));

    // A wasm trap / host-abort / non-OK maps to NodeFailed (never EvalError);
    // the facade's compile-failed path also surfaces as a failed result.
    NativeScenarioResult outcome;
    outcome.ok = true;
    outcome.observation_json = render_observation(
        manifest, scenario, status_name_workflow(result.status()), states,
        capabilities, argument_envelopes, result.output());
    return outcome;
}

// Drives an AGENT case through WasmAgentRunner (run_wasm_agent). The facade
// compiles the agent to wasm and drives it end-to-end on the wasm3 engine
// with the D1 dual-mode discipline (effects-free step-walk + canonical run).
// The wasm_host agent runner collects capabilities + argument envelopes from
// the CANONICAL run only (the step-walk's calls are discarded), so we use
// its collected fields directly rather than double-recording through a
// wrapper.
[[nodiscard]] NativeScenarioResult
run_agent_native(const ir::Program &program, const ConformanceCase &manifest,
                 const ConformanceScenario &scenario,
                 CapabilityRegistry &registry, Value input) {
    wr::WasmAgentRunnerConfig config;
    config.invoker = registry.as_contextual_invoker();
    // The effects-free step-walk instance uses the SAME side-effect-free mock
    // (the conformance harness supplies a scripted mock as both).
    config.states_invoker = config.invoker;

    auto result = wr::run_wasm_agent(program, manifest.entry, input, std::move(config));
    if (!result.has_value()) {
        return NativeScenarioResult{
            .ok = false,
            .observation_json = {},
            .error = "wasm agent runner failed: " + result.error(),
        };
    }

    // The wasm_host agent runner collects (agent, state) entries from the
    // effects-free step-walk. Map them to the (agent, state) pair shape the
    // observation document expects.
    std::vector<std::pair<std::string, std::string>> states;
    states.reserve(result->states.size());
    for (const auto &entry : result->states) {
        states.emplace_back(entry.agent, entry.state);
    }

    NativeScenarioResult outcome;
    outcome.ok = true;
    outcome.observation_json = render_observation(
        manifest, scenario, status_name_workflow(result->result.status()), states,
        result->capabilities, result->capability_arguments,
        result->result.output());
    return outcome;
}

} // namespace

NativeScenarioResult run_native_scenario(const LoadedConformanceCase &loaded,
                                         const ConformanceScenario &scenario) {
    const ConformanceCase &manifest = loaded.manifest;

    std::string error_buffer;
    auto program = compile_conformance_source(loaded.source_path, error_buffer);
    if (!program.has_value()) {
        return NativeScenarioResult{
            .ok = false,
            .observation_json = {},
            .error = "compile failed: " + error_buffer,
        };
    }

    auto registry = build_mock_registry(manifest, error_buffer);
    if (!registry.has_value()) {
        return NativeScenarioResult{
            .ok = false,
            .observation_json = {},
            .error = "mock registry build failed: " + error_buffer,
        };
    }

    auto input = ahfl::runtime::value_from_json(scenario.input_json);
    if (!input.has_value()) {
        return NativeScenarioResult{
            .ok = false,
            .observation_json = {},
            .error = "failed to decode scenario '" + scenario.name +
                     "' input from wire JSON",
        };
    }

    if (manifest.kind == CaseKind::Workflow) {
        return run_workflow_native(*program, manifest, scenario, *registry,
                                   std::move(*input));
    }
    return run_agent_native(*program, manifest, scenario, *registry,
                            std::move(*input));
}

} // namespace ahfl::conformance
