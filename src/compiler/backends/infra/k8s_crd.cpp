#include "compiler/backends/infra/k8s_crd.hpp"

#include "base/support/hash.hpp"
#include "base/support/structured_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ahfl::backends {

namespace {

[[nodiscard]] std::string capitalize(std::string_view s) {
    if (s.empty())
        return {};
    std::string result(s);
    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
    return result;
}

// DNS-1123 label charset: [a-z0-9-], lowercased. Any other byte is replaced
// with '-'; runs of separators are collapsed and leading/trailing separators
// trimmed.
[[nodiscard]] std::string dns_safe(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out.push_back(static_cast<char>(ch));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

// RFC 1123: a DNS label is at most 63 bytes; the CRD metadata.name
// (plural + "." + group) is a DNS subdomain capped at 253 bytes.
constexpr std::size_t kMaxDnsLabel = 63;
constexpr std::size_t kMaxDnsSubdomain = 253;

// Compose a sanitized stem plus a deterministic 10-hex-char qualified-name
// hash suffix so the field is unique without ever embedding '::' or uppercase
// bytes. `pluralize` appends an 's' to the human-readable stem (K8s plural
// resource names), not to the hash. The hash suffix is always preserved in
// full: the stem alone is what gets truncated. For a plural name the length
// budget additionally reserves the "." + apiGroup suffix so the composed
// metadata.name stays within the 253-byte subdomain limit.
[[nodiscard]] std::string rfc1123_name(const K8sCrdConfig &config, bool pluralize) {
    std::string stem = dns_safe(config.short_name.empty() ? config.agent_name : config.short_name);
    if (pluralize && !stem.empty()) {
        stem.push_back('s');
    }

    const std::string suffix = fnv1a_hex_suffix(config.agent_name, 10);
    constexpr std::size_t kSuffixDash = 1;

    std::size_t budget = kMaxDnsLabel;
    if (pluralize) {
        // metadata.name is plural + "." + apiGroup and must be a valid
        // subdomain, so the plural must leave room for the group suffix.
        const std::size_t required_group_suffix = config.api_group.size() + 1;
        if (required_group_suffix >= kMaxDnsSubdomain) {
            throw std::invalid_argument(
                "K8s CRD apiGroup is too long to form a valid metadata.name");
        }
        budget = std::min(budget, kMaxDnsSubdomain - required_group_suffix);
    }
    // Reserve the '-' separator and the full hash suffix; truncate only the
    // stem so the identity suffix survives intact.
    if (budget <= suffix.size() + kSuffixDash) {
        throw std::invalid_argument("K8s CRD name has no room for the identity hash suffix");
    }
    const std::size_t max_stem = budget - suffix.size() - kSuffixDash;
    if (stem.size() > max_stem) {
        stem.resize(max_stem);
        while (!stem.empty() && stem.back() == '-') {
            stem.pop_back();
        }
    }
    if (stem.empty()) {
        stem = "agent";
    }
    return stem + "-" + suffix;
}

void emit_placeholder_string(YamlWriter &yaml,
                             std::string_view property,
                             std::string_view canonical_type) {
    yaml.begin_mapping(property)
        .key_value("type", "string")
        .key_value_quoted("description",
                          std::string("AHFL ") + std::string(property) + " port; canonical type " +
                              std::string(canonical_type))
        .end_mapping();
}

} // anonymous namespace

K8sCrdOutput generate_crd(const K8sCrdConfig &config) {
    K8sCrdOutput output;
    output.kind = capitalize(config.short_name.empty() ? config.agent_name : config.short_name);
    output.plural = rfc1123_name(config, true);
    output.resource_name = output.plural + "." + config.api_group;

    YamlWriter yaml;
    yaml.key_value("apiVersion", "apiextensions.k8s.io/v1")
        .key_value("kind", "CustomResourceDefinition")
        .begin_mapping("metadata")
        .key_value("name", output.resource_name)
        .begin_mapping("labels")
        .key_value("app.kubernetes.io/managed-by", "ahfl")
        .end_mapping() // labels
        .begin_mapping("annotations")
        .key_value_quoted("ahfl.io/agent", config.agent_name);
    if (!config.module_name.empty()) {
        yaml.key_value_quoted("ahfl.io/module", config.module_name);
    }
    yaml.end_mapping() // annotations
        .end_mapping() // metadata
        .begin_mapping("spec")
        .key_value("group", config.api_group)
        .begin_mapping("versions")
        .begin_list_item()
        .key_value("name", config.api_version)
        .key_value_bool("served", true)
        .key_value_bool("storage", true)
        .begin_mapping("schema")
        .begin_mapping("openAPIV3Schema")
        .key_value("type", "object")
        .begin_mapping("properties")
        .begin_mapping("spec")
        .key_value("type", "object")
        .begin_mapping("properties");

    // State port (enumerated).
    yaml.begin_mapping("state").key_value("type", "string");
    if (!config.states.empty()) {
        yaml.key_inline_list("enum", config.states);
    }
    yaml.end_mapping(); // state

    // Initial / final state markers.
    if (!config.initial_state.empty()) {
        yaml.begin_mapping("initialState")
            .key_value("type", "string")
            .key_value("default", config.initial_state)
            .end_mapping();
    }
    if (!config.final_states.empty()) {
        yaml.begin_mapping("finalStates")
            .key_value("type", "array")
            .begin_mapping("items")
            .key_value("type", "string")
            .end_mapping();
        if (config.final_states.size() == 1) {
            yaml.key_inline_list("default", config.final_states);
        }
        yaml.end_mapping(); // finalStates
    }

    // Capability inventory (already collected during lowering).
    if (!config.capabilities.empty()) {
        yaml.begin_mapping("capabilities")
            .key_value("type", "array")
            .begin_mapping("items")
            .key_value("type", "string")
            .end_mapping()
            .key_inline_list("default", config.capabilities)
            .end_mapping();
    }

    // Input / context / output schema ports (placeholders carrying the
    // canonical type names until a shared schema export lands). The context
    // clause is optional; an omitted context lowers to Unit and carries an
    // empty canonical type, so emit only the ports the agent actually
    // declares rather than a misleading empty-canonical placeholder.
    if (!config.input_type.empty()) {
        emit_placeholder_string(yaml, "input", config.input_type);
    }
    if (!config.context_type.empty()) {
        emit_placeholder_string(yaml, "context", config.context_type);
    }
    if (!config.output_type.empty()) {
        emit_placeholder_string(yaml, "output", config.output_type);
    }

    yaml.end_mapping()   // properties
        .end_mapping()   // spec
        .end_mapping()   // properties
        .end_mapping()   // openAPIV3Schema
        .end_mapping()   // schema
        .end_list_item() // versions item
        .end_mapping()   // versions
        .key_value("scope", "Namespaced")
        .begin_mapping("names")
        .key_value("plural", output.plural)
        .key_value("singular", rfc1123_name(config, false))
        .key_value("kind", output.kind)
        .end_mapping()  // names
        .end_mapping(); // spec

    output.yaml = yaml.str();
    return output;
}

} // namespace ahfl::backends
