#include "ahfl/ir/ir.hpp"

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] std::string join(const std::vector<std::string> &parts, std::string_view delimiter) {
    std::ostringstream builder;

    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            builder << delimiter;
        }

        builder << parts[index];
    }

    return builder.str();
}

[[nodiscard]] std::string sanitize_identifier(std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size() + 2);

    for (const auto character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            sanitized.push_back(character);
        } else {
            sanitized.push_back('_');
        }
    }

    while (sanitized.find("__") != std::string::npos) {
        sanitized.replace(sanitized.find("__"), 2, "_");
    }

    if (sanitized.empty()) {
        return "id";
    }

    if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0) {
        sanitized.insert(sanitized.begin(), 'n');
        sanitized.insert(sanitized.begin() + 1, '_');
    }

    return sanitized;
}

[[nodiscard]] std::string
called_observation_symbol(std::string_view agent_name, std::string_view capability_name) {
    return "agent__" + sanitize_identifier(agent_name) + "__called__" +
           sanitize_identifier(capability_name);
}

[[nodiscard]] std::string observation_scope_base_name(const ir::FormalObservationScope &scope) {
    switch (scope.kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract__" + scope.owner + "__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow__" + scope.owner + "__safety__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow__" + scope.owner + "__liveness__" + std::to_string(scope.clause_index);
    }

    return "observation";
}

[[nodiscard]] std::string embedded_observation_symbol(const ir::FormalObservationScope &scope) {
    return sanitize_identifier(observation_scope_base_name(scope) + "__atom__" +
                               std::to_string(scope.atom_index));
}

[[nodiscard]] ir::PathRootKind lower_path_root_kind(ast::PathRootKind kind) {
    switch (kind) {
    case ast::PathRootKind::Identifier:
        return ir::PathRootKind::Identifier;
    case ast::PathRootKind::Input:
        return ir::PathRootKind::Input;
    case ast::PathRootKind::Output:
        return ir::PathRootKind::Output;
    }

    return ir::PathRootKind::Identifier;
}

[[nodiscard]] ir::ExprUnaryOp lower_expr_unary_op(ast::ExprUnaryOp op) {
    switch (op) {
    case ast::ExprUnaryOp::Not:
        return ir::ExprUnaryOp::Not;
    case ast::ExprUnaryOp::Negate:
        return ir::ExprUnaryOp::Negate;
    case ast::ExprUnaryOp::Positive:
        return ir::ExprUnaryOp::Positive;
    }

    return ir::ExprUnaryOp::Not;
}

[[nodiscard]] ir::ExprBinaryOp lower_expr_binary_op(ast::ExprBinaryOp op) {
    switch (op) {
    case ast::ExprBinaryOp::Implies:
        return ir::ExprBinaryOp::Implies;
    case ast::ExprBinaryOp::Or:
        return ir::ExprBinaryOp::Or;
    case ast::ExprBinaryOp::And:
        return ir::ExprBinaryOp::And;
    case ast::ExprBinaryOp::Equal:
        return ir::ExprBinaryOp::Equal;
    case ast::ExprBinaryOp::NotEqual:
        return ir::ExprBinaryOp::NotEqual;
    case ast::ExprBinaryOp::Less:
        return ir::ExprBinaryOp::Less;
    case ast::ExprBinaryOp::LessEqual:
        return ir::ExprBinaryOp::LessEqual;
    case ast::ExprBinaryOp::Greater:
        return ir::ExprBinaryOp::Greater;
    case ast::ExprBinaryOp::GreaterEqual:
        return ir::ExprBinaryOp::GreaterEqual;
    case ast::ExprBinaryOp::Add:
        return ir::ExprBinaryOp::Add;
    case ast::ExprBinaryOp::Subtract:
        return ir::ExprBinaryOp::Subtract;
    case ast::ExprBinaryOp::Multiply:
        return ir::ExprBinaryOp::Multiply;
    case ast::ExprBinaryOp::Divide:
        return ir::ExprBinaryOp::Divide;
    case ast::ExprBinaryOp::Modulo:
        return ir::ExprBinaryOp::Modulo;
    }

    return ir::ExprBinaryOp::Implies;
}

[[nodiscard]] ir::TemporalUnaryOp lower_temporal_unary_op(ast::TemporalUnaryOp op) {
    switch (op) {
    case ast::TemporalUnaryOp::Always:
        return ir::TemporalUnaryOp::Always;
    case ast::TemporalUnaryOp::Eventually:
        return ir::TemporalUnaryOp::Eventually;
    case ast::TemporalUnaryOp::Next:
        return ir::TemporalUnaryOp::Next;
    case ast::TemporalUnaryOp::Not:
        return ir::TemporalUnaryOp::Not;
    }

    return ir::TemporalUnaryOp::Always;
}

[[nodiscard]] ir::TemporalBinaryOp lower_temporal_binary_op(ast::TemporalBinaryOp op) {
    switch (op) {
    case ast::TemporalBinaryOp::Implies:
        return ir::TemporalBinaryOp::Implies;
    case ast::TemporalBinaryOp::Or:
        return ir::TemporalBinaryOp::Or;
    case ast::TemporalBinaryOp::And:
        return ir::TemporalBinaryOp::And;
    case ast::TemporalBinaryOp::Until:
        return ir::TemporalBinaryOp::Until;
    }

    return ir::TemporalBinaryOp::Implies;
}

[[nodiscard]] ir::ContractClauseKind lower_contract_clause_kind(ast::ContractClauseKind kind) {
    switch (kind) {
    case ast::ContractClauseKind::Requires:
        return ir::ContractClauseKind::Requires;
    case ast::ContractClauseKind::Ensures:
        return ir::ContractClauseKind::Ensures;
    case ast::ContractClauseKind::Invariant:
        return ir::ContractClauseKind::Invariant;
    case ast::ContractClauseKind::Forbid:
        return ir::ContractClauseKind::Forbid;
    }

    return ir::ContractClauseKind::Requires;
}

[[nodiscard]] std::string expr_binary_op_name(ir::ExprBinaryOp op) {
    switch (op) {
    case ir::ExprBinaryOp::Implies:
        return "=>";
    case ir::ExprBinaryOp::Or:
        return "||";
    case ir::ExprBinaryOp::And:
        return "&&";
    case ir::ExprBinaryOp::Equal:
        return "==";
    case ir::ExprBinaryOp::NotEqual:
        return "!=";
    case ir::ExprBinaryOp::Less:
        return "<";
    case ir::ExprBinaryOp::LessEqual:
        return "<=";
    case ir::ExprBinaryOp::Greater:
        return ">";
    case ir::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ir::ExprBinaryOp::Add:
        return "+";
    case ir::ExprBinaryOp::Subtract:
        return "-";
    case ir::ExprBinaryOp::Multiply:
        return "*";
    case ir::ExprBinaryOp::Divide:
        return "/";
    case ir::ExprBinaryOp::Modulo:
        return "%";
    }

    return "<invalid-binary-op>";
}

