#include "ahfl/compiler/semantics/typecheck.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/condition_facts.hpp"
#include "ahfl/compiler/semantics/const_sema.hpp"
#include "ahfl/compiler/semantics/name_suggestions.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"

#include "compiler/semantics/typecheck_internal.hpp"

#include <algorithm>
#include <cctype>
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
using internal::ConstEvalKind;
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
        // Linear scan: expressions.size() is typically O(thousands) and this
        // runs once per AST edge; pointer-difference alternative is unsafe
        // because the vector may reallocate between insertion and resolution,
        // so we compare by identity fields instead.
        for (std::size_t i = 0; i < program.expressions.size(); ++i) {
            if (program.expressions[i].node_id == child->node_id &&
                program.expressions[i].source_id == source_id) {
                child_index = static_cast<std::uint32_t>(i);
                break;
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
    switch (expr.kind) {
    case ast::ExprSyntaxKind::Some:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::Operand);
        break;
    case ast::ExprSyntaxKind::Call: {
        std::vector<std::string> parameter_names;
        if (expr.qualified_name != nullptr) {
            if (const auto reference = resolve_result.find_reference(
                    ReferenceKind::CallTarget, expr.qualified_name->range, source_id);
                reference.has_value()) {
                if (const auto capability = environment.get_capability(reference->get().target);
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
        for (std::size_t index = 0; index < expr.items.size(); ++index) {
            append_typed_child(children,
                               program,
                               expr.items[index].get(),
                               source_id,
                               TypedExprChildRole::Argument,
                               index < parameter_names.size() ? parameter_names[index]
                                                              : std::string{});
        }
        break;
    }
    case ast::ExprSyntaxKind::StructLiteral:
        for (const auto &field : expr.struct_fields) {
            append_typed_child(children,
                               program,
                               field->value.get(),
                               source_id,
                               TypedExprChildRole::StructFieldValue,
                               field->field_name);
        }
        break;
    case ast::ExprSyntaxKind::ListLiteral:
    case ast::ExprSyntaxKind::SetLiteral:
        for (const auto &item : expr.items) {
            append_typed_child(
                children, program, item.get(), source_id, TypedExprChildRole::CollectionElement);
        }
        break;
    case ast::ExprSyntaxKind::MapLiteral:
        for (const auto &entry : expr.map_entries) {
            append_typed_child(
                children, program, entry->key.get(), source_id, TypedExprChildRole::MapKey);
            append_typed_child(
                children, program, entry->value.get(), source_id, TypedExprChildRole::MapValue);
        }
        break;
    case ast::ExprSyntaxKind::Unary:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::Operand);
        break;
    case ast::ExprSyntaxKind::Binary:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::LeftOperand);
        append_typed_child(
            children, program, expr.second.get(), source_id, TypedExprChildRole::RightOperand);
        break;
    case ast::ExprSyntaxKind::MemberAccess:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::Base);
        break;
    case ast::ExprSyntaxKind::IndexAccess:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::Base);
        append_typed_child(
            children, program, expr.second.get(), source_id, TypedExprChildRole::Index);
        break;
    case ast::ExprSyntaxKind::Group:
        append_typed_child(
            children, program, expr.first.get(), source_id, TypedExprChildRole::Grouped);
        break;
    case ast::ExprSyntaxKind::BoolLiteral:
    case ast::ExprSyntaxKind::IntegerLiteral:
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::StringLiteral:
    case ast::ExprSyntaxKind::DurationLiteral:
    case ast::ExprSyntaxKind::NoneLiteral:
    case ast::ExprSyntaxKind::Path:
    case ast::ExprSyntaxKind::QualifiedValue:
        break;
    }
    return children;
}

[[nodiscard]] std::optional<Place> place_of_expr(const ast::ExprSyntax &expr) {
    if (expr.kind == ast::ExprSyntaxKind::Group && expr.first) {
        return place_of_expr(*expr.first);
    }
    if (expr.kind == ast::ExprSyntaxKind::MemberAccess && expr.first) {
        auto place = place_of_expr(*expr.first);
        if (!place.has_value()) {
            return std::nullopt;
        }
        place->members.push_back(expr.name);
        return place;
    }
    if (expr.kind == ast::ExprSyntaxKind::Path && expr.path) {
        return Place{.root = expr.path->root_name, .members = expr.path->members};
    }
    return std::nullopt;
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
    case ast::PathRootKind::Input:  return AssignTargetRootKind::Input;
    case ast::PathRootKind::Output: return AssignTargetRootKind::Output;
    case ast::PathRootKind::Identifier:
        if (path.root_name == "ctx")   return AssignTargetRootKind::Context;
        if (path.root_name == "state") return AssignTargetRootKind::State;
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
    switch (expr.kind) {
    case ast::ExprSyntaxKind::NoneLiteral:
        return "none";
    case ast::ExprSyntaxKind::BoolLiteral:
        return expr.bool_value ? "true" : "false";
    case ast::ExprSyntaxKind::IntegerLiteral:
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::StringLiteral:
    case ast::ExprSyntaxKind::DurationLiteral:
        return expr.text;
    case ast::ExprSyntaxKind::Path:
        return expr.path ? path_spelling(*expr.path) : "<path>";
    case ast::ExprSyntaxKind::MemberAccess:
        if (expr.first) {
            return expr_spelling(*expr.first) + "." + expr.name;
        }
        return expr.name;
    case ast::ExprSyntaxKind::Group:
        return expr.first ? expr_spelling(*expr.first) : "<group>";
    case ast::ExprSyntaxKind::Binary:
        if (expr.first && expr.second) {
            return expr_spelling(*expr.first) + " " +
                   std::string(binary_op_spelling(expr.binary_op)) + " " +
                   expr_spelling(*expr.second);
        }
        return "<binary>";
    default:
        break;
    }
    return "condition";
}

[[nodiscard]] std::optional<SymbolId> resolved_symbol_for(const ast::ExprSyntax &expr,
                                                          const ResolveResult &resolve_result,
                                                          std::optional<SourceId> source_id) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::Call:
        if (expr.qualified_name != nullptr) {
            if (const auto reference = resolve_result.find_reference(
                    ReferenceKind::CallTarget, expr.qualified_name->range, source_id);
                reference.has_value()) {
                return reference->get().target;
            }
        }
        break;
    case ast::ExprSyntaxKind::StructLiteral:
        if (expr.qualified_name != nullptr) {
            if (const auto reference = resolve_result.find_reference(
                    ReferenceKind::TypeName, expr.qualified_name->range, source_id);
                reference.has_value()) {
                return reference->get().target;
            }
        }
        break;
    case ast::ExprSyntaxKind::QualifiedValue:
        if (expr.qualified_name != nullptr) {
            if (const auto const_reference = resolve_result.find_reference(
                    ReferenceKind::ConstValue, expr.qualified_name->range, source_id);
                const_reference.has_value()) {
                return const_reference->get().target;
            }
            if (const auto owner_reference = resolve_result.find_reference(
                    ReferenceKind::QualifiedValueOwnerType, expr.qualified_name->range, source_id);
                owner_reference.has_value()) {
                return owner_reference->get().target;
            }
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] TypedCallTargetKind call_target_kind_for(const ast::ExprSyntax &expr,
                                                       const ResolveResult &resolve_result,
                                                       std::optional<SourceId> source_id) {
    if (expr.kind != ast::ExprSyntaxKind::Call || expr.qualified_name == nullptr) {
        return TypedCallTargetKind::None;
    }

    const auto reference = resolve_result.find_reference(
        ReferenceKind::CallTarget, expr.qualified_name->range, source_id);
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
    switch (expr.kind) {
    case ast::ExprSyntaxKind::Path:
        return expr.path ? path_spelling(*expr.path) : std::string{};
    case ast::ExprSyntaxKind::Call:
    case ast::ExprSyntaxKind::StructLiteral:
    case ast::ExprSyntaxKind::QualifiedValue:
        return expr.qualified_name ? expr.qualified_name->spelling() : std::string{};
    case ast::ExprSyntaxKind::MemberAccess:
        return expr_spelling(expr);
    case ast::ExprSyntaxKind::Group:
        return expr.first ? expr_spelling(*expr.first) : std::string{};
    case ast::ExprSyntaxKind::BoolLiteral:
        return expr.bool_value ? "true" : "false";
    case ast::ExprSyntaxKind::NoneLiteral:
        return "none";
    case ast::ExprSyntaxKind::StringLiteral:
    case ast::ExprSyntaxKind::IntegerLiteral:
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::DurationLiteral:
        return expr.text;
    default:
        break;
    }
    return {};
}

