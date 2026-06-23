#include "ahfl/compiler/semantics/expression_sema.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/name_suggestions.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "compiler/semantics/std_container_types.hpp"

#include <algorithm>
#include <string_view>
#include <variant>
#include <vector>

namespace ahfl {
namespace {

template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

[[nodiscard]] std::string_view local_nominal_name(std::string_view canonical_name) noexcept {
    const auto pos = canonical_name.rfind("::");
    if (pos == std::string_view::npos) {
        return canonical_name;
    }
    return canonical_name.substr(pos + 2);
}

[[nodiscard]] bool enum_fact_matches(const types::EnumT &enum_type, const TypeFact &fact) noexcept {
    return fact.enum_name == enum_type.canonical_name ||
           fact.enum_name == local_nominal_name(enum_type.canonical_name);
}

} // namespace

ExpressionValueFactory::ExpressionValueFactory(TypeContext &types) noexcept : types_(&types) {}

TypePtr ExpressionValueFactory::make_type(TypeKind kind) const {
    return types_->make(kind);
}

TypePtr ExpressionValueFactory::string_type() const {
    return types_->string();
}

TypePtr ExpressionValueFactory::decimal_type(std::int64_t scale) const {
    return types_->decimal(scale);
}

TypePtr ExpressionValueFactory::make_error_type() const {
    return types_->error_type();
}

TypePtr ExpressionValueFactory::clone_or_any(MaybeCRef<Type> type) const {
    return type.has_value() ? type->get().clone() : make_type(TypeKind::Any);
}

ExpressionValue ExpressionValueFactory::typed(TypePtr type, bool is_pure) const {
    return typed_effect(type, is_pure ? ExprEffect::Pure : ExprEffect::CapabilityCall);
}

ExpressionValue ExpressionValueFactory::typed_effect(TypePtr type, ExprEffect effect) const {
    return ExpressionValue{
        .type = type,
        .effect = effect,
        .is_pure = is_effect_pure(effect),
        .path_root_kind = std::nullopt,
    };
}

ExpressionValue ExpressionValueFactory::error_typed(bool is_pure) const {
    return error_typed_effect(is_pure ? ExprEffect::Pure : ExprEffect::Unknown);
}

ExpressionValue ExpressionValueFactory::error_typed_effect(ExprEffect effect) const {
    return ExpressionValue{
        .type = make_error_type(),
        .effect = effect,
        .is_pure = is_effect_pure(effect),
        .path_root_kind = std::nullopt,
    };
}

std::optional<Place> place_of_expression(const ast::ExprSyntax &expr) {
    return std::visit(overloaded{
                          [&](const ast::GroupExpr &e) -> std::optional<Place> {
                              if (!e.inner) {
                                  return std::nullopt;
                              }
                              return place_of_expression(*e.inner);
                          },
                          [&](const ast::MemberAccessExpr &e) -> std::optional<Place> {
                              if (!e.base) {
                                  return std::nullopt;
                              }
                              auto place = place_of_expression(*e.base);
                              if (!place.has_value()) {
                                  return std::nullopt;
                              }
                              place->members.push_back(e.member);
                              return place;
                          },
                          [&](const ast::PathExpr &e) -> std::optional<Place> {
                              if (!e.path) {
                                  return std::nullopt;
                              }
                              return Place{.root = e.path->root_name, .members = e.path->members};
                          },
                          [](const auto &) -> std::optional<Place> { return std::nullopt; },
                      },
                      expr.node);
}

AssignTargetRootKind classify_expression_path_root(const ast::PathSyntax &path,
                                                   const ExpressionContext &context) {
    switch (path.root_kind) {
    case ast::PathRootKind::Input:
        return AssignTargetRootKind::Input;
    case ast::PathRootKind::Output:
        return AssignTargetRootKind::Output;
    case ast::PathRootKind::Identifier:
        if (path.root_name == "ctx") {
            return AssignTargetRootKind::Context;
        }
        if (path.root_name == "state") {
            return AssignTargetRootKind::State;
        }
        if (context.call_context == ExpressionCallContext::Workflow) {
            return AssignTargetRootKind::Identifier;
        }
        if (context.call_context == ExpressionCallContext::Flow) {
            if (const auto iter = context.bindings.find(path.root_name);
                iter != context.bindings.end() && static_cast<bool>(iter->second)) {
                return AssignTargetRootKind::Local;
            }
        }
        return AssignTargetRootKind::Identifier;
    }
    return AssignTargetRootKind::Identifier;
}

TypePtr apply_expression_flow_narrowing(TypePtr type,
                                        const Place &place,
                                        const FlowFacts &facts,
                                        const TypeEnvironment &environment,
                                        TypeContext &types) {
    if (type == nullptr) {
        return type;
    }

    if (facts.has_fact(place, TypeFactKind::IsNotNone)) {
        const auto optional = stdlib_bridge::std_container_type_view(*type);
        if (optional.has_value() && optional->kind == stdlib_bridge::StdContainerKind::Option &&
            optional->first != nullptr) {
            type = optional->first->clone();
        }
    }

    const auto *enum_type = type->get_if<types::EnumT>();
    if (enum_type == nullptr) {
        return type;
    }

    const TypeFact *positive_variant = nullptr;
    std::vector<std::string> excluded_variants;
    facts.for_each([&](const TypeFact &fact) {
        if (!(fact.place == place) || !enum_fact_matches(*enum_type, fact)) {
            return;
        }
        if (fact.kind == TypeFactKind::IsVariant) {
            positive_variant = &fact;
            return;
        }
        if (fact.kind == TypeFactKind::IsNotVariant) {
            excluded_variants.push_back(fact.variant_name);
        }
    });

    const auto enum_info = environment.get_enum(*type);
    const auto variant_exists = [&](std::string_view variant_name) {
        return !enum_info.has_value() || enum_info->get().has_variant(variant_name);
    };

    if (positive_variant != nullptr && variant_exists(positive_variant->variant_name)) {
        return types.enum_variant_type(
            enum_type->canonical_name, positive_variant->variant_name, enum_type->symbol);
    }

    if (!enum_info.has_value() || excluded_variants.empty()) {
        return type;
    }

    const EnumVariantInfo *remaining = nullptr;
    for (const auto &variant : enum_info->get().variants) {
        if (std::find(excluded_variants.begin(), excluded_variants.end(), variant.name) !=
            excluded_variants.end()) {
            continue;
        }
        if (remaining != nullptr) {
            return type;
        }
        remaining = &variant;
    }

    if (remaining != nullptr) {
        return types.enum_variant_type(
            enum_type->canonical_name, remaining->name, enum_type->symbol);
    }

    return type;
}

TypePtr resolve_expression_field_access(const Type &base_type,
                                        std::string_view field_name,
                                        SourceRange range,
                                        const TypeEnvironment &environment,
                                        TypeContext &types,
                                        ExpressionSemaDelegate &delegate) {
    if (base_type.holds<types::ErrorT>()) {
        return types.error_type();
    }

    if (!base_type.holds<types::StructT>()) {
        delegate.typecheck_error(
            error_codes::typecheck::InvalidMemberAccess,
            messages::typecheck::InvalidMemberAccess.format_with(base_type.describe()),
            range);
        return types.error_type();
    }

    const auto struct_info = environment.get_struct(base_type);
    if (!struct_info.has_value()) {
        delegate.typecheck_error(
            error_codes::typecheck::MissingTypeMetadata,
            messages::typecheck::StructTypeInfoMissing.format_with(base_type.describe()),
            range);
        return types.error_type();
    }

    const auto field = struct_info->get().find_field(field_name);
    if (!field.has_value()) {
        std::vector<std::string> candidates;
        candidates.reserve(struct_info->get().fields.size());
        for (const auto &candidate : struct_info->get().fields) {
            candidates.push_back(candidate.name);
        }
        auto message =
            messages::typecheck::UnknownField.format_with(field_name, base_type.describe());
        if (const auto suggestion = suggest_name(field_name, candidates); suggestion.has_value()) {
            message += "; did you mean '" + *suggestion + "'?";
        }
        delegate.typecheck_error(error_codes::typecheck::UnknownField, std::move(message), range);
        return types.error_type();
    }

    return field->get().type ? field->get().type->clone() : types.error_type();
}

ExpressionValue resolve_expression_path(const ast::PathSyntax &path,
                                        const ExpressionContext &context,
                                        const TypeEnvironment &environment,
                                        TypeContext &types,
                                        ExpressionSemaDelegate &delegate) {
    const auto root = context.bindings.find(path.root_name);
    if (root == context.bindings.end() || !root->second) {
        delegate.typecheck_error(error_codes::typecheck::UnknownValue,
                                 messages::typecheck::UnknownValue.format_with(path.root_name),
                                 path.range);
        return ExpressionValue{
            .type = types.error_type(),
            .effect = ExprEffect::Pure,
            .is_pure = true,
            .path_root_kind = std::nullopt,
        };
    }

    auto current = root->second->clone();
    std::vector<std::string> traversed_members;
    for (const auto &member : path.members) {
        current = resolve_expression_field_access(
            *current, member, path.range, environment, types, delegate);
        traversed_members.push_back(member);
    }

    current = apply_expression_flow_narrowing(
        current,
        Place{.root = path.root_name, .members = std::move(traversed_members)},
        context.flow_facts,
        environment,
        types);

    return ExpressionValue{
        .type = current,
        .effect = ExprEffect::Pure,
        .is_pure = true,
        .path_root_kind = classify_expression_path_root(path, context),
    };
}

} // namespace ahfl
