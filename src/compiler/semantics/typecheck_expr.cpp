#include "compiler/semantics/typecheck_internal.hpp"

#include "ahfl/compiler/semantics/name_suggestions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

using internal::CallContext;
using internal::is_bool_type;
using internal::is_error_type;
using internal::is_numeric_type;
using internal::TypedValue;
using internal::ValueContext;

namespace {

[[nodiscard]] std::int64_t parse_decimal_scale(std::string_view text) {
    const auto dot_index = text.find('.');
    if (dot_index == std::string_view::npos) {
        return 0;
    }

    std::int64_t scale = 0;
    for (std::size_t index = dot_index + 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            break;
        }

        ++scale;
    }

    return scale;
}

[[nodiscard]] std::optional<TypeExpectation> derive_expectation(const TypeExpectation *parent,
                                                                TypePtr expected) {
    if (parent == nullptr || expected == nullptr) {
        return std::nullopt;
    }
    return TypeExpectation{
        .expected = expected,
        .origin_kind = parent->origin_kind,
        .origin_range = parent->origin_range,
        .description = parent->description,
    };
}

[[nodiscard]] TypeExpectation inferred_collection_expectation(const Type &expected,
                                                              SourceRange origin_range,
                                                              std::string description) {
    return TypeExpectation{
        .expected = expected.clone(),
        .origin_kind = TypeExpectationOriginKind::Annotation,
        .origin_range = origin_range,
        .description = std::move(description),
    };
}

} // namespace

class ExpressionCheckerServices final {
  public:
    ExpressionCheckerServices(const ResolveResult &resolve_result,
                              std::optional<SourceId> current_source_id,
                              const TypeEnvironment &environment,
                              TypeContext &types,
                              TypeRelationContext &relations,
                              ExpressionTypecheckErrorSink diagnose,
                              ExpressionTypecheckDiagnosticSink diagnose_with_notes,
                              ExpressionNestedChecker check_nested,
                              ExpressionExpectedNestedChecker check_nested_expected,
                              ExpressionTypeSymbolResolver resolve_type_symbol)
        : resolve_result_(resolve_result), current_source_id_(current_source_id),
          environment_(environment), types_(types), relations_(relations),
          diagnose_(std::move(diagnose)), diagnose_with_notes_(std::move(diagnose_with_notes)),
          check_nested_(std::move(check_nested)),
          check_nested_expected_(std::move(check_nested_expected)),
          resolve_type_symbol_(std::move(resolve_type_symbol)), values_(types_) {}

    [[nodiscard]] const ExpressionValueFactory &values() const noexcept {
        return values_;
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range) const {
        diagnose_(code, std::move(message), range);
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range,
                              std::vector<Diagnostic::Related> notes) const {
        diagnose_with_notes_(code, std::move(message), range, std::move(notes));
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type) const {
        return check_nested_(expr, context, expected_type);
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        const TypeExpectation &expectation) const {
        return check_nested_expected_(expr, context, expectation);
    }

    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label) const {
        if (is_error_type(source) || is_error_type(target)) {
            return true;
        }

        if (ahfl::is_assignable_to(source, target, relations_)) {
            return true;
        }

        std::vector<Diagnostic::Related> notes;
        notes.push_back(Diagnostic::Related{
            .message = actual_type_note(source),
            .range = range,
        });
        typecheck_error_here(error_codes::typecheck::TypeMismatch,
                             messages::typecheck::TypeMismatch.format_with(
                                 context_label, target.describe(), source.describe()),
                             range,
                             std::move(notes));
        return false;
    }

    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label,
                                        const TypeExpectation &expectation) const {
        if (is_error_type(source) || is_error_type(target)) {
            return true;
        }

        if (ahfl::is_assignable_to(source, target, relations_)) {
            return true;
        }

        std::vector<Diagnostic::Related> notes;
        notes.push_back(Diagnostic::Related{
            .message = expected_type_note(target, expectation),
            .range = expectation.origin_range,
        });
        notes.push_back(Diagnostic::Related{
            .message = actual_type_note(source),
            .range = range,
        });
        typecheck_error_here(error_codes::typecheck::TypeMismatch,
                             messages::typecheck::TypeMismatch.format_with(
                                 context_label, target.describe(), source.describe()),
                             range,
                             std::move(notes));
        return false;
    }

    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference(ReferenceKind kind,
                                                              SourceRange range) const {
        return resolve_result_.find_reference(kind, range, current_source_id_);
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const {
        return resolve_result_.symbol_table.get(id);
    }

    [[nodiscard]] MaybeCRef<Type> get_const_type(SymbolId id) const {
        return environment_.get_const_type(id);
    }

    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range) const {
        return resolve_type_symbol_(id, use_range);
    }

    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(const Type &type) const {
        return environment_.get_enum(type);
    }

    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(const Type &type) const {
        return environment_.get_struct(type);
    }

    [[nodiscard]] MaybeCRef<CapabilityTypeInfo> get_capability(SymbolId id) const {
        return environment_.get_capability(id);
    }

    [[nodiscard]] MaybeCRef<AgentTypeInfo> get_agent(SymbolId id) const {
        return environment_.get_agent(id);
    }

    [[nodiscard]] MaybeCRef<PredicateTypeInfo> get_predicate(SymbolId id) const {
        return environment_.get_predicate(id);
    }

    [[nodiscard]] TypePtr
    field_access(const Type &base_type, std::string_view field_name, SourceRange range) const {
        return resolve_expression_field_access(
            base_type, field_name, range, environment_, types_, diagnose_);
    }

    [[nodiscard]] TypedValue resolve_path(const ast::PathSyntax &path,
                                          const ValueContext &context) const {
        return resolve_expression_path(path, context, environment_, types_, diagnose_);
    }

    [[nodiscard]] TypePtr
    apply_flow_narrowing(TypePtr type, const Place &place, const FlowFacts &facts) const {
        return apply_expression_flow_narrowing(std::move(type), place, facts, environment_, types_);
    }

  private:
    const ResolveResult &resolve_result_;
    std::optional<SourceId> current_source_id_;
    const TypeEnvironment &environment_;
    TypeContext &types_;
    TypeRelationContext &relations_;
    ExpressionTypecheckErrorSink diagnose_;
    ExpressionTypecheckDiagnosticSink diagnose_with_notes_;
    ExpressionNestedChecker check_nested_;
    ExpressionExpectedNestedChecker check_nested_expected_;
    ExpressionTypeSymbolResolver resolve_type_symbol_;
    ExpressionValueFactory values_;
};