// Literal spelling kept as a dedicated field distinct from semantic_name:
//   * semantic_name for StringLiteral is the raw source text (including quotes
//     and escape markers) because it's used for diagnostics and user-facing
//     identity.
//   * literal_spelling uses the post-tokenisation "content" where it exists
//     (integer_literal->spelling, duration_literal->spelling, ...) so typed-
//     tree lowering matches legacy lowering exactly without needing AST.
[[nodiscard]] std::string literal_spelling_for(const ast::ExprSyntax &expr) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::IntegerLiteral:
        return expr.integer_literal ? expr.integer_literal->spelling
                                    : (expr.text.empty() ? "0" : expr.text);
    case ast::ExprSyntaxKind::DurationLiteral:
        return expr.duration_literal ? expr.duration_literal->spelling : expr.text;
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::StringLiteral:
        return expr.text;
    default:
        break;
    }
    return {};
}

[[nodiscard]] std::optional<Place> path_payload_for(const ast::ExprSyntax &expr) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::Path:
    case ast::ExprSyntaxKind::MemberAccess:
    case ast::ExprSyntaxKind::Group:
        return place_of_expr(expr);
    default:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] bool contains_binary_op(const ast::ExprSyntax &expr, ast::ExprBinaryOp op) noexcept {
    if (expr.kind == ast::ExprSyntaxKind::Group && expr.first) {
        return contains_binary_op(*expr.first, op);
    }
    if (expr.kind != ast::ExprSyntaxKind::Binary) {
        return false;
    }
    return expr.binary_op == op || (expr.first != nullptr && contains_binary_op(*expr.first, op)) ||
           (expr.second != nullptr && contains_binary_op(*expr.second, op));
}

[[nodiscard]] std::string_view narrowed_fact_description(TypeFactKind kind) noexcept {
    switch (kind) {
    case TypeFactKind::IsNone:
        return "none";
    case TypeFactKind::IsNotNone:
        return "non-none";
    }
    return "unknown";
}

[[nodiscard]] std::string expected_type_note(const Type &target,
                                             const TypeExpectation &expectation) {
    std::string source = expectation.description.empty()
                             ? describe_type_expectation_origin(expectation.origin_kind)
                             : expectation.description;
    return "expected type '" + target.describe() + "' from " + source + " declared here";
}

[[nodiscard]] std::string actual_type_note(const Type &source) {
    return "actual expression has type '" + source.describe() + "' here";
}

[[nodiscard]] std::string expected_schema_note(const Type &target,
                                               const TypeExpectation &expectation) {
    std::string source = expectation.description.empty()
                             ? describe_type_expectation_origin(expectation.origin_kind)
                             : expectation.description;
    return "expected schema '" + target.describe() + "' from " + source + " declared here";
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

} // namespace

TypeCheckResult TypeCheckPass::run() {
    relations_.enable_trace(options_.trace_type_relations);
    // When tracing is on, also record success steps: the typical consumer of
    // relation_trace is a debugging / IDE tool that wants to reconstruct the
    // full set of checks performed (not only the failures). Without this flag
    // a fully-typechecked program produces an empty trace, hiding the fact
    // that the relation machinery ran at all.
    relations_.include_success_steps(options_.trace_type_relations);
    result_.typed_program.ast_program = program_;
    result_.typed_program.source_graph = graph_;
    result_.typed_program.resolve_result = &resolve_result_;
    result_.typed_program.type_check_result = &result_;
    index_declarations();
    build_type_environment();
    check_agent_context_defaults();
    check_const_initializers();
    check_struct_defaults();
    check_contracts();
    check_flows();
    check_workflows();
    result_.relation_trace = relations_.trace();
    return std::move(result_);
}

void TypeCheckPass::enter_source(const SourceUnit &source) {
    current_source_ = &source;
    current_source_id_ = source.id;
    current_module_name_ = source.module_name;
}

void TypeCheckPass::leave_source() {
    current_source_ = nullptr;
    current_source_id_.reset();
    current_module_name_.clear();
}

MaybeCRef<SourceUnit> TypeCheckPass::source_unit_for(SourceId id) const {
    if (graph_ == nullptr) {
        return std::nullopt;
    }

    for (const auto &source : graph_->sources) {
        if (source.id == id) {
            return std::cref(source);
        }
    }

    return std::nullopt;
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
    if (current_source_ != nullptr) {
        result_.diagnostics.error()
            .code(error_codes::typecheck::SemanticError)
            .message(std::move(message))
            .range(range)
            .source(current_source_->source)
            .emit();
    } else {
        result_.diagnostics.error()
            .code(error_codes::typecheck::SemanticError)
            .message(std::move(message))
            .range(range)
            .emit();
    }
}

void TypeCheckPass::note_here(std::string message, SourceRange range) {
    if (current_source_ != nullptr) {
        result_.diagnostics.note()
            .message(std::move(message))
            .range(range)
            .source(current_source_->source)
            .emit();
    } else {
        result_.diagnostics.note().message(std::move(message)).range(range).emit();
    }
}

void TypeCheckPass::typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                                         std::string message,
                                         SourceRange range) {
    if (current_source_ != nullptr) {
        result_.diagnostics.error()
            .code(code)
            .message(std::move(message))
            .range(range)
            .source(current_source_->source)
            .emit();
    } else {
        result_.diagnostics.error().code(code).message(std::move(message)).range(range).emit();
    }
}

void TypeCheckPass::typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
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
    if (current_source_ != nullptr) {
        diagnostic.source_name = current_source_->source.display_name;
        diagnostic.position = current_source_->source.locate(range.begin_offset);
    }
    result_.diagnostics.add_diagnostic(std::move(diagnostic));
}

MaybeCRef<Symbol> TypeCheckPass::symbol_of(SymbolId id) const {
    return resolve_result_.symbol_table.get(id);
}

MaybeCRef<ast::TypeAliasDecl> TypeCheckPass::alias_decl_of(SymbolId id) const {
    return find_decl_ref(type_alias_decls_, id);
}

