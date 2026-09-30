#include "conformance/observation_document.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_value.hpp"
#include "runtime/value/value_json.hpp"

namespace ahfl::conformance {

using ahfl::runtime::WorkflowStatus;

namespace {

namespace json = ahfl::json;

[[nodiscard]] std::unique_ptr<json::JsonValue> make_string_node(std::string value) {
    return json::JsonValue::make_string(std::move(value));
}

} // namespace

const char *status_name_workflow(ahfl::runtime::WorkflowStatus status) {
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

std::string
render_observation(const ConformanceCase &manifest,
                   const ConformanceScenario &scenario, const char *status,
                   const std::vector<std::pair<std::string, std::string>> &states,
                   const std::vector<std::string> &capabilities,
                   const std::vector<std::string> &argument_envelopes,
                   const ahfl::runtime::Value *output) {
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

    // The per-call argument envelope is the SAME canonical frame every C++
    // transport sends (serialize_args_for_wire_json): bare struct /
    // {"value":..} / {"args":[..]} / {}. Embedded as a value subtree (never a
    // string) so the differential comparator can canonicalize bytes, exactly
    // like output_json.
    auto argument_array = json::JsonValue::make_array();
    for (const std::string &envelope : argument_envelopes) {
        auto parsed = json::parse_json(envelope);
        if (parsed.has_value() && *parsed) {
            argument_array->push(std::move(*parsed));
        } else {
            argument_array->push(make_string_node(envelope));
        }
    }
    root->set("capability_arguments", std::move(argument_array));

    if (output != nullptr) {
        // value_to_json is canonical compact wire JSON; re-parse only to embed
        // the value subtree (never as a string) into the observation DOM.
        auto output_dom = json::parse_json(runtime::value_to_json(*output));
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

} // namespace ahfl::conformance
