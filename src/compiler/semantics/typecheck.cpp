#include "ahfl/compiler/semantics/typecheck.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/condition_facts.hpp"
#include "ahfl/compiler/semantics/const_sema.hpp"
#include "ahfl/compiler/semantics/name_suggestions.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"

#include "compiler/semantics/typecheck_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

// Hoist internal helpers into ahfl::-scope so the existing implementation can
// keep using their unqualified names.
using internal::BindingMap;
using internal::CallContext;
using internal::ConstEvalResult;
using internal::find_binding;
using internal::find_decl_ref;
using internal::is_bool_type;
using internal::is_error_type;
using internal::is_numeric_type;
using internal::same_range;
using internal::TypedValue;
using internal::ValueContext;

namespace {

void append_typed_child(std::vector<TypedExprChild> &children,
                        const TypedProgram &program,
                        const ast::ExprSyntax *child,
                        std::optional<SourceId> source_id,
                        TypedExprChildRole role,
                        std::string name = {}) {
    if (child == nullptr) {
        return;
    }

    std::uint32_t child_index = UINT32_MAX;
    if (child->node_id != 0) {
        if (const TypedExpr *typed = program.find_expr(child->node_id, source_id);
            typed != nullptr && !program.expressions.empty()) {
            const auto offset = typed - program.expressions.data();
            if (offset >= 0 && static_cast<std::size_t>(offset) < program.expressions.size()) {
                child_index = static_cast<std::uint32_t>(offset);
            }
        }
    }
    if (child_index == UINT32_MAX) {
        for (std::size_t i = 0; i < program.expressions.size(); ++i) {
            if (program.expressions[i].source_id == source_id &&
                same_range(program.expressions[i].range, child->range)) {
                child_index = static_cast<std::uint32_t>(i);
                break;
            }
        }
    }
    if (child_index == UINT32_MAX) {
        return;
    }

    children.push_back(TypedExprChild{
        .role = role,
        .name = std::move(name),
        .expr_index = child_index,
    });
}

[[nodiscard]] std::vector<TypedExprChild> typed_children_for(const ast::ExprSyntax &expr,
                                                             const TypedProgram &program,
                                                             const ResolveResult &resolve_result,
                                                             const TypeEnvironment &environment,
                                                             std::optional<SourceId> source_id) {
    std::vector<TypedExprChild> children;
    std::visit(
        Overloaded{
            [&](const ast::NoneLiteralExpr &) {},
            [&](const ast::BoolLiteralExpr &) {},
            [&](const ast::IntegerLiteralExpr &) {},
            [&](const ast::FloatLiteralExpr &) {},
            [&](const ast::DecimalLiteralExpr &) {},
            [&](const ast::StringLiteralExpr &) {},
            [&](const ast::DurationLiteralExpr &) {},
            [&](const ast::SomeExpr &e) {
                append_typed_child(
                    children, program, e.value.get(), source_id, TypedExprChildRole::Operand);
            },
            [&](const ast::PathExpr &) {},
            [&](const ast::QualifiedValueExpr &) {},
            [&](const ast::CallExpr &e) {
                std::vector<std::string> parameter_names;
                if (e.callee != nullptr) {
                    // P2 (RFC §3.2.2): a fn call carries its parameter names on
                    // the FnTypeInfo. Check it first, then fall back to the
                    // legacy capability/predicate call targets.
                    if (const auto fn_reference = resolve_result.find_reference(
                            ReferenceKind::FnCallTarget, e.callee->range, source_id);
                        fn_reference.has_value()) {
                        if (const auto fn = environment.get_fn(fn_reference->get().target);
                            fn.has_value()) {
                            parameter_names.reserve(fn->get().params.size());
                            for (const auto &param : fn->get().params) {
                                parameter_names.push_back(param.name);
                            }
                        }
                    } else if (const auto reference = resolve_result.find_reference(
                                   ReferenceKind::CallTarget, e.callee->range, source_id);
                               reference.has_value()) {
                        if (const auto capability =
                                environment.get_capability(reference->get().target);
                            capability.has_value()) {
                            parameter_names.reserve(capability->get().params.size());
                            for (const auto &param : capability->get().params) {
                                parameter_names.push_back(param.name);
                            }
                        } else if (const auto predicate =
                                       environment.get_predicate(reference->get().target);
                                   predicate.has_value()) {
                            parameter_names.reserve(predicate->get().params.size());
                            for (const auto &param : predicate->get().params) {
                                parameter_names.push_back(param.name);
                            }
                        }
                    }
                }
                for (std::size_t index = 0; index < e.arguments.size(); ++index) {
                    append_typed_child(children,
                                       program,
                                       e.arguments[index].get(),
                                       source_id,
                                       TypedExprChildRole::Argument,
                                       index < parameter_names.size() ? parameter_names[index]
                                                                      : std::string{});
                }
            },
            [&](const ast::StructLiteralExpr &e) {
                for (const auto &field : e.fields) {
                    append_typed_child(children,
                                       program,
                                       field->value.get(),
                                       source_id,
                                       TypedExprChildRole::StructFieldValue,
                                       field->field_name);
                }
            },
            [&](const ast::ListLiteralExpr &e) {
                for (const auto &item : e.items) {
                    append_typed_child(children,
                                       program,
                                       item.get(),
                                       source_id,
                                       TypedExprChildRole::CollectionElement);
                }
            },
            [&](const ast::SetLiteralExpr &e) {
                for (const auto &item : e.items) {
                    append_typed_child(children,
                                       program,
                                       item.get(),
                                       source_id,
                                       TypedExprChildRole::CollectionElement);
                }
            },
            [&](const ast::MapLiteralExpr &e) {
                for (const auto &entry : e.entries) {
                    append_typed_child(
                        children, program, entry->key.get(), source_id, TypedExprChildRole::MapKey);
                    append_typed_child(children,
                                       program,
                                       entry->value.get(),
                                       source_id,
                                       TypedExprChildRole::MapValue);
                }
            },
            [&](const ast::UnaryExpr &e) {
                append_typed_child(
                    children, program, e.operand.get(), source_id, TypedExprChildRole::Operand);
            },
            [&](const ast::BinaryExpr &e) {
                append_typed_child(
                    children, program, e.lhs.get(), source_id, TypedExprChildRole::LeftOperand);
                append_typed_child(
                    children, program, e.rhs.get(), source_id, TypedExprChildRole::RightOperand);
            },
            [&](const ast::MemberAccessExpr &e) {
                append_typed_child(
                    children, program, e.base.get(), source_id, TypedExprChildRole::Base);
            },
            [&](const ast::IndexAccessExpr &e) {
                append_typed_child(
                    children, program, e.base.get(), source_id, TypedExprChildRole::Base);
                append_typed_child(
                    children, program, e.index.get(), source_id, TypedExprChildRole::Index);
            },
            [&](const ast::GroupExpr &e) {
                append_typed_child(
                    children, program, e.inner.get(), source_id, TypedExprChildRole::Grouped);
            },
            [&](const ast::MatchExpr &e) {
                // P1 (ADT): match is a primaryExpr. Its scrutinee and every
                // arm's guard/body participate as typed children so the
                // typed-tree graph covers all match sub-expressions. Patterns
                // are not expressions and are not tracked here (they are
                // handled by the match typecheck pass directly).
                if (e.scrutinee) {
                    append_typed_child(children,
                                       program,
                                       e.scrutinee.get(),
                                       source_id,
                                       TypedExprChildRole::Operand);
                }
                for (const auto &arm : e.arms) {
                    if (arm->guard) {
                        append_typed_child(children,
                                           program,
                                           arm->guard.get(),
                                           source_id,
                                           TypedExprChildRole::MatchArmGuard);
                    }
                    if (arm->body) {
                        append_typed_child(children,
                                           program,
                                           arm->body.get(),
                                           source_id,
                                           TypedExprChildRole::MatchArmBody);
                    }
                }
            },
            [&](const ast::LambdaExpr &e) {
                // P2 (RFC §6): a closure's body is its only expression child.
                // Parameters are not expressions (their optional type
                // annotations are tracked by the fn typecheck pass, P2b), so
                // only the body participates in the typed-tree graph here.
                if (e.body) {
                    append_typed_child(children,
                                       program,
                                       e.body.get(),
                                       source_id,
                                       TypedExprChildRole::Operand);
                }
            },
        },
        expr.node);
    return children;
}

[[nodiscard]] std::optional<Place> place_of_expr(const ast::ExprSyntax &expr) {
    return std::visit(Overloaded{
                          [](const ast::GroupExpr &e) -> std::optional<Place> {
                              if (e.inner)
                                  return place_of_expr(*e.inner);
                              return std::nullopt;
                          },
                          [](const ast::MemberAccessExpr &e) -> std::optional<Place> {
                              if (!e.base)
                                  return std::nullopt;
                              auto place = place_of_expr(*e.base);
                              if (!place.has_value())
                                  return std::nullopt;
                              place->members.push_back(e.member);
                              return place;
                          },
                          [](const ast::PathExpr &e) -> std::optional<Place> {
                              if (!e.path)
                                  return std::nullopt;
                              return Place{.root = e.path->root_name, .members = e.path->members};
                          },
                          [](const auto &) -> std::optional<Place> { return std::nullopt; },
                      },
                      expr.node);
}

[[nodiscard]] std::string path_spelling(const ast::PathSyntax &path) {
    std::string spelling = path.root_name;
    for (const auto &member : path.members) {
        spelling += ".";
        spelling += member;
    }
    return spelling;
}

// Map an AST PathSyntax root_kind + root_name to AssignTargetRootKind.
// The simple enum mapping is: ast PathRootKind directly maps for Input/Output;
// for Identifier we further classify by the well-known root names that AHFL
// semantics recognise (ctx → Context, state → State) so typed-tree lowering
// can reconstruct an ir::Path without re-reading the AST.
[[nodiscard]] AssignTargetRootKind
assign_target_root_kind_of(const ast::PathSyntax &path) noexcept {
    switch (path.root_kind) {
    case ast::PathRootKind::Input:
        return AssignTargetRootKind::Input;
    case ast::PathRootKind::Output:
        return AssignTargetRootKind::Output;
    case ast::PathRootKind::Identifier:
        if (path.root_name == "ctx")
            return AssignTargetRootKind::Context;
        if (path.root_name == "state")
            return AssignTargetRootKind::State;
        return AssignTargetRootKind::Identifier;
    }
    return AssignTargetRootKind::Identifier;
}

[[nodiscard]] std::string place_spelling(const Place &place) {
    std::string spelling = place.root;
    for (const auto &member : place.members) {
        spelling += ".";
        spelling += member;
    }
    return spelling;
}

[[nodiscard]] std::string_view binary_op_spelling(ast::ExprBinaryOp op) noexcept {
    switch (op) {
    case ast::ExprBinaryOp::Implies:
        return "=>";
    case ast::ExprBinaryOp::Or:
        return "||";
    case ast::ExprBinaryOp::And:
        return "&&";
    case ast::ExprBinaryOp::Equal:
        return "==";
    case ast::ExprBinaryOp::NotEqual:
        return "!=";
    case ast::ExprBinaryOp::Less:
        return "<";
    case ast::ExprBinaryOp::LessEqual:
        return "<=";
    case ast::ExprBinaryOp::Greater:
        return ">";
    case ast::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ast::ExprBinaryOp::Add:
        return "+";
    case ast::ExprBinaryOp::Subtract:
        return "-";
    case ast::ExprBinaryOp::Multiply:
        return "*";
    case ast::ExprBinaryOp::Divide:
        return "/";
    case ast::ExprBinaryOp::Modulo:
        return "%";
    }
    return "?";
}

[[nodiscard]] std::string expr_spelling(const ast::ExprSyntax &expr) {
    return std::visit(
        Overloaded{
            [](const ast::NoneLiteralExpr &) -> std::string { return "none"; },
            [](const ast::BoolLiteralExpr &e) -> std::string { return e.value ? "true" : "false"; },
            [](const ast::IntegerLiteralExpr &e) -> std::string {
                if (e.literal)
                    return e.literal->spelling;
                return "0";
            },
            [](const ast::FloatLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::DecimalLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::StringLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::DurationLiteralExpr &e) -> std::string {
                if (e.literal)
                    return e.literal->spelling;
                return "";
            },
            [](const ast::PathExpr &e) -> std::string {
                return e.path ? path_spelling(*e.path) : "<path>";
            },
            [](const ast::MemberAccessExpr &e) -> std::string {
                if (e.base)
                    return expr_spelling(*e.base) + "." + e.member;
                return e.member;
            },
            [](const ast::GroupExpr &e) -> std::string {
                return e.inner ? expr_spelling(*e.inner) : "<group>";
            },
            [](const ast::BinaryExpr &e) -> std::string {
                if (e.lhs && e.rhs) {
                    return expr_spelling(*e.lhs) + " " + std::string(binary_op_spelling(e.op)) +
                           " " + expr_spelling(*e.rhs);
                }
                return "<binary>";
            },
            [](const auto &) -> std::string { return "condition"; },
        },
        expr.node);
}

[[nodiscard]] std::optional<SymbolId> resolved_symbol_for(const ast::ExprSyntax &expr,
                                                          const ResolveResult &resolve_result,
                                                          std::optional<SourceId> source_id) {
    return std::visit(
        Overloaded{
            [&](const ast::CallExpr &e) -> std::optional<SymbolId> {
                if (e.callee != nullptr) {
                    // P2 (RFC §3.2.2): prefer the FnCallTarget reference for a
                    // fn call; fall back to the legacy capability/predicate
                    // CallTarget reference otherwise.
                    if (const auto fn_reference = resolve_result.find_reference(
                            ReferenceKind::FnCallTarget, e.callee->range, source_id);
                        fn_reference.has_value()) {
                        return fn_reference->get().target;
                    }
                    if (const auto reference = resolve_result.find_reference(
                            ReferenceKind::CallTarget, e.callee->range, source_id);
                        reference.has_value()) {
                        return reference->get().target;
                    }
                }
                return std::nullopt;
            },
            [&](const ast::StructLiteralExpr &e) -> std::optional<SymbolId> {
                if (e.type_name != nullptr) {
                    if (const auto reference = resolve_result.find_reference(
                            ReferenceKind::TypeName, e.type_name->range, source_id);
                        reference.has_value()) {
                        return reference->get().target;
                    }
                }
                return std::nullopt;
            },
            [&](const ast::QualifiedValueExpr &e) -> std::optional<SymbolId> {
                if (e.name != nullptr) {
                    if (const auto const_reference = resolve_result.find_reference(
                            ReferenceKind::ConstValue, e.name->range, source_id);
                        const_reference.has_value()) {
                        return const_reference->get().target;
                    }
                    if (const auto owner_reference = resolve_result.find_reference(
                            ReferenceKind::QualifiedValueOwnerType, e.name->range, source_id);
                        owner_reference.has_value()) {
                        return owner_reference->get().target;
                    }
                }
                return std::nullopt;
            },
            [](const auto &) -> std::optional<SymbolId> { return std::nullopt; },
        },
        expr.node);
}

[[nodiscard]] TypedCallTargetKind call_target_kind_for(const ast::ExprSyntax &expr,
                                                       const ResolveResult &resolve_result,
                                                       std::optional<SourceId> source_id) {
    if (!std::holds_alternative<ast::CallExpr>(expr.node)) {
        return TypedCallTargetKind::None;
    }
    const auto &call = std::get<ast::CallExpr>(expr.node);
    if (call.callee == nullptr) {
        return TypedCallTargetKind::None;
    }

    // P2 (RFC §3.2.2): a fn call resolves to a FnCallTarget reference whose
    // symbol is a Function, so it is recognised as a Function call target.
    if (const auto fn_reference =
            resolve_result.find_reference(ReferenceKind::FnCallTarget, call.callee->range, source_id);
        fn_reference.has_value()) {
        if (const auto symbol = resolve_result.symbol_table.get(fn_reference->get().target);
            symbol.has_value() && symbol->get().kind == SymbolKind::Function) {
            return TypedCallTargetKind::Function;
        }
    }

    const auto reference =
        resolve_result.find_reference(ReferenceKind::CallTarget, call.callee->range, source_id);
    if (!reference.has_value()) {
        return TypedCallTargetKind::None;
    }

    const auto symbol = resolve_result.symbol_table.get(reference->get().target);
    if (!symbol.has_value()) {
        return TypedCallTargetKind::None;
    }

    switch (symbol->get().kind) {
    case SymbolKind::Capability:
        return TypedCallTargetKind::Capability;
    case SymbolKind::Predicate:
        return TypedCallTargetKind::Predicate;
    case SymbolKind::Function:
        // A Function symbol reached via the legacy CallTarget reference (e.g.
        // when both a fn and a capability share a name). Treat it as a
        // Function call target for consistency.
        return TypedCallTargetKind::Function;
    case SymbolKind::Struct:
    case SymbolKind::Enum:
    case SymbolKind::TypeAlias:
    case SymbolKind::Const:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
        break;
    }
    return TypedCallTargetKind::None;
}

[[nodiscard]] std::string semantic_name_for(const ast::ExprSyntax &expr) {
    return std::visit(
        Overloaded{
            [](const ast::PathExpr &e) -> std::string {
                return e.path ? path_spelling(*e.path) : std::string{};
            },
            [](const ast::CallExpr &e) -> std::string {
                return e.callee ? e.callee->spelling() : std::string{};
            },
            [](const ast::StructLiteralExpr &e) -> std::string {
                return e.type_name ? e.type_name->spelling() : std::string{};
            },
            [](const ast::QualifiedValueExpr &e) -> std::string {
                return e.name ? e.name->spelling() : std::string{};
            },
            [&](const ast::MemberAccessExpr &) -> std::string { return expr_spelling(expr); },
            [](const ast::GroupExpr &e) -> std::string {
                return e.inner ? expr_spelling(*e.inner) : std::string{};
            },
            [](const ast::BoolLiteralExpr &e) -> std::string { return e.value ? "true" : "false"; },
            [](const ast::NoneLiteralExpr &) -> std::string { return "none"; },
            [](const ast::StringLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::IntegerLiteralExpr &e) -> std::string {
                return e.literal ? e.literal->spelling : std::string{};
            },
            [](const ast::FloatLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::DecimalLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::DurationLiteralExpr &e) -> std::string {
                return e.literal ? e.literal->spelling : std::string{};
            },
            [](const auto &) -> std::string { return {}; },
        },
        expr.node);
}

// Literal spelling kept as a dedicated field distinct from semantic_name:
//   * semantic_name for StringLiteral is the raw source text (including quotes
//     and escape markers) because it's used for diagnostics and user-facing
//     identity.
//   * literal_spelling uses the post-tokenisation "content" where it exists
//     (integer_literal->spelling, duration_literal->spelling, ...) so typed-
//     tree lowering can reproduce token payloads without needing AST.
[[nodiscard]] std::string literal_spelling_for(const ast::ExprSyntax &expr) {
    return std::visit(
        Overloaded{
            [](const ast::IntegerLiteralExpr &e) -> std::string {
                if (e.literal)
                    return e.literal->spelling;
                return "0";
            },
            [](const ast::DurationLiteralExpr &e) -> std::string {
                if (e.literal)
                    return e.literal->spelling;
                return "";
            },
            [](const ast::FloatLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::DecimalLiteralExpr &e) -> std::string { return e.spelling; },
            [](const ast::StringLiteralExpr &e) -> std::string { return e.spelling; },
            [](const auto &) -> std::string { return {}; },
        },
        expr.node);
}

[[nodiscard]] std::optional<Place> path_payload_for(const ast::ExprSyntax &expr) {
    if (expr.is<ast::PathExpr>() || expr.is<ast::MemberAccessExpr>() || expr.is<ast::GroupExpr>()) {
        return place_of_expr(expr);
    }
    return std::nullopt;
}

[[nodiscard]] bool contains_binary_op(const ast::ExprSyntax &expr, ast::ExprBinaryOp op) noexcept {
    return std::visit(Overloaded{
                          [&](const ast::GroupExpr &e) -> bool {
                              return e.inner ? contains_binary_op(*e.inner, op) : false;
                          },
                          [&](const ast::BinaryExpr &e) -> bool {
                              return e.op == op ||
                                     (e.lhs != nullptr && contains_binary_op(*e.lhs, op)) ||
                                     (e.rhs != nullptr && contains_binary_op(*e.rhs, op));
                          },
                          [](const auto &) -> bool { return false; },
                      },
                      expr.node);
}

[[nodiscard]] bool expr_bool_value(const ast::ExprSyntax &expr) noexcept {
    if (const auto *b = std::get_if<ast::BoolLiteralExpr>(&expr.node)) {
        return b->value;
    }
    return false;
}

[[nodiscard]] ast::ExprUnaryOp expr_unary_op(const ast::ExprSyntax &expr) noexcept {
    if (const auto *u = std::get_if<ast::UnaryExpr>(&expr.node)) {
        return u->op;
    }
    return ast::ExprUnaryOp::Not;
}

[[nodiscard]] ast::ExprBinaryOp expr_binary_op(const ast::ExprSyntax &expr) noexcept {
    if (const auto *b = std::get_if<ast::BinaryExpr>(&expr.node)) {
        return b->op;
    }
    return ast::ExprBinaryOp::Implies;
}

[[nodiscard]] std::string expr_member_name(const ast::ExprSyntax &expr) {
    if (const auto *m = std::get_if<ast::MemberAccessExpr>(&expr.node)) {
        return m->member;
    }
    return {};
}

[[nodiscard]] std::string_view narrowed_fact_description(TypeFactKind kind) noexcept {
    switch (kind) {
    case TypeFactKind::IsNone:
        return "none";
    case TypeFactKind::IsNotNone:
        return "non-none";
    case TypeFactKind::IsVariant:
        return "variant";
    case TypeFactKind::IsNotVariant:
        return "not-variant";
    }
    return "unknown";
}

void copy_resolver_snapshot_to_typed_program(const ResolveResult &resolve_result,
                                             TypedProgram &typed_program) {
    typed_program.symbols = resolve_result.symbol_table.symbols();
    typed_program.references = resolve_result.references();
    typed_program.imports = resolve_result.imports();
    typed_program.rebuild_resolver_indices();
}

} // namespace