class ExpressionChecker final {
  public:
    ExpressionChecker(ExpressionCheckerServices &services,
                      const ValueContext &context,
                      MaybeCRef<Type> expected_type,
                      const TypeExpectation *expectation)
        : services_(services), context_(context), expected_type_(expected_type),
          expectation_(expectation), values_(services.values()) {}

    [[nodiscard]] TypedValue check(const ast::ExprSyntax &expr) const {
        return internal::visit_expr_syntax(expr, *this);
    }

    [[nodiscard]] TypedValue visit_bool_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Bool));
    }

    [[nodiscard]] TypedValue visit_integer_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Int));
    }

    [[nodiscard]] TypedValue visit_float_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Float));
    }

    [[nodiscard]] TypedValue visit_decimal_literal(const ast::ExprSyntax &expr) const {
        return values_.typed(values_.decimal_type(parse_decimal_scale(expr.text)));
    }

    [[nodiscard]] TypedValue visit_string_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.string_type());
    }

    [[nodiscard]] TypedValue visit_duration_literal(const ast::ExprSyntax &) const {
        return values_.typed(values_.make_type(TypeKind::Duration));
    }

    [[nodiscard]] TypedValue visit_none_literal(const ast::ExprSyntax &expr) const {
        if (expected_type_.has_value() && expected_type_->get().holds<types::OptionalT>()) {
            return values_.typed(expected_type_->get().clone());
        }
        services_.typecheck_error_here(error_codes::typecheck::NoneWithoutContext,
                                       messages::typecheck::NoneWithoutContext.format_with(),
                                       expr.range);
        return values_.error_typed();
    }

    [[nodiscard]] TypedValue visit_some(const ast::ExprSyntax &expr) const {
        MaybeCRef<Type> inner_expected = std::nullopt;
        std::optional<TypeExpectation> inner_expectation;
        if (expected_type_.has_value()) {
            if (const auto *optional = expected_type_->get().get_if<types::OptionalT>();
                optional != nullptr && optional->inner != nullptr) {
                inner_expected = std::cref(*optional->inner);
                inner_expectation = derive_expectation(expectation_, optional->inner);
            }
        }
        const auto inner = inner_expectation.has_value()
                               ? services_.check_expr(*expr.first, context_, *inner_expectation)
                               : services_.check_expr(*expr.first, context_, inner_expected);
        if (inner_expectation.has_value() && inner_expectation->expected != nullptr) {
            (void)services_.check_assignable(*inner.type,
                                             *inner_expectation->expected,
                                             expr.first->range,
                                             "optional payload",
                                             *inner_expectation);
            return values_.typed_effect(expected_type_->get().clone(), inner.effect);
        }
        if (inner_expected.has_value()) {
            (void)services_.check_assignable(
                *inner.type, inner_expected->get(), expr.first->range, "optional payload");
            return values_.typed_effect(expected_type_->get().clone(), inner.effect);
        }
        return values_.typed_effect(
            values_.optional_type(inner.type ? inner.type->clone() : values_.make_error_type()),
            inner.effect);
    }

    [[nodiscard]] TypedValue visit_path(const ast::ExprSyntax &expr) const {
        return check_path(*expr.path);
    }

    [[nodiscard]] TypedValue visit_qualified_value(const ast::ExprSyntax &expr) const {
        return check_qualified_value(expr);
    }

    [[nodiscard]] TypedValue visit_call(const ast::ExprSyntax &expr) const {
        return check_call(expr);
    }

    [[nodiscard]] TypedValue visit_struct_literal(const ast::ExprSyntax &expr) const {
        return check_struct_literal(expr);
    }

    [[nodiscard]] TypedValue visit_list_literal(const ast::ExprSyntax &expr) const {
        return check_list_literal(expr);
    }

    [[nodiscard]] TypedValue visit_set_literal(const ast::ExprSyntax &expr) const {
        return check_set_literal(expr);
    }

    [[nodiscard]] TypedValue visit_map_literal(const ast::ExprSyntax &expr) const {
        return check_map_literal(expr);
    }

    [[nodiscard]] TypedValue visit_unary(const ast::ExprSyntax &expr) const {
        return check_unary_expr(expr);
    }

    [[nodiscard]] TypedValue visit_binary(const ast::ExprSyntax &expr) const {
        return check_binary_expr(expr);
    }

    [[nodiscard]] TypedValue visit_member_access(const ast::ExprSyntax &expr) const {
        return check_member_access(expr);
    }

    [[nodiscard]] TypedValue visit_index_access(const ast::ExprSyntax &expr) const {
        return check_index_access(expr);
    }

    [[nodiscard]] TypedValue visit_group(const ast::ExprSyntax &expr) const {
        if (expectation_ != nullptr) {
            return services_.check_expr(*expr.first, context_, *expectation_);
        }
        return services_.check_expr(*expr.first, context_, expected_type_);
    }

    [[nodiscard]] TypedValue visit_unknown(const ast::ExprSyntax &) const {
        return values_.error_typed();
    }

  private:
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path) const {
        return services_.resolve_path(path, context_);
    }

    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr) const {
        if (const auto const_reference =
                services_.find_reference(ReferenceKind::ConstValue, expr.qualified_name->range);
            const_reference.has_value()) {
            const auto const_type = services_.get_const_type(const_reference->get().target);
            if (!const_type.has_value()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::MissingConstMetadata,
                    messages::typecheck::ConstTypeInfoMissing.format_with(
                        expr.qualified_name->spelling()),
                    expr.range);
                return values_.error_typed();
            }

            return values_.typed_effect(const_type->get().clone(), ExprEffect::ConstOnly);
        }

        const auto owner_reference = services_.find_reference(
            ReferenceKind::QualifiedValueOwnerType, expr.qualified_name->range);
        if (!owner_reference.has_value()) {
            services_.typecheck_error_here(error_codes::typecheck::UnknownQualifiedValue,
                                           messages::typecheck::UnknownQualifiedValue.format_with(
                                               expr.qualified_name->spelling()),
                                           expr.range);
            return values_.error_typed();
        }

        auto owner_type = services_.resolve_type_symbol(owner_reference->get().target, expr.range);
        if (!owner_type || !owner_type->holds<types::EnumT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidQualifiedValue,
                messages::typecheck::QualifiedValueRequiresConstOrEnumVariant.format_with(
                    expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        const auto enum_info = services_.get_enum(*owner_type);
        if (!enum_info.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingTypeMetadata,
                messages::typecheck::EnumTypeInfoMissing.format_with(owner_type->describe()),
                expr.range);
            return values_.error_typed();
        }

        const auto &segments = expr.qualified_name->segments;
        if (segments.empty() || !enum_info->get().has_variant(segments.back())) {
            std::string message = messages::typecheck::UnknownEnumVariant.format_with(
                expr.qualified_name->spelling());
            if (!segments.empty()) {
                std::vector<std::string> candidates;
                candidates.reserve(enum_info->get().variants.size());
                for (const auto &variant : enum_info->get().variants) {
                    candidates.push_back(variant.name);
                }
                if (const auto suggestion = suggest_name(segments.back(), candidates);
                    suggestion.has_value()) {
                    message += "; did you mean '" + *suggestion + "'?";
                }
            }
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownEnumVariant, std::move(message), expr.range);
            return values_.error_typed();
        }

        return values_.typed_effect(std::move(owner_type), ExprEffect::ConstOnly);
    }

    [[nodiscard]] TypedValue check_struct_literal(const ast::ExprSyntax &expr) const {
        const auto reference =
            services_.find_reference(ReferenceKind::TypeName, expr.qualified_name->range);
        if (!reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownType,
                messages::typecheck::UnknownType.format_with(expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        auto struct_type = services_.resolve_type_symbol(reference->get().target, expr.range);
        if (!struct_type || !struct_type->holds<types::StructT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidStructLiteralTarget,
                messages::typecheck::StructLiteralTargetRequiresStruct.format_with(
                    expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        const auto struct_info = services_.get_struct(*struct_type);
        if (!struct_info.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingTypeMetadata,
                messages::typecheck::StructTypeInfoMissing.format_with(struct_type->describe()),
                expr.range);
            return values_.error_typed();
        }

        std::unordered_set<std::string> seen_fields;
        ExprEffect effect = ExprEffect::Pure;

        for (const auto &field_init : expr.struct_fields) {
            if (!seen_fields.insert(field_init->field_name).second) {
                services_.typecheck_error_here(
                    error_codes::typecheck::DuplicateField,
                    messages::typecheck::DuplicateField.format_with(field_init->field_name),
                    field_init->range);
                continue;
            }

            const auto field = struct_info->get().find_field(field_init->field_name);
            if (!field.has_value()) {
                std::vector<std::string> candidates;
                candidates.reserve(struct_info->get().fields.size());
                for (const auto &candidate : struct_info->get().fields) {
                    candidates.push_back(candidate.name);
                }
                auto message = messages::typecheck::UnknownField.format_with(
                    field_init->field_name, struct_type->describe());
                if (const auto suggestion = suggest_name(field_init->field_name, candidates);
                    suggestion.has_value()) {
                    message += "; did you mean '" + *suggestion + "'?";
                }
                services_.typecheck_error_here(
                    error_codes::typecheck::UnknownField, std::move(message), field_init->range);
                continue;
            }

            const auto expectation = TypeExpectation{
                .expected = field->get().type,
                .origin_kind = TypeExpectationOriginKind::StructField,
                .origin_range = field->get().declaration_range,
                .description = "struct field '" + field->get().name + "'",
            };
            const auto value = services_.check_expr(*field_init->value, context_, expectation);
            effect = join_effects(effect, value.effect);
            (void)services_.check_assignable(*value.type,
                                             *field->get().type,
                                             field_init->value->range,
                                             "struct field",
                                             expectation);
        }

        for (const auto &field : struct_info->get().fields) {
            if (!seen_fields.contains(field.name)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::MissingField,
                    messages::typecheck::MissingField.format_with(field.name),
                    expr.range);
            }
        }

        return values_.typed_effect(std::move(struct_type), effect);
    }

    [[nodiscard]] TypedValue check_list_literal(const ast::ExprSyntax &expr) const {
        MaybeCRef<Type> element_expected = std::nullopt;
        std::optional<TypeExpectation> element_expectation;
        if (expected_type_.has_value()) {
            if (const auto *list = expected_type_->get().get_if<types::ListT>();
                list != nullptr && list->element != nullptr) {
                element_expected = std::cref(*list->element);
                element_expectation = derive_expectation(expectation_, list->element);
            }
        }

        if (expr.items.empty()) {
            if (expected_type_.has_value() && expected_type_->get().holds<types::ListT>()) {
                return values_.typed(expected_type_->get().clone());
            }

            services_.typecheck_error_here(
                error_codes::typecheck::EmptyLiteralWithoutContext,
                messages::typecheck::EmptyListWithoutContext.format_with(),
                expr.range);
            return values_.typed(values_.list_type(values_.make_error_type()));
        }

        auto element_type = values_.clone_or_any(element_expected);
        bool have_element_type = element_expected.has_value();
        std::optional<SourceRange> inferred_element_origin;
        ExprEffect effect = ExprEffect::Pure;

        for (const auto &item : expr.items) {
            const auto value = element_expectation.has_value()
                                   ? services_.check_expr(*item, context_, *element_expectation)
                                   : services_.check_expr(*item, context_, element_expected);
            effect = join_effects(effect, value.effect);

            if (!have_element_type) {
                element_type = value.type ? value.type->clone() : values_.make_error_type();
                have_element_type = true;
                if (!element_expected.has_value()) {
                    inferred_element_origin = item->range;
                }
                continue;
            }

            if (element_expectation.has_value()) {
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "list element", *element_expectation);
            } else if (inferred_element_origin.has_value()) {
                const auto inferred_expectation = inferred_collection_expectation(
                    *element_type, *inferred_element_origin, "previous list element");
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "list element", inferred_expectation);
            } else {
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "list element");
            }
        }

        return values_.typed_effect(values_.list_type(std::move(element_type)), effect);
    }

    [[nodiscard]] TypedValue check_set_literal(const ast::ExprSyntax &expr) const {
        MaybeCRef<Type> element_expected = std::nullopt;
        std::optional<TypeExpectation> element_expectation;
        if (expected_type_.has_value()) {
            if (const auto *set = expected_type_->get().get_if<types::SetT>();
                set != nullptr && set->element != nullptr) {
                element_expected = std::cref(*set->element);
                element_expectation = derive_expectation(expectation_, set->element);
            }
        }

        if (expr.items.empty()) {
            if (expected_type_.has_value() && expected_type_->get().holds<types::SetT>()) {
                return values_.typed(expected_type_->get().clone());
            }

            services_.typecheck_error_here(
                error_codes::typecheck::EmptyLiteralWithoutContext,
                messages::typecheck::EmptySetWithoutContext.format_with(),
                expr.range);
            return values_.typed(values_.set_type(values_.make_error_type()));
        }

        auto element_type = values_.clone_or_any(element_expected);
        bool have_element_type = element_expected.has_value();
        std::optional<SourceRange> inferred_element_origin;
        ExprEffect effect = ExprEffect::Pure;

        for (const auto &item : expr.items) {
            const auto value = element_expectation.has_value()
                                   ? services_.check_expr(*item, context_, *element_expectation)
                                   : services_.check_expr(*item, context_, element_expected);
            effect = join_effects(effect, value.effect);

            if (!have_element_type) {
                element_type = value.type ? value.type->clone() : values_.make_error_type();
                have_element_type = true;
                if (!element_expected.has_value()) {
                    inferred_element_origin = item->range;
                }
                continue;
            }

            if (element_expectation.has_value()) {
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "set element", *element_expectation);
            } else if (inferred_element_origin.has_value()) {
                const auto inferred_expectation = inferred_collection_expectation(
                    *element_type, *inferred_element_origin, "previous set element");
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "set element", inferred_expectation);
            } else {
                (void)services_.check_assignable(
                    *value.type, *element_type, item->range, "set element");
            }
        }

        return values_.typed_effect(values_.set_type(std::move(element_type)), effect);
    }

    [[nodiscard]] TypedValue check_map_literal(const ast::ExprSyntax &expr) const {
        MaybeCRef<Type> key_expected = std::nullopt;
        MaybeCRef<Type> value_expected = std::nullopt;
        std::optional<TypeExpectation> key_expectation;
        std::optional<TypeExpectation> value_expectation;
        if (expected_type_.has_value()) {
            if (const auto *map = expected_type_->get().get_if<types::MapT>();
                map != nullptr && map->key != nullptr && map->value != nullptr) {
                key_expected = std::cref(*map->key);
                value_expected = std::cref(*map->value);
                key_expectation = derive_expectation(expectation_, map->key);
                value_expectation = derive_expectation(expectation_, map->value);
            }
        }

        if (expr.map_entries.empty()) {
            if (expected_type_.has_value() && expected_type_->get().holds<types::MapT>()) {
                return values_.typed(expected_type_->get().clone());
            }

            services_.typecheck_error_here(
                error_codes::typecheck::EmptyLiteralWithoutContext,
                messages::typecheck::EmptyMapWithoutContext.format_with(),
                expr.range);
            return values_.typed(
                values_.map_type(values_.make_error_type(), values_.make_error_type()));
        }

        auto key_type = values_.clone_or_any(key_expected);
        auto value_type = values_.clone_or_any(value_expected);
        bool have_key_type = key_expected.has_value();
        bool have_value_type = value_expected.has_value();
        std::optional<SourceRange> inferred_key_origin;
        std::optional<SourceRange> inferred_value_origin;
        ExprEffect effect = ExprEffect::Pure;

        for (const auto &entry : expr.map_entries) {
            const auto key = key_expectation.has_value()
                                 ? services_.check_expr(*entry->key, context_, *key_expectation)
                                 : services_.check_expr(*entry->key, context_, key_expected);
            const auto value =
                value_expectation.has_value()
                    ? services_.check_expr(*entry->value, context_, *value_expectation)
                    : services_.check_expr(*entry->value, context_, value_expected);
            effect = join_effects(effect, join_effects(key.effect, value.effect));

            if (!have_key_type) {
                key_type = key.type ? key.type->clone() : values_.make_error_type();
                have_key_type = true;
                if (!key_expected.has_value()) {
                    inferred_key_origin = entry->key->range;
                }
            } else {
                if (key_expectation.has_value()) {
                    (void)services_.check_assignable(
                        *key.type, *key_type, entry->key->range, "map key", *key_expectation);
                } else if (inferred_key_origin.has_value()) {
                    const auto inferred_expectation = inferred_collection_expectation(
                        *key_type, *inferred_key_origin, "previous map key");
                    (void)services_.check_assignable(
                        *key.type, *key_type, entry->key->range, "map key", inferred_expectation);
                } else {
                    (void)services_.check_assignable(
                        *key.type, *key_type, entry->key->range, "map key");
                }
            }

            if (!have_value_type) {
                value_type = value.type ? value.type->clone() : values_.make_error_type();
                have_value_type = true;
                if (!value_expected.has_value()) {
                    inferred_value_origin = entry->value->range;
                }
            } else {
                if (value_expectation.has_value()) {
                    (void)services_.check_assignable(*value.type,
                                                     *value_type,
                                                     entry->value->range,
                                                     "map value",
                                                     *value_expectation);
                } else if (inferred_value_origin.has_value()) {
                    const auto inferred_expectation = inferred_collection_expectation(
                        *value_type, *inferred_value_origin, "previous map value");
                    (void)services_.check_assignable(*value.type,
                                                     *value_type,
                                                     entry->value->range,
                                                     "map value",
                                                     inferred_expectation);
                } else {
                    (void)services_.check_assignable(
                        *value.type, *value_type, entry->value->range, "map value");
                }
            }
        }

        return values_.typed_effect(values_.map_type(std::move(key_type), std::move(value_type)),
                                    effect);
    }

    [[nodiscard]] TypedValue check_call(const ast::ExprSyntax &expr) const {
        const auto reference =
            services_.find_reference(ReferenceKind::CallTarget, expr.qualified_name->range);
        if (!reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownCallable,
                messages::typecheck::UnknownCallable.format_with(expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        const auto symbol = services_.symbol_of(reference->get().target);
        if (!symbol.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidCallableReference,
                messages::typecheck::CallTargetSymbolMissing.format_with(),
                expr.range);
            return values_.error_typed(false);
        }

        if (symbol->get().kind == SymbolKind::Capability) {
            return check_capability_call(expr, reference->get().target);
        }

        if (symbol->get().kind != SymbolKind::Predicate) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidCallableReference,
                messages::typecheck::SymbolDoesNotNameCallable.format_with(
                    symbol->get().canonical_name),
                expr.range);
            return values_.error_typed(false);
        }

        return check_predicate_call(expr, reference->get().target);
    }

    [[nodiscard]] TypedValue check_capability_call(const ast::ExprSyntax &expr,
                                                   SymbolId target) const {
        const auto capability = services_.get_capability(target);
        if (!capability.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::CapabilityTypeInfoMissing.format_with(
                    expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        if (context_.call_context != CallContext::Flow) {
            services_.typecheck_error_here(error_codes::typecheck::CapabilityNotAllowed,
                                           messages::typecheck::CapabilityNotAllowed.format_with(
                                               expr.qualified_name->spelling()),
                                           expr.range);
        }

        if (context_.current_agent.has_value()) {
            const auto agent_info = services_.get_agent(*context_.current_agent);
            if (agent_info.has_value()) {
                const auto allowed = std::find(agent_info->get().capability_symbols.begin(),
                                               agent_info->get().capability_symbols.end(),
                                               target);
                if (allowed == agent_info->get().capability_symbols.end()) {
                    services_.typecheck_error_here(
                        error_codes::typecheck::CapabilityNotAllowed,
                        messages::typecheck::CapabilityNotDeclared.format_with(
                            expr.qualified_name->spelling()),
                        expr.range);
                }
            }
        }

        if (expr.items.size() != capability->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "capability",
                                               expr.qualified_name->spelling(),
                                               std::to_string(capability->get().params.size()),
                                               std::to_string(expr.items.size())),
                                           expr.range);
        }

        const auto limit = std::min(expr.items.size(), capability->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = capability->get().params[index];
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
            };
            const auto argument = services_.check_expr(*expr.items[index], context_, expectation);
            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             expr.items[index]->range,
                                             "capability argument",
                                             expectation);
        }

        return values_.typed_effect(capability->get().return_type
                                        ? capability->get().return_type->clone()
                                        : values_.make_error_type(),
                                    ExprEffect::CapabilityCall);
    }

    [[nodiscard]] TypedValue check_predicate_call(const ast::ExprSyntax &expr,
                                                  SymbolId target) const {
        const auto predicate = services_.get_predicate(target);
        if (!predicate.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::PredicateTypeInfoMissing.format_with(
                    expr.qualified_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        if (expr.items.size() != predicate->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "predicate",
                                               expr.qualified_name->spelling(),
                                               std::to_string(predicate->get().params.size()),
                                               std::to_string(expr.items.size())),
                                           expr.range);
        }

        ExprEffect effect = ExprEffect::PredicateCall;
        const auto limit = std::min(expr.items.size(), predicate->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = predicate->get().params[index];
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
            };
            const auto argument = services_.check_expr(*expr.items[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            if (!argument.is_pure) {
                services_.typecheck_error_here(
                    error_codes::typecheck::NonPureExpression,
                    messages::typecheck::PredicateArgsNotPure.format_with() +
                        "; expression effect: " + std::string(to_string(argument.effect)),
                    expr.items[index]->range);
            }

            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             expr.items[index]->range,
                                             "predicate argument",
                                             expectation);
        }

        return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
    }

    [[nodiscard]] TypedValue check_unary_expr(const ast::ExprSyntax &expr) const {
        const auto operand = services_.check_expr(*expr.first, context_, std::nullopt);
        switch (expr.unary_op) {
        case ast::ExprUnaryOp::Not:
            if (!is_bool_type(*operand.type) && !is_error_type(*operand.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::LogicalNotRequiresBool.format_with(
                        operand.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), operand.effect);
        case ast::ExprUnaryOp::Negate:
        case ast::ExprUnaryOp::Positive:
            if (!is_numeric_type(*operand.type) && !is_error_type(*operand.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::NumericUnaryRequiresNumeric.format_with(
                        operand.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(
                operand.type ? operand.type->clone() : values_.make_error_type(), operand.effect);
        }

        return values_.error_typed_effect(operand.effect);
    }

    [[nodiscard]] TypedValue check_binary_expr(const ast::ExprSyntax &expr) const {
        if ((expr.binary_op == ast::ExprBinaryOp::Equal ||
             expr.binary_op == ast::ExprBinaryOp::NotEqual) &&
            expr.first && expr.second &&
            (expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ||
             expr.second->kind == ast::ExprSyntaxKind::NoneLiteral)) {
            const auto &none_operand =
                expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ? *expr.first : *expr.second;
            const auto &value_operand =
                expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ? *expr.second : *expr.first;
            const auto value = services_.check_expr(value_operand, context_, std::nullopt);
            const auto none = services_.check_expr(none_operand, context_, std::cref(*value.type));
            const auto effect = join_effects(value.effect, none.effect);
            if (!is_error_type(*value.type) && !value.type->holds<types::OptionalT>()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::NoneComparisonRequiresOptional.format_with(
                        value.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        }

        const auto lhs = services_.check_expr(*expr.first, context_, std::nullopt);
        const auto rhs = services_.check_expr(*expr.second, context_, std::nullopt);
        const auto effect = join_effects(lhs.effect, rhs.effect);

        const auto comparable = [&]() {
            return is_error_type(*lhs.type) || is_error_type(*rhs.type) ||
                   is_subtype_of(*lhs.type, *rhs.type) || is_subtype_of(*rhs.type, *lhs.type);
        };

        const auto strict_arithmetic = [&](ast::ExprBinaryOp op) -> TypePtr {
            if (is_error_type(*lhs.type) || is_error_type(*rhs.type)) {
                return values_.make_error_type();
            }

            const bool lhs_int = lhs.type->holds<types::IntT>();
            const bool rhs_int = rhs.type->holds<types::IntT>();
            const bool lhs_float = lhs.type->holds<types::FloatT>();
            const bool rhs_float = rhs.type->holds<types::FloatT>();
            const auto *lhs_dec = lhs.type->get_if<types::DecimalT>();
            const auto *rhs_dec = rhs.type->get_if<types::DecimalT>();

            if (lhs_int && rhs_int) {
                return values_.make_type(TypeKind::Int);
            }
            if (lhs_float && rhs_float) {
                return values_.make_type(TypeKind::Float);
            }
            if ((op == ast::ExprBinaryOp::Add || op == ast::ExprBinaryOp::Subtract) &&
                lhs_dec != nullptr && rhs_dec != nullptr && lhs_dec->scale == rhs_dec->scale) {
                return lhs.type->clone();
            }
            return nullptr;
        };

        switch (expr.binary_op) {
        case ast::ExprBinaryOp::Implies:
        case ast::ExprBinaryOp::Or:
        case ast::ExprBinaryOp::And:
            if ((!is_bool_type(*lhs.type) || !is_bool_type(*rhs.type)) &&
                !is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::LogicalOperatorRequiresBool.format_with(),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        case ast::ExprBinaryOp::Equal:
        case ast::ExprBinaryOp::NotEqual:
        case ast::ExprBinaryOp::Less:
        case ast::ExprBinaryOp::LessEqual:
        case ast::ExprBinaryOp::Greater:
        case ast::ExprBinaryOp::GreaterEqual:
            if (!comparable()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::ComparisonOperandsIncompatible.format_with(
                        lhs.type->describe(), rhs.type->describe()),
                    expr.range);
            }
            return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
        case ast::ExprBinaryOp::Add:
            if (lhs.type->holds<types::StringT>() && rhs.type->holds<types::StringT>()) {
                return values_.typed_effect(values_.string_type(), effect);
            }

            if (const auto result = strict_arithmetic(expr.binary_op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::InvalidOperator.format_with(
                                                   "+", lhs.type->describe(), rhs.type->describe()),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Subtract:
            if (const auto result = strict_arithmetic(expr.binary_op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::InvalidOperator.format_with(
                                                   "-", lhs.type->describe(), rhs.type->describe()),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Multiply:
        case ast::ExprBinaryOp::Divide:
            if (const auto result = strict_arithmetic(expr.binary_op); result != nullptr) {
                return values_.typed_effect(result, effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidOperation,
                    messages::typecheck::ArithmeticOperatorInvalid.format_with(
                        lhs.type->describe(), rhs.type->describe()),
                    expr.range);
            }
            return values_.error_typed_effect(effect);
        case ast::ExprBinaryOp::Modulo:
            if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
                return values_.typed_effect(values_.make_type(TypeKind::Int), effect);
            }

            if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
                services_.typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                               messages::typecheck::ModuloRequiresInt.format_with(),
                                               expr.range);
            }
            return values_.error_typed_effect(effect);
        }

        return values_.error_typed_effect(effect);
    }

    [[nodiscard]] TypedValue check_member_access(const ast::ExprSyntax &expr) const {
        const auto base = services_.check_expr(*expr.first, context_, std::nullopt);
        auto member_type = services_.field_access(*base.type, expr.name, expr.range);
        if (const auto place = place_of_expression(expr); place.has_value()) {
            member_type = services_.apply_flow_narrowing(member_type, *place, context_.flow_facts);
        }
        return values_.typed_effect(member_type, base.effect);
    }

    [[nodiscard]] TypedValue check_index_access(const ast::ExprSyntax &expr) const {
        const auto collection = services_.check_expr(*expr.first, context_, std::nullopt);
        const auto index = services_.check_expr(*expr.second, context_, std::nullopt);
        const auto effect = join_effects(collection.effect, index.effect);

        if (const auto *list = collection.type->get_if<types::ListT>(); list != nullptr) {
            if (!index.type->holds<types::IntT>() && !is_error_type(*index.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidIndexAccess,
                    messages::typecheck::ListIndexRequiresInt.format_with(),
                    expr.second->range);
            }

            if (list->element != nullptr) {
                return values_.typed_effect(list->element->clone(), effect);
            }

            return values_.error_typed_effect(effect);
        }

        if (const auto *map = collection.type->get_if<types::MapT>(); map != nullptr) {
            if (map->key != nullptr) {
                (void)services_.check_assignable(
                    *index.type, *map->key, expr.second->range, "map index");
            }

            if (map->value != nullptr) {
                return values_.typed_effect(map->value->clone(), effect);
            }

            return values_.error_typed_effect(effect);
        }

        if (!is_error_type(*collection.type)) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidIndexAccess,
                messages::typecheck::IndexTargetRequiresCollection.format_with(
                    collection.type->describe()),
                expr.range);
        }

        return values_.error_typed_effect(effect);
    }

    ExpressionCheckerServices &services_;
    const ValueContext &context_;
    MaybeCRef<Type> expected_type_;
    const TypeExpectation *expectation_{nullptr};
    const ExpressionValueFactory &values_;
};

TypedValue TypeCheckPass::check_expr(const ast::ExprSyntax &expr,
                                     const ValueContext &context,
                                     MaybeCRef<Type> expected_type) {
    auto value = check_expr_impl(expr, context, expected_type);
    remember_expression_type(expr, value);
    return value;
}

TypedValue TypeCheckPass::check_expr(const ast::ExprSyntax &expr,
                                     const ValueContext &context,
                                     const TypeExpectation &expectation) {
    MaybeCRef<Type> expected_type = std::nullopt;
    if (expectation.expected != nullptr) {
        expected_type = std::cref(*expectation.expected);
    }
    auto value = check_expr_impl(expr, context, expected_type, &expectation);
    remember_expression_type(expr, value);
    return value;
}

TypedValue TypeCheckPass::check_expr_impl(const ast::ExprSyntax &expr,
                                          const ValueContext &context,
                                          MaybeCRef<Type> expected_type,
                                          const TypeExpectation *expectation) {
    auto type_resolver = make_type_resolver();
    ExpressionCheckerServices services{
        resolve_result_,
        current_source_id_,
        result_.environment,
        *types_,
        relations_,
        [this](ErrorCode<DiagnosticCategory::TypeCheck> code,
               std::string message,
               SourceRange range) { typecheck_error_here(code, std::move(message), range); },
        [this](ErrorCode<DiagnosticCategory::TypeCheck> code,
               std::string message,
               SourceRange range,
               std::vector<Diagnostic::Related> notes) {
            typecheck_error_here(code, std::move(message), range, std::move(notes));
        },
        [this](const ast::ExprSyntax &nested_expr,
               const ValueContext &nested_context,
               MaybeCRef<Type> nested_expected) {
            return check_expr(nested_expr, nested_context, nested_expected);
        },
        [this](const ast::ExprSyntax &nested_expr,
               const ValueContext &nested_context,
               const TypeExpectation &nested_expectation) {
            return check_expr(nested_expr, nested_context, nested_expectation);
        },
        [&type_resolver](SymbolId id, SourceRange range) {
            return type_resolver.resolve_type_symbol(id, range);
        }};
    return ExpressionChecker{services, context, expected_type, expectation}.check(expr);
}

TypedValue TypeCheckPass::check_path(const ast::PathSyntax &path, const ValueContext &context) {
    return resolve_expression_path(path,
                                   context,
                                   result_.environment,
                                   *types_,
                                   [this](ErrorCode<DiagnosticCategory::TypeCheck> code,
                                          std::string message,
                                          SourceRange diagnostic_range) {
                                       typecheck_error_here(
                                           code, std::move(message), diagnostic_range);
                                   });
}

} // namespace ahfl
