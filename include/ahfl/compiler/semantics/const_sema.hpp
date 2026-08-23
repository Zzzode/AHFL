#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_expectation.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl {

class TypeEnvironment;
struct SourceUnit;
struct SourceGraph;

struct ConstExprSyntaxResult {
    bool is_const{false};
    std::string_view reason;
};

enum class ConstExprGateStatus {
    Accepted,
    RuntimeEffect,
    UnsupportedSyntax,
};

struct ConstExprGateResult {
    ConstExprGateStatus status{ConstExprGateStatus::UnsupportedSyntax};
    std::string reason;
};

enum class ConstEvalKind {
    KnownConst,
    NotConst,
    Error,
};

struct ConstEvalOutcome {
    ConstEvalKind kind{ConstEvalKind::Error};
    std::optional<ConstValue> const_value;
};

struct ConstCheckedExpression {
    TypePtr type;
    ExprEffect effect{ExprEffect::Unknown};
    bool is_pure{false};
};

[[nodiscard]] ConstEvalOutcome const_eval_known(std::optional<ConstValue> const_value);
[[nodiscard]] ConstEvalOutcome const_eval_not_const();
[[nodiscard]] ConstEvalOutcome const_eval_error();

using ConstDiagnosticRelated = Diagnostic::Related;

struct ConstTypeCheckDiagnostic {
    ErrorCode<DiagnosticCategory::TypeCheck> code{error_codes::typecheck::SemanticError};
    std::string message;
    std::vector<ConstDiagnosticRelated> related;
};

using ConstDependencyCycleDiagnostic = ConstTypeCheckDiagnostic;
using ConstExprRequiredDiagnostic = ConstTypeCheckDiagnostic;

struct ConstDiagnosticReport {
    ConstTypeCheckDiagnostic diagnostic;
    SourceRange range;
};

class ConstDiagnosticEmitter final {
  public:
    ConstDiagnosticEmitter(DiagnosticBag &diagnostics, const SourceFile *source);

    void emit(ConstDiagnosticReport report) const;
    void emit_all(std::vector<ConstDiagnosticReport> reports) const;

  private:
    DiagnosticBag &diagnostics_;
    const SourceFile *source_{nullptr};
};

enum class ConstStructDefaultValidationKind {
    Assignable,
    ExactAgentContextSchema,
};

struct ConstStructDefaultValidationPolicy {
    ConstStructDefaultValidationKind kind{ConstStructDefaultValidationKind::Assignable};
    std::string_view context_label{"struct field default"};
    TypeExpectation expectation;
    std::optional<SchemaBoundaryKind> schema_boundary;
};

struct ConstInitializerValidationPolicy {
    std::string_view context_label{"const initializer"};
    TypeExpectation expectation;
};

class ConstTypeRelationValidator final {
  public:
    ConstTypeRelationValidator(TypeRelationContext &relations, ConstDiagnosticEmitter diagnostics);
    ConstTypeRelationValidator(TypeRelationContext &relations,
                               ConstDiagnosticEmitter diagnostics,
                               const SymbolTable *symbols);

    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label,
                                        const TypeExpectation &expectation) const;
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range,
                                                   const TypeExpectation &expectation) const;
    [[nodiscard]] bool check_struct_default(const Type &source,
                                            const Type &target,
                                            SourceRange range,
                                            const ConstStructDefaultValidationPolicy &policy) const;

  private:
    TypeRelationContext &relations_;
    ConstDiagnosticEmitter diagnostics_;
    const SymbolTable *symbols_{nullptr};
};

[[nodiscard]] ConstExprSyntaxResult classify_const_expr_syntax(const ast::ExprSyntax &expr);
[[nodiscard]] ConstExprGateResult
classify_const_expr_gate(const ast::ExprSyntax &expr, bool is_pure, ExprEffect effect);
[[nodiscard]] ConstInitializerValidationPolicy describe_const_initializer_validation(
    const Type &declared_type, SourceRange annotation_range, std::string_view const_name);
[[nodiscard]] ConstStructDefaultValidationPolicy classify_const_struct_default_validation(
    const Type &field_type, SourceRange field_range, bool is_agent_context_struct);