void TypeCheckState::build_source_unit_index(const SourceGraph *graph) {
    source_units_by_id.clear();
    if (graph == nullptr) {
        return;
    }
    source_units_by_id.reserve(graph->sources.size());
    for (const auto &source : graph->sources) {
        source_units_by_id.emplace(source.id.value, &source);
    }
}

void TypeCheckState::enter_source(const SourceUnit &source) {
    current_source = &source;
    current_source_id = source.id;
    current_module_name = source.module_name;
}

void TypeCheckState::leave_source() {
    current_source = nullptr;
    current_source_id.reset();
    current_module_name.clear();
}

MaybeCRef<SourceUnit> TypeCheckState::source_unit_for(const SourceGraph *graph, SourceId id) const {
    if (graph == nullptr) {
        return std::nullopt;
    }

    if (const auto found = source_units_by_id.find(id.value);
        found != source_units_by_id.end() && found->second != nullptr) {
        return std::cref(*found->second);
    }

    return std::nullopt;
}

void DiagnosticReporter::error(std::string message, SourceRange range) {
    if (*current_source_ != nullptr) {
        diagnostics_->error()
            .code(error_codes::typecheck::SemanticError)
            .message(std::move(message))
            .range(range)
            .source((*current_source_)->source)
            .emit();
    } else {
        diagnostics_->error()
            .code(error_codes::typecheck::SemanticError)
            .message(std::move(message))
            .range(range)
            .emit();
    }
}