[[nodiscard]] std::string expr_unary_op_name(ir::ExprUnaryOp op) {
    switch (op) {
    case ir::ExprUnaryOp::Not:
        return "!";
    case ir::ExprUnaryOp::Negate:
        return "-";
    case ir::ExprUnaryOp::Positive:
        return "+";
    }

    return "<invalid-unary-op>";
}

[[nodiscard]] std::string temporal_binary_op_name(ir::TemporalBinaryOp op) {
    switch (op) {
    case ir::TemporalBinaryOp::Implies:
        return "=>";
    case ir::TemporalBinaryOp::Or:
        return "||";
    case ir::TemporalBinaryOp::And:
        return "&&";
    case ir::TemporalBinaryOp::Until:
        return "U";
    }

    return "<invalid-temporal-binary-op>";
}

[[nodiscard]] std::string temporal_unary_op_name(ir::TemporalUnaryOp op) {
    switch (op) {
    case ir::TemporalUnaryOp::Always:
        return "G";
    case ir::TemporalUnaryOp::Eventually:
        return "F";
    case ir::TemporalUnaryOp::Next:
        return "X";
    case ir::TemporalUnaryOp::Not:
        return "!";
    }

    return "<invalid-temporal-unary-op>";
}

[[nodiscard]] std::string contract_clause_name(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return "requires";
    case ir::ContractClauseKind::Ensures:
        return "ensures";
    case ir::ContractClauseKind::Invariant:
        return "invariant";
    case ir::ContractClauseKind::Forbid:
        return "forbid";
    }

    return "<invalid-contract-clause>";
}

[[nodiscard]] std::string quota_item_name(ast::AgentQuotaItemKind kind) {
    switch (kind) {
    case ast::AgentQuotaItemKind::MaxToolCalls:
        return "max_tool_calls";
    case ast::AgentQuotaItemKind::MaxExecutionTime:
        return "max_execution_time";
    }

    return "<invalid-quota-item>";
}

[[nodiscard]] bool needs_grouping_for_suffix(const ir::Expr &expr) {
    return std::visit(Overloaded{
                          [](const ir::NoneLiteralExpr &) { return false; },
                          [](const ir::BoolLiteralExpr &) { return false; },
                          [](const ir::IntegerLiteralExpr &) { return false; },
                          [](const ir::FloatLiteralExpr &) { return false; },
                          [](const ir::DecimalLiteralExpr &) { return false; },
                          [](const ir::StringLiteralExpr &) { return false; },
                          [](const ir::DurationLiteralExpr &) { return false; },
                          [](const ir::PathExpr &) { return false; },
                          [](const ir::QualifiedValueExpr &) { return false; },
                          [](const ir::CallExpr &) { return false; },
                          [](const ir::StructLiteralExpr &) { return false; },
                          [](const ir::ListLiteralExpr &) { return false; },
                          [](const ir::SetLiteralExpr &) { return false; },
                          [](const ir::MapLiteralExpr &) { return false; },
                          [](const ir::MemberAccessExpr &) { return false; },
                          [](const ir::IndexAccessExpr &) { return false; },
                          [](const ir::GroupExpr &) { return false; },
                          [](const ir::SomeExpr &) { return true; },
                          [](const ir::UnaryExpr &) { return true; },
                          [](const ir::BinaryExpr &) { return true; },
                      },
                      expr.node);
}

class IrLowerer final {
  public:
    IrLowerer(const ast::Program &program,
              const ResolveResult &resolve_result,
              const TypeCheckResult &type_check_result)
        : program_(program), resolve_result_(resolve_result),
          type_check_result_(type_check_result) {}

    [[nodiscard]] ir::Program lower() const {
        ir::Program program_ir;
        program_ir.declarations.reserve(program_.declarations.size());

        for (const auto &declaration : program_.declarations) {
            program_ir.declarations.push_back(lower_declaration(*declaration));
        }

        return program_ir;
    }

  private:
    const ast::Program &program_;
    const ResolveResult &resolve_result_;
    const TypeCheckResult &type_check_result_;

    template <typename T> [[nodiscard]] ir::ExprPtr make_expr(T node) const {
        auto expr = make_owned<ir::Expr>();
        expr->node = std::move(node);
        return expr;
    }

    template <typename T> [[nodiscard]] ir::TemporalExprPtr make_temporal(T node) const {
        auto expr = make_owned<ir::TemporalExpr>();
        expr->node = std::move(node);
        return expr;
    }

    template <typename T> [[nodiscard]] ir::StatementPtr make_statement(T node) const {
        auto statement = make_owned<ir::Statement>();
        statement->node = std::move(node);
        return statement;
    }

    [[nodiscard]] const SymbolTable &symbols() const noexcept {
        return resolve_result_.symbol_table;
    }

    [[nodiscard]] const TypeEnvironment &environment() const noexcept {
        return type_check_result_.environment;
    }

    [[nodiscard]] MaybeCRef<Symbol> find_local_symbol(SymbolNamespace name_space,
                                                      std::string_view name) const {
        return symbols().find_local(name_space, name);
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_from_reference(ReferenceKind kind,
                                                          SourceRange range) const {
        const auto reference = resolve_result_.find_reference(kind, range);
        if (!reference.has_value()) {
            return std::nullopt;
        }

        return symbols().get(reference->get().target);
    }

    [[nodiscard]] std::string canonical_name_from_reference(ReferenceKind kind,
                                                            SourceRange range,
                                                            std::string_view fallback) const {
        const auto symbol = symbol_from_reference(kind, range);
        if (!symbol.has_value()) {
            return std::string(fallback);
        }

        return symbol->get().canonical_name;
    }

    [[nodiscard]] std::string canonical_local_name(SymbolNamespace name_space,
                                                   std::string_view local_name) const {
        const auto symbol = find_local_symbol(name_space, local_name);
        if (!symbol.has_value()) {
            return std::string(local_name);
        }

        return symbol->get().canonical_name;
    }

    [[nodiscard]] std::string describe_type(MaybeCRef<Type> type) const {
        if (!type.has_value()) {
            return "Any";
        }

        return type->get().describe();
    }

    [[nodiscard]] std::string render_type_syntax(const ast::TypeSyntax &type) const {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Unit:
        case ast::TypeSyntaxKind::Bool:
        case ast::TypeSyntaxKind::Int:
        case ast::TypeSyntaxKind::Float:
        case ast::TypeSyntaxKind::String:
        case ast::TypeSyntaxKind::UUID:
        case ast::TypeSyntaxKind::Timestamp:
        case ast::TypeSyntaxKind::Duration:
        case ast::TypeSyntaxKind::BoundedString:
        case ast::TypeSyntaxKind::Decimal:
            return type.spelling();
        case ast::TypeSyntaxKind::Named:
            return canonical_name_from_reference(
                ReferenceKind::TypeName, type.name->range, type.name->spelling());
        case ast::TypeSyntaxKind::Optional:
            return "Optional<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::List:
            return "List<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::Set:
            return "Set<" + render_type_syntax(*type.first) + ">";
        case ast::TypeSyntaxKind::Map:
            return "Map<" + render_type_syntax(*type.first) + ", " +
                   render_type_syntax(*type.second) + ">";
        }

        return "<invalid-type>";
    }

