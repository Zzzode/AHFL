#include "compiler/backends/infra/k8s_crd.hpp"

#include "base/support/hash.hpp"
#include "base/support/json.hpp"
#include "base/support/structured_writer.hpp"
#include "compiler/backends/infra/k8s_naming.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ahfl::backends {

namespace {

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

// CRD projection is deliberately local: OpenAPI retains its reference and
// tuple semantics while this target enforces Kubernetes structural rules.
[[nodiscard]] std::expected<OpenApiSchema, std::string>
project_schema(const OpenApiSchema &source,
               const std::map<std::string, OpenApiSchema> &components,
               std::vector<const OpenApiSchema *> &active,
               const std::string &path) {
    const auto reject = [&](std::string reason) -> std::expected<OpenApiSchema, std::string> {
        return std::unexpected(path + ": " + std::move(reason));
    };
    if (std::find(active.begin(), active.end(), &source) != active.end()) {
        return reject("recursive types cannot be represented by a finite CRD schema");
    }
    active.push_back(&source);
    struct Pop {
        std::vector<const OpenApiSchema *> &stack;
        ~Pop() {
            stack.pop_back();
        }
    } pop{active};
    if (!source.ref.empty()) {
        constexpr std::string_view prefix = "#/components/schemas/";
        if (!source.ref.starts_with(prefix)) {
            return reject("unsupported schema reference '" + source.ref + "'");
        }
        const auto found = components.find(source.ref.substr(prefix.size()));
        if (found == components.end()) {
            return reject("unresolved schema reference '" + source.ref + "'");
        }
        return project_schema(found->second, components, active, path);
    }
    if (!source.tuple_items.empty()) {
        return reject("positional tuples are not supported by Kubernetes CRD schemas");
    }
    if (source.additional_properties != nullptr && !source.properties.empty()) {
        return reject("Kubernetes forbids combining properties with additionalProperties");
    }
    OpenApiSchema result;
    result.type = source.type;
    result.format = source.format;
    result.description = source.description;
    result.enum_values = source.enum_values;
    result.minimum = source.minimum;
    result.maximum = source.maximum;
    result.min_length = source.min_length;
    result.max_length = source.max_length;
    result.min_items = source.min_items;
    result.max_items = source.max_items;
    result.max_properties = source.max_properties;
    result.required = source.required;
    result.unique_items = source.unique_items;
    if (!source.one_of.empty()) {
        // Externally tagged payload enums have one distinct property per
        // variant. Put the structure outside oneOf; branches only require
        // their tag. maxProperties keeps the tag exclusive after pruning.
        if (!source.type.empty() || !source.properties.empty()) {
            return reject("unsupported union schema; expected an externally tagged enum");
        }
        result.type = "object";
        result.max_properties = 1;
        for (const auto &variant : source.one_of) {
            if (variant == nullptr || variant->type != "object" ||
                variant->properties.size() != 1 || variant->required.size() != 1 ||
                variant->properties.front().first != variant->required.front()) {
                return reject("unsupported union branch; expected one required variant tag");
            }
            const auto &[name, payload] = variant->properties.front();
            if (std::any_of(result.properties.begin(),
                            result.properties.end(),
                            [&](const auto &field) { return field.first == name; })) {
                return reject("duplicate enum variant tag '" + name + "'");
            }
            auto projected = project_schema(payload, components, active, path + "." + name);
            if (!projected) {
                return std::unexpected(projected.error());
            }
            result.properties.emplace_back(name, std::move(*projected));
            auto constraint = make_owned<OpenApiSchema>();
            constraint->required = {name};
            result.one_of.push_back(std::move(constraint));
        }
    } else {
        if (source.type.empty()) {
            return reject("type has no concrete JSON representation for a structural CRD schema");
        }
        for (const auto &[name, field] : source.properties) {
            auto projected = project_schema(field, components, active, path + "." + name);
            if (!projected) {
                return std::unexpected(projected.error());
            }
            result.properties.emplace_back(name, std::move(*projected));
        }
    }
    if (source.items != nullptr) {
        auto projected = project_schema(*source.items, components, active, path + "[]");
        if (!projected) {
            return std::unexpected(projected.error());
        }
        result.items = make_owned<OpenApiSchema>(std::move(*projected));
    }
    if (source.additional_properties != nullptr) {
        auto projected =
            project_schema(*source.additional_properties, components, active, path + ".*");
        if (!projected) {
            return std::unexpected(projected.error());
        }
        result.additional_properties = make_owned<OpenApiSchema>(std::move(*projected));
    }
    if (result.type == "array" && result.items == nullptr) {
        return reject("array has no concrete item schema");
    }
    if (result.unique_items && (result.items == nullptr || result.items->type == "object" ||
                                result.items->type == "array")) {
        return reject("Set requires scalar elements for a faithful Kubernetes list-set schema");
    }
    return result;
}

// JSON double-quoted strings are valid YAML scalars too. Quote source names
// so identifiers such as On/Off/Null cannot turn into booleans or null.
[[nodiscard]] std::string yaml_string(std::string_view value) {
    std::ostringstream out;
    write_escaped_json_string(out, value);
    return out.str();
}

void string_list(YamlWriter &yaml, std::string_view key,
                 const std::vector<std::string> &values) {
    std::vector<std::string> quoted;
    quoted.reserve(values.size());
    for (const auto &value : values) {
        quoted.push_back(yaml_string(value));
    }
    yaml.key_inline_list(key, quoted);
}

void render_schema_yaml(YamlWriter &yaml, std::string_view key, const OpenApiSchema &schema) {
    yaml.begin_mapping(key);

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
        string_list(yaml, "enum", schema.enum_values);
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
        yaml.key_value("x-kubernetes-list-type", "set");
    }
    if (schema.items != nullptr) {
        render_schema_yaml(yaml, "items", *schema.items);
    }
    if (schema.additional_properties != nullptr) {
        render_schema_yaml(yaml, "additionalProperties", *schema.additional_properties);
    }
    if (!schema.one_of.empty()) {
        yaml.begin_mapping("oneOf");
        for (const auto &variant : schema.one_of) {
            yaml.begin_list_item();
            string_list(yaml, "required", variant->required);
            yaml.end_list_item();
        }
        yaml.end_mapping();
    }
    if (!schema.properties.empty()) {
        yaml.begin_mapping("properties");
        for (const auto &[prop_name, prop_schema] : schema.properties) {
            render_schema_yaml(yaml, yaml_string(prop_name), prop_schema);
        }
        yaml.end_mapping(); // properties
    }
    if (!schema.required.empty()) {
        string_list(yaml, "required", schema.required);
    }

