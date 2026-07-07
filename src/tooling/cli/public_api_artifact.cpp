#include "tooling/cli/public_api_artifact.hpp"

#include "base/json/json_value.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ahfl::cli {
namespace {

using Json = ahfl::json::JsonValue;

constexpr std::string_view kPublicApiSchema = "ahfl.public_api.v1";
constexpr std::string_view kPublicApiDiffSchema = "ahfl.public_api.diff.v1";

struct ApiEntry {
    std::string api_id;
    std::string entry_kind;
    std::string symbol_kind;
    std::string name_space;
    std::string local_name;
    std::string canonical_name;
    std::string module_name;
    std::string source_path;
    ahfl::SourceRange range{};
    std::optional<ahfl::SourcePosition> begin;
    std::optional<ahfl::SourcePosition> end;
    std::optional<std::size_t> symbol_id;
    std::optional<std::size_t> alias_id;
    std::optional<std::size_t> target_symbol_id;
    std::string target_canonical_name;
    std::string signature_text;
    std::unique_ptr<Json> signature;
};

[[nodiscard]] std::string join_strings(const std::vector<std::string> &items,
                                       std::string_view separator) {
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            text.append(separator);
        }
        text.append(items[i]);
    }
    return text;
}

[[nodiscard]] std::unique_ptr<Json> string_array(const std::vector<std::string> &items) {
    auto array = Json::make_array();
    for (const auto &item : items) {
        array->push(Json::make_string(item));
    }
    return array;
}

[[nodiscard]] std::string namespace_name(ahfl::SymbolNamespace name_space) {
    switch (name_space) {
    case ahfl::SymbolNamespace::Types:
        return "types";
    case ahfl::SymbolNamespace::Consts:
        return "consts";
    case ahfl::SymbolNamespace::Capabilities:
        return "capabilities";
    case ahfl::SymbolNamespace::Predicates:
        return "predicates";
    case ahfl::SymbolNamespace::Agents:
        return "agents";
    case ahfl::SymbolNamespace::Workflows:
        return "workflows";
    case ahfl::SymbolNamespace::Functions:
        return "functions";
    case ahfl::SymbolNamespace::Traits:
        return "traits";
    }
    return "unknown";
}

[[nodiscard]] std::string symbol_kind_name(ahfl::SymbolKind kind) {
    switch (kind) {
    case ahfl::SymbolKind::Struct:
        return "struct";
    case ahfl::SymbolKind::Enum:
        return "enum";
    case ahfl::SymbolKind::TypeAlias:
        return "type_alias";
    case ahfl::SymbolKind::Const:
        return "const";
    case ahfl::SymbolKind::Capability:
        return "capability";
    case ahfl::SymbolKind::Predicate:
        return "predicate";
    case ahfl::SymbolKind::Agent:
        return "agent";
    case ahfl::SymbolKind::Workflow:
        return "workflow";
    case ahfl::SymbolKind::Function:
        return "function";
    case ahfl::SymbolKind::Trait:
        return "trait";
    }
    return "unknown";
}

[[nodiscard]] bool symbol_kind_matches_decl(ahfl::SymbolKind kind, ahfl::ast::NodeKind node_kind) {
    switch (kind) {
    case ahfl::SymbolKind::Struct:
        return node_kind == ahfl::ast::NodeKind::StructDecl;
    case ahfl::SymbolKind::Enum:
        return node_kind == ahfl::ast::NodeKind::EnumDecl;
    case ahfl::SymbolKind::TypeAlias:
        return node_kind == ahfl::ast::NodeKind::TypeAliasDecl;
    case ahfl::SymbolKind::Const:
        return node_kind == ahfl::ast::NodeKind::ConstDecl;
    case ahfl::SymbolKind::Capability:
        return node_kind == ahfl::ast::NodeKind::CapabilityDecl;
    case ahfl::SymbolKind::Predicate:
        return node_kind == ahfl::ast::NodeKind::PredicateDecl;
    case ahfl::SymbolKind::Agent:
        return node_kind == ahfl::ast::NodeKind::AgentDecl;
    case ahfl::SymbolKind::Workflow:
        return node_kind == ahfl::ast::NodeKind::WorkflowDecl;
    case ahfl::SymbolKind::Function:
        return node_kind == ahfl::ast::NodeKind::FnDecl;
    case ahfl::SymbolKind::Trait:
        return node_kind == ahfl::ast::NodeKind::TraitDecl;
    }
    return false;
}

[[nodiscard]] std::string declaration_name(const ahfl::ast::Decl &decl) {
    switch (decl.kind) {
    case ahfl::ast::NodeKind::ConstDecl:
        return static_cast<const ahfl::ast::ConstDecl &>(decl).name;
    case ahfl::ast::NodeKind::TypeAliasDecl:
        return static_cast<const ahfl::ast::TypeAliasDecl &>(decl).name;
    case ahfl::ast::NodeKind::StructDecl:
        return static_cast<const ahfl::ast::StructDecl &>(decl).name;
    case ahfl::ast::NodeKind::EnumDecl:
        return static_cast<const ahfl::ast::EnumDecl &>(decl).name;
    case ahfl::ast::NodeKind::CapabilityDecl:
        return static_cast<const ahfl::ast::CapabilityDecl &>(decl).name;
    case ahfl::ast::NodeKind::PredicateDecl:
        return static_cast<const ahfl::ast::PredicateDecl &>(decl).name;
    case ahfl::ast::NodeKind::AgentDecl:
        return static_cast<const ahfl::ast::AgentDecl &>(decl).name;
    case ahfl::ast::NodeKind::WorkflowDecl:
        return static_cast<const ahfl::ast::WorkflowDecl &>(decl).name;
    case ahfl::ast::NodeKind::FnDecl:
        return static_cast<const ahfl::ast::FnDecl &>(decl).name;
    case ahfl::ast::NodeKind::TraitDecl:
        return static_cast<const ahfl::ast::TraitDecl &>(decl).name;
    default:
        return {};
    }
}

[[nodiscard]] const ahfl::SourceUnit *source_unit_for_id(const ahfl::SourceGraph &graph,
                                                         ahfl::SourceId id) {
    for (const auto &source : graph.sources) {
        if (source.id == id) {
            return &source;
        }
    }
    return nullptr;
}