    [[nodiscard]] std::vector<std::string>
    lower_retry_targets(const std::vector<Owned<ast::QualifiedName>> &targets) const {
        std::vector<std::string> names;
        names.reserve(targets.size());

        for (const auto &target : targets) {
            names.push_back(target->spelling());
        }

        return names;
    }

    [[nodiscard]] std::string render_call_target(const ast::QualifiedName &name) const {
        return canonical_name_from_reference(
            ReferenceKind::CallTarget, name.range, name.spelling());
    }

    [[nodiscard]] std::string render_struct_target(const ast::QualifiedName &name) const {
        return canonical_name_from_reference(ReferenceKind::TypeName, name.range, name.spelling());
    }

    [[nodiscard]] std::string render_qualified_value(const ast::ExprSyntax &expr) const {
        const auto const_symbol =
            symbol_from_reference(ReferenceKind::ConstValue, expr.qualified_name->range);
        if (const_symbol.has_value()) {
            return const_symbol->get().canonical_name;
        }

        const auto owner_symbol = symbol_from_reference(ReferenceKind::QualifiedValueOwnerType,
                                                        expr.qualified_name->range);
        if (!owner_symbol.has_value()) {
            return expr.qualified_name->spelling();
        }

        const auto &segments = expr.qualified_name->segments;
        if (segments.empty()) {
            return owner_symbol->get().canonical_name;
        }

        return owner_symbol->get().canonical_name + "::" + segments.back();
    }

    [[nodiscard]] ir::Path lower_path(const ast::PathSyntax &path) const {
        return ir::Path{
            .root_kind = lower_path_root_kind(path.root_kind),
            .root_name = path.root_name,
            .members = path.members,
        };
    }

    [[nodiscard]] ir::ExprPtr lower_expr(const ast::ExprSyntax &expr) const {
        switch (expr.kind) {
        case ast::ExprSyntaxKind::BoolLiteral:
            return make_expr(ir::BoolLiteralExpr{.value = expr.bool_value});
        case ast::ExprSyntaxKind::IntegerLiteral:
            return make_expr(ir::IntegerLiteralExpr{
                .spelling = expr.integer_literal ? expr.integer_literal->spelling : "0",
            });
        case ast::ExprSyntaxKind::FloatLiteral:
            return make_expr(ir::FloatLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::DecimalLiteral:
            return make_expr(ir::DecimalLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::StringLiteral:
            return make_expr(ir::StringLiteralExpr{.spelling = expr.text});
        case ast::ExprSyntaxKind::DurationLiteral:
            return make_expr(ir::DurationLiteralExpr{
                .spelling = expr.duration_literal ? expr.duration_literal->spelling : expr.text,
            });
        case ast::ExprSyntaxKind::NoneLiteral:
            return make_expr(ir::NoneLiteralExpr{});
        case ast::ExprSyntaxKind::Some:
            return make_expr(ir::SomeExpr{.value = lower_expr(*expr.first)});
        case ast::ExprSyntaxKind::Path:
            return make_expr(ir::PathExpr{.path = lower_path(*expr.path)});
        case ast::ExprSyntaxKind::QualifiedValue:
            return make_expr(ir::QualifiedValueExpr{.value = render_qualified_value(expr)});
        case ast::ExprSyntaxKind::Call: {
            ir::CallExpr call{
                .callee = render_call_target(*expr.qualified_name),
            };
            call.arguments.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                call.arguments.push_back(lower_expr(*item));
            }
            return make_expr(std::move(call));
        }
        case ast::ExprSyntaxKind::StructLiteral: {
            ir::StructLiteralExpr literal{
                .type_name = render_struct_target(*expr.qualified_name),
            };
            literal.fields.reserve(expr.struct_fields.size());
            for (const auto &field : expr.struct_fields) {
                literal.fields.push_back(ir::StructFieldInit{
                    .name = field->field_name,
                    .value = lower_expr(*field->value),
                });
            }
            return make_expr(std::move(literal));
        }
        case ast::ExprSyntaxKind::ListLiteral: {
            ir::ListLiteralExpr literal;
            literal.items.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                literal.items.push_back(lower_expr(*item));
            }
            return make_expr(std::move(literal));
        }
        case ast::ExprSyntaxKind::SetLiteral: {
            ir::SetLiteralExpr literal;
            literal.items.reserve(expr.items.size());
            for (const auto &item : expr.items) {
                literal.items.push_back(lower_expr(*item));
            }
            return make_expr(std::move(literal));
        }
        case ast::ExprSyntaxKind::MapLiteral: {
            ir::MapLiteralExpr literal;
            literal.entries.reserve(expr.map_entries.size());
            for (const auto &entry : expr.map_entries) {
                literal.entries.push_back(ir::MapEntryExpr{
                    .key = lower_expr(*entry->key),
                    .value = lower_expr(*entry->value),
                });
            }
            return make_expr(std::move(literal));
        }
        case ast::ExprSyntaxKind::Unary:
            return make_expr(ir::UnaryExpr{
                .op = lower_expr_unary_op(expr.unary_op),
                .operand = lower_expr(*expr.first),
            });
        case ast::ExprSyntaxKind::Binary:
            return make_expr(ir::BinaryExpr{
                .op = lower_expr_binary_op(expr.binary_op),
                .lhs = lower_expr(*expr.first),
                .rhs = lower_expr(*expr.second),
            });
        case ast::ExprSyntaxKind::MemberAccess:
            return make_expr(ir::MemberAccessExpr{
                .base = lower_expr(*expr.first),
                .member = expr.name,
            });
        case ast::ExprSyntaxKind::IndexAccess:
            return make_expr(ir::IndexAccessExpr{
                .base = lower_expr(*expr.first),
                .index = lower_expr(*expr.second),
            });
        case ast::ExprSyntaxKind::Group:
            return make_expr(ir::GroupExpr{.expr = lower_expr(*expr.first)});
        }

        return make_expr(ir::QualifiedValueExpr{.value = "<invalid-expr>"});
    }

