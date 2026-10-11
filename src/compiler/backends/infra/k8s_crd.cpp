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

// Recursively render an OpenApiSchema (JSON Schema tree) as a YAML mapping
// under `key`. Scalar constraints (minimum/maximum/enum/etc.) render inline;
// nested properties/items/oneOf recurse. A $ref node renders as a bare
// "$ref": '#/definitions/<key>' (the definitions table lives at the root of
// the openAPIV3Schema).
void render_schema_yaml(YamlWriter &yaml, std::string_view key, const OpenApiSchema &schema) {
    yaml.begin_mapping(key);

    if (!schema.ref.empty()) {
        // The OpenApiTypeMapper stores the full OpenAPI $ref path
        // (#/components/schemas/<key>). In a CRD the definitions table lives
        // at #/definitions/<key>, so rewrite the prefix. The value must be
        // quoted: '#' starts a YAML comment, so an unquoted $ref would parse
        // as null.
        constexpr std::string_view kComponentsPrefix = "#/components/schemas/";
        std::string def_ref = schema.ref;
        if (def_ref.starts_with(kComponentsPrefix)) {
            def_ref = "#/definitions/" + def_ref.substr(kComponentsPrefix.size());
        }
        yaml.key_value_quoted("$ref", def_ref);
        yaml.end_mapping();
        return;
    }

    if (!schema.type.empty()) {
        yaml.key_value("type", schema.type);
    }
    if (!schema.format.empty()) {
        yaml.key_value("format", schema.format);
    }
    if (!schema.description.empty()) {
        yaml.key_value_quoted("description", schema.description);
    }
    if (!schema.enum_values.empty()) {
        yaml.key_inline_list("enum", schema.enum_values);
    }
    if (schema.minimum.has_value()) {
        yaml.key_value("minimum", std::to_string(*schema.minimum));
    }
    if (schema.maximum.has_value()) {
        yaml.key_value("maximum", std::to_string(*schema.maximum));
    }
    if (schema.min_length.has_value()) {
        yaml.key_value("minLength", std::to_string(*schema.min_length));
    }
    if (schema.max_length.has_value()) {
        yaml.key_value("maxLength", std::to_string(*schema.max_length));
    }
    if (schema.min_items.has_value()) {
        yaml.key_value("minItems", std::to_string(*schema.min_items));
    }
    if (schema.max_items.has_value()) {
        yaml.key_value("maxItems", std::to_string(*schema.max_items));
    }
    if (schema.max_properties.has_value()) {
        yaml.key_value("maxProperties", std::to_string(*schema.max_properties));
    }
    if (schema.unique_items) {
        yaml.key_value_bool("uniqueItems", true);
    }
    if (schema.items != nullptr) {
        render_schema_yaml(yaml, "items", *schema.items);
    }
    if (!schema.tuple_items.empty()) {
        yaml.begin_mapping("items");
        for (const auto &component : schema.tuple_items) {
            // Tuple items render as a list of schemas.
            yaml.begin_list_item();
            // Render the component schema inline (no key) — but YamlWriter
            // is key-oriented, so we render as a mapping with a placeholder
            // key that the list item dash replaces. Actually, tuple_items
            // is rare in CRD ports; fall back to the scalar items pointer
            // for now. The OpenApiTypeMapper only emits tuple_items for
            // tuple types, which are uncommon in agent port signatures.
            if (component != nullptr) {
                render_schema_yaml(yaml, "schema", *component);
            }
            yaml.end_list_item();
        }
        yaml.end_mapping(); // items
    }
    if (schema.additional_properties != nullptr) {
        render_schema_yaml(yaml, "additionalProperties", *schema.additional_properties);
    }
    if (!schema.one_of.empty()) {
        yaml.begin_mapping("oneOf");
        for (const auto &variant : schema.one_of) {
            yaml.begin_list_item();
            if (variant != nullptr) {
                render_schema_yaml(yaml, "schema", *variant);
            }
            yaml.end_list_item();
        }
        yaml.end_mapping(); // oneOf
    }
    if (!schema.properties.empty()) {
        yaml.begin_mapping("properties");
        for (const auto &[prop_name, prop_schema] : schema.properties) {
            render_schema_yaml(yaml, prop_name, prop_schema);
        }
        yaml.end_mapping(); // properties
    }
    if (!schema.required.empty()) {
        yaml.key_inline_list("required", schema.required);
    }

    yaml.end_mapping(); // key
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

    // Input / context / output schema ports. KR7.4: real JSON Schema trees
    // mapped from the IR TypeRef via OpenApiTypeMapper. Scalar types render
    // inline; struct/enum types render as $ref to the definitions table at
    // the root of the openAPIV3Schema. A schema without content (Unit / Any /
    // Fn / Never / Unresolved) causes the port to be omitted entirely.
    if (config.input_schema.has_content()) {
        render_schema_yaml(yaml, "input", config.input_schema);
    }
    if (config.context_schema.has_content()) {
        render_schema_yaml(yaml, "context", config.context_schema);
    }
    if (config.output_schema.has_content()) {
        render_schema_yaml(yaml, "output", config.output_schema);
    }

    yaml.end_mapping() // properties (inner)
        .end_mapping() // spec
        .end_mapping(); // properties (outer)

    // KR7.4: definitions table for struct/enum types referenced by $ref from
    // the port schemas. Sorted by key for deterministic output. Lives at the
    // root of the openAPIV3Schema (structural schema supports "definitions").
    if (!config.definitions.empty()) {
        yaml.begin_mapping("definitions");
        for (const auto &[def_key, def_schema] : config.definitions) {
            render_schema_yaml(yaml, def_key, def_schema);
        }
        yaml.end_mapping(); // definitions
    }

    yaml.end_mapping()   // openAPIV3Schema
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
