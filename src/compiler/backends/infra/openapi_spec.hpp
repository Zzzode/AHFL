#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/base/support/ownership.hpp"

namespace ahfl::backends {

// OpenApiSchema is the backend-local JSON Schema (Draft/OpenAPI 3.0 subset)
// tree shared by the OpenAPI renderer and the IR TypeRef -> JSON Schema mapper.
// It is plain data: every field maps to the JSON Schema keyword of the same
// name (camelCased in JSON). A node with neither `type` nor `$ref` nor any
// nested keyword renders as the unconstrained schema `{}`, which the mapper
// uses intentionally for kinds that cannot cross a JSON boundary (Unit, Any,
// Fn, Never, Unresolved); see OpenApiTypeMapper. Renderers treat such a node
// as "no content" rather than advertising an empty JSON object.
struct OpenApiSchema {
    std::string type; // boolean | integer | number | string | object | array
    std::string format;
    std::string description;

    // String-enum members for a C-style nominal enum rendered as a string
    // schema: {"type": "string", "enum": [...]}.
    std::vector<std::string> enum_values;

    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
    std::optional<std::int64_t> min_length;
    std::optional<std::int64_t> max_length;
    std::optional<std::int64_t> min_items;
    std::optional<std::int64_t> max_items;
    std::optional<std::int64_t> max_properties;
    bool unique_items{false};

    // When non-empty this node renders as a bare {"$ref": ref}; every other
    // field is ignored (JSON reference semantics).
    std::string ref;

    Owned<OpenApiSchema> items; // array element schema
    // OpenAPI 3.0 tuple typing: "items" as an ordered schema array (JSON
    // Schema array-form items), one entry per tuple component. Empty for an
    // unbounded array, which uses the scalar `items` pointer instead.
    std::vector<Owned<OpenApiSchema>> tuple_items;
    Owned<OpenApiSchema> additional_properties; // map value schema
    // Externally tagged (Rust/serde-default) nominal-enum variants: every
    // member is a standalone alternative schema combined with "oneOf".
    std::vector<Owned<OpenApiSchema>> one_of;
    std::vector<std::pair<std::string, OpenApiSchema>> properties;
    std::vector<std::string> required;

    OpenApiSchema() = default;
    ~OpenApiSchema() = default;
    OpenApiSchema(OpenApiSchema &&) noexcept = default;
    OpenApiSchema &operator=(OpenApiSchema &&) noexcept = default;

    // The schema tree is move-only. It is built once and moved into the
    // endpoint / component table; a copy would silently deep-clone a large
    // tree. Deleting the copy operations turns an accidental copy into a
    // compile error instead of an implicit O(n) clone.
    OpenApiSchema(const OpenApiSchema &) = delete;
    OpenApiSchema &operator=(const OpenApiSchema &) = delete;

    // Whether this node constrains a JSON value at all. A default-constructed
    // node (Unit / Any / Fn / Never / Unresolved mapping, or a zero-parameter
    // request body) has no content and must render as "no body" rather than
    // a required empty JSON object.
    [[nodiscard]] bool has_content() const noexcept {
        return !type.empty() || !ref.empty() || !enum_values.empty() || items != nullptr ||
               !tuple_items.empty() || additional_properties != nullptr || !one_of.empty() ||
               !properties.empty() || minimum.has_value() || maximum.has_value() ||
               min_length.has_value() || max_length.has_value() || min_items.has_value() ||
               max_items.has_value() || max_properties.has_value() || unique_items;
    }
};

struct OpenApiEndpoint {
    std::string path;
    std::string method;
    std::string summary;
    std::string operation_id;
    OpenApiSchema request_schema;
    OpenApiSchema response_schema;
};

struct OpenApiConfig {
    std::string title;
    std::string version = "1.0.0";
    std::string description;
    std::vector<OpenApiEndpoint> endpoints;
    // components/schemas keyed by canonical component name. std::map keeps the
    // emitted component set sorted, so artifact output is deterministic.
    std::map<std::string, OpenApiSchema> components;
};

struct OpenApiOutput {
    std::string json;
};

[[nodiscard]] OpenApiOutput generate_openapi(const OpenApiConfig &config);

} // namespace ahfl::backends
