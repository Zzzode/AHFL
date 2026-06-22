#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_expectation.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl {

class TypeContext;
class TypeEnvironment;
class TypeRelationContext;
namespace ast {
struct ExprSyntax;
struct PathSyntax;
} // namespace ast

using ExpressionBindingMap = std::unordered_map<std::string, TypePtr>;
enum class ExpressionCallContext {
    PureOnly,
    Flow,
    Workflow,
};

// P4a (RFC corelib-effect-system.zh.md §4.5): distinguishes "regular"
// pure-only contexts (if conditions, assert) from verified-subset contexts
// (contract clauses, invariant/safety/liveness formulas). The latter get
// richer diagnostics (EffectNotPure / NotInVerifiedSubset / NondetInInvariant)
// and stricter rules (no Nondet in temporal formulas).
enum class VerificationContext {
    None,
    Contract,
    InvariantSafetyLiveness,
};

struct ExpressionValue {
    TypePtr type;
    ExprEffect effect{ExprEffect::Pure};
    bool is_pure{true};
    std::optional<AssignTargetRootKind> path_root_kind;
};

struct ExpressionContext {
    ExpressionBindingMap bindings;
    FlowFacts flow_facts;
    ExpressionCallContext call_context{ExpressionCallContext::PureOnly};
    std::optional<SymbolId> current_agent;
    // P4a (RFC corelib-effect-system.zh.md §4.5): verified-subset context.
    // When != None, the expression typechecker emits richer effect diagnostics
    // (EffectNotPure / NotInVerifiedSubset / NondetInInvariant) for function
    // calls that violate the subset rules.
    VerificationContext verification_context{VerificationContext::None};
};

class ExpressionSemaDelegate {
  public:
    virtual ~ExpressionSemaDelegate() = default;

    virtual void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                 std::string message,
                                 SourceRange range) = 0;
    virtual void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                 std::string message,
                                 SourceRange range,
                                 std::vector<Diagnostic::Related> notes) = 0;
    [[nodiscard]] virtual ExpressionValue check_nested(const ast::ExprSyntax &expr,
                                                       const ExpressionContext &context,
                                                       MaybeCRef<Type> expected_type) = 0;
    [[nodiscard]] virtual ExpressionValue check_nested(const ast::ExprSyntax &expr,
                                                       const ExpressionContext &context,
                                                       const TypeExpectation &expectation) = 0;
    [[nodiscard]] virtual TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range) = 0;
    // P2 (RFC §6): resolve a closure parameter's optional type annotation.
    // Returns the error type when the annotation is absent (the closure
    // typecheck pass then treats the parameter as unannotated).
    [[nodiscard]] virtual TypePtr
    resolve_type_syntax(const ast::TypeSyntax &type) = 0;
    // P2c (RFC §3.5): record a resolved fn call site for the
    // monomorphization pass. Explicit type arguments are empty today (the
    // grammar's call surface does not yet carry `foo<T>(...)` syntax), so the
    // recorded entry ties the call range to the resolved fn symbol with an
    // empty type-args list; the monomorphization pass consumes it as the
    // instantiation surface input.
    virtual void record_fn_call_site(SymbolId fn_symbol,
                                     SourceRange call_range,
                                     std::vector<TypePtr> type_args) = 0;
};

struct ExpressionSemaServices {
    const ResolveResult *resolve_result{nullptr};
    std::optional<SourceId> current_source_id;
    const TypeEnvironment *environment{nullptr};
    TypeContext *types{nullptr};
    TypeRelationContext *relations{nullptr};
    ExpressionSemaDelegate *delegate{nullptr};
};

class ExpressionSema final {
  public:
    explicit ExpressionSema(ExpressionSemaServices services);

    [[nodiscard]] ExpressionValue check(const ast::ExprSyntax &expr,
                                        const ExpressionContext &context,
                                        MaybeCRef<Type> expected_type = std::nullopt) const;
    [[nodiscard]] ExpressionValue check(const ast::ExprSyntax &expr,
                                        const ExpressionContext &context,
                                        const TypeExpectation &expectation) const;

  private:
    ExpressionSemaServices services_;
};

class ExpressionValueFactory final {
  public:
    explicit ExpressionValueFactory(TypeContext &types) noexcept;

    [[nodiscard]] TypePtr make_type(TypeKind kind) const;
    [[nodiscard]] TypePtr string_type() const;
    [[nodiscard]] TypePtr decimal_type(std::int64_t scale) const;
    [[nodiscard]] TypePtr make_error_type() const;
    [[nodiscard]] TypePtr optional_type(TypePtr value_type) const;
    [[nodiscard]] TypePtr list_type(TypePtr element_type) const;
    [[nodiscard]] TypePtr set_type(TypePtr element_type) const;
    [[nodiscard]] TypePtr map_type(TypePtr key_type, TypePtr value_type) const;
    [[nodiscard]] TypePtr clone_or_any(MaybeCRef<Type> type) const;

    [[nodiscard]] ExpressionValue typed(TypePtr type, bool is_pure = true) const;
    [[nodiscard]] ExpressionValue typed_effect(TypePtr type, ExprEffect effect) const;
    [[nodiscard]] ExpressionValue error_typed(bool is_pure = true) const;
    [[nodiscard]] ExpressionValue error_typed_effect(ExprEffect effect) const;

  private:
    TypeContext *types_{nullptr};
};

[[nodiscard]] std::optional<Place> place_of_expression(const ast::ExprSyntax &expr);

[[nodiscard]] AssignTargetRootKind classify_expression_path_root(const ast::PathSyntax &path,
                                                                 const ExpressionContext &context);

[[nodiscard]] TypePtr apply_expression_flow_narrowing(TypePtr type,
                                                      const Place &place,
                                                      const FlowFacts &facts,
                                                      const TypeEnvironment &environment,
                                                      TypeContext &types);

[[nodiscard]] TypePtr resolve_expression_field_access(const Type &base_type,
                                                      std::string_view field_name,
                                                      SourceRange range,
                                                      const TypeEnvironment &environment,
                                                      TypeContext &types,
                                                      ExpressionSemaDelegate &delegate);

[[nodiscard]] ExpressionValue resolve_expression_path(const ast::PathSyntax &path,
                                                      const ExpressionContext &context,
                                                      const TypeEnvironment &environment,
                                                      TypeContext &types,
                                                      ExpressionSemaDelegate &delegate);

} // namespace ahfl