TypePtr TypeCheckPass::resolve_type(const ast::TypeSyntax &type) {
    switch (type.kind) {
    case ast::TypeSyntaxKind::Unit:
        return make_type(TypeKind::Unit);
    case ast::TypeSyntaxKind::Bool:
        return make_type(TypeKind::Bool);
    case ast::TypeSyntaxKind::Int:
        return make_type(TypeKind::Int);
    case ast::TypeSyntaxKind::Float:
        return make_type(TypeKind::Float);
    case ast::TypeSyntaxKind::String:
        return string_type();
    case ast::TypeSyntaxKind::BoundedString:
        return bounded_string_type(type.string_bounds->first, type.string_bounds->second);
    case ast::TypeSyntaxKind::UUID:
        return make_type(TypeKind::UUID);
    case ast::TypeSyntaxKind::Timestamp:
        return make_type(TypeKind::Timestamp);
    case ast::TypeSyntaxKind::Duration:
        return make_type(TypeKind::Duration);
    case ast::TypeSyntaxKind::Decimal:
        return decimal_type(*type.decimal_scale);
    case ast::TypeSyntaxKind::Named:
        return resolve_named_type(*type.name);
    case ast::TypeSyntaxKind::Optional:
        return optional_type(resolve_type(*type.first));
    case ast::TypeSyntaxKind::List:
        return list_type(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Set:
        return set_type(resolve_type(*type.first));
    case ast::TypeSyntaxKind::Map:
        return map_type(resolve_type(*type.first), resolve_type(*type.second));
    }

    return make_any_type();
}

TypePtr TypeCheckPass::resolve_named_type(const ast::QualifiedName &name) {
    const auto reference = find_reference_here(ReferenceKind::TypeName, name.range);
    if (!reference.has_value()) {
        error_here("unable to resolve type '" + name.spelling() + "'", name.range);
        return make_any_type();
    }

    return resolve_type_symbol(reference->get().target, name.range);
}

TypePtr TypeCheckPass::resolve_type_symbol(SymbolId id, SourceRange use_range) {
    const auto symbol = symbol_of(id);
    if (!symbol.has_value()) {
        error_here("resolved type symbol is missing", use_range);
        return make_any_type();
    }

    switch (symbol->get().kind) {
    case SymbolKind::Struct:
        return struct_type(symbol->get().canonical_name, id);
    case SymbolKind::Enum:
        return enum_type(symbol->get().canonical_name, id);
    case SymbolKind::TypeAlias:
        return resolve_type_alias(id, use_range);
    case SymbolKind::Const:
    case SymbolKind::Capability:
    case SymbolKind::Predicate:
    case SymbolKind::Agent:
    case SymbolKind::Workflow:
        error_here("symbol '" + symbol->get().canonical_name + "' does not name a type", use_range);
        return make_any_type();
    }

    return make_any_type();
}

TypePtr TypeCheckPass::resolve_type_alias(SymbolId id, SourceRange use_range) {
    if (const auto cached = resolved_alias_types_.find(id.value);
        cached != resolved_alias_types_.end()) {
        return cached->second ? cached->second->clone() : make_any_type();
    }

    if (!active_aliases_.insert(id.value).second) {
        error_here("type alias cycle reached during type resolution", use_range);
        return make_any_type();
    }

    const auto alias_decl = alias_decl_of(id);
    if (!alias_decl.has_value()) {
        active_aliases_.erase(id.value);
        error_here("type alias declaration is missing", use_range);
        return make_any_type();
    }

    auto resolved =
        with_symbol_context(id, [&]() { return resolve_type(*alias_decl->get().aliased_type); });
    resolved_alias_types_.emplace(id.value, resolved ? resolved->clone() : make_any_type());
    active_aliases_.erase(id.value);
    return resolved;
}

void TypeCheckPass::remember_expression_type(const ast::ExprSyntax &expr, const TypedValue &typed) {
    const auto path_payload = path_payload_for(expr);
    if (auto *typed_expr = result_.typed_program.find_expr(expr.node_id, current_source_id_);
        typed_expr != nullptr) {
        // Update in place when both the AST identity and the source agree.
        typed_expr->type = typed.type ? typed.type->clone() : make_any_type();
        typed_expr->effect = typed.effect;
        typed_expr->is_pure = typed.is_pure;
        typed_expr->resolved_symbol =
            resolved_symbol_for(expr, resolve_result_, current_source_id_);
        typed_expr->semantic_name = semantic_name_for(expr);
        typed_expr->call_target_kind =
            call_target_kind_for(expr, resolve_result_, current_source_id_);
        typed_expr->path_root = path_payload.has_value() ? path_payload->root : std::string{};
        typed_expr->member_path =
            path_payload.has_value() ? path_payload->members : std::vector<std::string>{};
        typed_expr->children = typed_children_for(
            expr, result_.typed_program, resolve_result_, result_.environment, current_source_id_);
        // Primitive payload mirrors so typed-tree consumers never need to
        // reach back into the AST for operator/literal/member-name details.
        typed_expr->bool_value = expr.bool_value;
        typed_expr->unary_op = expr.unary_op;
        typed_expr->binary_op = expr.binary_op;
        typed_expr->literal_spelling = literal_spelling_for(expr);
        typed_expr->member_name = expr.name;
        return;
    }

    // Fallback: range-based update for synthesized expressions that pre-date
    // NodeId assignment (node_id == 0).
    if (expr.node_id == 0) {
        if (auto *typed_expr =
                result_.typed_program.find_expr_by_range(expr.range, current_source_id_);
            typed_expr != nullptr) {
            typed_expr->type = typed.type ? typed.type->clone() : make_any_type();
            typed_expr->effect = typed.effect;
            typed_expr->is_pure = typed.is_pure;
            typed_expr->bool_value = expr.bool_value;
            typed_expr->unary_op = expr.unary_op;
            typed_expr->binary_op = expr.binary_op;
            typed_expr->literal_spelling = literal_spelling_for(expr);
            typed_expr->member_name = expr.name;
            return;
        }
    }

    result_.typed_program.expressions.push_back(TypedExpr{
        .kind = expr.kind,
        .range = expr.range,
        .source_id = current_source_id_,
        .node_id = expr.node_id,
        .type = typed.type ? typed.type->clone() : make_any_type(),
        .effect = typed.effect,
        .is_pure = typed.is_pure,
        .resolved_symbol = resolved_symbol_for(expr, resolve_result_, current_source_id_),
        .semantic_name = semantic_name_for(expr),
        .call_target_kind = call_target_kind_for(expr, resolve_result_, current_source_id_),
        .path_root = path_payload.has_value() ? path_payload->root : std::string{},
        .member_path =
            path_payload.has_value() ? path_payload->members : std::vector<std::string>{},
        .children = typed_children_for(
            expr, result_.typed_program, resolve_result_, result_.environment, current_source_id_),
        .bool_value = expr.bool_value,
        .unary_op = expr.unary_op,
        .binary_op = expr.binary_op,
        .literal_spelling = literal_spelling_for(expr),
        .member_name = expr.name,
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

ConstEvalResult TypeCheckPass::check_const_expr(const ast::ExprSyntax &expr,
                                                const ValueContext &context,
                                                MaybeCRef<Type> expected_type,
                                                std::string_view context_label) {
    auto value = check_expr(expr, context, expected_type);

    const auto syntax_result = classify_const_expr_syntax(expr);
    if (!syntax_result.is_const) {
        typecheck_error_here(
            error_codes::typecheck::ConstExprRequired,
            messages::typecheck::ConstExprRequired.format_with(context_label, syntax_result.reason),
            expr.range);
        return ConstEvalResult{
            .kind = ConstEvalKind::NotConst,
            .typed_value = std::move(value),
        };
    }

    if (!value.is_pure) {
        typecheck_error_here(error_codes::typecheck::ConstExprRequired,
                             messages::typecheck::ConstExprRequired.format_with(
                                 context_label, "expression has runtime effects"),
                             expr.range);
        return ConstEvalResult{
            .kind = ConstEvalKind::NotConst,
            .typed_value = std::move(value),
        };
    }

    return ConstEvalResult{
        .kind = ConstEvalKind::KnownConst,
        .typed_value = std::move(value),
    };
}

TypedValue TypeCheckPass::typed(TypePtr type, bool is_pure) const {
    return typed_effect(std::move(type), is_pure ? ExprEffect::Pure : ExprEffect::CapabilityCall);
}

TypedValue TypeCheckPass::typed_effect(TypePtr type, ExprEffect effect) const {
    return TypedValue{
        .type = std::move(type),
        .effect = effect,
        .is_pure = is_effect_pure(effect),
    };
}

TypedValue TypeCheckPass::error_typed(bool is_pure) const {
    return error_typed_effect(is_pure ? ExprEffect::Pure : ExprEffect::Unknown);
}

TypedValue TypeCheckPass::error_typed_effect(ExprEffect effect) const {
    return TypedValue{
        .type = make_any_type(),
        .effect = effect,
        .is_pure = is_effect_pure(effect),
    };
}

TypePtr
TypeCheckPass::field_access(const Type &base_type, std::string_view field_name, SourceRange range) {
    if (is_error_type(base_type)) {
        return make_any_type();
    }

    if (!base_type.holds<types::StructT>()) {
        error_here("member access requires a struct value, got " + base_type.describe(), range);
        return make_any_type();
    }

    const auto struct_info = result_.environment.get_struct(base_type);
    if (!struct_info.has_value()) {
        error_here("unknown struct type '" + base_type.describe() + "'", range);
        return make_any_type();
    }

    const auto field = struct_info->get().find_field(field_name);
    if (!field.has_value()) {
        std::vector<std::string> candidates;
        candidates.reserve(struct_info->get().fields.size());
        for (const auto &candidate : struct_info->get().fields) {
            candidates.push_back(candidate.name);
        }
        auto message = "unknown field '" + std::string(field_name) + "' on struct '" +
                       base_type.describe() + "'";
        if (const auto suggestion = suggest_name(field_name, candidates); suggestion.has_value()) {
            message += "; did you mean '" + *suggestion + "'?";
        }
        error_here(std::move(message), range);
        return make_any_type();
    }

    return field->get().type ? field->get().type->clone() : make_any_type();
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
    // Visitor wrapper around the per-kind handlers below. Routing through
    // visit_expr_syntax (Task #4) localises ExprSyntaxKind dispatch and lets
    // the compiler enforce that every kind has a corresponding visit_*
    // overload here when new kinds are added to the AST.
    struct ExprChecker {
        TypeCheckPass &pass;
        const ValueContext &context;
        MaybeCRef<Type> expected_type;
        const TypeExpectation *expectation{nullptr};

        TypedValue visit_bool_literal(const ast::ExprSyntax &) const {
            return pass.typed(pass.make_type(TypeKind::Bool));
        }
        TypedValue visit_integer_literal(const ast::ExprSyntax &) const {
            return pass.typed(pass.make_type(TypeKind::Int));
        }
        TypedValue visit_float_literal(const ast::ExprSyntax &) const {
            return pass.typed(pass.make_type(TypeKind::Float));
        }
        TypedValue visit_decimal_literal(const ast::ExprSyntax &e) const {
            return pass.typed(pass.decimal_type(parse_decimal_scale(e.text)));
        }
        TypedValue visit_string_literal(const ast::ExprSyntax &) const {
            return pass.typed(pass.string_type());
        }
        TypedValue visit_duration_literal(const ast::ExprSyntax &) const {
            return pass.typed(pass.make_type(TypeKind::Duration));
        }
        TypedValue visit_none_literal(const ast::ExprSyntax &e) const {
            if (expected_type.has_value() && expected_type->get().holds<types::OptionalT>()) {
                return pass.typed(expected_type->get().clone());
            }
            pass.error_here("cannot infer type of 'none' without an expected Optional<T> context",
                            e.range);
            return pass.error_typed();
        }
        TypedValue visit_some(const ast::ExprSyntax &e) const {
            MaybeCRef<Type> inner_expected = std::nullopt;
            std::optional<TypeExpectation> inner_expectation;
            if (expected_type.has_value()) {
                if (const auto *opt = expected_type->get().get_if<types::OptionalT>();
                    opt != nullptr && opt->inner != nullptr) {
                    inner_expected = std::cref(*opt->inner);
                    inner_expectation = derive_expectation(expectation, opt->inner);
                }
            }
            const auto inner = inner_expectation.has_value()
                                   ? pass.check_expr(*e.first, context, *inner_expectation)
                                   : pass.check_expr(*e.first, context, inner_expected);
            if (inner_expectation.has_value() && inner_expectation->expected != nullptr) {
                (void)pass.check_assignable(*inner.type,
                                            *inner_expectation->expected,
                                            e.first->range,
                                            "optional payload",
                                            *inner_expectation);
                return pass.typed_effect(expected_type->get().clone(), inner.effect);
            }
            if (inner_expected.has_value()) {
                (void)pass.check_assignable(
                    *inner.type, inner_expected->get(), e.first->range, "optional payload");
                return pass.typed_effect(expected_type->get().clone(), inner.effect);
            }
            return pass.typed_effect(
                pass.optional_type(inner.type ? inner.type->clone() : pass.make_any_type()),
                inner.effect);
        }
        TypedValue visit_path(const ast::ExprSyntax &e) const {
            return pass.check_path(*e.path, context);
        }
        TypedValue visit_qualified_value(const ast::ExprSyntax &e) const {
            return pass.check_qualified_value(e);
        }
        TypedValue visit_call(const ast::ExprSyntax &e) const {
            return pass.check_call(e, context);
        }
        TypedValue visit_struct_literal(const ast::ExprSyntax &e) const {
            return pass.check_struct_literal(e, context);
        }
        TypedValue visit_list_literal(const ast::ExprSyntax &e) const {
            return pass.check_list_literal(e, context, expected_type, expectation);
        }
        TypedValue visit_set_literal(const ast::ExprSyntax &e) const {
            return pass.check_set_literal(e, context, expected_type, expectation);
        }
        TypedValue visit_map_literal(const ast::ExprSyntax &e) const {
            return pass.check_map_literal(e, context, expected_type, expectation);
        }
        TypedValue visit_unary(const ast::ExprSyntax &e) const {
            return pass.check_unary_expr(e, context);
        }
        TypedValue visit_binary(const ast::ExprSyntax &e) const {
            return pass.check_binary_expr(e, context);
        }
        TypedValue visit_member_access(const ast::ExprSyntax &e) const {
            return pass.check_member_access(e, context);
        }
        TypedValue visit_index_access(const ast::ExprSyntax &e) const {
            return pass.check_index_access(e, context);
        }
        TypedValue visit_group(const ast::ExprSyntax &e) const {
            if (expectation != nullptr) {
                return pass.check_expr(*e.first, context, *expectation);
            }
            return pass.check_expr(*e.first, context, expected_type);
        }
        // Fallback for the unreachable default branch in visit_expr_syntax.
        TypedValue visit_unknown(const ast::ExprSyntax &) const {
            return pass.error_typed();
        }
    };

    return internal::visit_expr_syntax(expr,
                                       ExprChecker{*this, context, expected_type, expectation});
}

TypedValue TypeCheckPass::check_path(const ast::PathSyntax &path, const ValueContext &context) {
    const auto root = find_binding(context.bindings, path.root_name);
    if (!root.has_value()) {
        error_here("unknown value '" + path.root_name + "'", path.range);
        return error_typed();
    }

    auto current = root->get().clone();
    std::vector<std::string> traversed_members;
    for (const auto &member : path.members) {
        current = field_access(*current, member, path.range);
        traversed_members.push_back(member);
    }

    if (context.flow_facts.has_fact(
            Place{.root = path.root_name, .members = std::move(traversed_members)},
            TypeFactKind::IsNotNone)) {
        if (const auto *optional = current->get_if<types::OptionalT>();
            optional != nullptr && optional->inner != nullptr) {
            current = optional->inner->clone();
        }
    }

    return typed(std::move(current));
}

TypedValue TypeCheckPass::check_qualified_value(const ast::ExprSyntax &expr) {
    if (const auto const_reference =
            find_reference_here(ReferenceKind::ConstValue, expr.qualified_name->range);
        const_reference.has_value()) {
        const auto const_type = result_.environment.get_const_type(const_reference->get().target);
        if (!const_type.has_value()) {
            error_here("constant type is missing for '" + expr.qualified_name->spelling() + "'",
                       expr.range);
            return error_typed();
        }

        return typed_effect(const_type->get().clone(), ExprEffect::ConstOnly);
    }

    const auto owner_reference =
        find_reference_here(ReferenceKind::QualifiedValueOwnerType, expr.qualified_name->range);
    if (!owner_reference.has_value()) {
        error_here("unknown qualified value '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed();
    }

    auto owner_type = resolve_type_symbol(owner_reference->get().target, expr.range);
    if (!owner_type || !owner_type->holds<types::EnumT>()) {
        error_here("qualified value '" + expr.qualified_name->spelling() +
                       "' must refer to a constant or enum variant",
                   expr.range);
        return error_typed();
    }

    const auto enum_info = result_.environment.get_enum(*owner_type);
    if (!enum_info.has_value()) {
        error_here("enum type '" + owner_type->describe() + "' is missing", expr.range);
        return error_typed();
    }

    const auto &segments = expr.qualified_name->segments;
    if (segments.empty() || !enum_info->get().has_variant(segments.back())) {
        std::string message = "unknown enum variant '" + expr.qualified_name->spelling() + "'";
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
        error_here(std::move(message), expr.range);
        return error_typed();
    }

    return typed_effect(std::move(owner_type), ExprEffect::ConstOnly);
}

TypedValue TypeCheckPass::check_call(const ast::ExprSyntax &expr, const ValueContext &context) {
    const auto reference =
        find_reference_here(ReferenceKind::CallTarget, expr.qualified_name->range);
    if (!reference.has_value()) {
        error_here("unknown callable '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed(false);
    }

    const auto symbol = symbol_of(reference->get().target);
    if (!symbol.has_value()) {
        error_here("call target symbol is missing", expr.range);
        return error_typed(false);
    }

    if (symbol->get().kind == SymbolKind::Capability) {
        const auto capability = result_.environment.get_capability(reference->get().target);
        if (!capability.has_value()) {
            error_here("capability type info is missing for '" + expr.qualified_name->spelling() +
                           "'",
                       expr.range);
            return error_typed(false);
        }

        if (context.call_context != CallContext::Flow) {
            error_here("capability call '" + expr.qualified_name->spelling() +
                           "' is not allowed in this context",
                       expr.range);
        }

        if (context.current_agent.has_value()) {
            const auto agent_info = result_.environment.get_agent(*context.current_agent);
            if (agent_info.has_value()) {
                const auto allowed = std::find(agent_info->get().capability_symbols.begin(),
                                               agent_info->get().capability_symbols.end(),
                                               reference->get().target);
                if (allowed == agent_info->get().capability_symbols.end()) {
                    error_here("capability '" + expr.qualified_name->spelling() +
                                   "' is not declared in the current agent capabilities",
                               expr.range);
                }
            }
        }

        if (expr.items.size() != capability->get().params.size()) {
            error_here("capability '" + expr.qualified_name->spelling() + "' expects " +
                           std::to_string(capability->get().params.size()) + " argument(s), got " +
                           std::to_string(expr.items.size()),
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
            const auto argument = check_expr(*expr.items[index], context, expectation);
            (void)check_assignable(*argument.type,
                                   *param.type,
                                   expr.items[index]->range,
                                   "capability argument",
                                   expectation);
        }

        return typed_effect(capability->get().return_type ? capability->get().return_type->clone()
                                                          : make_any_type(),
                            ExprEffect::CapabilityCall);
    }

    const auto predicate = result_.environment.get_predicate(reference->get().target);
    if (!predicate.has_value()) {
        error_here("predicate type info is missing for '" + expr.qualified_name->spelling() + "'",
                   expr.range);
        return error_typed();
    }

    if (expr.items.size() != predicate->get().params.size()) {
        error_here("predicate '" + expr.qualified_name->spelling() + "' expects " +
                       std::to_string(predicate->get().params.size()) + " argument(s), got " +
                       std::to_string(expr.items.size()),
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
        const auto argument = check_expr(*expr.items[index], context, expectation);
        effect = join_effects(effect, argument.effect);
        if (!argument.is_pure) {
            error_here("predicate arguments must be pure expressions", expr.items[index]->range);
        }

        (void)check_assignable(*argument.type,
                               *param.type,
                               expr.items[index]->range,
                               "predicate argument",
                               expectation);
    }

    return typed_effect(make_type(TypeKind::Bool), effect);
}

TypedValue TypeCheckPass::check_struct_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context) {
    const auto reference = find_reference_here(ReferenceKind::TypeName, expr.qualified_name->range);
    if (!reference.has_value()) {
        error_here("unknown struct type '" + expr.qualified_name->spelling() + "'", expr.range);
        return error_typed();
    }

    auto struct_type = resolve_type_symbol(reference->get().target, expr.range);
    if (!struct_type || !struct_type->holds<types::StructT>()) {
        error_here("struct literal target '" + expr.qualified_name->spelling() +
                       "' does not resolve to a struct type",
                   expr.range);
        return error_typed();
    }

    const auto struct_info = result_.environment.get_struct(*struct_type);
    if (!struct_info.has_value()) {
        error_here("unknown struct type '" + struct_type->describe() + "'", expr.range);
        return error_typed();
    }

    std::unordered_set<std::string> seen_fields;
    ExprEffect effect = ExprEffect::Pure;

    for (const auto &field_init : expr.struct_fields) {
        if (!seen_fields.insert(field_init->field_name).second) {
            error_here("duplicate field '" + field_init->field_name + "' in struct literal",
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
            auto message = "unknown field '" + field_init->field_name +
                           "' in struct literal for '" + struct_type->describe() + "'";
            if (const auto suggestion = suggest_name(field_init->field_name, candidates);
                suggestion.has_value()) {
                message += "; did you mean '" + *suggestion + "'?";
            }
            error_here(std::move(message), field_init->range);
            continue;
        }

        const auto expectation = TypeExpectation{
            .expected = field->get().type,
            .origin_kind = TypeExpectationOriginKind::StructField,
            .origin_range = field->get().declaration_range,
            .description = "struct field '" + field->get().name + "'",
        };
        const auto value = check_expr(*field_init->value, context, expectation);
        effect = join_effects(effect, value.effect);
        (void)check_assignable(
            *value.type, *field->get().type, field_init->value->range, "struct field", expectation);
    }

    for (const auto &field : struct_info->get().fields) {
        if (!seen_fields.contains(field.name)) {
            error_here("missing field '" + field.name + "' in struct literal", expr.range);
        }
    }

    return typed_effect(std::move(struct_type), effect);
}

TypedValue TypeCheckPass::check_list_literal(const ast::ExprSyntax &expr,
                                             const ValueContext &context,
                                             MaybeCRef<Type> expected_type,
                                             const TypeExpectation *expectation) {
    MaybeCRef<Type> element_expected = std::nullopt;
    std::optional<TypeExpectation> element_expectation;
    if (expected_type.has_value()) {
        if (const auto *list = expected_type->get().get_if<types::ListT>();
            list != nullptr && list->element != nullptr) {
            element_expected = std::cref(*list->element);
            element_expectation = derive_expectation(expectation, list->element);
        }
    }

    if (expr.items.empty()) {
        if (expected_type.has_value() && expected_type->get().holds<types::ListT>()) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty list literal", expr.range);
        return typed(list_type(make_any_type()));
    }

    auto element_type = clone_or_any(element_expected);
    bool have_element_type = element_expected.has_value();
    ExprEffect effect = ExprEffect::Pure;

    for (const auto &item : expr.items) {
        const auto value = element_expectation.has_value()
                               ? check_expr(*item, context, *element_expectation)
                               : check_expr(*item, context, element_expected);
        effect = join_effects(effect, value.effect);

        if (!have_element_type) {
            element_type = value.type ? value.type->clone() : make_any_type();
            have_element_type = true;
            continue;
        }

        if (element_expectation.has_value()) {
            (void)check_assignable(
                *value.type, *element_type, item->range, "list element", *element_expectation);
        } else {
            (void)check_assignable(*value.type, *element_type, item->range, "list element");
        }
    }

    return typed_effect(list_type(std::move(element_type)), effect);
}

TypedValue TypeCheckPass::check_set_literal(const ast::ExprSyntax &expr,
                                            const ValueContext &context,
                                            MaybeCRef<Type> expected_type,
                                            const TypeExpectation *expectation) {
    MaybeCRef<Type> element_expected = std::nullopt;
    std::optional<TypeExpectation> element_expectation;
    if (expected_type.has_value()) {
        if (const auto *set = expected_type->get().get_if<types::SetT>();
            set != nullptr && set->element != nullptr) {
            element_expected = std::cref(*set->element);
            element_expectation = derive_expectation(expectation, set->element);
        }
    }

    if (expr.items.empty()) {
        if (expected_type.has_value() && expected_type->get().holds<types::SetT>()) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty set literal", expr.range);
        return typed(set_type(make_any_type()));
    }

    auto element_type = clone_or_any(element_expected);
    bool have_element_type = element_expected.has_value();
    ExprEffect effect = ExprEffect::Pure;

    for (const auto &item : expr.items) {
        const auto value = element_expectation.has_value()
                               ? check_expr(*item, context, *element_expectation)
                               : check_expr(*item, context, element_expected);
        effect = join_effects(effect, value.effect);

        if (!have_element_type) {
            element_type = value.type ? value.type->clone() : make_any_type();
            have_element_type = true;
            continue;
        }

        if (element_expectation.has_value()) {
            (void)check_assignable(
                *value.type, *element_type, item->range, "set element", *element_expectation);
        } else {
            (void)check_assignable(*value.type, *element_type, item->range, "set element");
        }
    }

    return typed_effect(set_type(std::move(element_type)), effect);
}

TypedValue TypeCheckPass::check_map_literal(const ast::ExprSyntax &expr,
                                            const ValueContext &context,
                                            MaybeCRef<Type> expected_type,
                                            const TypeExpectation *expectation) {
    MaybeCRef<Type> key_expected = std::nullopt;
    MaybeCRef<Type> value_expected = std::nullopt;
    std::optional<TypeExpectation> key_expectation;
    std::optional<TypeExpectation> value_expectation;
    if (expected_type.has_value()) {
        if (const auto *map = expected_type->get().get_if<types::MapT>();
            map != nullptr && map->key != nullptr && map->value != nullptr) {
            key_expected = std::cref(*map->key);
            value_expected = std::cref(*map->value);
            key_expectation = derive_expectation(expectation, map->key);
            value_expectation = derive_expectation(expectation, map->value);
        }
    }

    if (expr.map_entries.empty()) {
        if (expected_type.has_value() && expected_type->get().holds<types::MapT>()) {
            return typed(expected_type->get().clone());
        }

        error_here("cannot infer type of empty map literal", expr.range);
        return typed(map_type(make_any_type(), make_any_type()));
    }

    auto key_type = clone_or_any(key_expected);
    auto value_type = clone_or_any(value_expected);
    bool have_key_type = key_expected.has_value();
    bool have_value_type = value_expected.has_value();
    ExprEffect effect = ExprEffect::Pure;

    for (const auto &entry : expr.map_entries) {
        const auto key = key_expectation.has_value()
                             ? check_expr(*entry->key, context, *key_expectation)
                             : check_expr(*entry->key, context, key_expected);
        const auto value = value_expectation.has_value()
                               ? check_expr(*entry->value, context, *value_expectation)
                               : check_expr(*entry->value, context, value_expected);
        effect = join_effects(effect, join_effects(key.effect, value.effect));

        if (!have_key_type) {
            key_type = key.type ? key.type->clone() : make_any_type();
            have_key_type = true;
        } else {
            if (key_expectation.has_value()) {
                (void)check_assignable(
                    *key.type, *key_type, entry->key->range, "map key", *key_expectation);
            } else {
                (void)check_assignable(*key.type, *key_type, entry->key->range, "map key");
            }
        }

        if (!have_value_type) {
            value_type = value.type ? value.type->clone() : make_any_type();
            have_value_type = true;
        } else {
            if (value_expectation.has_value()) {
                (void)check_assignable(
                    *value.type, *value_type, entry->value->range, "map value", *value_expectation);
            } else {
                (void)check_assignable(*value.type, *value_type, entry->value->range, "map value");
            }
        }
    }

    return typed_effect(map_type(std::move(key_type), std::move(value_type)), effect);
}

TypedValue TypeCheckPass::check_unary_expr(const ast::ExprSyntax &expr,
                                           const ValueContext &context) {
    const auto operand = check_expr(*expr.first, context);
    switch (expr.unary_op) {
    case ast::ExprUnaryOp::Not:
        if (!is_bool_type(*operand.type) && !is_error_type(*operand.type)) {
            error_here("logical not requires Bool, got " + operand.type->describe(), expr.range);
        }
        return typed_effect(make_type(TypeKind::Bool), operand.effect);
    case ast::ExprUnaryOp::Negate:
    case ast::ExprUnaryOp::Positive:
        if (!is_numeric_type(*operand.type) && !is_error_type(*operand.type)) {
            error_here("numeric unary operator requires Int, Float, or Decimal, got " +
                           operand.type->describe(),
                       expr.range);
        }
        return typed_effect(operand.type ? operand.type->clone() : make_any_type(), operand.effect);
    }

    return error_typed_effect(operand.effect);
}

TypedValue TypeCheckPass::check_binary_expr(const ast::ExprSyntax &expr,
                                            const ValueContext &context) {
    if ((expr.binary_op == ast::ExprBinaryOp::Equal ||
         expr.binary_op == ast::ExprBinaryOp::NotEqual) &&
        expr.first && expr.second &&
        (expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ||
         expr.second->kind == ast::ExprSyntaxKind::NoneLiteral)) {
        const auto &none_operand =
            expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ? *expr.first : *expr.second;
        const auto &value_operand =
            expr.first->kind == ast::ExprSyntaxKind::NoneLiteral ? *expr.second : *expr.first;
        const auto value = check_expr(value_operand, context);
        const auto none = check_expr(none_operand, context, std::cref(*value.type));
        const auto effect = join_effects(value.effect, none.effect);
        if (!is_error_type(*value.type) && !value.type->holds<types::OptionalT>()) {
            error_here("comparison with none requires Optional<T>, got " + value.type->describe(),
                       expr.range);
        }
        return typed_effect(make_type(TypeKind::Bool), effect);
    }

    const auto lhs = check_expr(*expr.first, context);
    const auto rhs = check_expr(*expr.second, context);
    const auto effect = join_effects(lhs.effect, rhs.effect);

    const auto comparable = [&]() {
        return is_error_type(*lhs.type) || is_error_type(*rhs.type) ||
               is_subtype_of(*lhs.type, *rhs.type) || is_subtype_of(*rhs.type, *lhs.type);
    };

    const auto decimal_same_scale = [&]() {
        const auto *lhs_decimal = lhs.type->get_if<types::DecimalT>();
        const auto *rhs_decimal = rhs.type->get_if<types::DecimalT>();
        return lhs_decimal != nullptr && rhs_decimal != nullptr &&
               lhs_decimal->scale == rhs_decimal->scale;
    };

    switch (expr.binary_op) {
    case ast::ExprBinaryOp::Implies:
    case ast::ExprBinaryOp::Or:
    case ast::ExprBinaryOp::And:
        if ((!is_bool_type(*lhs.type) || !is_bool_type(*rhs.type)) && !is_error_type(*lhs.type) &&
            !is_error_type(*rhs.type)) {
            error_here("logical operator requires Bool operands", expr.range);
        }
        return typed_effect(make_type(TypeKind::Bool), effect);
    case ast::ExprBinaryOp::Equal:
    case ast::ExprBinaryOp::NotEqual:
    case ast::ExprBinaryOp::Less:
    case ast::ExprBinaryOp::LessEqual:
    case ast::ExprBinaryOp::Greater:
    case ast::ExprBinaryOp::GreaterEqual:
        if (!comparable()) {
            error_here("comparison operands are not type-compatible: " + lhs.type->describe() +
                           " vs " + rhs.type->describe(),
                       expr.range);
        }
        return typed_effect(make_type(TypeKind::Bool), effect);
    case ast::ExprBinaryOp::Add:
        if (lhs.type->holds<types::StringT>() && rhs.type->holds<types::StringT>()) {
            return typed_effect(string_type(), effect);
        }

        if (decimal_same_scale()) {
            return typed_effect(lhs.type->clone(), effect);
        }

        if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
            return typed_effect(make_type(TypeKind::Int), effect);
        }

        if (lhs.type->holds<types::FloatT>() && rhs.type->holds<types::FloatT>()) {
            return typed_effect(make_type(TypeKind::Float), effect);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '+' is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed_effect(effect);
    case ast::ExprBinaryOp::Subtract:
        if (decimal_same_scale()) {
            return typed_effect(lhs.type->clone(), effect);
        }

        if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
            return typed_effect(make_type(TypeKind::Int), effect);
        }

        if (lhs.type->holds<types::FloatT>() && rhs.type->holds<types::FloatT>()) {
            return typed_effect(make_type(TypeKind::Float), effect);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '-' is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed_effect(effect);
    case ast::ExprBinaryOp::Multiply:
    case ast::ExprBinaryOp::Divide:
        if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
            return typed_effect(make_type(TypeKind::Int), effect);
        }

        if (lhs.type->holds<types::FloatT>() && rhs.type->holds<types::FloatT>()) {
            return typed_effect(make_type(TypeKind::Float), effect);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("arithmetic operator is not defined for " + lhs.type->describe() + " and " +
                           rhs.type->describe(),
                       expr.range);
        }
        return error_typed_effect(effect);
    case ast::ExprBinaryOp::Modulo:
        if (lhs.type->holds<types::IntT>() && rhs.type->holds<types::IntT>()) {
            return typed_effect(make_type(TypeKind::Int), effect);
        }

        if (!is_error_type(*lhs.type) && !is_error_type(*rhs.type)) {
            error_here("operator '%' requires Int operands", expr.range);
        }
        return error_typed_effect(effect);
    }

    return error_typed_effect(effect);
}

TypedValue TypeCheckPass::check_member_access(const ast::ExprSyntax &expr,
                                              const ValueContext &context) {
    const auto base = check_expr(*expr.first, context);
    auto member_type = field_access(*base.type, expr.name, expr.range);
    if (const auto place = place_of_expr(expr);
        place.has_value() && context.flow_facts.has_fact(*place, TypeFactKind::IsNotNone)) {
        if (const auto *optional = member_type->get_if<types::OptionalT>();
            optional != nullptr && optional->inner != nullptr) {
            member_type = optional->inner->clone();
        }
    }
    return typed_effect(member_type, base.effect);
}

TypedValue TypeCheckPass::check_index_access(const ast::ExprSyntax &expr,
                                             const ValueContext &context) {
    const auto collection = check_expr(*expr.first, context);
    const auto index = check_expr(*expr.second, context);
    const auto effect = join_effects(collection.effect, index.effect);

    if (const auto *list = collection.type->get_if<types::ListT>(); list != nullptr) {
        if (!index.type->holds<types::IntT>() && !is_error_type(*index.type)) {
            error_here("list index must have type Int", expr.second->range);
        }

        if (list->element != nullptr) {
            return typed_effect(list->element->clone(), effect);
        }

        return error_typed_effect(effect);
    }

    if (const auto *map = collection.type->get_if<types::MapT>(); map != nullptr) {
        if (map->key != nullptr) {
            (void)check_assignable(*index.type, *map->key, expr.second->range, "map index");
        }

        if (map->value != nullptr) {
            return typed_effect(map->value->clone(), effect);
        }

        return error_typed_effect(effect);
    }

    if (!is_error_type(*collection.type)) {
        error_here("index access requires a List or Map value, got " + collection.type->describe(),
                   expr.range);
    }

    return error_typed_effect(effect);
}

void TypeCheckPass::check_contracts_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::ContractDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::ContractDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::ContractTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = result_.environment.get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        // Record the ContractDecl in the typed HIR program declarations
        // so passers downstream (lowering, rewrite, analysis) can iterate
        // the full top-level surface model without re-entering the AST.
        result_.typed_program.declarations.push_back(TypedDecl{
            .kind = ast::NodeKind::ContractDecl,
            .range = declaration->range,
            .source_id = current_source_id_,
            .associated_agent_symbol = target->get().target,
        });

        for (const auto &clause : decl.clauses) {
            if (!clause->expr && clause->temporal_expr) {
                ValueContext context;
                context.bindings.emplace("input",
                                         clone_or_any(std::cref(*agent_info->get().input_type)));
                check_temporal_embedded_exprs(*clause->temporal_expr, context);
                continue;
            }

            const auto bool_type = make_type(TypeKind::Bool);
            ValueContext context;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*agent_info->get().input_type)));
            if (clause->kind == ast::ContractClauseKind::Ensures) {
                context.bindings.emplace("output",
                                         clone_or_any(std::cref(*agent_info->get().output_type)));
            }

            const auto value = check_expr(*clause->expr, context, std::cref(*bool_type));
            if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
                error_here("contract expression must have type Bool", clause->expr->range);
            }
            if (!value.is_pure) {
                error_here("contract expression must be pure", clause->expr->range);
            }
        }
    }
}

void TypeCheckPass::check_contracts() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_contracts_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_contracts_in_program(require(program_, "typecheck program must exist"));
}

