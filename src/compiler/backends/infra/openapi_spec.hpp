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
// name (camelCased in JSON). A node with neither `type` nor `$ref` renders as
// the unconstrained schema `{}`.
struct OpenApiSchema {
    std::string type; // boolean | integer | number | string | object | array
    std::string format;
    std::string description;

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

    Owned<OpenApiSchema> items;                    // array element schema
    Owned<OpenApiSchema> additional_properties;    // map value schema
    std::vector<std::pair<std::string, OpenApiSchema>> properties;
    std::vector<std::string> required;

    OpenApiSchema() = default;
    ~OpenApiSchema() = default;
    OpenApiSchema(OpenApiSchema &&) noexcept = default;
    OpenApiSchema &operator=(OpenApiSchema &&) noexcept = default;

    OpenApiSchema(const OpenApiSchema &other)
        : type(other.type),
          format(other.format),
          description(other.description),
          minimum(other.minimum),
          maximum(other.maximum),
          min_length(other.min_length),
          max_length(other.max_length),
          min_items(other.min_items),
          max_items(other.max_items),
          max_properties(other.max_properties),
          unique_items(other.unique_items),
          ref(other.ref),
          items(other.items ? make_owned<OpenApiSchema>(*other.items) : nullptr),
          additional_properties(other.additional_properties
                                    ? make_owned<OpenApiSchema>(*other.additional_properties)
                                    : nullptr),
          properties(other.properties),
          required(other.required) {}

    OpenApiSchema &operator=(const OpenApiSchema &other) {
        if (this != &other) {
            *this = OpenApiSchema(other);
        }
        return *this;
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
