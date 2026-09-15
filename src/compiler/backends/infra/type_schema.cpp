#include "compiler/backends/infra/type_schema.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ahfl::backends {

namespace {

namespace ir = ahfl::ir;

constexpr std::string_view kListCanonical = "std::collections::List";
constexpr std::string_view kSetCanonical = "std::collections::Set";
constexpr std::string_view kMapCanonical = "std::collections::Map";

[[nodiscard]] bool canonical_is(const ir::TypeRef &type, std::string_view expected) {
    return type.canonical_name == expected || type.nominal_ref.canonical_name == expected;
}

// OpenAPI component keys restrict themselves to [A-Za-z0-9._-]; rewrite the
// '::' module separator (and any other disallowed byte) deterministically.
[[nodiscard]] std::string sanitize_component_token(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const unsigned char ch : name) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        out.push_back(safe ? static_cast<char>(ch) : '_');
    }
    return out;
}

[[nodiscard]] OpenApiSchema scalar(std::string type, std::string format = {}) {
    OpenApiSchema schema;
    schema.type = std::move(type);
    schema.format = std::move(format);
    return schema;
}

} // namespace

struct OpenApiTypeMapper::StructIndex {
    std::vector<const ir::StructDecl *> structs;
    std::unordered_map<std::size_t, const ir::StructDecl *> by_symbol_id;
    std::unordered_map<std::string, const ir::StructDecl *> by_canonical;
};

OpenApiTypeMapper::OpenApiTypeMapper(const ir::AhflIr &program) : program_(program) {    auto index = make_owned<StructIndex>();
    for (const auto &decl : program_.declarations) {
        if (const auto *structure = std::get_if<ir::StructDecl>(&decl)) {
            index->structs.push_back(structure);
            if (structure->symbol_ref.id.has_value()) {
                index->by_symbol_id.emplace(*structure->symbol_ref.id, structure);
            }
            if (!structure->symbol_ref.canonical_name.empty()) {
                index->by_canonical.emplace(structure->symbol_ref.canonical_name, structure);
            }
        }
    }
    index_ = std::move(index);
}

OpenApiTypeMapper::~OpenApiTypeMapper() = default;

std::string OpenApiTypeMapper::component_key(std::string_view canonical_name) {
    if (auto found = component_keys_.find(canonical_name); found != component_keys_.end()) {
        return found->second;
    }
    const std::string key = sanitize_component_token(canonical_name);
    component_keys_.emplace(std::string(canonical_name), key);
    return key;
}

OpenApiSchema OpenApiTypeMapper::map_struct(const ir::TypeRef &type) {
    const std::string canonical =
        type.canonical_name.empty() ? type.nominal_ref.canonical_name : type.canonical_name;
    if (canonical.empty()) {
        return {};
    }

    const ir::StructDecl *structure = nullptr;
    if (type.nominal_ref.id.has_value()) {
        if (auto found = index_->by_symbol_id.find(*type.nominal_ref.id);
            found != index_->by_symbol_id.end()) {
            structure = found->second;
        }
    }
    if (structure == nullptr) {
        if (auto found = index_->by_canonical.find(canonical);
            found != index_->by_canonical.end()) {
            structure = found->second;
        }
    }
    if (structure == nullptr) {
        // A nominal the program does not carry a StructDecl for (unknown
        // external type): emit the reference shape without a component body.
        OpenApiSchema schema;
        schema.ref = "#/components/schemas/" + component_key(canonical);
        return schema;
    }

    const std::string key = component_key(canonical);
    if (!components_.contains(key)) {
        // Reserve an empty object component before descending so recursive
        // struct references terminate.
        components_.emplace(key, OpenApiSchema{});

        OpenApiSchema object = scalar("object");
        object.properties.reserve(structure->fields.size());
        for (const auto &field : structure->fields) {
            object.properties.emplace_back(field.name, map_type(field.type_ref));
            if (!field.default_value.has_value()) {
                object.required.push_back(field.name);
            }
        }
        components_[key] = std::move(object);
    }

    OpenApiSchema schema;
    schema.ref = "#/components/schemas/" + key;
    return schema;
}

OpenApiSchema OpenApiTypeMapper::map_type(const ir::TypeRef &type) {
    using K = ir::TypeRefKind;
    switch (type.kind) {
    case K::Bool:
        return scalar("boolean");
    case K::Int:
        return scalar("integer", "int64");
    case K::BoundedInt: {
        auto schema = scalar("integer", "int64");
        if (type.int_bounds.has_value()) {
            schema.minimum = type.int_bounds->first;
            schema.maximum = type.int_bounds->second;
        }
        return schema;
    }
    case K::Float:
        return scalar("number", "double");
    case K::Decimal: {
        auto schema = scalar("string", "decimal");
        return schema;
    }
    case K::String:
        return scalar("string");
    case K::BoundedString: {
        auto schema = scalar("string");
        if (type.string_bounds.has_value()) {
            schema.min_length = type.string_bounds->first;
            schema.max_length = type.string_bounds->second;
        }
        return schema;
    }
    case K::UUID:
        return scalar("string", "uuid");
    case K::Timestamp:
        return scalar("string", "date-time");
    case K::Duration:
        return scalar("string", "duration");
    case K::Struct: {
        if (canonical_is(type, kListCanonical) && !type.params.empty()) {
            auto schema = scalar("array");
            schema.items = make_owned<OpenApiSchema>(map_type(*type.params.front()));
            if (type.collection_capacity.has_value()) {
                schema.max_items = static_cast<std::int64_t>(*type.collection_capacity);
            }
            return schema;
        }
        if (canonical_is(type, kSetCanonical) && !type.params.empty()) {
            auto schema = scalar("array");
            schema.items = make_owned<OpenApiSchema>(map_type(*type.params.front()));
            schema.unique_items = true;
            if (type.collection_capacity.has_value()) {
                schema.max_items = static_cast<std::int64_t>(*type.collection_capacity);
            }
            return schema;
        }
        if (canonical_is(type, kMapCanonical) && type.params.size() >= 2) {
            auto schema = scalar("object");
            schema.additional_properties =
                make_owned<OpenApiSchema>(map_type(*type.params[1]));
            if (type.collection_capacity.has_value()) {
                schema.max_properties =
                    static_cast<std::int64_t>(*type.collection_capacity);
            }
            return schema;
        }
        return map_struct(type);
    }
    case K::Any:
    case K::Unit:
    case K::Enum:
    case K::Fn:
    case K::Never:
    case K::Unresolved:
        return {};
    }
    return {};
}

std::map<std::string, OpenApiSchema> OpenApiTypeMapper::release_components() {
    return std::move(components_);
}

} // namespace ahfl::backends