    [[nodiscard]] ir::TemporalExprPtr lower_temporal(const ast::TemporalExprSyntax &expr) const {
        switch (expr.kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            return make_temporal(ir::EmbeddedTemporalExpr{.expr = lower_expr(*expr.expr)});
        case ast::TemporalExprSyntaxKind::Called:
            return make_temporal(ir::CalledTemporalExpr{
                .capability = canonical_name_from_reference(
                    ReferenceKind::TemporalCapability, expr.range, expr.name),
            });
        case ast::TemporalExprSyntaxKind::InState:
            return make_temporal(ir::InStateTemporalExpr{.state = expr.name});
        case ast::TemporalExprSyntaxKind::Running:
            return make_temporal(ir::RunningTemporalExpr{.node = expr.name});
        case ast::TemporalExprSyntaxKind::Completed:
            return make_temporal(ir::CompletedTemporalExpr{
                .node = expr.name,
                .state_name = expr.state_name,
            });
        case ast::TemporalExprSyntaxKind::Unary:
            return make_temporal(ir::TemporalUnaryExpr{
                .op = lower_temporal_unary_op(expr.unary_op),
                .operand = lower_temporal(*expr.first),
            });
        case ast::TemporalExprSyntaxKind::Binary:
            return make_temporal(ir::TemporalBinaryExpr{
                .op = lower_temporal_binary_op(expr.binary_op),
                .lhs = lower_temporal(*expr.first),
                .rhs = lower_temporal(*expr.second),
            });
        }

        return make_temporal(ir::CalledTemporalExpr{.capability = "<invalid-temporal-expr>"});
    }

    [[nodiscard]] std::string inferred_statement_type(const ast::LetStmtSyntax &statement) const {
        if (statement.type) {
            return render_type_syntax(*statement.type);
        }

        if (statement.initializer) {
            const auto expression_type =
                type_check_result_.find_expression_type(statement.initializer->range);
            if (expression_type.has_value() && expression_type->get().type) {
                return expression_type->get().type->describe();
            }
        }

        return "Any";
    }

    [[nodiscard]] ir::Block lower_block(const ast::BlockSyntax &block) const {
        ir::Block ir_block;
        ir_block.statements.reserve(block.statements.size());

        for (const auto &statement : block.statements) {
            ir_block.statements.push_back(lower_statement(*statement));
        }

        return ir_block;
    }

    [[nodiscard]] ir::StatementPtr lower_statement(const ast::StatementSyntax &statement) const {
        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
            return make_statement(ir::LetStatement{
                .name = statement.let_stmt->name,
                .type = inferred_statement_type(*statement.let_stmt),
                .initializer = lower_expr(*statement.let_stmt->initializer),
            });
        case ast::StatementSyntaxKind::Assign:
            return make_statement(ir::AssignStatement{
                .target = lower_path(*statement.assign_stmt->target),
                .value = lower_expr(*statement.assign_stmt->value),
            });
        case ast::StatementSyntaxKind::If:
            return make_statement(ir::IfStatement{
                .condition = lower_expr(*statement.if_stmt->condition),
                .then_block = make_owned<ir::Block>(lower_block(*statement.if_stmt->then_block)),
                .else_block = statement.if_stmt->else_block ? make_owned<ir::Block>(lower_block(
                                                                  *statement.if_stmt->else_block))
                                                            : nullptr,
            });
        case ast::StatementSyntaxKind::Goto:
            return make_statement(
                ir::GotoStatement{.target_state = statement.goto_stmt->target_state});
        case ast::StatementSyntaxKind::Return:
            return make_statement(ir::ReturnStatement{
                .value = statement.return_stmt->value ? lower_expr(*statement.return_stmt->value)
                                                      : nullptr,
            });
        case ast::StatementSyntaxKind::Assert:
            return make_statement(
                ir::AssertStatement{.condition = lower_expr(*statement.assert_stmt->condition)});
        case ast::StatementSyntaxKind::Expr:
            return make_statement(ir::ExprStatement{
                .expr = lower_expr(*statement.expr_stmt->expr),
            });
        }

