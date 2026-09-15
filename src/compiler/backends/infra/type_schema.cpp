#include "compiler/backends/infra/type_schema.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "base/support/hash.hpp"

namespace ahfl::backends {

namespace {

namespace ir = ahfl::ir;

constexpr std::string_view kListCanonical = "std::collections::List";
constexpr std::string_view kSetCanonical = "std::collections::Set";
constexpr std::string_view kMapCanonical = "std::collections::Map";

[[nodiscard]] bool canonical_is(const ir::TypeRef &type, std::string_view expected) {
    return type.canonical_name == expected || type.nominal_ref.canonical_name == expected;
}

[[nodiscard]] std::string canonical_of(const ir::TypeRef &type) {
    return type.canonical_name.empty() ? type.nominal_ref.canonical_name : type.canonical_name;
}

// A use-site TypeRef may be a generic instantiation whose canonical name does
// not carry the type arguments; prefer the display spelling ("Option<Int>")
// which does.
[[nodiscard]] std::string instantiation_arg_identity(const ir::TypeRef &arg) {
    return arg.display_name.empty() ? canonical_of(arg) : arg.display_name;
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

struct OpenApiTypeMapper::NominalIndex {
    std::unordered_map<std::size_t, const ir::StructDecl *> structs_by_id;
    std::unordered_map<std::string, const ir::StructDecl *> structs_by_canonical;
    std::unordered_map<std::size_t, const ir::EnumDecl *> enums_by_id;
    std::unordered_map<std::string, const ir::EnumDecl *> enums_by_canonical;
};

OpenApiTypeMapper::OpenApiTypeMapper(const ir::AhflIr &program) : program_(program) {
    auto index = make_owned<NominalIndex>();
    for (const auto &decl : program_.declarations) {
        if (const auto *structure = std::get_if<ir::StructDecl>(&decl)) {
            if (structure->symbol_ref.id.has_value()) {
                index->structs_by_id.emplace(*structure->symbol_ref.id, structure);
            }
            if (!structure->symbol_ref.canonical_name.empty()) {
                index->structs_by_canonical.emplace(structure->symbol_ref.canonical_name,
                                                    structure);
            }
        } else if (const auto *enumeration = std::get_if<ir::EnumDecl>(&decl)) {
            if (enumeration->symbol_ref.id.has_value()) {
                index->enums_by_id.emplace(*enumeration->symbol_ref.id, enumeration);
            }
            if (!enumeration->symbol_ref.canonical_name.empty()) {
                index->enums_by_canonical.emplace(enumeration->symbol_ref.canonical_name,
                                                  enumeration);
            }
        }
    }
    index_ = std::move(index);
}

OpenApiTypeMapper::~OpenApiTypeMapper() = default;

std::string OpenApiTypeMapper::component_key(const ir::TypeRef &type) {
    const std::string canonical = canonical_of(type);

    // Identity distinguishes distinct monomorphizations of one generic nominal
    // (e.g. Option<Int> vs Option<String> share a canonical name).
    std::string identity = canonical;
    if (!type.params.empty()) {
        identity += "<";
        for (std::size_t i = 0; i < type.params.size(); ++i) {
            if (i > 0) {
                identity += ", ";
            }
            identity += instantiation_arg_identity(*type.params[i]);
        }
        identity += ">";
    }

    if (auto found = component_keys_.find(identity); found != component_keys_.end()) {
        return found->second;
    }

    const std::string base = sanitize_component_token(canonical);
    std::string key = base;
    if (!type.params.empty()) {
        // Every instantiation of a generic nominal gets an FNV discriminator of
        // its full instantiation identity, so distinct type arguments never
        // share a component.
        key = base + "-" + fnv1a_hex_suffix(identity, 10);
    } else if (auto owner = key_owners_.find(base);
               owner != key_owners_.end() && owner->second != identity) {
        // Two distinct canonicals sanitized to one token (e.g. "a::b" vs
        // "a_b"); disambiguate the second with an FNV suffix instead of
        // silently shadowing the first component.
        key = base + "-" + fnv1a_hex_suffix(identity, 10);
    }

    component_keys_.emplace(identity, key);
    key_owners_.emplace(key, identity);
    return key;
}

ir::TypeRef
OpenApiTypeMapper::resolve_template(const std::vector<ir::MemberTypeTemplateNode> &arena,
                                    std::uint32_t root,
                                    const std::vector<const ir::TypeRef *> &type_args) const {
    if (root >= arena.size()) {
        throw std::logic_error("member type template root is outside the declaration arena");
    }
    const ir::MemberTypeTemplateNode &node = arena[root];
    switch (node.kind) {
    case ir::MemberTypeTemplateKind::Concrete:
        return ir::clone_type_ref(node.type_ref);
    case ir::MemberTypeTemplateKind::Param: {
        if (node.param_index < type_args.size() && type_args[node.param_index] != nullptr) {
            return ir::clone_type_ref(*type_args[node.param_index]);
        }
        // Unsubstituted parameter: the Typed HIR -> IR bridge erases TypeVarT
        // to Any, so mirror that rather than fabricating a concrete type.
        ir::TypeRef erased;
        erased.kind = ir::TypeRefKind::Any;
        return erased;
    }
    case ir::MemberTypeTemplateKind::Nominal: {
        ir::TypeRef result = ir::clone_type_ref(node.type_ref);
        result.params.clear();
        result.params.reserve(node.children.size());
        for (const auto child : node.children) {
            result.params.push_back(
                make_owned<ir::TypeRef>(resolve_template(arena, child, type_args)));
        }
        return result;
    }
    case ir::MemberTypeTemplateKind::Fn: {
        ir::TypeRef result;
        result.kind = ir::TypeRefKind::Fn;
        result.params.reserve(node.children.size());
        for (const auto child : node.children) {
            result.params.push_back(
                make_owned<ir::TypeRef>(resolve_template(arena, child, type_args)));
        }
        if (node.fn_return != ir::kInvalidMemberTypeTemplateNode) {
            result.first =
                make_owned<ir::TypeRef>(resolve_template(arena, node.fn_return, type_args));
        }
        return result;
    }
    }
    throw std::logic_error("unknown member type template kind");
}

OpenApiSchema OpenApiTypeMapper::map_struct(const ir::TypeRef &type) {
    const std::string canonical = canonical_of(type);
    if (canonical.empty()) {
        return {};
    }

    const ir::StructDecl *structure = nullptr;
    if (type.nominal_ref.id.has_value()) {
        if (auto found = index_->structs_by_id.find(*type.nominal_ref.id);
            found != index_->structs_by_id.end()) {
            structure = found->second;
        }
    }
    if (structure == nullptr) {
        if (auto found = index_->structs_by_canonical.find(canonical);
            found != index_->structs_by_canonical.end()) {
            structure = found->second;
        }
    }
    if (structure == nullptr) {
        // Fail closed: a nominal the program does not carry a StructDecl for
        // (an unresolved cross-module/stdlib symbol). A $ref here would dangle
        // because no component body is ever published, so record a lowering
        // error and return the empty schema; lower_openapi refuses to emit when
        // errors() is non-empty.
        errors_.emplace_back("unresolved struct nominal '" + canonical +
                             "' has no declaration in the program");
        return {};
    }

    const std::string key = component_key(type);
    if (components_.contains(key)) {
        OpenApiSchema schema;
        schema.ref = "#/components/schemas/" + key;
        return schema;
    }

    std::vector<const ir::TypeRef *> type_args;
    type_args.reserve(type.params.size());
    for (const auto &param : type.params) {
        type_args.push_back(param.get());
    }

    // Reserve an empty object component before descending so recursive struct
    // references terminate.
    components_.emplace(key, OpenApiSchema{});

    OpenApiSchema object = scalar("object");
    object.properties.reserve(structure->fields.size());
    for (std::size_t i = 0; i < structure->fields.size(); ++i) {
        const auto &field = structure->fields[i];
        ir::TypeRef field_type = i < structure->field_type_template_roots.size()
                                     ? resolve_template(structure->member_type_templates,
                                                        structure->field_type_template_roots[i],
                                                        type_args)
                                     : ir::clone_type_ref(field.type_ref);
        object.properties.emplace_back(field.name, map_type(field_type));
        if (!field.default_value.has_value()) {
            object.required.push_back(field.name);
        }
    }
    components_[key] = std::move(object);

    OpenApiSchema schema;
    schema.ref = "#/components/schemas/" + key;
    return schema;
}

OpenApiSchema OpenApiTypeMapper::map_enum(const ir::TypeRef &type) {
    const std::string canonical = canonical_of(type);
    if (canonical.empty()) {
        return {};
    }

    const ir::EnumDecl *enumeration = nullptr;
    if (type.nominal_ref.id.has_value()) {
        if (auto found = index_->enums_by_id.find(*type.nominal_ref.id);
            found != index_->enums_by_id.end()) {
            enumeration = found->second;
        }
    }
    if (enumeration == nullptr) {
        if (auto found = index_->enums_by_canonical.find(canonical);
            found != index_->enums_by_canonical.end()) {
            enumeration = found->second;
        }
    }
    if (enumeration == nullptr) {
        errors_.emplace_back("unresolved enum nominal '" + canonical +
                             "' has no declaration in the program");
        return {};
    }

    const std::string key = component_key(type);
    if (components_.contains(key)) {
        OpenApiSchema schema;
        schema.ref = "#/components/schemas/" + key;
        return schema;
    }

    std::vector<const ir::TypeRef *> type_args;
    type_args.reserve(type.params.size());
    for (const auto &param : type.params) {
        type_args.push_back(param.get());
    }

    const bool all_unit =
        std::all_of(enumeration->variants.begin(),
                    enumeration->variants.end(),
                    [](const ir::EnumVariantDecl &variant) {
                        return variant.payload_kind == ir::EnumVariantPayloadKind::Unit;
                    });

    // Reserve before descent so recursive / self-referential enums terminate.
    components_.emplace(key, OpenApiSchema{});

    OpenApiSchema component;
    if (all_unit) {
        // C-style nominal enum: a string restricted to the declared variant
        // names, so generated clients/validators reject anything else instead
        // of accepting arbitrary JSON.
        component = scalar("string");
        component.enum_values.reserve(enumeration->variants.size());
        for (const auto &variant : enumeration->variants) {
            component.enum_values.push_back(variant.name);
        }
    } else {
        // Payload-carrying enum: externally tagged oneOf (Rust/serde default).
        // Each alternative is an object with one property named after the
        // variant; the property value is the payload schema (unit -> {},
        // single-tuple -> inline payload, multi-tuple -> positional tuple,
        // struct -> payload object).
        component.one_of.reserve(enumeration->variants.size());
        for (const auto &variant : enumeration->variants) {
            OpenApiSchema alternative = scalar("object");
            alternative.required.push_back(variant.name);

            OpenApiSchema payload;
            if (variant.payload_kind == ir::EnumVariantPayloadKind::Tuple) {
                std::vector<ir::TypeRef> payload_types;
                if (!variant.payload_type_template_roots.empty()) {
                    payload_types.reserve(variant.payload_type_template_roots.size());
                    for (const auto root : variant.payload_type_template_roots) {
                        payload_types.push_back(
                            resolve_template(enumeration->member_type_templates, root, type_args));
                    }
                } else {
                    payload_types.reserve(variant.payload.size());
                    for (const auto &part : variant.payload) {
                        payload_types.push_back(ir::clone_type_ref(part));
                    }
                }

                if (payload_types.size() == 1) {
                    payload = map_type(payload_types.front());
                } else {
                    payload = scalar("array");
                    payload.tuple_items.reserve(payload_types.size());
                    for (const auto &part : payload_types) {
                        payload.tuple_items.push_back(make_owned<OpenApiSchema>(map_type(part)));
                    }
                }
            } else if (variant.payload_kind == ir::EnumVariantPayloadKind::Struct) {
                payload = scalar("object");
                for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                    const auto &field = variant.fields[i];
                    ir::TypeRef field_type =
                        i < variant.payload_type_template_roots.size()
                            ? resolve_template(enumeration->member_type_templates,
                                               variant.payload_type_template_roots[i],
                                               type_args)
                            : ir::clone_type_ref(field.type_ref);
                    payload.properties.emplace_back(field.name, map_type(field_type));
                    payload.required.push_back(field.name);
                }
            }
            // Unit variants leave `payload` as the empty schema (unit marker).

            alternative.properties.emplace_back(variant.name, std::move(payload));
            component.one_of.push_back(make_owned<OpenApiSchema>(std::move(alternative)));
        }
    }
    components_[key] = std::move(component);

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
    case K::Decimal:
        return scalar("string", "decimal");
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
            schema.additional_properties = make_owned<OpenApiSchema>(map_type(*type.params[1]));
            if (type.collection_capacity.has_value()) {
                schema.max_properties = static_cast<std::int64_t>(*type.collection_capacity);
            }
            return schema;
        }
        return map_struct(type);
    }
    case K::Enum:
        return map_enum(type);
    case K::Any:
    case K::Unit:
    case K::Fn:
    case K::Never:
    case K::Unresolved:
        // No JSON-crossing representation; the renderer omits the body.
        return {};
    }
    return {};
}

std::map<std::string, OpenApiSchema> OpenApiTypeMapper::release_components() {
    return std::move(components_);
}

} // namespace ahfl::backends