void DiagnosticReporter::note(std::string message, SourceRange range) {
    if (*current_source_ != nullptr) {
        diagnostics_->note()
            .message(std::move(message))
            .range(range)
            .source((*current_source_)->source)
            .emit();
    } else {
        diagnostics_->note().message(std::move(message)).range(range).emit();
    }
}

void DiagnosticReporter::typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         SourceRange range) {
    if (*current_source_ != nullptr) {
        diagnostics_->error()
            .code(code)
            .message(std::move(message))
            .range(range)
            .source((*current_source_)->source)
            .emit();
    } else {
        diagnostics_->error().code(code).message(std::move(message)).range(range).emit();
    }
}

void DiagnosticReporter::typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         SourceRange range,
                                         std::vector<Diagnostic::Related> notes) {
    Diagnostic diagnostic{
        .severity = DiagnosticSeverity::Error,
        .message = std::move(message),
        .code = code.full_code(),
        .range = range,
        .source_name = std::nullopt,
        .position = std::nullopt,
        .related = std::move(notes),
    };
    if (*current_source_ != nullptr) {
        diagnostic.source_name = (*current_source_)->source.display_name;
        diagnostic.position = (*current_source_)->source.locate(range.begin_offset);
    }
    diagnostics_->add_diagnostic(std::move(diagnostic));
}

void ConstSema::run() {
    check_agent_context_defaults();
    check_const_initializers();
    check_struct_defaults();
}

void ContractSema::run() {
    check_contracts();
}

void FlowSema::run() {
    check_flows();
}

void WorkflowSema::run() {
    check_workflows();
}

void TypedHirBuilder::append_declaration(TypedDecl decl) {
    program_->declarations.push_back(std::move(decl));
}

void TypedHirBuilder::append_expression(TypedExpr expr) {
    program_->expressions.push_back(std::move(expr));
}

std::uint32_t TypedHirBuilder::append_temporal_expr(TypedTemporalExpr expr) {
    program_->temporal_exprs.push_back(std::move(expr));
    return static_cast<std::uint32_t>(program_->temporal_exprs.size() - 1);
}

std::uint32_t TypedHirBuilder::append_block(TypedBlock block) {
    program_->blocks.push_back(std::move(block));
    return static_cast<std::uint32_t>(program_->blocks.size() - 1);
}

std::uint32_t TypedHirBuilder::append_statement(TypedStatement statement) {
    program_->statements.push_back(std::move(statement));
    return static_cast<std::uint32_t>(program_->statements.size() - 1);
}

void TypedHirBuilder::apply_declaration_payload_updates(
    std::vector<DeclarationPayloadUpdate> updates) {
    for (auto &update : updates) {
        if (update.declaration_index >= program_->declarations.size()) {
            continue;
        }
        auto &decl = program_->declarations[update.declaration_index];
        if (update.type != nullptr) {
            decl.type = update.type;
        }
        if (!std::holds_alternative<std::monostate>(update.payload)) {
            decl.payload = std::move(update.payload);
        }
    }
}

TypeCheckResult TypeCheckPass::run() {
    relations_.enable_trace(options_.trace_type_relations);
    // When tracing is on, also record success steps: the typical consumer of
    // relation_trace is a debugging / IDE tool that wants to reconstruct the
    // full set of checks performed (not only the failures). Without this flag
    // a fully-typechecked program produces an empty trace, hiding the fact
    // that the relation machinery ran at all.
    relations_.include_success_steps(options_.trace_type_relations);
    build_source_unit_index();
    copy_resolver_snapshot_to_typed_program(resolve_result_, result_.typed_program);
    DeclarationIndexBuilder(session_, state_, declaration_index_, hir_builder_).run();
    auto environment_result = EnvironmentBuilder(*this).run();
    result_.environment = std::move(environment_result.environment);
    hir_builder_.apply_declaration_payload_updates(
        std::move(environment_result.declaration_updates));
    ConstSema(*this).run();
    ContractSema(*this).run();
    FlowSema(*this).run();
    WorkflowSema(*this).run();
    result_.relation_trace = relations_.trace();
    return std::move(result_);
}

void TypeCheckPass::build_source_unit_index() {
    state_.build_source_unit_index(graph_);
}