        return make_statement(ir::ExprStatement{
            .expr = make_expr(ir::QualifiedValueExpr{.value = "<invalid-statement>"}),
        });
    }

    [[nodiscard]] ir::Decl lower_declaration(const ast::Decl &declaration) const {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
            return lower_module(static_cast<const ast::ModuleDecl &>(declaration));
        case ast::NodeKind::ImportDecl:
            return lower_import(static_cast<const ast::ImportDecl &>(declaration));
        case ast::NodeKind::ConstDecl:
            return lower_const(static_cast<const ast::ConstDecl &>(declaration));
        case ast::NodeKind::TypeAliasDecl:
            return lower_type_alias(static_cast<const ast::TypeAliasDecl &>(declaration));
        case ast::NodeKind::StructDecl:
            return lower_struct(static_cast<const ast::StructDecl &>(declaration));
        case ast::NodeKind::EnumDecl:
            return lower_enum(static_cast<const ast::EnumDecl &>(declaration));
        case ast::NodeKind::CapabilityDecl:
            return lower_capability(static_cast<const ast::CapabilityDecl &>(declaration));
        case ast::NodeKind::PredicateDecl:
            return lower_predicate(static_cast<const ast::PredicateDecl &>(declaration));
        case ast::NodeKind::AgentDecl:
            return lower_agent(static_cast<const ast::AgentDecl &>(declaration));
        case ast::NodeKind::ContractDecl:
            return lower_contract(static_cast<const ast::ContractDecl &>(declaration));
        case ast::NodeKind::FlowDecl:
            return lower_flow(static_cast<const ast::FlowDecl &>(declaration));
        case ast::NodeKind::WorkflowDecl:
            return lower_workflow(static_cast<const ast::WorkflowDecl &>(declaration));
        case ast::NodeKind::Program:
            return ir::ModuleDecl{.name = "<invalid-program-decl>"};
        }

        return ir::ModuleDecl{.name = "<invalid-decl>"};
    }

    [[nodiscard]] ir::ModuleDecl lower_module(const ast::ModuleDecl &node) const {
        return ir::ModuleDecl{
            .name = node.name->spelling(),
        };
    }

    [[nodiscard]] ir::ImportDecl lower_import(const ast::ImportDecl &node) const {
        return ir::ImportDecl{
            .path = node.path->spelling(),
            .alias = node.alias.empty() ? std::nullopt : std::make_optional(node.alias),
        };
    }

    [[nodiscard]] ir::ConstDecl lower_const(const ast::ConstDecl &node) const {
        const auto symbol_name = canonical_local_name(SymbolNamespace::Consts, node.name);
        const auto const_symbol = find_local_symbol(SymbolNamespace::Consts, node.name);

        MaybeCRef<Type> const_type;
        if (const_symbol.has_value()) {
            const_type = environment().get_const_type(const_symbol->get().id);
        }

        return ir::ConstDecl{
            .name = symbol_name,
            .type = describe_type(const_type),
            .value = lower_expr(*node.value),
        };
    }

    [[nodiscard]] ir::TypeAliasDecl lower_type_alias(const ast::TypeAliasDecl &node) const {
        return ir::TypeAliasDecl{
            .name = canonical_local_name(SymbolNamespace::Types, node.name),
            .aliased_type = render_type_syntax(*node.aliased_type),
        };
    }

    [[nodiscard]] ir::StructDecl lower_struct(const ast::StructDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Types, node.name);
        const auto info =
            symbol.has_value() ? environment().get_struct(symbol->get().id) : std::nullopt;

        ir::StructDecl declaration{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
        };
        declaration.fields.reserve(node.fields.size());

        for (std::size_t index = 0; index < node.fields.size(); ++index) {
            const auto &field = node.fields[index];

            std::string field_type = render_type_syntax(*field->type);
            if (info.has_value() && index < info->get().fields.size() &&
                info->get().fields[index].type) {
                field_type = info->get().fields[index].type->describe();
            }

            declaration.fields.push_back(ir::FieldDecl{
                .name = field->name,
                .type = std::move(field_type),
                .default_value = field->default_value ? lower_expr(*field->default_value) : nullptr,
            });
        }

        return declaration;
    }

    [[nodiscard]] ir::EnumDecl lower_enum(const ast::EnumDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Types, node.name);

        ir::EnumDecl declaration{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
        };
        declaration.variants.reserve(node.variants.size());

        for (const auto &variant : node.variants) {
            declaration.variants.push_back(variant->name);
        }

        return declaration;
    }

    [[nodiscard]] std::vector<ir::ParamDecl>
    lower_params(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                 MaybeCRef<CapabilityTypeInfo> capability_info,
                 MaybeCRef<PredicateTypeInfo> predicate_info) const {
        std::vector<ir::ParamDecl> result;

        if (capability_info.has_value()) {
            result.reserve(capability_info->get().params.size());
            for (const auto &param : capability_info->get().params) {
                result.push_back(ir::ParamDecl{
                    .name = param.name,
                    .type = describe_type(borrow(param.type.get())),
                });
            }
            return result;
        }

        if (predicate_info.has_value()) {
            result.reserve(predicate_info->get().params.size());
            for (const auto &param : predicate_info->get().params) {
                result.push_back(ir::ParamDecl{
                    .name = param.name,
                    .type = describe_type(borrow(param.type.get())),
                });
            }
            return result;
        }

        result.reserve(params.size());
        for (const auto &param : params) {
            result.push_back(ir::ParamDecl{
                .name = param->name,
                .type = render_type_syntax(*param->type),
            });
        }

        return result;
    }

    [[nodiscard]] ir::CapabilityDecl lower_capability(const ast::CapabilityDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Capabilities, node.name);
        const auto info =
            symbol.has_value() ? environment().get_capability(symbol->get().id) : std::nullopt;

        return ir::CapabilityDecl{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
            .params = lower_params(node.params, info, std::nullopt),
            .return_type = info.has_value() ? describe_type(borrow(info->get().return_type.get()))
                                            : render_type_syntax(*node.return_type),
        };
    }

    [[nodiscard]] ir::PredicateDecl lower_predicate(const ast::PredicateDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Predicates, node.name);
        const auto info =
            symbol.has_value() ? environment().get_predicate(symbol->get().id) : std::nullopt;

        return ir::PredicateDecl{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
            .params = lower_params(node.params, std::nullopt, info),
        };
    }

    [[nodiscard]] ir::AgentDecl lower_agent(const ast::AgentDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Agents, node.name);
        const auto info =
            symbol.has_value() ? environment().get_agent(symbol->get().id) : std::nullopt;

        ir::AgentDecl declaration{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
            .input_type = info.has_value() ? describe_type(borrow(info->get().input_type.get()))
                                           : render_type_syntax(*node.input_type),
            .context_type = info.has_value() ? describe_type(borrow(info->get().context_type.get()))
                                             : render_type_syntax(*node.context_type),
            .output_type = info.has_value() ? describe_type(borrow(info->get().output_type.get()))
                                            : render_type_syntax(*node.output_type),
            .states = node.states,
            .initial_state = node.initial_state,
            .final_states = node.final_states,
        };

        if (info.has_value()) {
            declaration.capabilities.reserve(info->get().capability_symbols.size());
            for (const auto capability_symbol : info->get().capability_symbols) {
                const auto capability = symbols().get(capability_symbol);
                declaration.capabilities.push_back(capability.has_value()
                                                       ? capability->get().canonical_name
                                                       : "<missing-capability>");
            }
        } else {
            declaration.capabilities.reserve(node.capabilities.size());
            for (const auto &capability : node.capabilities) {
                declaration.capabilities.push_back(
                    canonical_local_name(SymbolNamespace::Capabilities, capability));
            }
        }

        if (node.quota) {
            declaration.quota.reserve(node.quota->items.size());
            for (const auto &item : node.quota->items) {
                std::string value;
                if (item->integer_value) {
                    value = item->integer_value->spelling;
                } else if (item->duration_value) {
                    value = item->duration_value->spelling;
                } else {
                    value = "<missing-quota-value>";
                }

                declaration.quota.push_back(ir::QuotaItem{
                    .name = quota_item_name(item->kind),
                    .value = std::move(value),
                });
            }
        }

        declaration.transitions.reserve(node.transitions.size());
        for (const auto &transition : node.transitions) {
            declaration.transitions.push_back(ir::TransitionDecl{
                .from_state = transition->from_state,
                .to_state = transition->to_state,
            });
        }

        return declaration;
    }

    [[nodiscard]] ir::ContractDecl lower_contract(const ast::ContractDecl &node) const {
        ir::ContractDecl declaration{
            .target = canonical_name_from_reference(
                ReferenceKind::ContractTarget, node.target->range, node.target->spelling()),
        };
        declaration.clauses.reserve(node.clauses.size());

        for (const auto &clause : node.clauses) {
            ir::ContractClause lowered_clause{
                .kind = lower_contract_clause_kind(clause->kind),
            };

            if (clause->expr) {
                lowered_clause.value = lower_expr(*clause->expr);
            } else {
                lowered_clause.value = lower_temporal(*clause->temporal_expr);
            }

            declaration.clauses.push_back(std::move(lowered_clause));
        }

        return declaration;
    }

    [[nodiscard]] ir::FlowDecl lower_flow(const ast::FlowDecl &node) const {
        ir::FlowDecl declaration{
            .target = canonical_name_from_reference(
                ReferenceKind::FlowTarget, node.target->range, node.target->spelling()),
        };
        declaration.state_handlers.reserve(node.state_handlers.size());

        for (const auto &handler : node.state_handlers) {
            ir::StateHandler state_handler{
                .state_name = handler->state_name,
                .body = lower_block(*handler->body),
            };

            if (handler->policy) {
                state_handler.policy.reserve(handler->policy->items.size());
                for (const auto &item : handler->policy->items) {
                    switch (item->kind) {
                    case ast::StatePolicyItemKind::Retry:
                        state_handler.policy.push_back(ir::RetryPolicy{
                            .limit =
                                item->retry_limit ? item->retry_limit->spelling : "<missing-retry>",
                        });
                        break;
                    case ast::StatePolicyItemKind::RetryOn:
                        state_handler.policy.push_back(ir::RetryOnPolicy{
                            .targets = lower_retry_targets(item->retry_on),
                        });
                        break;
                    case ast::StatePolicyItemKind::Timeout:
                        state_handler.policy.push_back(ir::TimeoutPolicy{
                            .duration =
                                item->timeout ? item->timeout->spelling : "<missing-timeout>",
                        });
                        break;
                    }
                }
            }

            declaration.state_handlers.push_back(std::move(state_handler));
        }

        return declaration;
    }

    [[nodiscard]] ir::WorkflowDecl lower_workflow(const ast::WorkflowDecl &node) const {
        const auto symbol = find_local_symbol(SymbolNamespace::Workflows, node.name);
        const auto info =
            symbol.has_value() ? environment().get_workflow(symbol->get().id) : std::nullopt;

        ir::WorkflowDecl declaration{
            .name = symbol.has_value() ? symbol->get().canonical_name : node.name,
            .input_type = info.has_value() ? describe_type(borrow(info->get().input_type.get()))
                                           : render_type_syntax(*node.input_type),
            .output_type = info.has_value() ? describe_type(borrow(info->get().output_type.get()))
                                            : render_type_syntax(*node.output_type),
            .return_value = lower_expr(*node.return_value),
        };

        declaration.nodes.reserve(node.nodes.size());
        for (const auto &workflow_node : node.nodes) {
            declaration.nodes.push_back(ir::WorkflowNode{
                .name = workflow_node->name,
                .target = canonical_name_from_reference(ReferenceKind::WorkflowNodeTarget,
                                                        workflow_node->target->range,
                                                        workflow_node->target->spelling()),
                .input = lower_expr(*workflow_node->input),
                .after = workflow_node->after,
            });
        }

        declaration.safety.reserve(node.safety.size());
        for (const auto &formula : node.safety) {
            declaration.safety.push_back(lower_temporal(*formula));
        }

        declaration.liveness.reserve(node.liveness.size());
        for (const auto &formula : node.liveness) {
            declaration.liveness.push_back(lower_temporal(*formula));
        }

        return declaration;
    }
};