[[nodiscard]] ConstExprRequiredDiagnostic
describe_const_expr_required(std::string_view context_label, const ConstExprGateResult &gate);
[[nodiscard]] ConstDependencyCycleDiagnostic describe_const_dependency_cycle(const Symbol *symbol);

void add_const_child(ConstValue &value, std::string name, ConstValue child);
[[nodiscard]] const ConstValue &effective_const_value(const ConstValue &value);
[[nodiscard]] ConstValue duration_const_value(std::string_view text);
[[nodiscard]] bool const_values_equal(const ConstValue &lhs, const ConstValue &rhs);
[[nodiscard]] std::string const_value_sort_key(const ConstValue &value);
void normalize_set_const_value(ConstValue &value);
void normalize_map_const_value(ConstValue &value);
[[nodiscard]] std::optional<ConstValue> fold_unary_const(ast::ExprUnaryOp op,
                                                         const ConstValue &operand);
[[nodiscard]] std::optional<ConstValue>
fold_binary_const(ast::ExprBinaryOp op, const ConstValue &lhs, const ConstValue &rhs);
[[nodiscard]] std::optional<ConstValue> fold_member_const(const ConstValue &base,
                                                          std::string_view member);
[[nodiscard]] std::optional<ConstValue> fold_index_const(const ConstValue &base,
                                                         const ConstValue &index_value);

class ConstEvaluator final {
  public:
    ConstEvaluator(const ResolveResult &resolve_result,
                   std::optional<SourceId> source_id,
                   const std::unordered_map<std::size_t, ConstValue> &const_values);

    [[nodiscard]] std::optional<ConstValue> evaluate(const ast::ExprSyntax &expr) const;

  private:
    const ResolveResult &resolve_result_;
    std::optional<SourceId> source_id_;
    const std::unordered_map<std::size_t, ConstValue> &const_values_;
};

class ConstValueTreeRecorder final {
  public:
    using RememberConstValueFn = std::function<void(const ast::ExprSyntax &, const ConstValue &)>;

    ConstValueTreeRecorder(const ResolveResult &resolve_result,
                           std::optional<SourceId> source_id,
                           const std::unordered_map<std::size_t, ConstValue> &const_values,
                           RememberConstValueFn remember_const_value);

    [[nodiscard]] ConstEvalOutcome evaluate_and_record(const ast::ExprSyntax &expr) const;
    void record(const ast::ExprSyntax &expr) const;

  private:
    void record_children(const ast::ExprSyntax &expr) const;

    ConstEvaluator evaluator_;
    RememberConstValueFn remember_const_value_;
};

enum class ConstValueResolutionStatus {
    Known,
    Failed,
    Cycle,
    Entered,
};

struct ConstValueResolutionBeginResult {
    ConstValueResolutionStatus status{ConstValueResolutionStatus::Failed};
    std::vector<ConstDiagnosticReport> diagnostics;
};

class ConstValueResolutionState final {
  public:
    ConstValueResolutionState(std::unordered_map<std::size_t, ConstValue> &const_values,
                              std::unordered_set<std::size_t> &active_const_values,
                              std::unordered_set<std::size_t> &failed_const_values);

    [[nodiscard]] ConstValueResolutionBeginResult
    begin(SymbolId id, const Symbol *symbol, SourceRange use_range);
    [[nodiscard]] bool fail(SymbolId id);
    [[nodiscard]] bool
    finish_after_assignability(SymbolId id, const ConstEvalOutcome &outcome, bool type_assignable);

  private:
    [[nodiscard]] bool finish(SymbolId id, bool ok);
    void remember(SymbolId id, ConstValue value);

    std::unordered_map<std::size_t, ConstValue> &const_values_;
    std::unordered_set<std::size_t> &active_const_values_;
    std::unordered_set<std::size_t> &failed_const_values_;
};

struct ConstDependencyReference {
    SymbolId target;
    SourceRange use_range;
    SourceRange reference_range;
    std::optional<SourceId> source_id;
};

class ConstDependencyResolver final {
  public:
    ConstDependencyResolver(const ResolveResult &resolve_result, std::optional<SourceId> source_id);

    [[nodiscard]] std::vector<ConstDependencyReference> collect(const ast::ExprSyntax &expr) const;