void TypeCheckPass::enter_source(const SourceUnit &source) {
    state_.enter_source(source);
}

void TypeCheckPass::leave_source() {
    state_.leave_source();
}

MaybeCRef<SourceUnit> TypeCheckPass::source_unit_for(SourceId id) const {
    return state_.source_unit_for(graph_, id);
}

MaybeCRef<Symbol> TypeCheckPass::find_local_here(SymbolNamespace name_space,
                                                 std::string_view name) const {
    if (!current_module_name_.empty()) {
        return resolve_result_.symbol_table.find_local(name_space, name, current_module_name_);
    }

    return resolve_result_.symbol_table.find_local(name_space, name);
}

MaybeCRef<ResolvedReference> TypeCheckPass::find_reference_here(ReferenceKind kind,
                                                                SourceRange range) const {
    return resolve_result_.find_reference(kind, range, current_source_id_);
}

void TypeCheckPass::error_here(std::string message, SourceRange range) {
    reporter_.error(std::move(message), range);
}

void TypeCheckPass::note_here(std::string message, SourceRange range) {
    reporter_.note(std::move(message), range);
}

void TypeCheckPass::typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         SourceRange range) {
    reporter_.typecheck_error(code, std::move(message), range);
}

void TypeCheckPass::typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         SourceRange range,
                                         std::vector<Diagnostic::Related> notes) {
    reporter_.typecheck_error(code, std::move(message), range, std::move(notes));
}

void TypeCheckPass::non_pure_error_here(std::string_view context_label,
                                        ExprEffect effect,
                                        SourceRange range) {
    typecheck_error_here(error_codes::typecheck::NonPureExpression,
                         std::string(context_label) +
                             " must be pure; expression effect: " + std::string(to_string(effect)),
                         range);
}

MaybeCRef<Symbol> TypeCheckPass::symbol_of(SymbolId id) const {
    return resolve_result_.symbol_table.get(id);
}

MaybeCRef<ast::TypeAliasDecl> TypeCheckPass::alias_decl_of(SymbolId id) const {
    return find_decl_ref(type_alias_decls_, id);
}

TypeResolver TypeCheckPass::make_type_resolver() {
    return TypeResolver{
        resolve_result_,
        *types_,
        alias_resolution_,
        [this]() { return current_source_id_; },
        [this](ErrorCode<DiagnosticCategory::TypeCheck> code,
               std::string message,
               SourceRange range) { typecheck_error_here(code, std::move(message), range); },
        [this](SymbolId id) { return alias_decl_of(id); },
        [this](SymbolId id, const ast::TypeSyntax &aliased_type) {
            return with_symbol_context(id, [&]() { return resolve_type(aliased_type); });
        }};
}

TypePtr TypeCheckPass::resolve_type(const ast::TypeSyntax &type) {
    auto resolver = make_type_resolver();
    return resolver.resolve_type(type);
}

TypePtr TypeCheckPass::resolve_named_type(const ast::QualifiedName &name) {
    auto resolver = make_type_resolver();
    return resolver.resolve_named_type(name);
}

TypePtr TypeCheckPass::resolve_type_symbol(SymbolId id, SourceRange use_range) {
    auto resolver = make_type_resolver();
    return resolver.resolve_type_symbol(id, use_range);
}

// P2 (RFC §6): ExpressionSemaDelegate hook used by the lambda typecheck to
// resolve a closure parameter's type annotation through the shared
// TypeResolver (which honours the current symbol / module context).
TypePtr TypeCheckPass::resolve_type_syntax(const ast::TypeSyntax &type) {
    return resolve_type(type);
}

// P2c (RFC §3.5): ExpressionSemaDelegate hook that records a resolved fn call
// site into the typed program so the monomorphization pass has stable input
// without re-walking the typed tree. The recorded entry ties the resolved fn
// symbol to the call range and the (currently empty) explicit type-args list;
// inference of type args from argument types lands with the generic-body
// typecheck work.
void TypeCheckPass::record_fn_call_site(SymbolId fn_symbol,
                                        SourceRange call_range,
                                        std::vector<TypePtr> type_args) {
    result_.typed_program.fn_call_sites.push_back(
        TypedProgram::FnCallSiteRecord{.fn_symbol = fn_symbol,
                                       .call_range = call_range,
                                       .source_id = current_source_id_,
                                       .type_args = std::move(type_args)});
}

TypePtr TypeCheckPass::resolve_type_alias(SymbolId id, SourceRange use_range) {
    auto resolver = make_type_resolver();
    return resolver.resolve_type_alias(id, use_range);
}

void TypeCheckPass::remember_expression_type(const ast::ExprSyntax &expr, const TypedValue &typed) {
    const auto path_payload = path_payload_for(expr);
    if (auto *typed_expr = result_.typed_program.find_expr(expr.node_id, current_source_id_);
        typed_expr != nullptr) {
        // Update in place when both the AST identity and the source agree.
        typed_expr->type = typed.type ? typed.type->clone() : make_error_type();
        typed_expr->effect = typed.effect;
        typed_expr->is_pure = typed.is_pure;
        typed_expr->resolved_symbol =
            resolved_symbol_for(expr, resolve_result_, current_source_id_);
        typed_expr->semantic_name = semantic_name_for(expr);
        typed_expr->call_target_kind =
            call_target_kind_for(expr, resolve_result_, current_source_id_);
        typed_expr->path_root = path_payload.has_value() ? path_payload->root : std::string{};
        typed_expr->path_root_kind =
            typed.path_root_kind.value_or(AssignTargetRootKind::Identifier);
        typed_expr->member_path =
            path_payload.has_value() ? path_payload->members : std::vector<std::string>{};
        typed_expr->children = typed_children_for(
            expr, result_.typed_program, resolve_result_, result_.environment, current_source_id_);
        // Primitive payload mirrors so typed-tree consumers never need to
        // reach back into the AST for operator/literal/member-name details.
        typed_expr->bool_value = expr_bool_value(expr);
        typed_expr->unary_op = expr_unary_op(expr);
        typed_expr->binary_op = expr_binary_op(expr);
        typed_expr->literal_spelling = literal_spelling_for(expr);
        typed_expr->member_name = expr_member_name(expr);
        return;
    }

    // Fallback: range-based update for synthesized expressions that pre-date
    // NodeId assignment (node_id == 0).
    if (expr.node_id == 0) {
        if (auto *typed_expr =
                result_.typed_program.find_expr_by_range(expr.range, current_source_id_);
            typed_expr != nullptr) {
            typed_expr->type = typed.type ? typed.type->clone() : make_error_type();
            typed_expr->effect = typed.effect;
            typed_expr->is_pure = typed.is_pure;
            typed_expr->bool_value = expr_bool_value(expr);
            typed_expr->unary_op = expr_unary_op(expr);
            typed_expr->binary_op = expr_binary_op(expr);
            typed_expr->literal_spelling = literal_spelling_for(expr);
            typed_expr->member_name = expr_member_name(expr);
            typed_expr->path_root_kind =
                typed.path_root_kind.value_or(AssignTargetRootKind::Identifier);
            return;
        }
    }

    hir_builder_.append_expression(TypedExpr{
        .kind = ast::expr_syntax_kind(expr),
        .range = expr.range,
        .source_id = current_source_id_,
        .node_id = expr.node_id,
        .type = typed.type ? typed.type->clone() : make_error_type(),
        .effect = typed.effect,
        .is_pure = typed.is_pure,
        .resolved_symbol = resolved_symbol_for(expr, resolve_result_, current_source_id_),
        .semantic_name = semantic_name_for(expr),
        .call_target_kind = call_target_kind_for(expr, resolve_result_, current_source_id_),
        .path_root = path_payload.has_value() ? path_payload->root : std::string{},
        .path_root_kind = typed.path_root_kind.value_or(AssignTargetRootKind::Identifier),
        .member_path =
            path_payload.has_value() ? path_payload->members : std::vector<std::string>{},
        .children = typed_children_for(
            expr, result_.typed_program, resolve_result_, result_.environment, current_source_id_),
        .bool_value = expr_bool_value(expr),
        .unary_op = expr_unary_op(expr),
        .binary_op = expr_binary_op(expr),
        .literal_spelling = literal_spelling_for(expr),
        .member_name = expr_member_name(expr),
        .const_value = std::nullopt,
    });
    if (expr.node_id != 0) {
        const auto index = static_cast<std::uint32_t>(result_.typed_program.expressions.size() - 1);
        result_.typed_program.expr_index_.insert_or_assign(
            TypedProgram::ExprIndexKey{
                .node_id = expr.node_id,
                .source_id_value =
                    current_source_id_.has_value() ? current_source_id_->value : std::size_t{0},
                .has_source_id = current_source_id_.has_value(),
            },
            index);
    }
}

void ConstSema::remember_const_value(const ast::ExprSyntax &expr, const ConstValue &value) {
    if (auto *typed_expr =
            driver_->result_.typed_program.find_expr(expr.node_id, driver_->current_source_id_);
        typed_expr != nullptr) {
        typed_expr->const_value = value;
        return;
    }

    if (expr.node_id == 0) {
        if (auto *typed_expr = driver_->result_.typed_program.find_expr_by_range(
                expr.range, driver_->current_source_id_);
            typed_expr != nullptr) {
            typed_expr->const_value = value;
        }
    }
}