class FormalObservationCollector final {
  public:
    [[nodiscard]] std::vector<ir::FormalObservation> collect(const ir::Program &program) {
        observations_.clear();
        observation_index_by_symbol_.clear();

        for (const auto &declaration : program.declarations) {
            std::visit(
                Overloaded{
                    [this](const ir::AgentDecl &agent) { collect_agent(agent); },
                    [this](const ir::ContractDecl &contract) { collect_contract(contract); },
                    [this](const ir::WorkflowDecl &workflow) { collect_workflow(workflow); },
                    [](const auto &) {},
                },
                declaration);
        }

        return observations_;
    }

  private:
    std::vector<ir::FormalObservation> observations_;
    std::unordered_map<std::string, std::size_t> observation_index_by_symbol_;

    void collect_agent(const ir::AgentDecl &agent) {
        for (const auto &capability : agent.capabilities) {
            add_called_observation(agent.name, capability);
        }
    }

    void collect_contract(const ir::ContractDecl &contract) {
        for (std::size_t clause_index = 0; clause_index < contract.clauses.size(); ++clause_index) {
            const auto temporal = std::get_if<ir::TemporalExprPtr>(&contract.clauses[clause_index].value);
            if (temporal == nullptr) {
                continue;
            }

            std::size_t atom_index = 0;
            collect_contract_formula(**temporal, contract.target, clause_index, atom_index);
        }
    }

    void collect_workflow(const ir::WorkflowDecl &workflow) {
        for (std::size_t clause_index = 0; clause_index < workflow.safety.size(); ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.safety[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }

        for (std::size_t clause_index = 0; clause_index < workflow.liveness.size();
             ++clause_index) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.liveness[clause_index],
                                     ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                     workflow.name,
                                     clause_index,
                                     atom_index);
        }
    }

    void collect_contract_formula(const ir::TemporalExpr &expr,
                                  std::string_view agent_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    add_embedded_observation(ir::FormalObservationScope{
                        .kind = ir::FormalObservationScopeKind::ContractClause,
                        .owner = std::string(agent_name),
                        .clause_index = clause_index,
                        .atom_index = atom_index++,
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    add_called_observation(agent_name, value.capability);
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    collect_contract_formula(*value.operand, agent_name, clause_index, atom_index);
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    collect_contract_formula(*value.lhs, agent_name, clause_index, atom_index);
                    collect_contract_formula(*value.rhs, agent_name, clause_index, atom_index);
                },
                [&](const auto &) {},
            },
            expr.node);
    }

    void collect_workflow_formula(const ir::TemporalExpr &expr,
                                  ir::FormalObservationScopeKind scope_kind,
                                  std::string_view workflow_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    add_embedded_observation(ir::FormalObservationScope{
                        .kind = scope_kind,
                        .owner = std::string(workflow_name),
                        .clause_index = clause_index,
                        .atom_index = atom_index++,
                    });
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    collect_workflow_formula(
                        *value.operand, scope_kind, workflow_name, clause_index, atom_index);
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    collect_workflow_formula(
                        *value.lhs, scope_kind, workflow_name, clause_index, atom_index);
                    collect_workflow_formula(
                        *value.rhs, scope_kind, workflow_name, clause_index, atom_index);
                },
                [&](const auto &) {},
            },
            expr.node);
    }

    void add_called_observation(std::string_view agent_name, std::string_view capability_name) {
        const auto symbol = called_observation_symbol(agent_name, capability_name);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node = ir::CalledCapabilityObservation{
                .agent = std::string(agent_name),
                .capability = std::string(capability_name),
            },
        });
    }

    void add_embedded_observation(ir::FormalObservationScope scope) {
        const auto symbol = embedded_observation_symbol(scope);
        if (observation_index_by_symbol_.contains(symbol)) {
            return;
        }

        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node = ir::EmbeddedBoolObservation{
                .scope = std::move(scope),
            },
        });
    }
};

class IrProgramPrinter final {
  public:
    explicit IrProgramPrinter(std::ostream &out) : out_(out) {}

    void print(const ir::Program &program) {
        out_ << program.format_version << '\n';

        bool emitted_section = false;
        if (!program.formal_observations.empty()) {
            print_formal_observations(program.formal_observations);
            emitted_section = true;
        }

        for (const auto &declaration : program.declarations) {
            if (emitted_section) {
                out_ << '\n';
            }

            std::visit([this](const auto &typed_decl) { print_decl(typed_decl); }, declaration);
            emitted_section = true;
        }
    }

  private:
    std::ostream &out_;