std::uint32_t
TypeCheckPass::check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
                                             const ValueContext &context) {
    TypedTemporalExpr te;
    te.range = expr.range;
    te.source_id = current_source_id_;

    switch (expr.kind) {
    case ast::TemporalExprSyntaxKind::EmbeddedExpr: {
        const auto bool_type = make_type(TypeKind::Bool);
        const auto value = check_expr(*expr.expr, context, std::cref(*bool_type));
        if (!is_bool_type(*value.type) && !is_error_type(*value.type)) {
            error_here("temporal embedded expression must have type Bool", expr.expr->range);
        }
        if (!value.is_pure) {
            error_here("temporal embedded expression must be pure", expr.expr->range);
        }
        te.kind = TypedTemporalKind::Atom;
        te.op = TypedTemporalOp::Atom;
        const std::uint32_t expr_idx = resolve_payload_expr_index(*expr.expr);
        te.children_index.push_back(expr_idx);
        break;
    }
    case ast::TemporalExprSyntaxKind::Called: {
        te.kind = TypedTemporalKind::NameLiteral;
        te.op = TypedTemporalOp::NameLiteralCalled;
        std::string canonical = expr.name;
        if (const auto ref = find_reference_here(ReferenceKind::TemporalCapability, expr.range);
            ref.has_value()) {
            if (const auto sym = resolve_result_.symbol_table.get(ref->get().target);
                sym.has_value()) {
                canonical = sym->get().canonical_name;
            }
        }
        // T1.7 P1: payload_spelling stores the canonical name directly
        // (no "called:" prefix). The op enum encodes the variant.
        te.payload_spelling = std::move(canonical);
        break;
    }
    case ast::TemporalExprSyntaxKind::InState: {
        te.kind = TypedTemporalKind::StateLiteral;
        te.op = TypedTemporalOp::StateLiteral;
        // payload_spelling stores the state name directly (no "state:" prefix).
        te.payload_spelling = expr.name;
        break;
    }
    case ast::TemporalExprSyntaxKind::Running: {
        te.kind = TypedTemporalKind::NameLiteral;
        te.op = TypedTemporalOp::NameLiteralRunning;
        // payload_spelling stores the node name directly (no "running:" prefix).
        te.payload_spelling = expr.name;
        break;
    }
    case ast::TemporalExprSyntaxKind::Completed: {
        te.kind = TypedTemporalKind::NameLiteral;
        te.op = TypedTemporalOp::NameLiteralCompleted;
        // payload_spelling encodes "node_name|optional_state_name" for the
        // Completed variant so lowering can extract both without AST.
        std::string data = expr.name;
        if (expr.state_name.has_value()) {
            data += "|";
            data += *expr.state_name;
        }
        te.payload_spelling = std::move(data);
        break;
    }
    case ast::TemporalExprSyntaxKind::Unary: {
        const std::uint32_t child_idx = check_temporal_embedded_exprs(*expr.first, context);
        te.kind = TypedTemporalKind::Unary;
        te.children_index.push_back(child_idx);
        switch (expr.unary_op) {
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
        break;
    }
    case ast::TemporalExprSyntaxKind::Binary: {
        const std::uint32_t lhs_idx = check_temporal_embedded_exprs(*expr.first, context);
        const std::uint32_t rhs_idx = check_temporal_embedded_exprs(*expr.second, context);
        te.kind = TypedTemporalKind::Binary;
        te.children_index.push_back(lhs_idx);
        te.children_index.push_back(rhs_idx);
        switch (expr.binary_op) {
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
        break;
    }
    }

    result_.typed_program.temporal_exprs.push_back(std::move(te));
    return static_cast<std::uint32_t>(result_.typed_program.temporal_exprs.size() - 1);
}

void TypeCheckPass::check_flows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::FlowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::FlowDecl &>(*declaration);
        const auto target = find_reference_here(ReferenceKind::FlowTarget, decl.target->range);
        if (!target.has_value()) {
            continue;
        }

        const auto agent_info = result_.environment.get_agent(target->get().target);
        if (!agent_info.has_value()) {
            continue;
        }

        // Record the FlowDecl in the typed HIR program declarations so
        // downstream passes can enumerate all agent-associated top-level
        // constructs without walking the AST.
        result_.typed_program.declarations.push_back(TypedDecl{
            .kind = ast::NodeKind::FlowDecl,
            .range = declaration->range,
            .source_id = current_source_id_,
            .associated_agent_symbol = target->get().target,
        });

        for (const auto &handler : decl.state_handlers) {
            ValueContext context;
            context.call_context = CallContext::Flow;
            context.current_agent = target->get().target;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*agent_info->get().input_type)));
            context.bindings.emplace("ctx",
                                     clone_or_any(std::cref(*agent_info->get().context_type)));
            check_block(*handler->body,
                        context,
                        std::cref(*agent_info->get().output_type),
                        handler->state_name,
                        agent_info->get().declaration_range);
        }
    }
}

