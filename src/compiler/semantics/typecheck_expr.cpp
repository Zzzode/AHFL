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
#include <variant>
#include <vector>

namespace ahfl {

using internal::BindingMap;
using internal::CallContext;
using internal::is_bool_type;
using internal::is_error_type;
using internal::is_numeric_type;
using internal::TypedValue;
using internal::ValueContext;

namespace {

template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

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
                              ExpressionSemaDelegate &delegate)
        : resolve_result_(resolve_result), current_source_id_(current_source_id),
          environment_(environment), types_(types), relations_(relations), delegate_(&delegate),
          values_(types_) {}

    [[nodiscard]] const ExpressionValueFactory &values() const noexcept {
        return values_;
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range) const {
        delegate_->typecheck_error(code, std::move(message), range);
    }

    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range,
                              std::vector<Diagnostic::Related> notes) const {
        delegate_->typecheck_error(code, std::move(message), range, std::move(notes));
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type) const {
        return delegate_->check_nested(expr, context, expected_type);
    }

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        const TypeExpectation &expectation) const {
        return delegate_->check_nested(expr, context, expectation);
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
        return delegate_->resolve_type_symbol(id, use_range);
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
            base_type, field_name, range, environment_, types_, *delegate_);
    }

    [[nodiscard]] TypedValue resolve_path(const ast::PathSyntax &path,
                                          const ValueContext &context) const {
        return resolve_expression_path(path, context, environment_, types_, *delegate_);
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
    ExpressionSemaDelegate *delegate_{nullptr};
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
        return std::visit(
            overloaded{
                [&](const ast::NoneLiteralExpr &) { return visit_none_literal(expr); },
                [&](const ast::BoolLiteralExpr &) { return visit_bool_literal(expr); },
                [&](const ast::IntegerLiteralExpr &) { return visit_integer_literal(expr); },
                [&](const ast::FloatLiteralExpr &) { return visit_float_literal(expr); },
                [&](const ast::DecimalLiteralExpr &) { return visit_decimal_literal(expr); },
                [&](const ast::StringLiteralExpr &) { return visit_string_literal(expr); },
                [&](const ast::DurationLiteralExpr &) { return visit_duration_literal(expr); },
                [&](const ast::SomeExpr &) { return visit_some(expr); },
                [&](const ast::PathExpr &) { return visit_path(expr); },
                [&](const ast::QualifiedValueExpr &) { return visit_qualified_value(expr); },
                [&](const ast::CallExpr &) { return visit_call(expr); },
                [&](const ast::StructLiteralExpr &) { return visit_struct_literal(expr); },
                [&](const ast::ListLiteralExpr &) { return visit_list_literal(expr); },
                [&](const ast::SetLiteralExpr &) { return visit_set_literal(expr); },
                [&](const ast::MapLiteralExpr &) { return visit_map_literal(expr); },
                [&](const ast::UnaryExpr &) { return visit_unary(expr); },
                [&](const ast::BinaryExpr &) { return visit_binary(expr); },
                [&](const ast::MemberAccessExpr &) { return visit_member_access(expr); },
                [&](const ast::IndexAccessExpr &) { return visit_index_access(expr); },
                [&](const ast::GroupExpr &) { return visit_group(expr); },
                [&](const ast::MatchExpr &) { return visit_match(expr); },
            },
            expr.node);
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
        return values_.typed(
            values_.decimal_type(parse_decimal_scale(expr.as<ast::DecimalLiteralExpr>().spelling)));
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
        const auto &some = expr.as<ast::SomeExpr>();
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
                               ? services_.check_expr(*some.value, context_, *inner_expectation)
                               : services_.check_expr(*some.value, context_, inner_expected);
        if (inner_expectation.has_value() && inner_expectation->expected != nullptr) {
            (void)services_.check_assignable(*inner.type,
                                             *inner_expectation->expected,
                                             some.value->range,
                                             "optional payload",
                                             *inner_expectation);
            return values_.typed_effect(expected_type_->get().clone(), inner.effect);
        }
        if (inner_expected.has_value()) {
            (void)services_.check_assignable(
                *inner.type, inner_expected->get(), some.value->range, "optional payload");
            return values_.typed_effect(expected_type_->get().clone(), inner.effect);
        }
        return values_.typed_effect(
            values_.optional_type(inner.type ? inner.type->clone() : values_.make_error_type()),
            inner.effect);
    }

    [[nodiscard]] TypedValue visit_path(const ast::ExprSyntax &expr) const {
        return check_path(*expr.as<ast::PathExpr>().path);
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
        const auto &group = expr.as<ast::GroupExpr>();
        if (expectation_ != nullptr) {
            return services_.check_expr(*group.inner, context_, *expectation_);
        }
        return services_.check_expr(*group.inner, context_, expected_type_);
    }

    // P1b (ADT, RFC §1.6): full match typecheck.
    //
    //   1. The scrutinee must type-check to an enum type (the source of
    //      variants we narrow against). Otherwise report
    //      MATCH_SCRUTINEE_REQUIRES_ENUM.
    //   2. For each arm, walk the pattern, registering any `@`-free / explicit
    //      binding name against the corresponding payload slot type. Sub-patterns
    //      (tuple/or/literal) are checked structurally; payload arity mismatches
    //      surface MATCH_VARIANT_PAYLOAD_ARITY.
    //   3. Each arm body is checked in a context carrying the pattern bindings;
    //      the first arm's body type is the expected type for every subsequent
    //      arm body (MATCH_ARM_TYPE_MISMATCH on divergence).
    //   4. Exhaustiveness: the match is exhaustive when an arm's pattern can
    //      match every value of the scrutinee type. The first version accepts
    //      either a wildcard arm (`_`) or a binding-only arm (`x`) at any
    //      position, or full coverage of all declared variants. Anything else
    //      reports MATCH_NOT_EXHAUSTIVE with the missing variant names.
    [[nodiscard]] TypedValue visit_match(const ast::ExprSyntax &expr) const {
        const auto &match = expr.as<ast::MatchExpr>();

        const auto scrutinee = services_.check_expr(*match.scrutinee, context_, std::nullopt);
        ExprEffect joined = scrutinee.effect;

        const auto *enum_payload = scrutinee.type->get_if<types::EnumT>();
        if (enum_payload == nullptr && !is_error_type(*scrutinee.type)) {
            services_.typecheck_error_here(
                error_codes::typecheck::MatchScrutineeRequiresEnum,
                messages::typecheck::MatchScrutineeRequiresEnum.format_with(
                    scrutinee.type->describe()),
                match.scrutinee->range);
            return values_.error_typed_effect(joined);
        }

        // If the scrutinee failed to resolve to an enum, still walk the arms so
        // cascaded diagnostics stay localized; the match result is error.
        const auto enum_info =
            enum_payload != nullptr ? services_.get_enum(*scrutinee.type) : std::nullopt;

        bool has_catch_all = false;
        std::unordered_set<std::string> covered_variants;
        std::optional<TypePtr> unified_body_type;
        bool unified_is_error = false;

        for (const auto &arm : match.arms) {
            // Build a per-arm value context that layers the pattern's bindings
            // on top of the surrounding bindings. Bindings introduced by the
            // pattern shadow any same-named outer binding (match-arm scope).
            ValueContext arm_context = context_;
            const bool arm_catches_all =
                lower_pattern(*arm->pattern,
                              scrutinee.type,
                              enum_info,
                              arm_context.bindings,
                              covered_variants,
                              arm->pattern->range);

            if (arm_catches_all) {
                has_catch_all = true;
            }

            // Optional guard: must be a pure Bool expression evaluated in the
            // arm-local binding scope. A guard with side effects degrades the
            // match's effect but does not invalidate exhaustiveness accounting.
            if (arm->guard) {
                const auto guard_value =
                    services_.check_expr(*arm->guard, arm_context, std::nullopt);
                joined = join_effects(joined, guard_value.effect);
            }

            MaybeCRef<Type> arm_expected = std::nullopt;
            if (unified_body_type.has_value()) {
                arm_expected = std::cref(**unified_body_type);
            }
            const auto body =
                services_.check_expr(*arm->body, arm_context, arm_expected);
            joined = join_effects(joined, body.effect);

            if (!unified_body_type.has_value()) {
                if (body.type != nullptr) {
                    unified_body_type = body.type;
                    unified_is_error = is_error_type(*body.type);
                } else {
                    unified_is_error = true;
                }
            } else if (!unified_is_error && body.type != nullptr &&
                       !is_error_type(*body.type)) {
                (void)services_.check_assignable(*body.type,
                                                 **unified_body_type,
                                                 arm->body->range,
                                                 "match arm body");
            }
        }

        // Exhaustiveness: catch-all arm short-circuits; otherwise every variant
        // of the scrutinee enum must appear in some arm's pattern.
        if (!has_catch_all) {
            if (enum_info.has_value()) {
                for (const auto &variant : enum_info->get().variants) {
                    if (covered_variants.count(variant.name) == 0) {
                        std::vector<std::string> missing;
                        missing.reserve(enum_info->get().variants.size());
                        for (const auto &v : enum_info->get().variants) {
                            if (covered_variants.count(v.name) == 0) {
                                missing.push_back(v.name);
                            }
                        }
                        std::string listing;
                        for (std::size_t i = 0; i < missing.size(); ++i) {
                            listing += (i == 0 ? "" : ", ") + missing[i];
                        }
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchNotExhaustive,
                            messages::typecheck::MatchNotExhaustive.format_with(listing),
                            expr.range);
                        break;
                    }
                }
            }
            // When the scrutinee is not an enum (error path) we skip the
            // exhaustiveness check — the MATCH_SCRUTINEE_REQUIRES_ENUM
            // diagnostic above already flags the real problem.
        }

        TypePtr result_type =
            unified_body_type.has_value() && !unified_is_error
                ? *unified_body_type
                : values_.make_error_type();
        return values_.typed_effect(std::move(result_type), joined);
    }

  private:
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path) const {
        return services_.resolve_path(path, context_);
    }

    // ---------------------------------------------------------------------------
    // P1b ADT: pattern lowering for `match` arms.
    //
    // `lower_pattern` walks a PatternSyntax, recording:
    //   * payload-binding names into `bindings` (mapping name -> slot type)
    //   * matched variant names into `covered_variants` (for exhaustiveness)
    //
    // Returns true when the pattern is irrefutable at the scrutinee type — i.e.
    // a wildcard `_`, a bare binding `x` (no `@`), or a tuple pattern composed
    // entirely of irrefutable sub-patterns. Such patterns make the enclosing
    // arm a catch-all for exhaustiveness purposes.
    //
    // `scrutinee_type` is the narrowed type for this sub-pattern (the enum type
    // at the top level, the slot type inside a variant payload). `enum_info`
    // is the looked-up EnumTypeInfo for the top-level scrutinee; sub-pattern
    // recursion passes std::nullopt since nested scrutinees are not enums.
    [[nodiscard]] bool lower_pattern(const ast::PatternSyntax &pattern,
                                     TypePtr scrutinee_type,
                                     std::optional<std::reference_wrapper<const EnumTypeInfo>> enum_info,
                                     BindingMap &bindings,
                                     std::unordered_set<std::string> &covered_variants,
                                     SourceRange range) const {
        return std::visit(
            overloaded{
                [&](const ast::LiteralPattern &) {
                    // Literal patterns only narrow (no binding); they are not
                    // catch-alls. Exhaustiveness against literals is not modeled
                    // in the first version — only variant coverage counts.
                    (void)scrutinee_type;
                    (void)enum_info;
                    (void)bindings;
                    (void)covered_variants;
                    (void)range;
                    return false;
                },
                [&](const ast::WildcardPattern &) {
                    (void)scrutinee_type;
                    (void)enum_info;
                    (void)bindings;
                    (void)covered_variants;
                    (void)range;
                    return true;
                },
                [&](const ast::BindingPattern &binding) {
                    // P1 disambiguation (Rust-style): in an enum scrutinee
                    // context, a bare identifier that names a variant of that
                    // enum is a payload-less variant pattern (covers the variant,
                    // is NOT a catch-all). A non-variant identifier is an
                    // irrefutable binding (catch-all).
                    if (enum_info.has_value() && !binding.nested &&
                        enum_info->get().has_variant(binding.name)) {
                        covered_variants.insert(std::string(binding.name));
                        return false;
                    }
                    // A bare binding matches everything and is irrefutable.
                    // Bind the name to the (narrowed) scrutinee type. When an
                    // `@`-bound nested pattern is present, recurse into it for
                    // its own bindings; the outer name still binds the whole.
                    if (!binding.name.empty()) {
                        const auto [iter, inserted] = bindings.emplace(
                            binding.name, scrutinee_type != nullptr ? scrutinee_type
                                                                    : values_.make_error_type());
                        if (!inserted &&
                            (!iter->second || !is_error_type(*iter->second))) {
                            services_.typecheck_error_here(
                                error_codes::typecheck::MatchDuplicateBinding,
                                messages::typecheck::MatchDuplicateBinding.format_with(
                                    binding.name),
                                range);
                        }
                    }
                    if (binding.nested) {
                        (void)lower_pattern(*binding.nested,
                                            scrutinee_type,
                                            enum_info,
                                            bindings,
                                            covered_variants,
                                            binding.nested->range);
                    }
                    // A binding is a catch-all unless it is constrained by an
                    // `@`-bound variant sub-pattern.
                    if (binding.nested &&
                        binding.nested->is<ast::VariantPattern>()) {
                        return false;
                    }
                    return true;
                },
                [&](const ast::TuplePattern &tuple) {
                    // Tuple patterns are irrefutable iff every element is. The
                    // first-version match grammar uses tuple patterns only for
                    // payload positional structure; the scrutinee slot types
                    // are not tuples at this stage, so we conservatively treat
                    // each element against the same scrutinee type.
                    bool all_irrefutable = true;
                    for (const auto &element : tuple.elements) {
                        const auto sub_irrefutable = lower_pattern(*element,
                                                                   scrutinee_type,
                                                                   enum_info,
                                                                   bindings,
                                                                   covered_variants,
                                                                   element->range);
                        all_irrefutable = all_irrefutable && sub_irrefutable;
                    }
                    return all_irrefutable && !tuple.elements.empty();
                },
                [&](const ast::VariantPattern &variant) {
                    // Variant patterns only narrow; they are never catch-alls.
                    if (!enum_info.has_value()) {
                        // No enum context to validate against (scrutinee failed
                        // to resolve). Skip variant coverage bookkeeping.
                        return false;
                    }
                    const auto &segments = variant.path->segments;
                    if (segments.empty()) {
                        return false;
                    }
                    const auto &variant_name = segments.back();
                    const auto variant_info = enum_info->get().find_variant(variant_name);
                    if (!variant_info.has_value()) {
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchUnknownVariant,
                            messages::typecheck::MatchUnknownVariant.format_with(
                                variant.path->spelling(),
                                enum_info->get().canonical_name),
                            range);
                        return false;
                    }

                    covered_variants.insert(std::string(variant_name));

                    const auto &payload = variant_info->get().payload;
                    if (variant.subpatterns.size() != payload.size()) {
                        services_.typecheck_error_here(
                            error_codes::typecheck::MatchVariantPayloadArity,
                            messages::typecheck::MatchVariantPayloadArity.format_with(
                                std::string(variant_name),
                                enum_info->get().canonical_name,
                                std::to_string(payload.size()),
                                std::to_string(variant.subpatterns.size())),
                            range);
                    }

                    const std::size_t limit =
                        std::min(variant.subpatterns.size(), payload.size());
                    for (std::size_t index = 0; index < limit; ++index) {
                        (void)lower_pattern(*variant.subpatterns[index],
                                            payload[index],
                                            std::nullopt,
                                            bindings,
                                            covered_variants,
                                            variant.subpatterns[index]->range);
                    }
                    return false;
                },
                [&](const ast::OrPattern &or_pattern) {
                    // An or-pattern is catch-all only if every branch is. We
                    // lower every branch so each branch's variant coverage and
                    // bindings are recorded (the first-version requirement is
                    // that all branches bind equivalent names; we do not yet
                    // enforce that here — coverage is what matters for
                    // exhaustiveness).
                    bool all_irrefutable = true;
                    for (const auto &branch : or_pattern.branches) {
                        const auto sub_irrefutable = lower_pattern(*branch,
                                                                   scrutinee_type,
                                                                   enum_info,
                                                                   bindings,
                                                                   covered_variants,
                                                                   branch->range);
                        all_irrefutable = all_irrefutable && sub_irrefutable;
                    }
                    return all_irrefutable && !or_pattern.branches.empty();
                },
            },
            pattern.node);
    }

    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr) const {
        const auto &qualified = expr.as<ast::QualifiedValueExpr>();
        if (const auto const_reference =
                services_.find_reference(ReferenceKind::ConstValue, qualified.name->range);
            const_reference.has_value()) {
            const auto const_type = services_.get_const_type(const_reference->get().target);
            if (!const_type.has_value()) {
                services_.typecheck_error_here(
                    error_codes::typecheck::MissingConstMetadata,
                    messages::typecheck::ConstTypeInfoMissing.format_with(
                        qualified.name->spelling()),
                    expr.range);
                return values_.error_typed();
            }

            return values_.typed_effect(const_type->get().clone(), ExprEffect::ConstOnly);
        }

        const auto owner_reference =
            services_.find_reference(ReferenceKind::QualifiedValueOwnerType, qualified.name->range);
        if (!owner_reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownQualifiedValue,
                messages::typecheck::UnknownQualifiedValue.format_with(qualified.name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        auto owner_type = services_.resolve_type_symbol(owner_reference->get().target, expr.range);
        if (!owner_type || !owner_type->holds<types::EnumT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidQualifiedValue,
                messages::typecheck::QualifiedValueRequiresConstOrEnumVariant.format_with(
                    qualified.name->spelling()),
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

        const auto &segments = qualified.name->segments;
        if (segments.empty() || !enum_info->get().has_variant(segments.back())) {
            std::string message =
                messages::typecheck::UnknownEnumVariant.format_with(qualified.name->spelling());
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
        const auto &struct_lit = expr.as<ast::StructLiteralExpr>();
        const auto reference =
            services_.find_reference(ReferenceKind::TypeName, struct_lit.type_name->range);
        if (!reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownType,
                messages::typecheck::UnknownType.format_with(struct_lit.type_name->spelling()),
                expr.range);
            return values_.error_typed();
        }

        auto struct_type = services_.resolve_type_symbol(reference->get().target, expr.range);
        if (!struct_type || !struct_type->holds<types::StructT>()) {
            services_.typecheck_error_here(
                error_codes::typecheck::InvalidStructLiteralTarget,
                messages::typecheck::StructLiteralTargetRequiresStruct.format_with(
                    struct_lit.type_name->spelling()),
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

        for (const auto &field_init : struct_lit.fields) {
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
        const auto &list = expr.as<ast::ListLiteralExpr>();
        MaybeCRef<Type> element_expected = std::nullopt;
        std::optional<TypeExpectation> element_expectation;
        if (expected_type_.has_value()) {
            if (const auto *list_type = expected_type_->get().get_if<types::ListT>();
                list_type != nullptr && list_type->element != nullptr) {
                element_expected = std::cref(*list_type->element);
                element_expectation = derive_expectation(expectation_, list_type->element);
            }
        }

        if (list.items.empty()) {
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

        for (const auto &item : list.items) {
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
        const auto &set = expr.as<ast::SetLiteralExpr>();
        MaybeCRef<Type> element_expected = std::nullopt;
        std::optional<TypeExpectation> element_expectation;
        if (expected_type_.has_value()) {
            if (const auto *set_type = expected_type_->get().get_if<types::SetT>();
                set_type != nullptr && set_type->element != nullptr) {
                element_expected = std::cref(*set_type->element);
                element_expectation = derive_expectation(expectation_, set_type->element);
            }
        }

        if (set.items.empty()) {
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

        for (const auto &item : set.items) {
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
        const auto &map = expr.as<ast::MapLiteralExpr>();
        MaybeCRef<Type> key_expected = std::nullopt;
        MaybeCRef<Type> value_expected = std::nullopt;
        std::optional<TypeExpectation> key_expectation;
        std::optional<TypeExpectation> value_expectation;
        if (expected_type_.has_value()) {
            if (const auto *map_type = expected_type_->get().get_if<types::MapT>();
                map_type != nullptr && map_type->key != nullptr && map_type->value != nullptr) {
                key_expected = std::cref(*map_type->key);
                value_expected = std::cref(*map_type->value);
                key_expectation = derive_expectation(expectation_, map_type->key);
                value_expectation = derive_expectation(expectation_, map_type->value);
            }
        }

        if (map.entries.empty()) {
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

        for (const auto &entry : map.entries) {
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
        const auto &call = expr.as<ast::CallExpr>();
        const auto reference =
            services_.find_reference(ReferenceKind::CallTarget, call.callee->range);
        if (!reference.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::UnknownCallable,
                messages::typecheck::UnknownCallable.format_with(call.callee->spelling()),
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
        const auto &call = expr.as<ast::CallExpr>();
        const auto capability = services_.get_capability(target);
        if (!capability.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::CapabilityTypeInfoMissing.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed(false);
        }

        if (context_.call_context != CallContext::Flow) {
            services_.typecheck_error_here(
                error_codes::typecheck::CapabilityNotAllowed,
                messages::typecheck::CapabilityNotAllowed.format_with(call.callee->spelling()),
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
                            call.callee->spelling()),
                        expr.range);
                }
            }
        }

        if (call.arguments.size() != capability->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "capability",
                                               call.callee->spelling(),
                                               std::to_string(capability->get().params.size()),
                                               std::to_string(call.arguments.size())),
                                           expr.range);
        }

        const auto limit = std::min(call.arguments.size(), capability->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = capability->get().params[index];
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             call.arguments[index]->range,
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
        const auto &call = expr.as<ast::CallExpr>();
        const auto predicate = services_.get_predicate(target);
        if (!predicate.has_value()) {
            services_.typecheck_error_here(
                error_codes::typecheck::MissingCallableMetadata,
                messages::typecheck::PredicateTypeInfoMissing.format_with(call.callee->spelling()),
                expr.range);
            return values_.error_typed();
        }

        if (call.arguments.size() != predicate->get().params.size()) {
            services_.typecheck_error_here(error_codes::typecheck::WrongArity,
                                           messages::typecheck::WrongArity.format_with(
                                               "predicate",
                                               call.callee->spelling(),
                                               std::to_string(predicate->get().params.size()),
                                               std::to_string(call.arguments.size())),
                                           expr.range);
        }

        ExprEffect effect = ExprEffect::PredicateCall;
        const auto limit = std::min(call.arguments.size(), predicate->get().params.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const auto &param = predicate->get().params[index];
            const auto expectation = TypeExpectation{
                .expected = param.type,
                .origin_kind = TypeExpectationOriginKind::FunctionParameter,
                .origin_range = param.declaration_range,
                .description = "parameter '" + param.name + "'",
            };
            const auto argument =
                services_.check_expr(*call.arguments[index], context_, expectation);
            effect = join_effects(effect, argument.effect);
            if (!argument.is_pure) {
                services_.typecheck_error_here(
                    error_codes::typecheck::NonPureExpression,
                    messages::typecheck::PredicateArgsNotPure.format_with() +
                        "; expression effect: " + std::string(to_string(argument.effect)),
                    call.arguments[index]->range);
            }

            (void)services_.check_assignable(*argument.type,
                                             *param.type,
                                             call.arguments[index]->range,
                                             "predicate argument",
                                             expectation);
        }

        return values_.typed_effect(values_.make_type(TypeKind::Bool), effect);
    }

    [[nodiscard]] TypedValue check_unary_expr(const ast::ExprSyntax &expr) const {
        const auto &unary = expr.as<ast::UnaryExpr>();
        const auto operand = services_.check_expr(*unary.operand, context_, std::nullopt);
        switch (unary.op) {
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
        const auto &binary = expr.as<ast::BinaryExpr>();
        if ((binary.op == ast::ExprBinaryOp::Equal || binary.op == ast::ExprBinaryOp::NotEqual) &&
            binary.lhs && binary.rhs &&
            (binary.lhs->is<ast::NoneLiteralExpr>() || binary.rhs->is<ast::NoneLiteralExpr>())) {
            const auto &none_operand =
                binary.lhs->is<ast::NoneLiteralExpr>() ? *binary.lhs : *binary.rhs;
            const auto &value_operand =
                binary.lhs->is<ast::NoneLiteralExpr>() ? *binary.rhs : *binary.lhs;
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

        const auto lhs = services_.check_expr(*binary.lhs, context_, std::nullopt);
        const auto rhs = services_.check_expr(*binary.rhs, context_, std::nullopt);
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

        switch (binary.op) {
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

            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
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
            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
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
            if (const auto result = strict_arithmetic(binary.op); result != nullptr) {
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
        const auto &member_access = expr.as<ast::MemberAccessExpr>();
        const auto base = services_.check_expr(*member_access.base, context_, std::nullopt);
        auto member_type = services_.field_access(*base.type, member_access.member, expr.range);
        if (const auto place = place_of_expression(expr); place.has_value()) {
            member_type = services_.apply_flow_narrowing(member_type, *place, context_.flow_facts);
        }
        return values_.typed_effect(member_type, base.effect);
    }

    [[nodiscard]] TypedValue check_index_access(const ast::ExprSyntax &expr) const {
        const auto &index_access = expr.as<ast::IndexAccessExpr>();
        const auto collection = services_.check_expr(*index_access.base, context_, std::nullopt);
        const auto index = services_.check_expr(*index_access.index, context_, std::nullopt);
        const auto effect = join_effects(collection.effect, index.effect);

        if (const auto *list = collection.type->get_if<types::ListT>(); list != nullptr) {
            if (!index.type->holds<types::IntT>() && !is_error_type(*index.type)) {
                services_.typecheck_error_here(
                    error_codes::typecheck::InvalidIndexAccess,
                    messages::typecheck::ListIndexRequiresInt.format_with(),
                    index_access.index->range);
            }

            if (list->element != nullptr) {
                return values_.typed_effect(list->element->clone(), effect);
            }

            return values_.error_typed_effect(effect);
        }

        if (const auto *map = collection.type->get_if<types::MapT>(); map != nullptr) {
            if (map->key != nullptr) {
                (void)services_.check_assignable(
                    *index.type, *map->key, index_access.index->range, "map index");
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

ExpressionSema::ExpressionSema(ExpressionSemaServices services) : services_(std::move(services)) {}

ExpressionValue ExpressionSema::check(const ast::ExprSyntax &expr,
                                      const ExpressionContext &context,
                                      MaybeCRef<Type> expected_type) const {
    ExpressionCheckerServices services{
        *services_.resolve_result,
        services_.current_source_id,
        *services_.environment,
        *services_.types,
        *services_.relations,
        *services_.delegate,
    };
    return ExpressionChecker{services, context, expected_type, nullptr}.check(expr);
}

ExpressionValue ExpressionSema::check(const ast::ExprSyntax &expr,
                                      const ExpressionContext &context,
                                      const TypeExpectation &expectation) const {
    MaybeCRef<Type> expected_type = std::nullopt;
    if (expectation.expected != nullptr) {
        expected_type = std::cref(*expectation.expected);
    }
    ExpressionCheckerServices services{
        *services_.resolve_result,
        services_.current_source_id,
        *services_.environment,
        *services_.types,
        *services_.relations,
        *services_.delegate,
    };
    return ExpressionChecker{services, context, expected_type, &expectation}.check(expr);
}

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

    class PassExpressionSemaDelegate final : public ExpressionSemaDelegate {
      public:
        PassExpressionSemaDelegate(TypeCheckPass &pass, TypeResolver &type_resolver)
            : pass_(&pass), type_resolver_(&type_resolver) {}

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range) override {
            pass_->typecheck_error_here(code, std::move(message), range);
        }

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range,
                             std::vector<Diagnostic::Related> notes) override {
            pass_->typecheck_error_here(code, std::move(message), range, std::move(notes));
        }

        ExpressionValue check_nested(const ast::ExprSyntax &nested_expr,
                                     const ExpressionContext &nested_context,
                                     MaybeCRef<Type> nested_expected) override {
            return pass_->check_expr(nested_expr, nested_context, nested_expected);
        }

        ExpressionValue check_nested(const ast::ExprSyntax &nested_expr,
                                     const ExpressionContext &nested_context,
                                     const TypeExpectation &nested_expectation) override {
            return pass_->check_expr(nested_expr, nested_context, nested_expectation);
        }

        TypePtr resolve_type_symbol(SymbolId id, SourceRange range) override {
            return type_resolver_->resolve_type_symbol(id, range);
        }

      private:
        TypeCheckPass *pass_{nullptr};
        TypeResolver *type_resolver_{nullptr};
    };

    PassExpressionSemaDelegate delegate{*this, type_resolver};
    ExpressionSema sema{ExpressionSemaServices{
        .resolve_result = &resolve_result_,
        .current_source_id = current_source_id_,
        .environment = &result_.environment,
        .types = types_,
        .relations = &relations_,
        .delegate = &delegate,
    }};
    if (expectation != nullptr) {
        return sema.check(expr, context, *expectation);
    }
    return sema.check(expr, context, expected_type);
}

TypedValue TypeCheckPass::check_path(const ast::PathSyntax &path, const ValueContext &context) {
    class PathDelegate final : public ExpressionSemaDelegate {
      public:
        explicit PathDelegate(TypeCheckPass &pass) : pass_(&pass) {}

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range) override {
            pass_->typecheck_error_here(code, std::move(message), range);
        }

        void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                             std::string message,
                             SourceRange range,
                             std::vector<Diagnostic::Related> notes) override {
            pass_->typecheck_error_here(code, std::move(message), range, std::move(notes));
        }

        ExpressionValue
        check_nested(const ast::ExprSyntax &, const ExpressionContext &, MaybeCRef<Type>) override {
            return ExpressionValue{
                .type = pass_->make_error_type(),
                .effect = ExprEffect::Pure,
                .is_pure = true,
                .path_root_kind = std::nullopt,
            };
        }

        ExpressionValue check_nested(const ast::ExprSyntax &,
                                     const ExpressionContext &,
                                     const TypeExpectation &) override {
            return ExpressionValue{
                .type = pass_->make_error_type(),
                .effect = ExprEffect::Pure,
                .is_pure = true,
                .path_root_kind = std::nullopt,
            };
        }

        TypePtr resolve_type_symbol(SymbolId id, SourceRange range) override {
            return pass_->resolve_type_symbol(id, range);
        }

      private:
        TypeCheckPass *pass_{nullptr};
    };

    PathDelegate delegate{*this};
    return resolve_expression_path(path, context, result_.environment, *types_, delegate);
}

} // namespace ahfl
