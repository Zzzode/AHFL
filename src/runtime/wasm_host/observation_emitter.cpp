#include "runtime/wasm_host/observation_emitter.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace ahfl::runtime::wasm_host {

namespace {

// JSON string escaping, byte-identical to json::serialize_json's String arm
// and the conformance emit_canonical_string: the eight C0 controls plus
// quote/backslash get short escapes; the remaining C0 controls get \u00XX.
[[nodiscard]] std::string json_escape(std::string_view value) {
    constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20U) {
                out += "\\u00";
                out.push_back(kHex[(byte >> 4U) & 0x0FU]);
                out.push_back(kHex[byte & 0x0FU]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

// A (key, raw-json-value) field pair. The key is a bare JSON string token
// (including quotes); the value is a pre-serialized JSON fragment.
struct Field {
    std::string key;
    std::string value;
};

[[nodiscard]] std::string emit_state_sequence(const std::vector<StateEntry> &states) {
    std::string out = "[";
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += "{\"agent\":";
        out += json_escape(states[i].agent);
        out += ",\"state\":";
        out += json_escape(states[i].state);
        out.push_back('}');
    }
    out.push_back(']');
    return out;
}

[[nodiscard]] std::string
emit_capability_sequence(const std::vector<std::string> &capabilities) {
    std::string out = "[";
    for (std::size_t i = 0; i < capabilities.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += json_escape(capabilities[i]);
    }
    out.push_back(']');
    return out;
}

[[nodiscard]] std::string
emit_capability_arguments(const std::vector<std::string> &arguments) {
    // Each element is a pre-serialized wire envelope ({} / {"value":..} /
    // bare struct / {"args":[..]}). Embed it as a raw JSON value, exactly
    // like the JS oracle's `capabilityArguments.join(",")`.
    std::string out = "[";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += arguments[i];
    }
    out.push_back(']');
    return out;
}

} // namespace

std::string emit_observation(const ObservationData &data) {
    // Build the field list in the SAME order the JS oracle builds it, then
    // sort by key -- the JS `fields.sort(([a],[b]) => ...)` is a stable
    // lexicographic sort on the quoted key token.
    std::vector<Field> fields;
    fields.reserve(10);

    fields.push_back({R"("capability_arguments")", emit_capability_arguments(data.capability_arguments)});
    fields.push_back({R"("capability_sequence")", emit_capability_sequence(data.capabilities)});
    fields.push_back({R"("case")", json_escape(data.case_name)});
    if (data.output_json.has_value()) {
        fields.push_back({R"("output_json")", *data.output_json});
    }
    fields.push_back({R"("scenario")", json_escape(data.scenario_name)});
    fields.push_back({R"("schema")", json_escape("ahfl.node-observation.v1")});
    fields.push_back({R"("state_sequence")", emit_state_sequence(data.states)});
    fields.push_back({R"("status")", json_escape(data.status)});
    fields.push_back({R"("transition_count")", std::to_string(data.transition_count)});
    if (data.workflow_completed_count.has_value()) {
        fields.push_back({R"("workflow_completed_count")",
                          std::to_string(*data.workflow_completed_count)});
    }

    std::ranges::sort(fields, {}, &Field::key);

    std::string out;
    out.push_back('{');
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            out.push_back(',');
        }
        out += fields[i].key;
        out.push_back(':');
        out += fields[i].value;
    }
    out.push_back('}');
    return out;
}

} // namespace ahfl::runtime::wasm_host