[[nodiscard]] const ahfl::ast::Decl *find_symbol_declaration(const ahfl::SourceGraph &graph,
                                                             const ahfl::Symbol &symbol) {
    if (!symbol.source_id.has_value()) {
        return nullptr;
    }
    const auto *source = source_unit_for_id(graph, *symbol.source_id);
    if (source == nullptr || !source->program) {
        return nullptr;
    }
    for (const auto &decl : source->program->declarations) {
        if (!decl || !symbol_kind_matches_decl(symbol.kind, decl->kind)) {
            continue;
        }
        if (declaration_name(*decl) == symbol.local_name) {
            return decl.get();
        }
    }
    return nullptr;
}

[[nodiscard]] std::string use_decl_public_name(const ahfl::ast::UseDecl &decl) {
    if (!decl.alias.empty()) {
        return decl.alias;
    }
    if (!decl.path || decl.path->segments.empty()) {
        return {};
    }
    return decl.path->segments.back();
}

[[nodiscard]] const ahfl::ast::UseDecl *find_alias_declaration(const ahfl::SourceGraph &graph,
                                                               const ahfl::PublicAlias &alias) {
    if (!alias.source_id.has_value()) {
        return nullptr;
    }
    const auto *source = source_unit_for_id(graph, *alias.source_id);
    if (source == nullptr || !source->program) {
        return nullptr;
    }
    for (const auto &decl : source->program->declarations) {
        if (!decl || decl->kind != ahfl::ast::NodeKind::UseDecl) {
            continue;
        }
        const auto &use_decl = static_cast<const ahfl::ast::UseDecl &>(*decl);
        if (use_decl_public_name(use_decl) == alias.local_name) {
            return &use_decl;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string type_text(const ahfl::Owned<ahfl::ast::TypeSyntax> &type) {
    return type ? type->spelling() : "Unit";
}

[[nodiscard]] std::string expr_text(const ahfl::Owned<ahfl::ast::ExprSyntax> &expr) {
    if (!expr) {
        return {};
    }
    if (!expr->text.empty()) {
        return expr->text;
    }
    return "expr@" + std::to_string(expr->range.begin_offset);
}

[[nodiscard]] std::string type_param_text(const ahfl::ast::TypeParamSyntax &param) {
    std::string text = param.name;
    if (!param.bounds.empty()) {
        std::vector<std::string> bounds;
        bounds.reserve(param.bounds.size());
        for (const auto &bound : param.bounds) {
            bounds.push_back(type_text(bound));
        }
        text.append(": ");
        text.append(join_strings(bounds, " + "));
    }
    return text;
}

[[nodiscard]] std::vector<std::string>
type_param_texts(const std::vector<ahfl::Owned<ahfl::ast::TypeParamSyntax>> &params) {
    std::vector<std::string> texts;
    texts.reserve(params.size());
    for (const auto &param : params) {
        if (param) {
            texts.push_back(type_param_text(*param));
        }
    }
    return texts;
}

[[nodiscard]] std::string
type_param_suffix(const std::vector<ahfl::Owned<ahfl::ast::TypeParamSyntax>> &params) {
    auto texts = type_param_texts(params);
    if (texts.empty()) {
        return {};
    }
    return "<" + join_strings(texts, ", ") + ">";
}

[[nodiscard]] std::unique_ptr<Json>
type_params_json(const std::vector<ahfl::Owned<ahfl::ast::TypeParamSyntax>> &params) {
    auto array = Json::make_array();
    for (const auto &param : params) {
        if (!param) {
            continue;
        }
        auto item = Json::make_object();
        item->set("name", Json::make_string(param->name));
        std::vector<std::string> bounds;
        bounds.reserve(param->bounds.size());
        for (const auto &bound : param->bounds) {
            bounds.push_back(type_text(bound));
        }
        item->set("bounds", string_array(bounds));
        array->push(std::move(item));
    }
    return array;
}

[[nodiscard]] std::string param_text(const ahfl::ast::ParamDeclSyntax &param) {
    if (param.is_self && !param.type) {
        return param.is_self_mut ? "mut self" : "self";
    }
    std::string text = param.name;
    if (!param.type) {
        return text;
    }
    text.append(": ");
    text.append(param.type->spelling());
    return text;
}

[[nodiscard]] std::vector<std::string>
param_texts(const std::vector<ahfl::Owned<ahfl::ast::ParamDeclSyntax>> &params) {
    std::vector<std::string> texts;
    texts.reserve(params.size());
    for (const auto &param : params) {
        if (param) {
            texts.push_back(param_text(*param));
        }
    }
    return texts;
}

[[nodiscard]] std::unique_ptr<Json>
params_json(const std::vector<ahfl::Owned<ahfl::ast::ParamDeclSyntax>> &params) {
    auto array = Json::make_array();
    for (const auto &param : params) {
        if (!param) {
            continue;
        }
        auto item = Json::make_object();
        item->set("name", Json::make_string(param->name));
        item->set("type", Json::make_string(type_text(param->type)));
        item->set("self", Json::make_bool(param->is_self));
        item->set("mutable_self", Json::make_bool(param->is_self_mut));
        array->push(std::move(item));
    }
    return array;
}

[[nodiscard]] std::string
where_constraint_text(const ahfl::ast::WhereConstraintSyntax &constraint) {
    std::string text = type_text(constraint.subject);
    if (constraint.is_predicate) {
        text.append("::");
        text.append(constraint.trait_name);
        text.push_back('(');
        std::vector<std::string> arguments;
        arguments.reserve(constraint.arguments.size());
        for (const auto &argument : constraint.arguments) {
            arguments.push_back(type_text(argument));
        }
        text.append(join_strings(arguments, ", "));
        text.push_back(')');
        return text;
    }

    std::vector<std::string> bounds;
    bounds.reserve(constraint.bounds.size());
    for (const auto &bound : constraint.bounds) {
        bounds.push_back(type_text(bound));
    }
    if (!bounds.empty()) {
        text.append(": ");
        text.append(join_strings(bounds, " + "));
    }
    return text;
}

[[nodiscard]] std::vector<std::string>
where_constraint_texts(const ahfl::Owned<ahfl::ast::WhereClauseSyntax> &where_clause) {
    std::vector<std::string> texts;
    if (!where_clause) {
        return texts;
    }
    texts.reserve(where_clause->constraints.size());
    for (const auto &constraint : where_clause->constraints) {
        if (constraint) {
            texts.push_back(where_constraint_text(*constraint));
        }
    }
    return texts;
}

[[nodiscard]] std::string
where_suffix(const ahfl::Owned<ahfl::ast::WhereClauseSyntax> &where_clause) {
    auto texts = where_constraint_texts(where_clause);
    if (texts.empty()) {
        return {};
    }
    return " where " + join_strings(texts, ", ");
}

[[nodiscard]] std::unique_ptr<Json>
where_json(const ahfl::Owned<ahfl::ast::WhereClauseSyntax> &where_clause) {
    return string_array(where_constraint_texts(where_clause));
}

[[nodiscard]] std::vector<std::string>
effect_capability_texts(const ahfl::ast::EffectClauseSyntax &effect) {
    std::vector<std::string> capabilities;
    capabilities.reserve(effect.capabilities.size());
    for (const auto &capability : effect.capabilities) {
        if (capability) {
            capabilities.push_back(capability->spelling());
        }
    }
    return capabilities;
}

[[nodiscard]] std::string effect_text(const ahfl::Owned<ahfl::ast::EffectClauseSyntax> &effect) {
    if (!effect) {
        return {};
    }

    std::string text = " effect ";
    if (effect->kind == ahfl::ast::EffectClauseKind::Capability) {
        text.append(join_strings(effect_capability_texts(*effect), ", "));
    } else {
        text.append(std::string{ahfl::ast::to_string(effect->kind)});
    }
    if (effect->decreases_expr) {
        text.append(" decreases ");
        text.append(expr_text(effect->decreases_expr));
    }
    return text;
}

[[nodiscard]] std::unique_ptr<Json>
effect_json(const ahfl::Owned<ahfl::ast::EffectClauseSyntax> &effect) {
    if (!effect) {
        return Json::make_null();
    }
    auto object = Json::make_object();
    object->set("kind", Json::make_string(std::string{ahfl::ast::to_string(effect->kind)}));
    object->set("capabilities", string_array(effect_capability_texts(*effect)));
    object->set("decreases", Json::make_string(expr_text(effect->decreases_expr)));
    return object;
}

[[nodiscard]] std::vector<std::string>
qualified_name_texts(const std::vector<ahfl::Owned<ahfl::ast::QualifiedName>> &names) {
    std::vector<std::string> texts;
    texts.reserve(names.size());
    for (const auto &name : names) {
        if (name) {
            texts.push_back(name->spelling());
        }
    }
    return texts;
}

[[nodiscard]] std::string
capability_effect_text(const ahfl::Owned<ahfl::ast::CapabilityEffectSyntax> &effect) {
    if (!effect) {
        return {};
    }
    std::string text = std::string{ahfl::ast::to_string(effect->effect_kind)};
    if (effect->domain) {
        text.append(" domain ");
        text.append(effect->domain->spelling());
    }
    return text;
}

[[nodiscard]] std::unique_ptr<Json>
capability_effect_json(const ahfl::Owned<ahfl::ast::CapabilityEffectSyntax> &effect) {
    if (!effect) {
        return Json::make_null();
    }
    auto object = Json::make_object();
    object->set("kind", Json::make_string(std::string{ahfl::ast::to_string(effect->effect_kind)}));
    object->set("domain",
                effect->domain ? Json::make_string(effect->domain->spelling()) : Json::make_null());
    object->set("idempotency_key",
                effect->idempotency_key ? Json::make_string(effect->idempotency_key->spelling())
                                        : Json::make_null());
    object->set("receipt",
                Json::make_string(std::string{ahfl::ast::to_string(effect->receipt_mode)}));
    object->set("retry", Json::make_string(std::string{ahfl::ast::to_string(effect->retry_mode)}));
    object->set("timeout",
                effect->timeout ? Json::make_string(effect->timeout->spelling) : Json::make_null());
    object->set("compensation",
                effect->compensation ? Json::make_string(effect->compensation->spelling())
                                     : Json::make_null());
    object->set("policies", string_array(qualified_name_texts(effect->policies)));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> base_signature_json(std::string kind, std::string text) {
    auto object = Json::make_object();
    object->set("kind", Json::make_string(std::move(kind)));
    object->set("text", Json::make_string(std::move(text)));
    return object;
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
struct_signature(const ahfl::ast::StructDecl &decl) {
    std::ostringstream text;
    text << "struct " << decl.name << type_param_suffix(decl.type_params);
    text << where_suffix(decl.where_clause) << " {";
    auto fields = Json::make_array();
    for (std::size_t i = 0; i < decl.fields.size(); ++i) {
        const auto &field = decl.fields[i];
        if (!field) {
            continue;
        }
        text << " " << field->name << ": " << type_text(field->type) << ";";
        auto item = Json::make_object();
        item->set("name", Json::make_string(field->name));
        item->set("type", Json::make_string(type_text(field->type)));
        item->set("has_default", Json::make_bool(static_cast<bool>(field->default_value)));
        fields->push(std::move(item));
    }
    text << " }";

    auto object = base_signature_json("struct", text.str());
    object->set("type_params", type_params_json(decl.type_params));
    object->set("where", where_json(decl.where_clause));
    object->set("fields", std::move(fields));
    return {object->get("text")->string_val, std::move(object)};
}

[[nodiscard]] std::string variant_payload_kind_name(ahfl::ast::EnumVariantPayloadKind kind) {
    switch (kind) {
    case ahfl::ast::EnumVariantPayloadKind::Unit:
        return "unit";
    case ahfl::ast::EnumVariantPayloadKind::Tuple:
        return "tuple";
    case ahfl::ast::EnumVariantPayloadKind::Struct:
        return "struct";
    }
    return "unknown";
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
enum_signature(const ahfl::ast::EnumDecl &decl) {
    std::ostringstream text;
    text << "enum " << decl.name << type_param_suffix(decl.type_params);
    text << where_suffix(decl.where_clause) << " {";
    auto variants = Json::make_array();
    for (std::size_t i = 0; i < decl.variants.size(); ++i) {
        const auto &variant = decl.variants[i];
        if (!variant) {
            continue;
        }
        if (i != 0) {
            text << ",";
        }
        text << " " << variant->name;

        auto item = Json::make_object();
        item->set("name", Json::make_string(variant->name));
        item->set("payload_kind",
                  Json::make_string(variant_payload_kind_name(variant->payload_kind)));

        auto payload = Json::make_array();
        if (variant->payload_kind == ahfl::ast::EnumVariantPayloadKind::Tuple) {
            std::vector<std::string> payload_types;
            payload_types.reserve(variant->payload.size());
            for (const auto &type : variant->payload) {
                payload_types.push_back(type_text(type));
                payload->push(Json::make_string(type_text(type)));
            }
            text << "(" << join_strings(payload_types, ", ") << ")";
        }

        auto named_fields = Json::make_array();
        if (variant->payload_kind == ahfl::ast::EnumVariantPayloadKind::Struct) {
            std::vector<std::string> field_texts;
            field_texts.reserve(variant->named_fields.size());
            for (const auto &field : variant->named_fields) {
                if (!field) {
                    continue;
                }
                field_texts.push_back(field->name + ": " + type_text(field->type));
                auto field_item = Json::make_object();
                field_item->set("name", Json::make_string(field->name));
                field_item->set("type", Json::make_string(type_text(field->type)));
                field_item->set("has_default",
                                Json::make_bool(static_cast<bool>(field->default_value)));
                named_fields->push(std::move(field_item));
            }
            text << " { " << join_strings(field_texts, ", ") << " }";
        }
        item->set("payload", std::move(payload));
        item->set("named_fields", std::move(named_fields));
        variants->push(std::move(item));
    }
    text << " }";

    auto object = base_signature_json("enum", text.str());
    object->set("type_params", type_params_json(decl.type_params));
    object->set("where", where_json(decl.where_clause));
    object->set("variants", std::move(variants));
    return {object->get("text")->string_val, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
function_signature(const ahfl::ast::FnDecl &decl) {
    const auto params = param_texts(decl.params);
    std::string text = "fn " + decl.name + type_param_suffix(decl.type_params) + "(" +
                       join_strings(params, ", ") + ")";
    text.append(" -> ");
    text.append(type_text(decl.return_type));
    text.append(effect_text(decl.effect_clause));
    text.append(where_suffix(decl.where_clause));

    auto object = base_signature_json("function", text);
    object->set("type_params", type_params_json(decl.type_params));
    object->set("params", params_json(decl.params));
    object->set("return_type", Json::make_string(type_text(decl.return_type)));
    object->set("effect", effect_json(decl.effect_clause));
    object->set("where", where_json(decl.where_clause));
    object->set("builtin",
                decl.builtin_name ? Json::make_string(*decl.builtin_name) : Json::make_null());
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
capability_signature(const ahfl::ast::CapabilityDecl &decl) {
    const auto params = param_texts(decl.params);
    std::string text = "capability " + decl.name + "(" + join_strings(params, ", ") + ")";
    text.append(" -> ");
    text.append(type_text(decl.return_type));
    const auto effect = capability_effect_text(decl.effect);
    if (!effect.empty()) {
        text.append(" effect ");
        text.append(effect);
    }
    text.append(where_suffix(decl.where_clause));

    auto object = base_signature_json("capability", text);
    object->set("params", params_json(decl.params));
    object->set("return_type", Json::make_string(type_text(decl.return_type)));
    object->set("effect_text", Json::make_string(capability_effect_text(decl.effect)));
    object->set("effect", capability_effect_json(decl.effect));
    object->set("where", where_json(decl.where_clause));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
predicate_signature(const ahfl::ast::PredicateDecl &decl) {
    const auto params = param_texts(decl.params);
    std::string text = "predicate " + decl.name + "(" + join_strings(params, ", ") + ")";

    auto object = base_signature_json("predicate", text);
    object->set("params", params_json(decl.params));
    object->set("effect", effect_json(decl.effect_clause));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
type_alias_signature(const ahfl::ast::TypeAliasDecl &decl) {
    std::string text = "type " + decl.name + type_param_suffix(decl.type_params) + " = " +
                       type_text(decl.aliased_type);

    auto object = base_signature_json("type_alias", text);
    object->set("type_params", type_params_json(decl.type_params));
    object->set("aliased_type", Json::make_string(type_text(decl.aliased_type)));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
const_signature(const ahfl::ast::ConstDecl &decl) {
    std::string text = "const " + decl.name + ": " + type_text(decl.type);
    auto object = base_signature_json("const", text);
    object->set("type", Json::make_string(type_text(decl.type)));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
agent_signature(const ahfl::ast::AgentDecl &decl) {
    std::string text = "agent " + decl.name;
    auto object = base_signature_json("agent", text);
    object->set("input_type", Json::make_string(type_text(decl.input_type)));
    object->set("context_type", Json::make_string(type_text(decl.context_type)));
    object->set("output_type", Json::make_string(type_text(decl.output_type)));
    object->set("state_count", Json::make_int(static_cast<std::int64_t>(decl.states.size())));
    object->set("transition_count",
                Json::make_int(static_cast<std::int64_t>(decl.transitions.size())));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
workflow_signature(const ahfl::ast::WorkflowDecl &decl) {
    std::string text = "workflow " + decl.name;
    auto object = base_signature_json("workflow", text);
    object->set("input_type", Json::make_string(type_text(decl.input_type)));
    object->set("output_type", Json::make_string(type_text(decl.output_type)));
    object->set("node_count", Json::make_int(static_cast<std::int64_t>(decl.nodes.size())));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
trait_signature(const ahfl::ast::TraitDecl &decl) {
    std::vector<std::string> super_traits;
    super_traits.reserve(decl.super_traits.size());
    for (const auto &super_trait : decl.super_traits) {
        super_traits.push_back(type_text(super_trait));
    }
    std::string text = "trait " + decl.name + type_param_suffix(decl.type_params);
    if (!super_traits.empty()) {
        text.append(": ");
        text.append(join_strings(super_traits, " + "));
    }
    text.append(where_suffix(decl.where_clause));

    auto items = Json::make_array();
    for (const auto &item : decl.items) {
        if (!item) {
            continue;
        }
        auto json_item = Json::make_object();
        switch (item->kind) {
        case ahfl::ast::TraitItemKind::Fn:
            json_item->set("kind", Json::make_string("fn"));
            json_item->set("name", Json::make_string(item->name));
            json_item->set("params", params_json(item->params));
            json_item->set("return_type", Json::make_string(type_text(item->return_type)));
            break;
        case ahfl::ast::TraitItemKind::AssocType:
            json_item->set("kind", Json::make_string("assoc_type"));
            json_item->set("name",
                           Json::make_string(item->assoc_type ? item->assoc_type->name : ""));
            break;
        case ahfl::ast::TraitItemKind::AssocConst:
            json_item->set("kind", Json::make_string("assoc_const"));
            json_item->set("name",
                           Json::make_string(item->assoc_const ? item->assoc_const->name : ""));
            break;
        }
        items->push(std::move(json_item));
    }

    auto object = base_signature_json("trait", text);
    object->set("type_params", type_params_json(decl.type_params));
    object->set("super_traits", string_array(super_traits));
    object->set("where", where_json(decl.where_clause));
    object->set("items", std::move(items));
    return {text, std::move(object)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
fallback_signature(const ahfl::Symbol &symbol, const ahfl::ast::Decl *decl) {
    std::string text = symbol_kind_name(symbol.kind) + " " + symbol.local_name;
    if (decl != nullptr) {
        text = decl->headline();
    }
    return {text, base_signature_json(symbol_kind_name(symbol.kind), text)};
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
signature_for_symbol(const ahfl::Symbol &symbol, const ahfl::ast::Decl *decl) {
    if (decl == nullptr) {
        return fallback_signature(symbol, decl);
    }

    switch (decl->kind) {
    case ahfl::ast::NodeKind::ConstDecl:
        return const_signature(static_cast<const ahfl::ast::ConstDecl &>(*decl));
    case ahfl::ast::NodeKind::TypeAliasDecl:
        return type_alias_signature(static_cast<const ahfl::ast::TypeAliasDecl &>(*decl));
    case ahfl::ast::NodeKind::StructDecl:
        return struct_signature(static_cast<const ahfl::ast::StructDecl &>(*decl));
    case ahfl::ast::NodeKind::EnumDecl:
        return enum_signature(static_cast<const ahfl::ast::EnumDecl &>(*decl));
    case ahfl::ast::NodeKind::CapabilityDecl:
        return capability_signature(static_cast<const ahfl::ast::CapabilityDecl &>(*decl));
    case ahfl::ast::NodeKind::PredicateDecl:
        return predicate_signature(static_cast<const ahfl::ast::PredicateDecl &>(*decl));
    case ahfl::ast::NodeKind::AgentDecl:
        return agent_signature(static_cast<const ahfl::ast::AgentDecl &>(*decl));
    case ahfl::ast::NodeKind::WorkflowDecl:
        return workflow_signature(static_cast<const ahfl::ast::WorkflowDecl &>(*decl));
    case ahfl::ast::NodeKind::FnDecl:
        return function_signature(static_cast<const ahfl::ast::FnDecl &>(*decl));
    case ahfl::ast::NodeKind::TraitDecl:
        return trait_signature(static_cast<const ahfl::ast::TraitDecl &>(*decl));
    default:
        return fallback_signature(symbol, decl);
    }
}

[[nodiscard]] std::pair<std::string, std::unique_ptr<Json>>
signature_for_alias(const ahfl::PublicAlias &alias,
                    const ahfl::ast::UseDecl *decl,
                    const std::string &target_name) {
    std::string path = target_name;
    if (decl != nullptr && decl->path) {
        path = decl->path->spelling();
    }

    std::string text = "pub use " + path;
    if (!alias.local_name.empty() && alias.local_name != path) {
        text.append(" as ");
        text.append(alias.local_name);
    }

    auto object = base_signature_json("alias", text);
    object->set("target", Json::make_string(target_name));
    return {text, std::move(object)};
}

[[nodiscard]] std::string stable_package_path(const std::filesystem::path &package_root,
                                              const std::filesystem::path &path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    auto relative = std::filesystem::relative(path, package_root, error);
    if (!error && !relative.empty() && *relative.begin() != "..") {
        return relative.lexically_normal().generic_string();
    }

    relative = path.lexically_relative(package_root);
    if (!relative.empty() && *relative.begin() != "..") {
        return relative.lexically_normal().generic_string();
    }

    return path.lexically_normal().generic_string();
}

void fill_source_location(const ahfl::SourceGraph &graph,
                          const PublicApiPackageContext &package,
                          std::optional<ahfl::SourceId> source_id,
                          ahfl::SourceRange range,
                          ApiEntry &entry) {
    if (!source_id.has_value()) {
        return;
    }
    const auto *source = source_unit_for_id(graph, *source_id);
    if (source == nullptr) {
        return;
    }
    entry.source_path = stable_package_path(package.package_root, source->path);
    entry.begin = source->source.locate(range.begin_offset);
    entry.end = source->source.locate(range.end_offset);
}

[[nodiscard]] ApiEntry api_entry_for_symbol(const ahfl::SourceGraph &graph,
                                            const ahfl::ResolveResult &resolve_result,
                                            const PublicApiPackageContext &package,
                                            const ahfl::Symbol &symbol) {
    ApiEntry entry;
    entry.api_id = "symbol:" + namespace_name(symbol.name_space) + ":" + symbol.canonical_name;
    entry.entry_kind = "symbol";
    entry.symbol_kind = symbol_kind_name(symbol.kind);
    entry.name_space = namespace_name(symbol.name_space);
    entry.local_name = symbol.local_name;
    entry.canonical_name = symbol.canonical_name;
    entry.module_name = symbol.module_name;
    entry.range = symbol.declaration_range;
    entry.symbol_id = symbol.id.value;
    fill_source_location(graph, package, symbol.source_id, symbol.declaration_range, entry);

    const auto *decl = find_symbol_declaration(graph, symbol);
    auto [text, signature] = signature_for_symbol(symbol, decl);
    entry.signature_text = std::move(text);
    entry.signature = std::move(signature);
    (void)resolve_result;
    return entry;
}

[[nodiscard]] ApiEntry api_entry_for_alias(const ahfl::SourceGraph &graph,
                                           const ahfl::ResolveResult &resolve_result,
                                           const PublicApiPackageContext &package,
                                           const ahfl::PublicAlias &alias) {
    ApiEntry entry;
    entry.api_id = "alias:" + namespace_name(alias.name_space) + ":" + alias.canonical_name;
    entry.entry_kind = "alias";
    entry.symbol_kind = "alias";
    entry.name_space = namespace_name(alias.name_space);
    entry.local_name = alias.local_name;
    entry.canonical_name = alias.canonical_name;
    entry.module_name = alias.module_name;
    entry.range = alias.declaration_range;
    entry.alias_id = alias.id.value;
    entry.target_symbol_id = alias.target.value;
    fill_source_location(graph, package, alias.source_id, alias.declaration_range, entry);

    if (const auto target = resolve_result.symbol_table.get(alias.target); target.has_value()) {
        entry.target_canonical_name = target->get().canonical_name;
    }

    const auto *decl = find_alias_declaration(graph, alias);
    auto [text, signature] = signature_for_alias(alias, decl, entry.target_canonical_name);
    entry.signature_text = std::move(text);
    entry.signature = std::move(signature);
    return entry;
}

[[nodiscard]] std::vector<ApiEntry>
collect_public_api_entries(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const PublicApiPackageContext &package) {
    const auto is_package_module = [&](std::string_view module_name) {
        return module_name == package.module_prefix ||
               module_name.starts_with(package.module_prefix + "::");
    };

    std::vector<ApiEntry> entries;
    for (const auto &symbol : resolve_result.symbol_table.symbols()) {
        if (symbol.visibility != ahfl::ast::Visibility::Public ||
            !is_package_module(symbol.module_name) || !resolve_result.is_api_reachable(symbol.id)) {
            continue;
        }
        entries.push_back(api_entry_for_symbol(graph, resolve_result, package, symbol));
    }
    for (const auto &alias : resolve_result.public_aliases()) {
        if (alias.visibility != ahfl::ast::Visibility::Public ||
            !is_package_module(alias.module_name) || !resolve_result.is_api_reachable(alias.id)) {
            continue;
        }
        entries.push_back(api_entry_for_alias(graph, resolve_result, package, alias));
    }

    std::sort(entries.begin(), entries.end(), [](const ApiEntry &lhs, const ApiEntry &rhs) {
        return std::tie(lhs.api_id, lhs.signature_text) < std::tie(rhs.api_id, rhs.signature_text);
    });
    return entries;
}

[[nodiscard]] std::unique_ptr<Json> range_json(const ahfl::SourceRange &range,
                                               const std::optional<ahfl::SourcePosition> &begin,
                                               const std::optional<ahfl::SourcePosition> &end) {
    auto object = Json::make_object();
    object->set("begin_offset", Json::make_int(static_cast<std::int64_t>(range.begin_offset)));
    object->set("end_offset", Json::make_int(static_cast<std::int64_t>(range.end_offset)));
    if (begin.has_value()) {
        object->set("begin_line", Json::make_int(static_cast<std::int64_t>(begin->line)));
        object->set("begin_column", Json::make_int(static_cast<std::int64_t>(begin->column)));
    }
    if (end.has_value()) {
        object->set("end_line", Json::make_int(static_cast<std::int64_t>(end->line)));
        object->set("end_column", Json::make_int(static_cast<std::int64_t>(end->column)));
    }
    return object;
}

[[nodiscard]] std::unique_ptr<Json> entry_json(ApiEntry &&entry) {
    auto object = Json::make_object();
    object->set("api_id", Json::make_string(std::move(entry.api_id)));
    object->set("entry_kind", Json::make_string(std::move(entry.entry_kind)));
    object->set("symbol_kind", Json::make_string(std::move(entry.symbol_kind)));
    object->set("namespace", Json::make_string(std::move(entry.name_space)));
    object->set("local_name", Json::make_string(std::move(entry.local_name)));
    object->set("canonical_name", Json::make_string(std::move(entry.canonical_name)));
    object->set("module", Json::make_string(std::move(entry.module_name)));
    object->set("source", Json::make_string(std::move(entry.source_path)));
    object->set("range", range_json(entry.range, entry.begin, entry.end));
    object->set("symbol_id",
                entry.symbol_id.has_value()
                    ? Json::make_int(static_cast<std::int64_t>(*entry.symbol_id))
                    : Json::make_null());
    object->set("alias_id",
                entry.alias_id.has_value()
                    ? Json::make_int(static_cast<std::int64_t>(*entry.alias_id))
                    : Json::make_null());
    object->set("target_symbol_id",
                entry.target_symbol_id.has_value()
                    ? Json::make_int(static_cast<std::int64_t>(*entry.target_symbol_id))
                    : Json::make_null());
    object->set("target_canonical_name", Json::make_string(std::move(entry.target_canonical_name)));
    object->set("signature_text", Json::make_string(std::move(entry.signature_text)));
    object->set("signature", std::move(entry.signature));
    return object;
}

[[nodiscard]] std::unique_ptr<Json> snapshot_json(std::vector<ApiEntry> entries,
                                                  const PublicApiPackageContext &package) {
    auto root = Json::make_object();
    root->set("schema", Json::make_string(std::string{kPublicApiSchema}));

    auto package_json = Json::make_object();
    package_json->set("name", Json::make_string(package.name));
    package_json->set("version", Json::make_string(package.version));
    package_json->set("module_prefix", Json::make_string(package.module_prefix));
    package_json->set(
        "manifest",
        Json::make_string(stable_package_path(package.package_root, package.manifest_path)));
    root->set("package", std::move(package_json));

    auto entries_json = Json::make_array();
    for (auto &entry : entries) {
        entries_json->push(entry_json(std::move(entry)));
    }
    root->set("entries", std::move(entries_json));
    return root;
}

[[nodiscard]] std::string markdown_heading_for_kind(std::string_view kind) {
    if (kind == "alias") {
        return "Aliases";
    }
    if (kind == "struct" || kind == "enum" || kind == "type_alias") {
        return "Types";
    }
    if (kind == "function") {
        return "Functions";
    }
    if (kind == "capability") {
        return "Capabilities";
    }
    if (kind == "predicate") {
        return "Predicates";
    }
    if (kind == "agent") {
        return "Agents";
    }
    if (kind == "workflow") {
        return "Workflows";
    }
    if (kind == "trait") {
        return "Traits";
    }
    if (kind == "const") {
        return "Constants";
    }
    return "Other";
}

void render_public_api_docs(const std::vector<ApiEntry> &entries,
                            const PublicApiPackageContext &package,
                            std::ostream &out) {
    out << "# Public API: " << package.name << "\n\n";
    out << "- Schema: `" << kPublicApiSchema << "`\n";
    out << "- Version: `" << package.version << "`\n";
    out << "- Module prefix: `" << package.module_prefix << "`\n";
    out << "- Entries: " << entries.size() << "\n\n";

    std::string current_heading;
    for (const auto &entry : entries) {
        const auto heading = markdown_heading_for_kind(entry.symbol_kind);
        if (heading != current_heading) {
            current_heading = heading;
            out << "## " << current_heading << "\n\n";
        }
        out << "### `" << entry.canonical_name << "`\n\n";
        out << "- Kind: `" << entry.symbol_kind << "`\n";
        out << "- Namespace: `" << entry.name_space << "`\n";
        if (!entry.target_canonical_name.empty()) {
            out << "- Target: `" << entry.target_canonical_name << "`\n";
        }
        if (!entry.source_path.empty()) {
            out << "- Source: `" << entry.source_path << "`";
            if (entry.begin.has_value()) {
                out << ":" << entry.begin->line << ":" << entry.begin->column;
            }
            out << "\n";
        }
        out << "\n```ahfl\n" << entry.signature_text << "\n```\n\n";
    }
}

[[nodiscard]] bool
read_file(const std::filesystem::path &path, std::string &content, std::ostream &err) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        err << "error: failed to open " << path << "\n";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

struct DiffEntry {
    std::string api_id;
    std::string canonical_name;
    std::string symbol_kind;
    std::string signature_text;
};

struct SemVer {
    int major{0};
    int minor{0};
    int patch{0};
};

enum class ApiDiffSeverity {
    Internal,
    Additive,
    Breaking,
};

enum class VersionBump {
    None,
    Patch,
    Minor,
    Major,
};

[[nodiscard]] std::optional<std::string> string_field(const Json &object, std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_string();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::string{*value};
}

[[nodiscard]] std::optional<std::unordered_map<std::string, DiffEntry>>
read_snapshot_entries_from_content(std::string_view content,
                                   std::string_view label,
                                   std::ostream &err) {
    auto parsed = ahfl::json::parse_json(content);
    if (!parsed.has_value() || !*parsed || !(**parsed).is_object()) {
        err << "error: public API snapshot is not valid JSON: " << label << "\n";
        return std::nullopt;
    }

    const auto &root = **parsed;
    const auto schema = string_field(root, "schema");
    if (!schema.has_value() || *schema != kPublicApiSchema) {
        err << "error: public API snapshot has unsupported schema: " << label << "\n";
        return std::nullopt;
    }

    const auto *entries_value = root.get("entries");
    if (entries_value == nullptr || !entries_value->is_array()) {
        err << "error: public API snapshot is missing entries array: " << label << "\n";
        return std::nullopt;
    }

    std::unordered_map<std::string, DiffEntry> entries;
    for (const auto &item : entries_value->array_items) {
        if (!item || !item->is_object()) {
            err << "error: public API snapshot entry is not an object: " << label << "\n";
            return std::nullopt;
        }
        auto api_id = string_field(*item, "api_id");
        auto canonical_name = string_field(*item, "canonical_name");
        auto symbol_kind = string_field(*item, "symbol_kind");
        auto signature_text = string_field(*item, "signature_text");
        if (!api_id.has_value() || !canonical_name.has_value() || !symbol_kind.has_value() ||
            !signature_text.has_value()) {
            err << "error: public API snapshot entry is missing required fields: " << label << "\n";
            return std::nullopt;
        }
        const auto key = *api_id;
        auto entry_id = key;
        entries.emplace(key,
                        DiffEntry{
                            .api_id = std::move(entry_id),
                            .canonical_name = std::move(*canonical_name),
                            .symbol_kind = std::move(*symbol_kind),
                            .signature_text = std::move(*signature_text),
                        });
    }
    return entries;
}

[[nodiscard]] std::optional<std::unordered_map<std::string, DiffEntry>>
read_snapshot_entries(const std::filesystem::path &path, std::ostream &err) {
    std::string content;
    if (!read_file(path, content, err)) {
        return std::nullopt;
    }
    return read_snapshot_entries_from_content(content, path.generic_string(), err);
}

[[nodiscard]] std::vector<DiffEntry> sorted_diff_entries(std::vector<DiffEntry> entries) {
    std::sort(entries.begin(), entries.end(), [](const DiffEntry &lhs, const DiffEntry &rhs) {
        return std::tie(lhs.api_id, lhs.signature_text) < std::tie(rhs.api_id, rhs.signature_text);
    });
    return entries;
}

[[nodiscard]] std::optional<int> parse_int_component(std::string_view text) {
    int value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<SemVer> parse_semver(std::string_view text) {
    const auto first_dot = text.find('.');
    if (first_dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto second_dot = text.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos ||
        text.find('.', second_dot + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const auto major = parse_int_component(text.substr(0, first_dot));
    const auto minor = parse_int_component(text.substr(first_dot + 1, second_dot - first_dot - 1));
    const auto patch = parse_int_component(text.substr(second_dot + 1));
    if (!major.has_value() || !minor.has_value() || !patch.has_value()) {
        return std::nullopt;
    }
    return SemVer{.major = *major, .minor = *minor, .patch = *patch};
}

[[nodiscard]] VersionBump classify_version_bump(SemVer from, SemVer to) {
    if (to.major > from.major) {
        return VersionBump::Major;
    }
    if (to.major == from.major && to.minor > from.minor) {
        return VersionBump::Minor;
    }
    if (to.major == from.major && to.minor == from.minor && to.patch > from.patch) {
        return VersionBump::Patch;
    }
    return VersionBump::None;
}

[[nodiscard]] std::string_view severity_name(ApiDiffSeverity severity) noexcept {
    switch (severity) {
    case ApiDiffSeverity::Internal:
        return "internal";
    case ApiDiffSeverity::Additive:
        return "additive";
    case ApiDiffSeverity::Breaking:
        return "breaking";
    }
    return "internal";
}

[[nodiscard]] std::string_view bump_name(VersionBump bump) noexcept {
    switch (bump) {
    case VersionBump::None:
        return "none";
    case VersionBump::Patch:
        return "patch";
    case VersionBump::Minor:
        return "minor";
    case VersionBump::Major:
        return "major";
    }
    return "none";
}

[[nodiscard]] ApiDiffSeverity diff_severity(const std::vector<DiffEntry> &added,
                                            const std::vector<DiffEntry> &removed,
                                            const std::vector<DiffEntry> &changed) {
    if (!removed.empty() || !changed.empty()) {
        return ApiDiffSeverity::Breaking;
    }
    if (!added.empty()) {
        return ApiDiffSeverity::Additive;
    }
    return ApiDiffSeverity::Internal;
}

[[nodiscard]] VersionBump
required_bump_for_severity(ApiDiffSeverity severity, SemVer from, SemVer to) {
    switch (severity) {
    case ApiDiffSeverity::Breaking:
        return from.major == 0 && to.major == 0 ? VersionBump::Minor : VersionBump::Major;
    case ApiDiffSeverity::Additive:
        return VersionBump::Minor;
    case ApiDiffSeverity::Internal:
        return VersionBump::Patch;
    }
    return VersionBump::Patch;
}

[[nodiscard]] bool bump_satisfies(VersionBump actual, VersionBump required) {
    return static_cast<int>(actual) >= static_cast<int>(required);
}

[[nodiscard]] int evaluate_semver_gate(const PublicApiSemVerGate &gate,
                                       ApiDiffSeverity severity,
                                       std::ostream &out,
                                       std::ostream &err) {
    const auto from = parse_semver(gate.from_version);
    const auto to = parse_semver(gate.to_version);
    if (!from.has_value()) {
        err << "error: --from must be an exact SemVer version MAJOR.MINOR.PATCH\n";
        return 1;
    }
    if (!to.has_value()) {
        err << "error: --to must be an exact SemVer version MAJOR.MINOR.PATCH\n";
        return 1;
    }

    const auto actual = classify_version_bump(*from, *to);
    const auto required = required_bump_for_severity(severity, *from, *to);
    const auto pass = bump_satisfies(actual, required);
    out << "semver-gate: " << (pass ? "pass" : "fail") << '\n';
    out << "severity: " << severity_name(severity) << '\n';
    out << "required-bump: " << bump_name(required) << '\n';
    out << "actual-bump: " << bump_name(actual) << '\n';
    out << "from: " << gate.from_version << '\n';
    out << "to: " << gate.to_version << '\n';
    if (!pass) {
        err << "error: public API " << severity_name(severity) << " diff requires a "
            << bump_name(required) << " version bump; got " << bump_name(actual) << "\n";
        return 1;
    }
    return 0;
}

[[nodiscard]] int
emit_public_api_diff_from_entries(const std::unordered_map<std::string, DiffEntry> &old_entries,
                                  const std::unordered_map<std::string, DiffEntry> &new_entries,
                                  std::optional<PublicApiSemVerGate> semver_gate,
                                  std::ostream &out,
                                  std::ostream &err) {
    std::vector<DiffEntry> added;
    std::vector<DiffEntry> removed;
    std::vector<DiffEntry> changed;

    for (const auto &[api_id, old_entry] : old_entries) {
        const auto found = new_entries.find(api_id);
        if (found == new_entries.end()) {
            removed.push_back(old_entry);
            continue;
        }
        if (old_entry.signature_text != found->second.signature_text) {
            changed.push_back(found->second);
        }
    }
    for (const auto &[api_id, new_entry] : new_entries) {
        if (!old_entries.contains(api_id)) {
            added.push_back(new_entry);
        }
    }

    added = sorted_diff_entries(std::move(added));
    removed = sorted_diff_entries(std::move(removed));
    changed = sorted_diff_entries(std::move(changed));

    out << kPublicApiDiffSchema << '\n';
    out << "added: " << added.size() << '\n';
    for (const auto &entry : added) {
        out << "+ " << entry.symbol_kind << " " << entry.canonical_name << '\n';
    }
    out << "removed: " << removed.size() << '\n';
    for (const auto &entry : removed) {
        out << "- " << entry.symbol_kind << " " << entry.canonical_name << '\n';
    }
    out << "changed: " << changed.size() << '\n';
    for (const auto &entry : changed) {
        out << "~ " << entry.symbol_kind << " " << entry.canonical_name << '\n';
    }
    if (semver_gate.has_value()) {
        return evaluate_semver_gate(*semver_gate, diff_severity(added, removed, changed), out, err);
    }
    return 0;
}

} // namespace

int emit_public_api_snapshot(const ahfl::SourceGraph &graph,
                             const ahfl::ResolveResult &resolve_result,
                             [[maybe_unused]] const ahfl::TypeCheckResult &type_check_result,
                             const PublicApiPackageContext &package,
                             std::ostream &out,
                             [[maybe_unused]] std::ostream &err) {
    auto entries = collect_public_api_entries(graph, resolve_result, package);
    auto snapshot = snapshot_json(std::move(entries), package);
    out << ahfl::json::serialize_json(*snapshot) << '\n';
    return 0;
}

int emit_public_api_docs(const ahfl::SourceGraph &graph,
                         const ahfl::ResolveResult &resolve_result,
                         [[maybe_unused]] const ahfl::TypeCheckResult &type_check_result,
                         const PublicApiPackageContext &package,
                         std::ostream &out,
                         [[maybe_unused]] std::ostream &err) {
    auto entries = collect_public_api_entries(graph, resolve_result, package);
    render_public_api_docs(entries, package, out);
    return 0;
}

int emit_public_api_diff(const std::filesystem::path &old_snapshot,
                         const std::filesystem::path &new_snapshot,
                         std::optional<PublicApiSemVerGate> semver_gate,
                         std::ostream &out,
                         std::ostream &err) {
    const auto old_entries = read_snapshot_entries(old_snapshot, err);
    const auto new_entries = read_snapshot_entries(new_snapshot, err);
    if (!old_entries.has_value() || !new_entries.has_value()) {
        return 1;
    }

    return emit_public_api_diff_from_entries(
        *old_entries, *new_entries, std::move(semver_gate), out, err);
}

int emit_public_api_diff_from_snapshots(std::string_view old_snapshot,
                                        std::string_view old_snapshot_label,
                                        std::string_view new_snapshot,
                                        std::string_view new_snapshot_label,
                                        std::optional<PublicApiSemVerGate> semver_gate,
                                        std::ostream &out,
                                        std::ostream &err) {
    const auto old_entries =
        read_snapshot_entries_from_content(old_snapshot, old_snapshot_label, err);
    const auto new_entries =
        read_snapshot_entries_from_content(new_snapshot, new_snapshot_label, err);
    if (!old_entries.has_value() || !new_entries.has_value()) {
        return 1;
    }
    return emit_public_api_diff_from_entries(
        *old_entries, *new_entries, std::move(semver_gate), out, err);
}

} // namespace ahfl::cli