    yaml.end_mapping(); // key
}

} // anonymous namespace

std::expected<OpenApiSchema, std::string>
project_crd_schema(const OpenApiSchema &schema,
                   const std::map<std::string, OpenApiSchema> &components,
                   std::string_view path) {
    std::vector<const OpenApiSchema *> active;
    return project_schema(schema, components, active, std::string(path));
}

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
        string_list(yaml, "enum", config.states);
    }
    yaml.end_mapping(); // state

    // Initial / final state markers.
    if (!config.initial_state.empty()) {
        yaml.begin_mapping("initialState")
            .key_value("type", "string")
            .key_value_quoted("default", config.initial_state)
            .end_mapping();
    }
    if (!config.final_states.empty()) {
        yaml.begin_mapping("finalStates")
            .key_value("type", "array")
            .begin_mapping("items")
            .key_value("type", "string")
            .end_mapping();
        if (config.final_states.size() == 1) {
            string_list(yaml, "default", config.final_states);
        }
        yaml.end_mapping(); // finalStates
    }

    // Capability inventory (already collected during lowering).
    if (!config.capabilities.empty()) {
        yaml.begin_mapping("capabilities")
            .key_value("type", "array")
            .begin_mapping("items")
            .key_value("type", "string")
            .end_mapping();
        string_list(yaml, "default", config.capabilities);
        yaml.end_mapping();
    }

    for (const auto &[name, schema] :
         {std::pair<std::string_view, const OpenApiSchema *>{"input", &config.input_schema},
          {"context", &config.context_schema},
          {"output", &config.output_schema}}) {
        if (!schema->has_content()) {
            continue;
        }
        auto projected = project_crd_schema(*schema, config.components, name);
        if (!projected) {
            throw std::invalid_argument(projected.error());
        }
        render_schema_yaml(yaml, name, *projected);
    }

    yaml.end_mapping()  // properties (inner)
        .end_mapping()  // spec
        .end_mapping(); // properties (outer)

    yaml.end_mapping()   // openAPIV3Schema
        .end_mapping()   // schema
        .end_list_item() // versions item
        .end_mapping()   // versions
        .key_value("scope", "Namespaced")
        .begin_mapping("names")
        .key_value("plural", output.plural)
        .key_value("singular", rfc1123_name(config, false))
        .key_value_quoted("kind", output.kind)
        .end_mapping()  // names
        .end_mapping(); // spec

    output.yaml = yaml.str();
    return output;
}

} // namespace ahfl::backends
