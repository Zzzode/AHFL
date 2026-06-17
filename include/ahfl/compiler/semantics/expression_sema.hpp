#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_expectation.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl {

class TypeContext;
class TypeEnvironment;
namespace ast {
struct ExprSyntax;
struct PathSyntax;
} // namespace ast

using ExpressionBindingMap = std::unordered_map<std::string, TypePtr>;
using ExpressionTypecheckErrorSink =
    std::function<void(ErrorCode<DiagnosticCategory::TypeCheck>, std::string, SourceRange)>;
using ExpressionTypecheckDiagnosticSink =
    std::function<void(ErrorCode<DiagnosticCategory::TypeCheck>,
                       std::string,
                       SourceRange,
                       std::vector<Diagnostic::Related>)>;
using ExpressionTypeSymbolResolver = std::function<TypePtr(SymbolId, SourceRange)>;

enum class ExpressionCallContext {
    PureOnly,
    Flow,
    Workflow,
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
};

using ExpressionNestedChecker = std::function<ExpressionValue(
    const ast::ExprSyntax &, const ExpressionContext &, MaybeCRef<Type>)>;
using ExpressionExpectedNestedChecker = std::function<ExpressionValue(
    const ast::ExprSyntax &, const ExpressionContext &, const TypeExpectation &)>;

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
                                                      const ExpressionTypecheckErrorSink &diagnose);

[[nodiscard]] ExpressionValue resolve_expression_path(const ast::PathSyntax &path,
                                                      const ExpressionContext &context,
                                                      const TypeEnvironment &environment,
                                                      TypeContext &types,
                                                      const ExpressionTypecheckErrorSink &diagnose);

} // namespace ahfl