    void line(int indent_level, std::string_view text) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
    }

    [[nodiscard]] std::string render_formal_observation(
        const ir::FormalObservation &observation) const {
        return std::visit(
            Overloaded{
                [&](const ir::CalledCapabilityObservation &value) {
                    return "observation " + observation.symbol + ": called_capability(agent=" +
                           value.agent + ", capability=" + value.capability + ")";
                },
                [&](const ir::EmbeddedBoolObservation &value) {
                    const auto scope_kind = [&]() {
                        switch (value.scope.kind) {
                        case ir::FormalObservationScopeKind::ContractClause:
                            return std::string("contract_clause");
                        case ir::FormalObservationScopeKind::WorkflowSafetyClause:
                            return std::string("workflow_safety_clause");
                        case ir::FormalObservationScopeKind::WorkflowLivenessClause:
                            return std::string("workflow_liveness_clause");
                        }

                        return std::string("invalid");
                    }();

                    return "observation " + observation.symbol +
                           ": embedded_bool_expr(scope=" + scope_kind + ", owner=" +
                           value.scope.owner + ", clause=" +
                           std::to_string(value.scope.clause_index) + ", atom=" +
                           std::to_string(value.scope.atom_index) + ")";
                },
            },
            observation.node);
    }

    void print_formal_observations(const std::vector<ir::FormalObservation> &observations) {
        line(0, "formal_observations {");
        for (const auto &observation : observations) {
            line(1, render_formal_observation(observation));
        }
        line(0, "}");
    }

    [[nodiscard]] std::string render_path(const ir::Path &path) const {
        std::ostringstream builder;

        switch (path.root_kind) {
        case ir::PathRootKind::Identifier:
            builder << path.root_name;
            break;
        case ir::PathRootKind::Input:
            builder << "input";
            break;
        case ir::PathRootKind::Output:
            builder << "output";
            break;
        }

        for (const auto &member : path.members) {
            builder << '.' << member;
        }

        return builder.str();
    }

    [[nodiscard]] std::string render_suffix_base(const ir::Expr &expr) const {
        const auto rendered = render_expr(expr);
        if (needs_grouping_for_suffix(expr)) {
            return "(" + rendered + ")";
        }

        return rendered;
    }

    [[nodiscard]] std::string render_expr(const ir::Expr &expr) const {
        return std::visit(
            Overloaded{
                [](const ir::NoneLiteralExpr &) { return std::string("none"); },
                [](const ir::BoolLiteralExpr &value) {
                    return std::string(value.value ? "true" : "false");
                },
                [](const ir::IntegerLiteralExpr &value) { return value.spelling; },
                [](const ir::FloatLiteralExpr &value) { return value.spelling; },
                [](const ir::DecimalLiteralExpr &value) { return value.spelling; },
                [](const ir::StringLiteralExpr &value) { return value.spelling; },
                [](const ir::DurationLiteralExpr &value) { return value.spelling; },
                [this](const ir::SomeExpr &value) {
                    return "some(" + render_expr(*value.value) + ")";
                },
                [this](const ir::PathExpr &value) { return render_path(value.path); },
                [](const ir::QualifiedValueExpr &value) { return value.value; },
                [this](const ir::CallExpr &value) {
                    std::vector<std::string> arguments;
                    arguments.reserve(value.arguments.size());
                    for (const auto &argument : value.arguments) {
                        arguments.push_back(render_expr(*argument));
                    }

                    return value.callee + "(" + join(arguments, ", ") + ")";
                },
                [this](const ir::StructLiteralExpr &value) {
                    std::vector<std::string> fields;
                    fields.reserve(value.fields.size());
                    for (const auto &field : value.fields) {
                        fields.push_back(field.name + ": " + render_expr(*field.value));
                    }

                    return value.type_name + " { " + join(fields, ", ") + " }";
                },
                [this](const ir::ListLiteralExpr &value) {
                    std::vector<std::string> items;
                    items.reserve(value.items.size());
                    for (const auto &item : value.items) {
                        items.push_back(render_expr(*item));
                    }

                    return "[" + join(items, ", ") + "]";
                },
                [this](const ir::SetLiteralExpr &value) {
                    std::vector<std::string> items;
                    items.reserve(value.items.size());
                    for (const auto &item : value.items) {
                        items.push_back(render_expr(*item));
                    }

                    return "{" + join(items, ", ") + "}";
                },
                [this](const ir::MapLiteralExpr &value) {
                    std::vector<std::string> entries;
                    entries.reserve(value.entries.size());
                    for (const auto &entry : value.entries) {
                        entries.push_back(render_expr(*entry.key) + ": " +
                                          render_expr(*entry.value));
                    }

                    return "{" + join(entries, ", ") + "}";
                },
                [this](const ir::UnaryExpr &value) {
                    return "(" + expr_unary_op_name(value.op) + render_expr(*value.operand) + ")";
                },
                [this](const ir::BinaryExpr &value) {
                    return "(" + render_expr(*value.lhs) + " " + expr_binary_op_name(value.op) +
                           " " + render_expr(*value.rhs) + ")";
                },
                [this](const ir::MemberAccessExpr &value) {
                    return render_suffix_base(*value.base) + "." + value.member;
                },
                [this](const ir::IndexAccessExpr &value) {
                    return render_suffix_base(*value.base) + "[" + render_expr(*value.index) + "]";
                },
                [this](const ir::GroupExpr &value) { return "(" + render_expr(*value.expr) + ")"; },
            },
            expr.node);
    }

    [[nodiscard]] std::string render_temporal(const ir::TemporalExpr &expr) const {
        return std::visit(
            Overloaded{
                [this](const ir::EmbeddedTemporalExpr &value) { return render_expr(*value.expr); },
                [](const ir::CalledTemporalExpr &value) {
                    return "called(" + value.capability + ")";
                },
                [](const ir::InStateTemporalExpr &value) {
                    return "in_state(" + value.state + ")";
                },
                [](const ir::RunningTemporalExpr &value) { return "running(" + value.node + ")"; },
                [](const ir::CompletedTemporalExpr &value) {
                    if (value.state_name.has_value()) {
                        return "completed(" + value.node + ", " + *value.state_name + ")";
                    }

                    return "completed(" + value.node + ")";
                },
                [this](const ir::TemporalUnaryExpr &value) {
                    return temporal_unary_op_name(value.op) + "(" +
                           render_temporal(*value.operand) + ")";
                },
                [this](const ir::TemporalBinaryExpr &value) {
                    return "(" + render_temporal(*value.lhs) + " " +
                           temporal_binary_op_name(value.op) + " " + render_temporal(*value.rhs) +
                           ")";
                },
            },
            expr.node);
    }

    void print_block(const ir::Block &block, int indent_level) {
        for (const auto &statement : block.statements) {
            print_statement(*statement, indent_level);
        }
    }

    void print_statement(const ir::Statement &statement, int indent_level) {
        std::visit(Overloaded{
                       [this, indent_level](const ir::LetStatement &value) {
                           line(indent_level,
                                "let " + value.name + ": " + value.type + " = " +
                                    render_expr(*value.initializer));
                       },
                       [this, indent_level](const ir::AssignStatement &value) {
                           line(indent_level,
                                render_path(value.target) + " = " + render_expr(*value.value));
                       },
                       [this, indent_level](const ir::IfStatement &value) {
                           line(indent_level, "if " + render_expr(*value.condition) + " {");
                           print_block(*value.then_block, indent_level + 1);
                           if (value.else_block) {
                               line(indent_level, "} else {");
                               print_block(*value.else_block, indent_level + 1);
                           }
                           line(indent_level, "}");
                       },
                       [this, indent_level](const ir::GotoStatement &value) {
                           line(indent_level, "goto " + value.target_state);
                       },
                       [this, indent_level](const ir::ReturnStatement &value) {
                           if (value.value) {
                               line(indent_level, "return " + render_expr(*value.value));
                           } else {
                               line(indent_level, "return");
                           }
                       },
                       [this, indent_level](const ir::AssertStatement &value) {
                           line(indent_level, "assert " + render_expr(*value.condition));
                       },
                       [this, indent_level](const ir::ExprStatement &value) {
                           line(indent_level, render_expr(*value.expr));
                       },
                   },
                   statement.node);
    }

    void print_policy_item(const ir::StatePolicyItem &policy_item, int indent_level) {
        std::visit(Overloaded{
                       [this, indent_level](const ir::RetryPolicy &value) {
                           line(indent_level, "retry: " + value.limit);
                       },
                       [this, indent_level](const ir::RetryOnPolicy &value) {
                           line(indent_level, "retry_on: [" + join(value.targets, ", ") + "]");
                       },
                       [this, indent_level](const ir::TimeoutPolicy &value) {
                           line(indent_level, "timeout: " + value.duration);
                       },
                   },
                   policy_item);
    }

    [[nodiscard]] std::string print_params(const std::vector<ir::ParamDecl> &params) const {
        std::vector<std::string> rendered;
        rendered.reserve(params.size());

        for (const auto &param : params) {
            rendered.push_back(param.name + ": " + param.type);
        }

        return join(rendered, ", ");
    }

    void print_decl(const ir::ModuleDecl &declaration) {
        line(0, "module " + declaration.name);
    }

    void print_decl(const ir::ImportDecl &declaration) {
        if (declaration.alias.has_value()) {
            line(0, "import " + declaration.path + " as " + *declaration.alias);
            return;
        }

        line(0, "import " + declaration.path);
    }

    void print_decl(const ir::ConstDecl &declaration) {
        line(0,
             "const " + declaration.name + ": " + declaration.type + " = " +
                 render_expr(*declaration.value));
    }

    void print_decl(const ir::TypeAliasDecl &declaration) {
        line(0, "typealias " + declaration.name + " = " + declaration.aliased_type);
    }

    void print_decl(const ir::StructDecl &declaration) {
        line(0, "struct " + declaration.name + " {");

        for (const auto &field : declaration.fields) {
            std::string text = "field " + field.name + ": " + field.type;
            if (field.default_value) {
                text += " = " + render_expr(*field.default_value);
            }

            line(1, text);
        }

        line(0, "}");
    }

    void print_decl(const ir::EnumDecl &declaration) {
        line(0, "enum " + declaration.name + " {");
        for (const auto &variant : declaration.variants) {
            line(1, "variant " + variant);
        }
        line(0, "}");
    }

    void print_decl(const ir::CapabilityDecl &declaration) {
        line(0,
             "capability " + declaration.name + "(" + print_params(declaration.params) + ") -> " +
                 declaration.return_type);
    }

    void print_decl(const ir::PredicateDecl &declaration) {
        line(0,
             "predicate " + declaration.name + "(" + print_params(declaration.params) +
                 ") -> Bool");
    }

    void print_decl(const ir::AgentDecl &declaration) {
        line(0, "agent " + declaration.name + " {");
        line(1, "input: " + declaration.input_type);
        line(1, "context: " + declaration.context_type);
        line(1, "output: " + declaration.output_type);
        line(1, "states: [" + join(declaration.states, ", ") + "]");
        line(1, "initial: " + declaration.initial_state);
        line(1, "final: [" + join(declaration.final_states, ", ") + "]");
        line(1, "capabilities: [" + join(declaration.capabilities, ", ") + "]");

        if (!declaration.quota.empty()) {
            line(1, "quota {");
            for (const auto &item : declaration.quota) {
                line(2, item.name + ": " + item.value);
            }
            line(1, "}");
        }

        for (const auto &transition : declaration.transitions) {
            line(1, "transition " + transition.from_state + " -> " + transition.to_state);
        }

        line(0, "}");
    }

    void print_decl(const ir::ContractDecl &declaration) {
        line(0, "contract " + declaration.target + " {");
        for (const auto &clause : declaration.clauses) {
            const auto value = std::visit(
                Overloaded{
                    [this](const ir::ExprPtr &expr) { return render_expr(*expr); },
                    [this](const ir::TemporalExprPtr &expr) { return render_temporal(*expr); },
                },
                clause.value);
            line(1, contract_clause_name(clause.kind) + ": " + value);
        }
        line(0, "}");
    }

    void print_decl(const ir::FlowDecl &declaration) {
        line(0, "flow " + declaration.target + " {");

        for (const auto &handler : declaration.state_handlers) {
            line(1, "state " + handler.state_name + " {");
            for (const auto &policy_item : handler.policy) {
                print_policy_item(policy_item, 2);
            }
            print_block(handler.body, 2);
            line(1, "}");
        }

        line(0, "}");
    }

    void print_decl(const ir::WorkflowDecl &declaration) {
        line(0, "workflow " + declaration.name + " {");
        line(1, "input: " + declaration.input_type);
        line(1, "output: " + declaration.output_type);

        for (const auto &node : declaration.nodes) {
            std::string text =
                "node " + node.name + ": " + node.target + "(" + render_expr(*node.input) + ")";
            if (!node.after.empty()) {
                text += " after [" + join(node.after, ", ") + "]";
            }

            line(1, text);
        }

        for (const auto &formula : declaration.safety) {
            line(1, "safety: " + render_temporal(*formula));
        }
        for (const auto &formula : declaration.liveness) {
            line(1, "liveness: " + render_temporal(*formula));
        }

        line(1, "return: " + render_expr(*declaration.return_value));
        line(0, "}");
    }
};

} // namespace

ir::Program lower_program_ir(const ast::Program &program,
                             const ResolveResult &resolve_result,
                             const TypeCheckResult &type_check_result) {
    IrLowerer lowerer(program, resolve_result, type_check_result);
    auto program_ir = lowerer.lower();
    program_ir.formal_observations = collect_formal_observations(program_ir);
    return program_ir;
}

std::vector<ir::FormalObservation> collect_formal_observations(const ir::Program &program) {
    FormalObservationCollector collector;
    return collector.collect(program);
}

void print_program_ir(const ir::Program &program, std::ostream &out) {
    IrProgramPrinter printer(out);
    printer.print(program);
}

void emit_program_ir(const ast::Program &program,
                     const ResolveResult &resolve_result,
                     const TypeCheckResult &type_check_result,
                     std::ostream &out) {
    print_program_ir(lower_program_ir(program, resolve_result, type_check_result), out);
}

} // namespace ahfl