bool ConstSema::ensure_const_value(SymbolId id, SourceRange use_range) {
    ConstValueResolutionState resolution_state{
        const_values_,
        active_const_values_,
        failed_const_values_,
    };
    const auto symbol = driver_->symbol_of(id);
    auto begin_result =
        resolution_state.begin(id, symbol.has_value() ? &symbol->get() : nullptr, use_range);
    if (begin_result.status == ConstValueResolutionStatus::Known) {
        return true;
    }
    if (begin_result.status == ConstValueResolutionStatus::Failed) {
        return false;
    }

    if (begin_result.status == ConstValueResolutionStatus::Cycle) {
        ConstDiagnosticEmitter{
            driver_->result_.diagnostics,
            driver_->current_source_ != nullptr ? &driver_->current_source_->source : nullptr,
        }
            .emit_all(std::move(begin_result.diagnostics));
        return false;
    }

    const auto decl = internal::find_decl_ref(driver_->const_decls_, id);
    if (!decl.has_value() || decl->get().value == nullptr || decl->get().type == nullptr) {
        return resolution_state.fail(id);
    }

    const auto declared_type = driver_->environment().get_const_type(id);
    if (!declared_type.has_value()) {
        return resolution_state.fail(id);
    }

    return driver_->with_symbol_context(id, [&]() {
        const ValueContext context;
        const auto validation = describe_const_initializer_validation(
            declared_type->get(), decl->get().type->range, decl->get().name);
        auto value = check_const_expr(
            *decl->get().value, context, declared_type, validation.context_label, id);
        ConstTypeRelationValidator const_relations{
            driver_->relations_,
            ConstDiagnosticEmitter{
                driver_->result_.diagnostics,
                driver_->current_source_ != nullptr ? &driver_->current_source_->source : nullptr,
            },
        };
        const bool assignable = const_relations.check_assignable(*value.checked_expr.type,
                                                                 declared_type->get(),
                                                                 decl->get().value->range,
                                                                 validation.context_label,
                                                                 validation.expectation);
        return resolution_state.finish_after_assignability(id, value.outcome, assignable);
    });
}

bool TypeCheckPass::check_assignable(const Type &source,
                                     const Type &target,
                                     SourceRange range,
                                     std::string_view context_label) {
    return check_assignable(source, target, range, context_label, std::nullopt);
}

bool TypeCheckPass::check_assignable(const Type &source,
                                     const Type &target,
                                     SourceRange range,
                                     std::string_view context_label,
                                     std::optional<SourceRange> expected_origin) {
    if (expected_origin.has_value()) {
        return check_assignable(source,
                                target,
                                range,
                                context_label,
                                TypeExpectation{
                                    .expected = target.clone(),
                                    .origin_kind = TypeExpectationOriginKind::Annotation,
                                    .origin_range = *expected_origin,
                                    .description = std::string(context_label),
                                });
    }

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

bool TypeCheckPass::check_assignable(const Type &source,
                                     const Type &target,
                                     SourceRange range,
                                     std::string_view context_label,
                                     const TypeExpectation &expectation) {
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

bool TypeCheckPass::check_exact_schema_boundary(const Type &source,
                                                const Type &target,
                                                SchemaBoundaryKind boundary,
                                                SourceRange range) {
    return check_exact_schema_boundary(source, target, boundary, range, std::nullopt);
}

bool TypeCheckPass::check_exact_schema_boundary(const Type &source,
                                                const Type &target,
                                                SchemaBoundaryKind boundary,
                                                SourceRange range,
                                                std::optional<SourceRange> expected_origin) {
    if (expected_origin.has_value()) {
        return check_exact_schema_boundary(
            source,
            target,
            boundary,
            range,
            TypeExpectation{
                .expected = target.clone(),
                .origin_kind = TypeExpectationOriginKind::SchemaBoundary,
                .origin_range = *expected_origin,
                .description = std::string(to_string(boundary)),
            });
    }

    if (is_error_type(source) || is_error_type(target)) {
        return true;
    }

    if (ahfl::is_exact_schema_match(source, target, relations_)) {
        return true;
    }

    std::vector<Diagnostic::Related> notes;
    notes.push_back(Diagnostic::Related{
        .message = std::string(to_string(boundary)) +
                   " requires an exact schema match (no width or depth subtyping)",
        .range = std::nullopt,
    });
    notes.push_back(Diagnostic::Related{
        .message = actual_type_note(source),
        .range = range,
    });
    typecheck_error_here(error_codes::typecheck::ExactSchemaMismatch,
                         messages::typecheck::ExactSchemaMismatch.format_with(
                             to_string(boundary), target.describe(), source.describe()),
                         range,
                         std::move(notes));
    return false;
}

bool TypeCheckPass::check_exact_schema_boundary(const Type &source,
                                                const Type &target,
                                                SchemaBoundaryKind boundary,
                                                SourceRange range,
                                                const TypeExpectation &expectation) {
    if (is_error_type(source) || is_error_type(target)) {
        return true;
    }

    if (ahfl::is_exact_schema_match(source, target, relations_)) {
        return true;
    }

    std::vector<Diagnostic::Related> notes;
    notes.push_back(Diagnostic::Related{
        .message = std::string(to_string(boundary)) +
                   " requires an exact schema match (no width or depth subtyping)",
        .range = std::nullopt,
    });
    notes.push_back(Diagnostic::Related{
        .message = expected_schema_note(target, expectation),
        .range = expectation.origin_range,
    });
    notes.push_back(Diagnostic::Related{
        .message = actual_type_note(source),
        .range = range,
    });
    typecheck_error_here(error_codes::typecheck::ExactSchemaMismatch,
                         messages::typecheck::ExactSchemaMismatch.format_with(
                             to_string(boundary), target.describe(), source.describe()),
                         range,
                         std::move(notes));
    return false;
}

void TypeCheckPass::check_schema_boundary_decl_type(const TypePtr &type,
                                                    SchemaBoundaryKind boundary,
                                                    SourceRange range) {
    if (!type || is_error_type(*type) || type->holds<types::StructT>()) {
        return;
    }

    typecheck_error_here(
        error_codes::typecheck::InvalidAgentType,
        messages::typecheck::SchemaBoundaryTypeRequiresStruct.format_with(to_string(boundary)),
        range);
}

ConstEvalResult ConstSema::check_const_expr(const ast::ExprSyntax &expr,
                                            const ValueContext &context,
                                            MaybeCRef<Type> expected_type,
                                            std::string_view context_label,
                                            std::optional<SymbolId> source_const) {
    const ConstExpressionDriver const_expr{
        ConstEvalPipeline{
            driver_->resolve_result_,
            driver_->current_source_id_,
            driver_->result_.typed_program.const_dependencies,
            const_values_,
            [this](SymbolId target, SourceRange use_range) {
                return ensure_const_value(target, use_range);
            },
            [this](const ast::ExprSyntax &recorded_expr, const ConstValue &const_value) {
                remember_const_value(recorded_expr, const_value);
            },
        },
        ConstDiagnosticEmitter{
            driver_->result_.diagnostics,
            driver_->current_source_ != nullptr ? &driver_->current_source_->source : nullptr,
        },
        [this, &context](const ast::ExprSyntax &checked_expr,
                         MaybeCRef<Type> expected_checked_type) {
            auto value = driver_->check_expr(checked_expr, context, expected_checked_type);
            return ConstCheckedExpression{
                .type = std::move(value.type),
                .effect = value.effect,
                .is_pure = value.is_pure,
            };
        },
    };
    return const_expr.evaluate(expr, expected_type, context_label, source_const);
}

TypedValue TypeCheckPass::typed(TypePtr type, bool is_pure) const {
    return typed_effect(std::move(type), is_pure ? ExprEffect::Pure : ExprEffect::CapabilityCall);
}

TypedValue TypeCheckPass::typed_effect(TypePtr type, ExprEffect effect) const {
    return TypedValue{
        .type = std::move(type),
        .effect = effect,
        .is_pure = is_effect_pure(effect),
        .path_root_kind = std::nullopt,
    };
}

TypedValue TypeCheckPass::error_typed(bool is_pure) const {
    return error_typed_effect(is_pure ? ExprEffect::Pure : ExprEffect::Unknown);
}

TypedValue TypeCheckPass::error_typed_effect(ExprEffect effect) const {
    return TypedValue{
        .type = make_error_type(),
        .effect = effect,
        .is_pure = is_effect_pure(effect),
        .path_root_kind = std::nullopt,
    };
}

void ContractSema::check_contracts_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ContractDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
        const auto target =
            driver_->find_reference_here(ReferenceKind::ContractTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = driver_->environment().get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        // Record the ContractDecl in the typed HIR program declarations
        // so passers downstream (lowering, rewrite, analysis) can iterate
        // the full top-level surface model without re-entering the AST.
        TypedDecl typed_decl{
            .kind = ast::NodeKind::ContractDecl,
            .symbol = target->get().target,
            .range = declaration->range,
            .source_id = driver_->current_source_id_,
            .associated_agent_symbol = target->get().target,
            .type = nullptr,
            .payload = {},
        };
        if (const auto contract_info = driver_->environment().get_contract(target->get().target);
            contract_info.has_value()) {
            typed_decl.payload = contract_info->get();
        }
        driver_->hir_builder_.append_declaration(std::move(typed_decl));

        for (const auto &clause : decl.clauses) {
            if (!clause->expr && clause->temporal_expr) {
                ValueContext context;
                context.bindings.emplace(
                    "input", driver_->clone_or_any(std::cref(*agent_info->get().input_type)));
                driver_->check_temporal_embedded_exprs(*clause->temporal_expr, context);
                continue;
            }

            const auto bool_type = driver_->make_type(TypeKind::Bool);
            ValueContext context;
            context.bindings.emplace(
                "input", driver_->clone_or_any(std::cref(*agent_info->get().input_type)));
            if (clause->kind == ast::ContractClauseKind::Ensures) {
                context.bindings.emplace(
                    "output", driver_->clone_or_any(std::cref(*agent_info->get().output_type)));
            }

            const auto value = driver_->check_expr(*clause->expr, context, std::cref(*bool_type));
            if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
                driver_->typecheck_error_here(
                    error_codes::typecheck::TypeMismatch,
                    messages::typecheck::BoolExpressionRequired.format_with("contract expression"),
                    clause->expr->range,
                    std::vector<Diagnostic::Related>{Diagnostic::Related{
                        .message = actual_type_note(*value.type),
                        .range = clause->expr->range,
                    }});
            }
            if (!value.is_pure) {
                driver_->non_pure_error_here(
                    "contract expression", value.effect, clause->expr->range);
            }
        }
    }
}

