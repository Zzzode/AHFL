#pragma once

// KR6.7 (RFC 0026 P7): differential comparator between the in-process evaluator
// observation and the Node embedded-engine observation.
//
// Both adapters emit a canonical observation document over the SAME case /
// scenario. They use different schema tags and the Node document carries extra
// execution internals (transition_count, workflow_completed_count), so this
// comparator asserts equality on exactly the three KR6.7 differential
// dimensions, plus run status:
//
//   1. state_sequence       ordered {agent,state} entries (declaration order);
//   2. capability_sequence  ordered canonical capability names;
//   3. output_json          canonical output wire bytes (or both absent);
//   0. status               completed / suspended / failed.
//
// It returns an empty optional when they agree, or a human-readable reason
// naming the first diverging dimension. Pure: it only parses the two documents.

#include <optional>
#include <string>
#include <string_view>

#include "base/json/json_value.hpp"
#include "conformance/conformance_case.hpp"
namespace ahfl::conformance {

namespace detail {

[[nodiscard]] inline std::optional<std::string> compare_string_array(const json::JsonValue &lhs,
                                                                     const json::JsonValue &rhs,
                                                                     std::string_view dimension) {
    if (!lhs.is_array() || !rhs.is_array()) {
        return std::string{dimension} + " is not an array in one observation";
    }
    if (lhs.array_items.size() != rhs.array_items.size()) {
        return std::string{dimension} + " length diverged (evaluator " +
               std::to_string(lhs.array_items.size()) + ", node " +
               std::to_string(rhs.array_items.size()) + ")";
    }
    for (std::size_t i = 0; i < lhs.array_items.size(); ++i) {
        const auto a = lhs.array_items[i]->as_string();
        const auto b = rhs.array_items[i]->as_string();
        if (!a.has_value() || !b.has_value() || *a != *b) {
            return std::string{dimension} + "[" + std::to_string(i) + "] diverged";
        }
    }
    return std::nullopt;
}

// Compares the (agent,state) entry sequence element-by-element.
[[nodiscard]] inline std::optional<std::string> compare_state_sequence(const json::JsonValue &lhs,
                                                                       const json::JsonValue &rhs) {
    constexpr std::string_view dimension = "state_sequence";
    if (!lhs.is_array() || !rhs.is_array()) {
        return std::string{dimension} + " is not an array in one observation";
    }
    if (lhs.array_items.size() != rhs.array_items.size()) {
        return std::string{dimension} + " length diverged (evaluator " +
               std::to_string(lhs.array_items.size()) + ", node " +
               std::to_string(rhs.array_items.size()) + ")";
    }
    for (std::size_t i = 0; i < lhs.array_items.size(); ++i) {
        const auto *a = lhs.array_items[i].get();
        const auto *b = rhs.array_items[i].get();
        if (a == nullptr || b == nullptr || !a->is_object() || !b->is_object()) {
            return std::string{dimension} + "[" + std::to_string(i) +
                   "] is not an object in one observation";
        }
        const auto *a_agent = a->get("agent");
        const auto *a_state = a->get("state");
        const auto *b_agent = b->get("agent");
        const auto *b_state = b->get("state");
        if (a_agent == nullptr || b_agent == nullptr || a_state == nullptr || b_state == nullptr) {
            return std::string{dimension} + "[" + std::to_string(i) +
                   "] entry is missing agent/state";
        }
        const auto aa = a_agent->as_string();
        const auto ba = b_agent->as_string();
        const auto as = a_state->as_string();
        const auto bs = b_state->as_string();
        if (!aa.has_value() || !ba.has_value() || *aa != *ba) {
            return std::string{dimension} + "[" + std::to_string(i) + "].agent diverged";
        }
        if (!as.has_value() || !bs.has_value() || *as != *bs) {
            return std::string{dimension} + "[" + std::to_string(i) +
                   "].state diverged (evaluator '" + std::string{as.value_or("?")} + "', node '" +
                   std::string{bs.value_or("?")} + "')";
        }
    }
    return std::nullopt;
}

// Compares the output value subtree by re-serializing both through the canonical
// emitter, so the comparison is byte-exact regardless of document formatting.
[[nodiscard]] inline std::optional<std::string> compare_output(const json::JsonValue &lhs,
                                                               const json::JsonValue &rhs) {
    const json::JsonValue *a = lhs.get("output_json");
    const json::JsonValue *b = rhs.get("output_json");
    if (a == nullptr && b == nullptr) {
        return std::nullopt; // both runs produce no inspectable output
    }
    if (a == nullptr || b == nullptr) {
        return "output_json presence diverged (only one engine produced output)";
    }
    const std::string a_bytes = canonical_json(*a);
    const std::string b_bytes = canonical_json(*b);
    if (a_bytes != b_bytes) {
        return "output_json diverged:\n  evaluator: " + a_bytes + "\n  node:      " + b_bytes;
    }
    return std::nullopt;
}

} // namespace detail

/// Asserts an evaluator observation document and a Node embedded-engine
/// observation document agree on status + the three KR6.7 differential
/// dimensions. `expected_status` is the manifest's blessed terminal status so
/// the comparison is independent of either engine's spelling.
[[nodiscard]] inline std::optional<std::string>
observations_agree(std::string_view evaluator_observation_json,
                   std::string_view node_observation_json) {
    auto evaluator_dom = json::parse_json(evaluator_observation_json);
    auto node_dom = json::parse_json(node_observation_json);
    if (!evaluator_dom.has_value() || !*evaluator_dom || !(*evaluator_dom)->is_object()) {
        return "evaluator observation is not a JSON object";
    }
    if (!node_dom.has_value() || !*node_dom || !(*node_dom)->is_object()) {
        return "node observation is not a JSON object";
    }
    const json::JsonValue &ev = **evaluator_dom;
    const json::JsonValue &nd = **node_dom;

    if (const auto *a = ev.get("status"); a == nullptr || a->as_string() == std::nullopt) {
        return "evaluator observation has no status";
    }
    if (const auto *b = nd.get("status"); b == nullptr || b->as_string() == std::nullopt) {
        return "node observation has no status";
    }
    if (*ev.get("status")->as_string() != *nd.get("status")->as_string()) {
        return "run status diverged (evaluator '" + std::string{*ev.get("status")->as_string()} +
               "', node '" + std::string{*nd.get("status")->as_string()} + "')";
    }

    if (auto divergence =
            detail::compare_state_sequence(*ev.get("state_sequence"), *nd.get("state_sequence"));
        divergence.has_value()) {
        return divergence;
    }
    if (auto divergence = detail::compare_string_array(
            *ev.get("capability_sequence"), *nd.get("capability_sequence"), "capability_sequence");
        divergence.has_value()) {
        return divergence;
    }
    if (auto divergence = detail::compare_output(ev, nd); divergence.has_value()) {
        return divergence;
    }
    return std::nullopt;
}

/// RFC 0026 FB-3b node-only lane: when no in-process evaluator reference exists
/// (the surfaced pure-fn / first-class-closure construct awaits KR6.8), the
/// Node embedded-engine observation is checked DIRECTLY against the manifest's
/// blessed expectation: terminal run status, the declaration-order state
/// sequence (state names only), the capability sequence, and the canonical
/// output JSON. Returns an empty optional on agreement, else a reason.
[[nodiscard]] inline std::optional<std::string>
node_observation_matches_expectation(const CaseExpectations &expect,
                                     std::string_view node_observation_json) {
    auto node_dom = json::parse_json(node_observation_json);
    if (!node_dom.has_value() || !*node_dom || !(*node_dom)->is_object()) {
        return "node observation is not a JSON object";
    }
    const json::JsonValue &nd = **node_dom;

    const char *expected_status = "completed";
    switch (expect.run_status) {
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
    if (const auto *b = nd.get("status"); b == nullptr || b->as_string() == std::nullopt) {
        return "node observation has no status";
    }
    if (*nd.get("status")->as_string() != expected_status) {
        return "run status diverged (expected '" + std::string{expected_status} + "', node '" +
               std::string{*nd.get("status")->as_string()} + "')";
    }

    // state_sequence: the Node document is an array of {agent,state} objects;
    // compare the STATE names in order against the blessed declaration order.
    const auto *node_states = nd.get("state_sequence");
    if (node_states == nullptr || !node_states->is_array()) {
        return "node observation has no state_sequence array";
    }
    if (node_states->array_items.size() != expect.state_sequence.size()) {
        return "state_sequence length diverged (expected " +
               std::to_string(expect.state_sequence.size()) + ", node " +
               std::to_string(node_states->array_items.size()) + ")";
    }
    for (std::size_t i = 0; i < expect.state_sequence.size(); ++i) {
        const auto *entry = node_states->array_items[i].get();
        if (entry == nullptr) {
            return "state_sequence entry is null";
        }
        const auto *state = entry->get("state");
        if (state == nullptr || state->as_string() == std::nullopt) {
            return "state_sequence entry has no state";
        }
        if (*state->as_string() != expect.state_sequence[i]) {
            return "state_sequence[" + std::to_string(i) + "] diverged (expected '" +
                   expect.state_sequence[i] + "', node '" +
                   std::string{*state->as_string()} + "')";
        }
    }

    // capability_sequence: a plain array of canonical names.
    {
        json::JsonValue expected_caps;
        expected_caps.kind = json::Kind::Array;
        for (const std::string &cap : expect.capability_sequence) {
            expected_caps.array_items.push_back(json::JsonValue::make_string(cap));
        }
        if (auto divergence = detail::compare_string_array(
                expected_caps, *nd.get("capability_sequence"), "capability_sequence");
            divergence.has_value()) {
            return divergence;
        }
    }

    // output_json: canonical re-serialize comparison (or both absent).
    if (expect.output_json.has_value()) {
        auto expected_dom = json::parse_json(*expect.output_json);
        if (!expected_dom.has_value() || !*expected_dom) {
            return "blessed output_json is not a JSON value";
        }
        const auto *node_output = nd.get("output_json");
        if (node_output == nullptr) {
            return "node observation produced no output_json but one was expected";
        }
        const std::string expected_bytes = detail::canonical_json(**expected_dom);
        const std::string node_bytes = detail::canonical_json(*node_output);
        if (expected_bytes != node_bytes) {
            return "output_json diverged:\n  expected: " + expected_bytes +
                   "\n  node:     " + node_bytes;
        }
    } else if (nd.get("output_json") != nullptr) {
        return "node observation produced output_json but none was expected";
    }
    return std::nullopt;
}

} // namespace ahfl::conformance