  private:
    void collect_into(const ast::ExprSyntax &expr,
                      std::vector<ConstDependencyReference> &references) const;

    const ResolveResult &resolve_result_;
    std::optional<SourceId> source_id_;
};

class ConstDependencyScheduler final {
  public:
    using EnsureConstValueFn = std::function<bool(SymbolId, SourceRange)>;

    ConstDependencyScheduler(const ResolveResult &resolve_result,
                             std::optional<SourceId> source_id,
                             std::vector<ConstDependencyEdge> &dependency_edges,
                             EnsureConstValueFn ensure_const_value);

    [[nodiscard]] bool ensure(std::optional<SymbolId> source_const,
                              const ast::ExprSyntax &expr) const;

  private:
    const ResolveResult &resolve_result_;
    std::optional<SourceId> source_id_;
    std::vector<ConstDependencyEdge> &dependency_edges_;
    EnsureConstValueFn ensure_const_value_;
};

struct ConstEvalPipelineResult {
    ConstEvalOutcome outcome;
    std::vector<ConstDiagnosticReport> diagnostics;
};

struct ConstExpressionResult {
    ConstEvalOutcome outcome;
    ConstCheckedExpression checked_expr;
};

class ConstEvalPipeline final {
  public:
    using EnsureConstValueFn = ConstDependencyScheduler::EnsureConstValueFn;
    using RememberConstValueFn = ConstValueTreeRecorder::RememberConstValueFn;

    ConstEvalPipeline(const ResolveResult &resolve_result,
                      std::optional<SourceId> source_id,
                      std::vector<ConstDependencyEdge> &dependency_edges,
                      const std::unordered_map<std::size_t, ConstValue> &const_values,
                      EnsureConstValueFn ensure_const_value,
                      RememberConstValueFn remember_const_value);

    [[nodiscard]] ConstEvalPipelineResult evaluate(const ast::ExprSyntax &expr,
                                                   const ConstCheckedExpression &checked_expr,
                                                   std::string_view context_label,
                                                   std::optional<SymbolId> source_const) const;

  private:
    const ResolveResult &resolve_result_;
    std::optional<SourceId> source_id_;
    std::vector<ConstDependencyEdge> &dependency_edges_;
    const std::unordered_map<std::size_t, ConstValue> &const_values_;
    EnsureConstValueFn ensure_const_value_;
    RememberConstValueFn remember_const_value_;
};

class ConstExpressionDriver final {
  public:
    using CheckExpressionFn =
        std::function<ConstCheckedExpression(const ast::ExprSyntax &, MaybeCRef<Type>)>;

    ConstExpressionDriver(ConstEvalPipeline const_eval,
                          ConstDiagnosticEmitter diagnostics,
                          CheckExpressionFn check_expression);

    [[nodiscard]] ConstExpressionResult evaluate(const ast::ExprSyntax &expr,
                                                 MaybeCRef<Type> expected_type,
                                                 std::string_view context_label,
                                                 std::optional<SymbolId> source_const) const;

  private:
    ConstEvalPipeline const_eval_;
    ConstDiagnosticEmitter diagnostics_;
    CheckExpressionFn check_expression_;
};

// ============================================================================
// ConstSema dependency seam
// ============================================================================
//
// ConstSema used to hold a raw `TypeCheckPass *driver_` and reach into ~30 of
// its members directly. This mirrors LLVM/Clang Sema owners that were coupled
// to the whole Sema object. Following the ExpressionSema / ExpressionSemaDelegate
// pattern already used in this subsystem, ConstSema now depends only on an
// explicit accessor interface (ConstSemaDelegate). TypeCheckPass provides a
// PassConstSemaDelegate implementation (see typecheck.cpp) that forwards each
// call to the appropriate pass member. ConstSema no longer names TypeCheckPass.
//
// The interface is deliberately narrow: it exposes exactly the source-context,
// symbol/decl-table, environment, relation, and diagnostic surfaces that the
// const-initializer / struct-default / enum-variant-default / agent-context
// checks require, plus a single check_expression hook that drives the shared
// expression typechecker. The const-value evaluation core (ConstEvaluator,
// ConstEvalPipeline, ...) is already fully decoupled and takes explicit
// ResolveResult / DiagnosticBag / SourceId dependencies.