void ContractSema::check_contracts() {
    if (driver_->graph_ != nullptr) {
        for (const auto &source : driver_->graph_->sources) {
            driver_->enter_source(source);
            check_contracts_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            driver_->leave_source();
        }
        return;
    }

    check_contracts_in_program(require(driver_->program_, "typecheck program must exist"));
}

std::uint32_t TypeCheckPass::check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
                                                           const ValueContext &context) {
    TypedTemporalExpr te;
    te.range = expr.range;
    te.source_id = current_source_id_;

    std::visit(
        Overloaded{
            [&](const ast::EmbeddedTemporalExpr &e) {
                const auto bool_type = make_type(TypeKind::Bool);
                const auto value = check_expr(*e.expr, context, std::cref(*bool_type));
                if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
                    typecheck_error_here(error_codes::typecheck::TypeMismatch,
                                         messages::typecheck::BoolExpressionRequired.format_with(
                                             "temporal embedded expression"),
                                         e.expr->range,
                                         std::vector<Diagnostic::Related>{Diagnostic::Related{
                                             .message = actual_type_note(*value.type),
                                             .range = e.expr->range,
                                         }});
                }
                if (!value.is_pure) {
                    non_pure_error_here(
                        "temporal embedded expression", value.effect, e.expr->range);
                }
                te.kind = TypedTemporalKind::Atom;
                te.op = TypedTemporalOp::Atom;
                const std::uint32_t expr_idx = resolve_payload_expr_index(*e.expr);
                te.children_index.push_back(expr_idx);
            },
            [&](const ast::CalledTemporalExpr &e) {
                te.kind = TypedTemporalKind::NameLiteral;
                te.op = TypedTemporalOp::NameLiteralCalled;
                std::string canonical = e.name;
                if (const auto ref =
                        find_reference_here(ReferenceKind::TemporalCapability, expr.range);
                    ref.has_value()) {
                    if (const auto sym = resolve_result_.symbol_table.get(ref->get().target);
                        sym.has_value()) {
                        canonical = sym->get().canonical_name;
                    }
                }
                // T1.7 P1: payload_spelling stores the canonical name directly
                // (no "called:" prefix). The op enum encodes the variant.
                te.payload_spelling = std::move(canonical);
            },
            [&](const ast::InStateTemporalExpr &e) {
                te.kind = TypedTemporalKind::StateLiteral;
                te.op = TypedTemporalOp::StateLiteral;
                // payload_spelling stores the state name directly (no "state:" prefix).
                te.payload_spelling = e.name;
            },
            [&](const ast::RunningTemporalExpr &e) {
                te.kind = TypedTemporalKind::NameLiteral;
                te.op = TypedTemporalOp::NameLiteralRunning;
                // payload_spelling stores the node name directly (no "running:" prefix).
                te.payload_spelling = e.name;
            },
            [&](const ast::CompletedTemporalExpr &e) {
                te.kind = TypedTemporalKind::NameLiteral;
                te.op = TypedTemporalOp::NameLiteralCompleted;
                // payload_spelling encodes "node_name|optional_state_name" for the
                // Completed variant so lowering can extract both without AST.
                std::string data = e.name;
                if (e.state_name.has_value()) {
                    data += "|";
                    data += *e.state_name;
                }
                te.payload_spelling = std::move(data);
            },
            [&](const ast::UnaryTemporalExpr &e) {
                const std::uint32_t child_idx = check_temporal_embedded_exprs(*e.operand, context);
                te.kind = TypedTemporalKind::Unary;
                te.children_index.push_back(child_idx);
                switch (e.op) {
                case ast::TemporalUnaryOp::Always:
                    te.op = TypedTemporalOp::TemporalAlways;
                    te.payload_spelling = "always";
                    break;
                case ast::TemporalUnaryOp::Eventually:
                    te.op = TypedTemporalOp::TemporalEventually;
                    te.payload_spelling = "eventually";
                    break;
                case ast::TemporalUnaryOp::Next:
                    te.op = TypedTemporalOp::TemporalNext;
                    te.payload_spelling = "next";
                    break;
                case ast::TemporalUnaryOp::Not:
                    te.op = TypedTemporalOp::TemporalNot;
                    te.payload_spelling = "not";
                    break;
                }
            },
            [&](const ast::BinaryTemporalExpr &e) {
                const std::uint32_t lhs_idx = check_temporal_embedded_exprs(*e.lhs, context);
                const std::uint32_t rhs_idx = check_temporal_embedded_exprs(*e.rhs, context);
                te.kind = TypedTemporalKind::Binary;
                te.children_index.push_back(lhs_idx);
                te.children_index.push_back(rhs_idx);
                switch (e.op) {
                case ast::TemporalBinaryOp::Implies:
                    te.op = TypedTemporalOp::TemporalImply;
                    te.payload_spelling = "implies";
                    break;
                case ast::TemporalBinaryOp::Or:
                    te.op = TypedTemporalOp::TemporalOr;
                    te.payload_spelling = "or";
                    break;
                case ast::TemporalBinaryOp::And:
                    te.op = TypedTemporalOp::TemporalAnd;
                    te.payload_spelling = "and";
                    break;
                case ast::TemporalBinaryOp::Until:
                    te.op = TypedTemporalOp::TemporalUntil;
                    te.payload_spelling = "until";
                    break;
                }
            },
        },
        expr.node);

    return hir_builder_.append_temporal_expr(std::move(te));
}

void FlowSema::check_flows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::FlowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::FlowDecl &>(*declaration);
        const auto target =
            driver_->find_reference_here(ReferenceKind::FlowTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = driver_->environment().get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        // Record the FlowDecl in the typed HIR program declarations so
        // downstream passes can enumerate all agent-associated top-level
        // constructs without walking the AST.
        TypedDecl typed_decl{
            .kind = ast::NodeKind::FlowDecl,
            .symbol = target->get().target,
            .range = declaration->range,
            .source_id = driver_->current_source_id_,
            .associated_agent_symbol = target->get().target,
            .type = nullptr,
            .payload = {},
        };
        if (const auto flow_info = driver_->environment().get_flow(target->get().target);
            flow_info.has_value()) {
            typed_decl.payload = flow_info->get();
        }
        driver_->hir_builder_.append_declaration(std::move(typed_decl));

        for (const auto &handler : decl.state_handlers) {
            ValueContext context;
            context.call_context = CallContext::Flow;
            context.current_agent = target->get().target;
            context.bindings.emplace(
                "input", driver_->clone_or_any(std::cref(*agent_info->get().input_type)));
            context.bindings.emplace(
                "ctx", driver_->clone_or_any(std::cref(*agent_info->get().context_type)));
            driver_->check_block(*handler->body,
                                 context,
                                 std::cref(*agent_info->get().output_type),
                                 handler->state_name,
                                 agent_info->get().output_type_range);
        }
    }
}

void FlowSema::check_flows() {
    if (driver_->graph_ != nullptr) {
        for (const auto &source : driver_->graph_->sources) {
            driver_->enter_source(source);
            check_flows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            driver_->leave_source();
        }
        return;
    }

    check_flows_in_program(require(driver_->program_, "typecheck program must exist"));
}