void TypeCheckPass::check_flows() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_flows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_flows_in_program(require(program_, "typecheck program must exist"));
}

void TypeCheckPass::check_workflows_in_program(const ast::Program &program) {
    for (const auto &declaration : program.declarations) {
        if (declaration->kind != ast::NodeKind::WorkflowDecl) {
            continue;
        }

        const auto &decl = static_cast<const ast::WorkflowDecl &>(*declaration);
        const auto workflow_symbol = find_local_here(SymbolNamespace::Workflows, decl.name);
        if (!workflow_symbol.has_value()) {
            continue;
        }

        const auto workflow_info = result_.environment.get_workflow(workflow_symbol->get().id);
        if (!workflow_info.has_value()) {
            continue;
        }

        BindingMap all_node_outputs;

        for (const auto &node : decl.nodes) {
            const auto target =
                find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = result_.environment.get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            all_node_outputs.emplace(node->name,
                                     clone_or_any(std::cref(*agent_info->get().output_type)));
        }

        for (const auto &node : decl.nodes) {
            const auto target =
                find_reference_here(ReferenceKind::WorkflowNodeTarget, node->target->range);
            if (!target.has_value()) {
                continue;
            }

            const auto agent_info = result_.environment.get_agent(target->get().target);
            if (!agent_info.has_value()) {
                continue;
            }

            ValueContext context;
            context.call_context = CallContext::Workflow;
            context.bindings.emplace("input",
                                     clone_or_any(std::cref(*workflow_info->get().input_type)));

            for (const auto &dependency : node->after) {
                const auto dependency_type = find_binding(all_node_outputs, dependency);
                if (dependency_type.has_value()) {
                    context.bindings.emplace(dependency, dependency_type->get().clone());
                }
            }

            const auto argument =
                check_expr(*node->input, context, std::cref(*agent_info->get().input_type));
            (void)check_exact_schema_boundary(*argument.type,
                                              *agent_info->get().input_type,
                                              SchemaBoundaryKind::WorkflowNodeInput,
                                              node->input->range,
                                              agent_info->get().declaration_range);
        }

        ValueContext return_context;
        return_context.call_context = CallContext::Workflow;
        return_context.bindings.emplace("input",
                                        clone_or_any(std::cref(*workflow_info->get().input_type)));
        for (const auto &[name, type] : all_node_outputs) {
            return_context.bindings.emplace(name, type ? type->clone() : make_any_type());
        }

        for (const auto &formula : decl.safety) {
            check_temporal_embedded_exprs(*formula, return_context);
        }

        for (const auto &formula : decl.liveness) {
            check_temporal_embedded_exprs(*formula, return_context);
        }

        const auto return_value = check_expr(
            *decl.return_value, return_context, std::cref(*workflow_info->get().output_type));
        (void)check_exact_schema_boundary(*return_value.type,
                                          *workflow_info->get().output_type,
                                          SchemaBoundaryKind::WorkflowOutput,
                                          decl.return_value->range,
                                          workflow_info->get().declaration_range);
    }
}

