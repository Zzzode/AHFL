#include "compiler/backends/infra/k8s_crd.hpp"

#include "base/support/structured_writer.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ahfl::backends {

namespace {

// FNV-1a 64-bit. The module-hash suffix makes a sanitized name unique across
// agents whose short names collide after DNS-1123 sanitization, and it is
// deterministic across identical compilations.
[[nodiscard]] std::uint64_t fnv1a_64(std::string_view data) {
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t hash = kOffset;
    for (const unsigned char byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

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

[[nodiscard]] std::string hex_suffix(std::string_view qualified_name) {
    static constexpr char kHex[] = "0123456789abcdef";
    const std::uint64_t hash = fnv1a_64(qualified_name);
    std::string suffix(10, '0');
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        suffix[suffix.size() - 1 - i] =
            kHex[(hash >> (4U * static_cast<unsigned>(i))) & 0x0FU];
    }
    return suffix;
}

// RFC 1123 subdomain: lowercase [a-z0-9-.], max 253 bytes. Compose a sanitized
// stem plus a deterministic 10-hex-char qualified-name hash suffix so the
// field is unique without ever embedding '::' or uppercase bytes. `pluralize`
// appends an 's' to the human-readable stem (K8s plural resource names), not
// to the hash.
[[nodiscard]] std::string rfc1123_name(const K8sCrdConfig &config, bool pluralize) {
    std::string stem = dns_safe(config.short_name.empty() ? config.agent_name
                                                         : config.short_name);
    if (pluralize && !stem.empty()) {
        stem.push_back('s');
    }
    std::string name = stem + "-" + hex_suffix(config.agent_name);
    static constexpr std::size_t kMaxSubdomain = 253;
    if (name.size() > kMaxSubdomain) {
        name.resize(kMaxSubdomain);
        while (!name.empty() && name.back() == '-') {
            name.pop_back();
        }
    }
    return name;
}

void emit_placeholder_string(YamlWriter &yaml,
                             std::string_view property,
                             std::string_view canonical_type) {
    yaml.begin_mapping(property)
        .key_value("type", "string")
        .key_value_quoted("description",
                          std::string("AHFL ") + std::string(property) +
                              " port; canonical type " + std::string(canonical_type))
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
    // canonical type names until a shared schema export lands).
    emit_placeholder_string(yaml, "input", config.input_type);
    emit_placeholder_string(yaml, "context", config.context_type);
    emit_placeholder_string(yaml, "output", config.output_type);

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