void WorkflowSema::check_workflows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::WorkflowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
        const auto workflow_symbol =
            driver_->find_local_here(SymbolNamespace::Workflows, decl.name);
        if (!workflow_symbol.has_value()) {
            continue;
        }

        const auto workflow_info = driver_->environment().get_workflow(workflow_symbol->get().id);
        if (!workflow_info.has_value()) {
            continue;
        }

        BindingMap all_node_outputs;

        for (const auto &node : decl.nodes) {
            const auto target = driver_->find_reference_here(ReferenceKind::WorkflowNodeTarget,
                                                             node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = driver_->environment().get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            all_node_outputs.emplace(
                node->name, driver_->clone_or_any(std::cref(*agent_info->get().output_type)));
        }

        for (const auto &node : decl.nodes) {
            const auto target = driver_->find_reference_here(ReferenceKind::WorkflowNodeTarget,
                                                             node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = driver_->environment().get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            ValueContext context;
            context.call_context = CallContext::Workflow;
            context.bindings.emplace(
                "input", driver_->clone_or_any(std::cref(*workflow_info->get().input_type)));

            for (const auto &dependency : node->after) {
                const auto dependency_type = find_binding(all_node_outputs, dependency);
                if (dependency_type.has_value()) {
                    context.bindings.emplace(dependency, dependency_type->get().clone());
                }
            }

            const auto argument = driver_->check_expr(
                *node->input, context, std::cref(*agent_info->get().input_type));
            (void)driver_->check_exact_schema_boundary(*argument.type,
                                                       *agent_info->get().input_type,
                                                       SchemaBoundaryKind::WorkflowNodeInput,
                                                       node->input->range,
                                                       agent_info->get().input_type_range);
        }

        ValueContext return_context;
        return_context.call_context = CallContext::Workflow;
        return_context.bindings.emplace(
            "input", driver_->clone_or_any(std::cref(*workflow_info->get().input_type)));
        for (const auto &[name, type] : all_node_outputs) {
            return_context.bindings.emplace(name,
                                            type ? type->clone() : driver_->make_error_type());
        }

        for (const auto &formula : decl.safety) {
            driver_->check_temporal_embedded_exprs(*formula, return_context);
        }

        for (const auto &formula : decl.liveness) {
            driver_->check_temporal_embedded_exprs(*formula, return_context);
        }

        const auto return_value = driver_->check_expr(
            *decl.return_value, return_context, std::cref(*workflow_info->get().output_type));
        (void)driver_->check_exact_schema_boundary(*return_value.type,
                                                   *workflow_info->get().output_type,
                                                   SchemaBoundaryKind::WorkflowOutput,
                                                   decl.return_value->range,
                                                   workflow_info->get().output_type_range);
    }
}

void WorkflowSema::check_workflows() {
    if (driver_->graph_ != nullptr) {
        for (const auto &source : driver_->graph_->sources) {
            driver_->enter_source(source);
            check_workflows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            driver_->leave_source();
        }
        return;
    }

    check_workflows_in_program(require(driver_->program_, "typecheck program must exist"));
}

std::uint32_t
TypeCheckPass::resolve_payload_expr_index(const ast::ExprSyntax &expr) const noexcept {
    auto &tp = result_.typed_program;
    if (const auto *typed = tp.find_expr(expr.node_id, current_source_id_); typed != nullptr) {
        const auto *base = tp.expressions.data();
        return static_cast<std::uint32_t>(typed - base);
    }
    if (expr.node_id == 0) {
        if (const auto *typed = tp.find_expr_by_range(expr.range, current_source_id_);
            typed != nullptr) {
            const auto *base = tp.expressions.data();
            return static_cast<std::uint32_t>(typed - base);
        }
    }
    return UINT32_MAX;
}

std::uint32_t
TypeCheckPass::find_block_index_by_range(const ast::BlockSyntax &block) const noexcept {
    const auto &blocks = result_.typed_program.blocks;
    for (auto i = blocks.size(); i > 0; --i) {
        const auto idx = i - 1;
        if (same_range(blocks[idx].range, block.range) &&
            blocks[idx].source_id == current_source_id_) {
            return static_cast<std::uint32_t>(idx);
        }
    }
    return UINT32_MAX;
}

void TypeCheckPass::check_block(const ast::BlockSyntax &block,
                                ValueContext &context,
                                MaybeCRef<Type> expected_return_type,
                                std::string_view state_name,
                                std::optional<SourceRange> expected_return_origin) {
    const auto my_block_idx = hir_builder_.append_block(TypedBlock{
        .range = block.range,
        .source_id = current_source_id_,
        .statement_indexes = {},
    });

    for (const auto &statement : block.statements) {
        check_statement(
            *statement, context, expected_return_type, state_name, expected_return_origin);
        if (last_written_statement_index_.has_value()) {
            result_.typed_program.blocks[my_block_idx].statement_indexes.push_back(
                static_cast<std::uint32_t>(*last_written_statement_index_));
        }
    }
}