void TypeCheckPass::check_workflows() {
    if (graph_ != nullptr) {
        for (const auto &source : graph_->sources) {
            enter_source(source);
            check_workflows_in_program(
                require(source.program.get(), "source graph program must exist before typecheck"));
            leave_source();
        }
        return;
    }

    check_workflows_in_program(require(program_, "typecheck program must exist"));
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
    result_.typed_program.blocks.push_back(TypedBlock{
        .range = block.range,
        .source_id = current_source_id_,
        .statement_indexes = {},
    });
    const auto my_block_idx = result_.typed_program.blocks.size() - 1;

    for (const auto &statement : block.statements) {
        check_statement(*statement, context, expected_return_type, state_name,
                        expected_return_origin);
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
            Diagnostic diagnostic{
                .severity = DiagnosticSeverity::Warning,
                .message = "let binding '" + statement.let_stmt->name +
                           "' shadows an existing binding of type '" + previous->get().describe() +
                           "'",
                .code = error_codes::typecheck::ShadowedBinding.full_code(),
                .range = statement.let_stmt->range,
                .source_name = std::nullopt,
                .position = std::nullopt,
                .related = {},
            };
            if (current_source_ != nullptr) {
                diagnostic.source_name = current_source_->source.display_name;
                diagnostic.position =
                    current_source_->source.locate(statement.let_stmt->range.begin_offset);
            }
            result_.diagnostics.add_diagnostic(std::move(diagnostic));
        }

        if (expected.has_value()) {
            context.bindings[statement.let_stmt->name] =
                annotated_type ? annotated_type->clone() : make_any_type();
        } else {
            context.bindings[statement.let_stmt->name] =
                initializer.type ? initializer.type->clone() : make_any_type();
        }

        // Determine the let type-ref strategy and (optional) spelling string.
        // The lowering pass reconstructs an ir::TypeRef from this pair plus
        // the initializer's resolved type without consulting the AST:
        //   * FromSyntax: user wrote a type annotation; spelling = canonical
        //     TypeSyntax text (e.g. "Optional<Foo>").
        //   * FromInitializerType: no annotation; binding type was inferred
        //     from the initializer.
        //   * AnySentinel: reserved for error paths.
        LetTypeRefStrategy let_strategy = LetTypeRefStrategy::FromInitializerType;
        std::string let_type_spelling;
        if (statement.let_stmt->type) {
            let_strategy = LetTypeRefStrategy::FromSyntax;
            let_type_spelling = statement.let_stmt->type->spelling();
        } else if (!initializer.type) {
            let_strategy = LetTypeRefStrategy::AnySentinel;
        } else {
            let_strategy = LetTypeRefStrategy::FromInitializerType;
        }

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::Let,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.let_stmt->initializer)},
            .target_name = statement.let_stmt->name,
            .let_type_ref_strategy = let_strategy,
            .let_type_ref_spelling = std::move(let_type_spelling),
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
        break;
    }
    case ast::StatementSyntaxKind::Assign: {
        if (statement.assign_stmt->target->root_name != "ctx") {
            typecheck_error_here(error_codes::typecheck::InvalidOperation,
                                 "assignment target must be rooted at writable 'ctx'",
                                 statement.assign_stmt->target->range);
        } else {
            context.flow_facts.invalidate(
                Place{.root = statement.assign_stmt->target->root_name,
                      .members = statement.assign_stmt->target->members});
            const auto target = check_path(*statement.assign_stmt->target, context);
            const auto expectation = TypeExpectation{
                .expected = target.type ? target.type->clone() : make_any_type(),
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

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::Assign,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index =
                statement.assign_stmt->value
                    ? std::vector<std::uint32_t>{resolve_payload_expr_index(
                          *statement.assign_stmt->value)}
                    : std::vector<std::uint32_t>{UINT32_MAX},
            .target_name = path_spelling(*statement.assign_stmt->target),
            .assign_target_root_kind = assign_target_root_kind_of(*statement.assign_stmt->target),
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
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
            error_here("if condition must have type Bool", statement.if_stmt->condition->range);
        }
        if (!condition.is_pure) {
            error_here("if condition must be pure", statement.if_stmt->condition->range);
        }

        const auto condition_facts = extract_condition_facts(*statement.if_stmt->condition);
        if (options_.explain_narrowing) {
            const std::string condition_text = current_source_ != nullptr
                                                   ? std::string(current_source_->source.slice(
                                                         statement.if_stmt->condition->range))
                                                   : expr_spelling(*statement.if_stmt->condition);
            bool emitted_explanation = false;
            for (const auto &fact : condition_facts.when_true.facts) {
                note_here("narrowing: condition '" + condition_text + "' narrows '" +
                              place_spelling(fact.place) + "' to " +
                              std::string(narrowed_fact_description(fact.kind)) + " on then branch",
                          fact.origin);
                emitted_explanation = true;
            }
            for (const auto &fact : condition_facts.when_false.facts) {
                note_here("narrowing: condition '" + condition_text + "' narrows '" +
                              place_spelling(fact.place) + "' to " +
                              std::string(narrowed_fact_description(fact.kind)) + " on else branch",
                          fact.origin);
                emitted_explanation = true;
            }
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

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::If,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.if_stmt->condition)},
            .then_block_index = find_block_index_by_range(*statement.if_stmt->then_block),
            .else_block_index = statement.if_stmt->else_block
                                    ? find_block_index_by_range(*statement.if_stmt->else_block)
                                    : UINT32_MAX,
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
        break;
    }
    case ast::StatementSyntaxKind::Goto: {
        // GotoStmtSyntax::target_state is always populated by the parser; the
        // typed-record field mirrors it directly so lowering never needs to
        // reach back into the AST. The empty-string fallback in
        // typed_hir_lower.cpp is now dead code (kept for T1.7 backward compat).
        const std::string target_state = statement.goto_stmt
                                             ? statement.goto_stmt->target_state
                                             : std::string{};
        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::Goto,
            .range = statement.range,
            .source_id = current_source_id_,
            .goto_target_state = target_state,
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
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

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::Return,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {value_index},
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
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
            error_here("assert condition must have type Bool",
                       statement.assert_stmt->condition->range);
        }
        if (!condition.is_pure) {
            error_here("assert condition must be pure", statement.assert_stmt->condition->range);
        }

        // Assert message: currently AssertStmtSyntax in the AST does not
        // carry a user-facing message (field reserved for a future AST
        // extension). We deliberately leave assert_message empty so the
        // typed record remains forward-compatible: once the AST gains a
        // message field the filling here will light up.
        std::string assert_msg;
        (void)assert_msg; // silence unused warning when the AST lacks the field

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::Assert,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.assert_stmt->condition)},
            .assert_message = std::move(assert_msg),
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
        break;
    }
    case ast::StatementSyntaxKind::Expr: {
        (void)check_expr(*statement.expr_stmt->expr, context);

        result_.typed_program.statements.push_back(TypedStatement{
            .kind = TypedStmtKind::ExprStatement,
            .range = statement.range,
            .source_id = current_source_id_,
            .children_expr_index = {resolve_payload_expr_index(*statement.expr_stmt->expr)},
        });
        last_written_statement_index_ = result_.typed_program.statements.size() - 1;
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