using ConstDeclRefMap =
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::ConstDecl>>;
using StructDeclRefMap =
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::StructDecl>>;
using EnumDeclRefMap =
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::EnumDecl>>;

class ConstSemaDelegate {
  public:
    virtual ~ConstSemaDelegate() = default;

    // --- Source-context management -----------------------------------------
    // The driver keeps a moving "current source" cursor (source unit + id +
    // module name). ConstSema pushes/pops it while walking a multi-source graph
    // and reads it back when constructing diagnostics.
    [[nodiscard]] virtual const SourceGraph *graph() const = 0;
    [[nodiscard]] virtual const ast::Program *program() const = 0;
    virtual void enter_source(const SourceUnit &source) = 0;
    virtual void leave_source() = 0;
    // Runs `body` with the given symbol's defining source/module as the current
    // context, restoring the previous context afterwards (mirrors
    // TypeCheckPass::with_symbol_context). Non-template so it can be virtual;
    // callers that need a result capture it through the closure.
    virtual void with_symbol_context(SymbolId id, const std::function<void()> &body) = 0;
    [[nodiscard]] virtual const SourceFile *current_source_file() const = 0;
    [[nodiscard]] virtual std::optional<SourceId> current_source_id() const = 0;

    // --- Symbol / declaration-table access ---------------------------------
    [[nodiscard]] virtual MaybeCRef<Symbol> symbol_of(SymbolId id) const = 0;
    [[nodiscard]] virtual MaybeCRef<Symbol> find_local_here(SymbolNamespace name_space,
                                                            std::string_view name) const = 0;
    [[nodiscard]] virtual MaybeCRef<ast::ConstDecl> const_decl(SymbolId id) const = 0;
    [[nodiscard]] virtual const StructDeclRefMap &struct_decls() const = 0;
    [[nodiscard]] virtual const EnumDeclRefMap &enum_decls() const = 0;

    // --- Type / relation / symbol-table surfaces ---------------------------
    [[nodiscard]] virtual const TypeEnvironment &environment() const = 0;
    [[nodiscard]] virtual TypeRelationContext &relations() = 0;
    [[nodiscard]] virtual const SymbolTable &symbol_table() const = 0;
    [[nodiscard]] virtual const ResolveResult &resolve_result() const = 0;
    [[nodiscard]] virtual TypedProgram &typed_program() = 0;

    // --- Diagnostics -------------------------------------------------------
    [[nodiscard]] virtual DiagnosticBag &diagnostics() = 0;
    virtual void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                      std::string message,
                                      SourceRange range) = 0;

    // --- Expression typechecking hook --------------------------------------
    // Drives the shared expression typechecker for a const-position expression
    // with an implicit default value context and the supplied expected type.
    [[nodiscard]] virtual ConstCheckedExpression
    check_expression(const ast::ExprSyntax &expr, MaybeCRef<Type> expected_type) = 0;
};

class ConstSema final {
  public:
    explicit ConstSema(ConstSemaDelegate &delegate) : delegate_(&delegate) {}

    void run();

  private:
    ConstSemaDelegate *delegate_{nullptr};
    std::unordered_map<std::size_t, ConstValue> const_values_;
    std::unordered_set<std::size_t> active_const_values_;
    std::unordered_set<std::size_t> failed_const_values_;

    [[nodiscard]] ConstDiagnosticEmitter make_diagnostic_emitter() const;

    void remember_const_value(const ast::ExprSyntax &expr, const ConstValue &value);
    [[nodiscard]] bool ensure_const_value(SymbolId id, SourceRange use_range);
    [[nodiscard]] ConstExpressionResult
    check_const_expr(const ast::ExprSyntax &expr,
                     MaybeCRef<Type> expected_type,
                     std::string_view context_label,
                     std::optional<SymbolId> source_const = std::nullopt);
    void check_const_initializers_in_program(const ast::Program &program);
    void check_const_initializers();
    void check_enum_variant_defaults();
    void check_struct_defaults();
    void check_agent_context_defaults();
};

} // namespace ahfl