void TypeCheckPass::check_statement(const ast::StatementSyntax &statement,
                                    ValueContext &context,
                                    MaybeCRef<Type> expected_return_type,
                                    std::string_view state_name,
                                    std::optional<SourceRange> expected_return_origin) {
    // Reset the bookkeeping index at entry so a nested / earlier
    // check_block returning with a leftover index can't pollute the
    // parent's block iteration if, for any reason, the current
    // statement does not push a new TypedStatement.
    last_written_statement_index_.reset();
    (void)state_name;

    switch (statement.kind) {
    case ast::StatementSyntaxKind::Let: {
        TypePtr annotated_type;
        MaybeCRef<Type> expected = std::nullopt;
        if (statement.let_stmt->type) {
            annotated_type = resolve_type(*statement.let_stmt->type);
            expected = std::cref(*annotated_type);
        }

        const auto initializer = check_expr(*statement.let_stmt->initializer, context, expected);
        if (expected.has_value() && statement.let_stmt->type) {
            const auto expectation = TypeExpectation{
                .expected = expected->get().clone(),
                .origin_kind = TypeExpectationOriginKind::Annotation,
                .origin_range = statement.let_stmt->type->range,
                .description = "declared type '" + statement.let_stmt->type->spelling() +
                               "' on let '" + statement.let_stmt->name + "'",
            };
            (void)check_assignable(*initializer.type,
                                   expected->get(),
                                   statement.let_stmt->initializer->range,
                                   "let initializer",
                                   expectation);
        } else if (expected.has_value()) {
            (void)check_assignable(*initializer.type,
                                   expected->get(),
                                   statement.let_stmt->initializer->range,
                                   "let initializer");
        }

        // Emit a SHADOWED_BINDING warning when the new binding masks an
        // existing one in the surrounding scope. Mirrors Rust/Swift behaviour
        // where shadowing is permitted but flagged so the user notices the
        // accidental masking of `input`/`ctx`/`output`/an enclosing `let`.
        if (const auto previous = find_binding(context.bindings, statement.let_stmt->name);
            previous.has_value()) {
            if (current_source_ != nullptr) {
                result_.diagnostics.warning()
                    .code(error_codes::typecheck::ShadowedBinding)
                    .message(messages::typecheck::ShadowedBinding,
                             statement.let_stmt->name,
                             previous->get().describe())
                    .range(statement.let_stmt->range)
                    .source(current_source_->source)
                    .emit();
            } else {
                result_.diagnostics.warning()
                    .code(error_codes::typecheck::ShadowedBinding)
                    .message(messages::typecheck::ShadowedBinding,
                             statement.let_stmt->name,
                             previous->get().describe())
                    .range(statement.let_stmt->range)
                    .emit();
            }
        }

        if (expected.has_value()) {
            context.bindings[statement.let_stmt->name] =
                annotated_type ? annotated_type->clone() : make_error_type();
        } else {
            context.bindings[statement.let_stmt->name] =
                initializer.type ? initializer.type->clone() : make_error_type();
        }

        // Determine the let type-ref strategy and structured semantic type.
        // Lowering consumes let_type directly and never reparses annotation
        // spelling.
        LetTypeRefStrategy let_strategy = LetTypeRefStrategy::FromInitializerType;
        TypePtr let_type;
        if (statement.let_stmt->type) {
            let_strategy = LetTypeRefStrategy::FromSyntax;
            let_type = annotated_type ? annotated_type->clone() : make_error_type();
        } else if (!initializer.type) {
            let_strategy = LetTypeRefStrategy::MissingType;
            let_type = make_error_type();
        } else {
            let_strategy = LetTypeRefStrategy::FromInitializerType;
            let_type = initializer.type->clone();
        }

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::Let,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.let_stmt->initializer)},
            .target_name = statement.let_stmt->name,
            .goto_target_state = {},
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = let_strategy,
            .let_type = std::move(let_type),
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = {},
        });
        break;
    }
    case ast::StatementSyntaxKind::Assign: {
        if (statement.assign_stmt->target->root_name != "ctx") {
            typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                 messages::typecheck::AssignmentTargetRequiresContext.format_with(),
                                 statement.assign_stmt->target->range);
        } else {
            context.flow_facts.invalidate(Place{.root = statement.assign_stmt->target->root_name,
                                                .members = statement.assign_stmt->target->members});
            const auto target = check_path(*statement.assign_stmt->target, context);
            const auto expectation = TypeExpectation{
                .expected = target.type ? target.type->clone() : make_error_type(),
                .origin_kind = TypeExpectationOriginKind::AssignmentTarget,
                .origin_range = statement.assign_stmt->target->range,
                .description =
                    "assignment target '" + path_spelling(*statement.assign_stmt->target) + "'",
            };
            const auto value = check_expr(*statement.assign_stmt->value, context, expectation);
            (void)check_assignable(*value.type,
                                   *target.type,
                                   statement.assign_stmt->value->range,
                                   "assignment",
                                   expectation);
        }

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::Assign,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = statement.assign_stmt->value
                                       ? std::vector<std::uint32_t>{resolve_payload_expr_index(
                                             *statement.assign_stmt->value)}
                                       : std::vector<std::uint32_t>{UINT32_MAX},
            .target_name = path_spelling(*statement.assign_stmt->target),
            .goto_target_state = {},
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = assign_target_root_kind_of(*statement.assign_stmt->target),
            .assert_message = {},
        });
        break;
    }
    case ast::StatementSyntaxKind::If: {
        auto condition_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .flow_facts = {},
            .call_context = CallContext::PureOnly,
            .current_agent = context.current_agent,
        };
        const auto condition = check_expr(*statement.if_stmt->condition, condition_context);
        if (!is_bool_type(*condition.type) && !is_error_type(*condition.type)) {
            typecheck_error_here(
                error_codes::typecheck::TypeMismatch,
                messages::typecheck::BoolExpressionRequired.format_with("if condition"),
                statement.if_stmt->condition->range,
                std::vector<Diagnostic::Related>{Diagnostic::Related{
                    .message = actual_type_note(*condition.type),
                    .range = statement.if_stmt->condition->range,
                }});
        }
        if (!condition.is_pure) {
            non_pure_error_here(
                "if condition", condition.effect, statement.if_stmt->condition->range);
        }

        const auto condition_facts = extract_condition_facts(*statement.if_stmt->condition);
        if (options_.explain_narrowing) {
            const std::string condition_text = current_source_ != nullptr
                                                   ? std::string(current_source_->source.slice(
                                                         statement.if_stmt->condition->range))
                                                   : expr_spelling(*statement.if_stmt->condition);
            bool emitted_explanation = false;
            condition_facts.when_true.for_each([&](const TypeFact &fact) {
                note_here("narrowing: condition '" + condition_text + "' narrows '" +
                              place_spelling(fact.place) + "' to " +
                              std::string(narrowed_fact_description(fact.kind)) + " on then branch",
                          fact.origin);
                emitted_explanation = true;
            });
            condition_facts.when_false.for_each([&](const TypeFact &fact) {
                note_here("narrowing: condition '" + condition_text + "' narrows '" +
                              place_spelling(fact.place) + "' to " +
                              std::string(narrowed_fact_description(fact.kind)) + " on else branch",
                          fact.origin);
                emitted_explanation = true;
            });
            if (!emitted_explanation) {
                if (contains_binary_op(*statement.if_stmt->condition, ast::ExprBinaryOp::Or)) {
                    note_here("narrowing: condition '" + condition_text +
                                  "' did not produce Optional narrowing facts because "
                                  "disjunctive conditions are not represented",
                              statement.if_stmt->condition->range);
                } else {
                    note_here("narrowing: condition '" + condition_text +
                                  "' did not produce Optional narrowing facts; only direct "
                                  "comparisons with none and && chains are supported",
                              statement.if_stmt->condition->range);
                }
            }
        }

        auto then_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .flow_facts = context.flow_facts,
            .call_context = context.call_context,
            .current_agent = context.current_agent,
        };
        then_context.flow_facts.merge_from(condition_facts.when_true);
        check_block(*statement.if_stmt->then_block,
                    then_context,
                    expected_return_type,
                    state_name,
                    expected_return_origin);

        if (statement.if_stmt->else_block) {
            auto else_context = ValueContext{
                .bindings = clone_bindings(context.bindings),
                .flow_facts = context.flow_facts,
                .call_context = context.call_context,
                .current_agent = context.current_agent,
            };
            else_context.flow_facts.merge_from(condition_facts.when_false);
            check_block(*statement.if_stmt->else_block,
                        else_context,
                        expected_return_type,
                        state_name,
                        expected_return_origin);
        }

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::If,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.if_stmt->condition)},
            .target_name = {},
            .goto_target_state = {},
            .then_block_index = find_block_index_by_range(*statement.if_stmt->then_block),
            .else_block_index = statement.if_stmt->else_block
                                    ? find_block_index_by_range(*statement.if_stmt->else_block)
                                    : UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = {},
        });
        break;
    }
    case ast::StatementSyntaxKind::Goto: {
        // GotoStmtSyntax::target_state is always populated by the parser; the
        // typed-record field mirrors it directly so lowering never needs to
        // reach back into the AST.
        const std::string target_state =
            statement.goto_stmt ? statement.goto_stmt->target_state : std::string{};
        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::Goto,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {},
            .target_name = {},
            .goto_target_state = target_state,
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = {},
        });
        break;
    }
    case ast::StatementSyntaxKind::Return: {
        std::uint32_t value_index = UINT32_MAX;
        if (statement.return_stmt && statement.return_stmt->value) {
            if (expected_return_type.has_value()) {
                if (expected_return_origin.has_value()) {
                    const auto expectation = TypeExpectation{
                        .expected = expected_return_type->get().clone(),
                        .origin_kind = TypeExpectationOriginKind::ReturnType,
                        .origin_range = *expected_return_origin,
                        .description = "flow return",
                    };
                    const auto value =
                        check_expr(*statement.return_stmt->value, context, expectation);
                    (void)check_exact_schema_boundary(*value.type,
                                                      expected_return_type->get(),
                                                      SchemaBoundaryKind::AgentOutput,
                                                      statement.return_stmt->value->range,
                                                      expectation);
                } else {
                    const auto value =
                        check_expr(*statement.return_stmt->value, context, expected_return_type);
                    (void)check_exact_schema_boundary(*value.type,
                                                      expected_return_type->get(),
                                                      SchemaBoundaryKind::AgentOutput,
                                                      statement.return_stmt->value->range,
                                                      expected_return_origin);
                }
            } else {
                // Still walk the return value so nested expressions are recorded
                // into the flat TypedProgram::expressions store even when
                // no return type context is available.
                (void)check_expr(*statement.return_stmt->value, context);
            }
            value_index = resolve_payload_expr_index(*statement.return_stmt->value);
        }

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::Return,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {value_index},
            .target_name = {},
            .goto_target_state = {},
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = {},
        });
        break;
    }
    case ast::StatementSyntaxKind::Assert: {
        auto condition_context = ValueContext{
            .bindings = clone_bindings(context.bindings),
            .flow_facts = {},
            .call_context = CallContext::PureOnly,
            .current_agent = context.current_agent,
        };
        const auto condition = check_expr(*statement.assert_stmt->condition, condition_context);
        if (!is_bool_type(*condition.type) && !is_error_type(*condition.type)) {
            typecheck_error_here(
                error_codes::typecheck::TypeMismatch,
                messages::typecheck::BoolExpressionRequired.format_with("assert condition"),
                statement.assert_stmt->condition->range,
                std::vector<Diagnostic::Related>{Diagnostic::Related{
                    .message = actual_type_note(*condition.type),
                    .range = statement.assert_stmt->condition->range,
                }});
        }
        if (!condition.is_pure) {
            non_pure_error_here(
                "assert condition", condition.effect, statement.assert_stmt->condition->range);
        }

        // AssertStmtSyntax currently does not carry a user-facing message, so
        // the typed record mirrors the current AST shape with an empty message.
        std::string assert_msg;
        (void)assert_msg; // silence unused warning when the AST lacks the field

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::Assert,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.assert_stmt->condition)},
            .target_name = {},
            .goto_target_state = {},
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = std::move(assert_msg),
        });
        break;
    }
    case ast::StatementSyntaxKind::Expr: {
        (void)check_expr(*statement.expr_stmt->expr, context);

        last_written_statement_index_ = hir_builder_.append_statement(TypedStatement{
            .kind = TypedStmtKind::ExprStatement,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.expr_stmt->expr)},
            .target_name = {},
            .goto_target_state = {},
            .then_block_index = UINT32_MAX,
            .else_block_index = UINT32_MAX,
            .let_type_ref_strategy = LetTypeRefStrategy::NoAnnotation,
            .let_type = nullptr,
            .assign_target_root_kind = AssignTargetRootKind::Identifier,
            .assert_message = {},
        });
        break;
    }
    }
}

TypeCheckResult TypeChecker::check(const ast::Program &program,
                                   const ResolveResult &resolve_result) const {
    return check(program, resolve_result, TypeCheckOptions{});
}

TypeCheckResult TypeChecker::check(const ast::Program &program,
                                   const ResolveResult &resolve_result,
                                   TypeCheckOptions options) const {
    TypeCheckPass pass(program, resolve_result, options);
    return pass.run();
}

TypeCheckResult TypeChecker::check(const ast::Program &program,
                                   const ResolveResult &resolve_result,
                                   TypeContext &types) const {
    return check(program, resolve_result, types, TypeCheckOptions{});
}

TypeCheckResult TypeChecker::check(const ast::Program &program,
                                   const ResolveResult &resolve_result,
                                   TypeContext &types,
                                   TypeCheckOptions options) const {
    TypeCheckPass pass(program, resolve_result, types, options);
    return pass.run();
}

TypeCheckResult TypeChecker::check(const SourceGraph &graph,
                                   const ResolveResult &resolve_result) const {
    return check(graph, resolve_result, TypeCheckOptions{});
}

TypeCheckResult TypeChecker::check(const SourceGraph &graph,
                                   const ResolveResult &resolve_result,
                                   TypeCheckOptions options) const {
    TypeCheckPass pass(graph, resolve_result, options);
    return pass.run();
}

TypeCheckResult TypeChecker::check(const SourceGraph &graph,
                                   const ResolveResult &resolve_result,
                                   TypeContext &types) const {
    return check(graph, resolve_result, types, TypeCheckOptions{});
}

TypeCheckResult TypeChecker::check(const SourceGraph &graph,
                                   const ResolveResult &resolve_result,
                                   TypeContext &types,
                                   TypeCheckOptions options) const {
    TypeCheckPass pass(graph, resolve_result, types, options);
    return pass.run();
}

} // namespace ahfl
