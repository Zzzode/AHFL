#include "ahfl/frontend/ast.hpp"

#include <sstream>
#include <utility>

namespace ahfl::ast {

namespace {

std::string with_name(std::string_view prefix, const std::string &value) {
    std::string text(prefix);
    text += value;
    return text;
}

std::string with_count(std::string prefix, std::size_t count, std::string_view noun) {
    std::ostringstream builder;
    builder << prefix << " (" << count << " " << noun;
    builder << (count == 1 ? ")" : "s)");
    return builder.str();
}

std::string join_segments(const std::vector<std::string> &segments) {
    std::string text;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index != 0) {
            text += "::";
        }

        text += segments[index];
    }

    return text;
}

std::string_view expr_kind_name(ExprSyntaxKind kind) {
    switch (kind) {
    case ExprSyntaxKind::BoolLiteral:
        return "BoolLiteral";
    case ExprSyntaxKind::IntegerLiteral:
        return "IntegerLiteral";
    case ExprSyntaxKind::FloatLiteral:
        return "FloatLiteral";
    case ExprSyntaxKind::DecimalLiteral:
        return "DecimalLiteral";
    case ExprSyntaxKind::StringLiteral:
        return "StringLiteral";
    case ExprSyntaxKind::DurationLiteral:
        return "DurationLiteral";
    case ExprSyntaxKind::NoneLiteral:
        return "NoneLiteral";
    case ExprSyntaxKind::Some:
        return "Some";
    case ExprSyntaxKind::Path:
        return "Path";
    case ExprSyntaxKind::QualifiedValue:
        return "QualifiedValue";
    case ExprSyntaxKind::Call:
        return "Call";
    case ExprSyntaxKind::StructLiteral:
        return "StructLiteral";
    case ExprSyntaxKind::ListLiteral:
        return "ListLiteral";
    case ExprSyntaxKind::SetLiteral:
        return "SetLiteral";
    case ExprSyntaxKind::MapLiteral:
        return "MapLiteral";
    case ExprSyntaxKind::Unary:
        return "Unary";
    case ExprSyntaxKind::Binary:
        return "Binary";
    case ExprSyntaxKind::MemberAccess:
        return "MemberAccess";
    case ExprSyntaxKind::IndexAccess:
        return "IndexAccess";
    case ExprSyntaxKind::Group:
        return "Group";
    }

    return "Unknown";
}

std::string_view temporal_kind_name(TemporalExprSyntaxKind kind) {
    switch (kind) {
    case TemporalExprSyntaxKind::EmbeddedExpr:
        return "EmbeddedExpr";
    case TemporalExprSyntaxKind::Called:
        return "Called";
    case TemporalExprSyntaxKind::InState:
        return "InState";
    case TemporalExprSyntaxKind::Running:
        return "Running";
    case TemporalExprSyntaxKind::Completed:
        return "Completed";
    case TemporalExprSyntaxKind::Unary:
        return "Unary";
    case TemporalExprSyntaxKind::Binary:
        return "Binary";
    }

    return "Unknown";
}

class AstInvariantValidator final {
  public:
    [[nodiscard]] std::vector<AstInvariantViolation> validate(const Program &program) {
        violations_.clear();

        for (const auto &declaration : program.declarations) {
            if (!declaration) {
                fail(program.range, "Program.declarations contains a null declaration");
                continue;
            }

            validate_declaration(*declaration);
        }

        return std::move(violations_);
    }

  private:
    std::vector<AstInvariantViolation> violations_;

    void fail(SourceRange range, std::string message) {
        violations_.push_back(AstInvariantViolation{
            .range = range,
            .message = std::move(message),
        });
    }

    void require(bool condition, SourceRange range, std::string_view message) {
        if (!condition) {
            fail(range, std::string(message));
        }
    }

    void validate_qualified_name(const QualifiedName *name,
                                 SourceRange range,
                                 std::string_view field_name) {
        require(name != nullptr, range, std::string(field_name) + " is missing");
        if (name != nullptr) {
            require(
                !name->segments.empty(), name->range, std::string(field_name) + " has no segments");
        }
    }

    void validate_type(const TypeSyntax &type) {
        switch (type.kind) {
        case TypeSyntaxKind::Unit:
        case TypeSyntaxKind::Bool:
        case TypeSyntaxKind::Int:
        case TypeSyntaxKind::Float:
        case TypeSyntaxKind::String:
        case TypeSyntaxKind::UUID:
        case TypeSyntaxKind::Timestamp:
        case TypeSyntaxKind::Duration:
            require(!type.name && !type.string_bounds && !type.decimal_scale && !type.first &&
                        !type.second,
                    type.range,
                    "primitive TypeSyntax has unexpected payload fields");
            break;
        case TypeSyntaxKind::BoundedString:
            require(type.string_bounds.has_value(),
                    type.range,
                    "BoundedString TypeSyntax is missing string_bounds");
            require(!type.name && !type.decimal_scale && !type.first && !type.second,
                    type.range,
                    "BoundedString TypeSyntax has unexpected payload fields");
            break;
        case TypeSyntaxKind::Decimal:
            require(type.decimal_scale.has_value(),
                    type.range,
                    "Decimal TypeSyntax is missing decimal_scale");
            require(!type.name && !type.string_bounds && !type.first && !type.second,
                    type.range,
                    "Decimal TypeSyntax has unexpected payload fields");
            break;
        case TypeSyntaxKind::Named:
            validate_qualified_name(type.name.get(), type.range, "Named TypeSyntax.name");
            require(!type.string_bounds && !type.decimal_scale && !type.first && !type.second,
                    type.range,
                    "Named TypeSyntax has unexpected payload fields");
            break;
        case TypeSyntaxKind::Optional:
        case TypeSyntaxKind::List:
        case TypeSyntaxKind::Set:
            require(type.first != nullptr,
                    type.range,
                    std::string(to_string(type.kind)) + " TypeSyntax is missing first");
            require(!type.name && !type.string_bounds && !type.decimal_scale && !type.second,
                    type.range,
                    std::string(to_string(type.kind)) +
                        " TypeSyntax has unexpected payload fields");
            if (type.first) {
                validate_type(*type.first);
            }
            break;
        case TypeSyntaxKind::Map:
            require(type.first != nullptr, type.range, "Map TypeSyntax is missing key type");
            require(type.second != nullptr, type.range, "Map TypeSyntax is missing value type");
            require(!type.name && !type.string_bounds && !type.decimal_scale,
                    type.range,
                    "Map TypeSyntax has unexpected payload fields");
            if (type.first) {
                validate_type(*type.first);
            }
            if (type.second) {
                validate_type(*type.second);
            }
            break;
        }
    }

    void validate_expr(const ExprSyntax &expr) {
        switch (expr.kind) {
        case ExprSyntaxKind::BoolLiteral:
        case ExprSyntaxKind::FloatLiteral:
        case ExprSyntaxKind::DecimalLiteral:
        case ExprSyntaxKind::StringLiteral:
        case ExprSyntaxKind::NoneLiteral:
            break;
        case ExprSyntaxKind::IntegerLiteral:
            require(expr.integer_literal != nullptr,
                    expr.range,
                    "IntegerLiteral ExprSyntax is missing integer_literal");
            break;
        case ExprSyntaxKind::DurationLiteral:
            require(expr.duration_literal != nullptr,
                    expr.range,
                    "DurationLiteral ExprSyntax is missing duration_literal");
            break;
        case ExprSyntaxKind::Some:
        case ExprSyntaxKind::Unary:
        case ExprSyntaxKind::Group:
            require(expr.first != nullptr,
                    expr.range,
                    std::string(expr_kind_name(expr.kind)) + " ExprSyntax is missing first");
            break;
        case ExprSyntaxKind::Binary:
            require(expr.first != nullptr, expr.range, "Binary ExprSyntax is missing lhs");
            require(expr.second != nullptr, expr.range, "Binary ExprSyntax is missing rhs");
            break;
        case ExprSyntaxKind::Path:
            require(expr.path != nullptr, expr.range, "Path ExprSyntax is missing path");
            break;
        case ExprSyntaxKind::QualifiedValue:
            validate_qualified_name(
                expr.qualified_name.get(), expr.range, "QualifiedValue ExprSyntax.qualified_name");
            break;
        case ExprSyntaxKind::Call:
            validate_qualified_name(
                expr.qualified_name.get(), expr.range, "Call ExprSyntax.qualified_name");
            break;
        case ExprSyntaxKind::StructLiteral:
            validate_qualified_name(
                expr.qualified_name.get(), expr.range, "StructLiteral ExprSyntax.qualified_name");
            break;
        case ExprSyntaxKind::ListLiteral:
        case ExprSyntaxKind::SetLiteral:
            break;
        case ExprSyntaxKind::MapLiteral:
            break;
        case ExprSyntaxKind::MemberAccess:
            require(
                expr.first != nullptr, expr.range, "MemberAccess ExprSyntax is missing receiver");
            require(!expr.name.empty(), expr.range, "MemberAccess ExprSyntax is missing member");
            break;
        case ExprSyntaxKind::IndexAccess:
            require(expr.first != nullptr, expr.range, "IndexAccess ExprSyntax is missing base");
            require(expr.second != nullptr, expr.range, "IndexAccess ExprSyntax is missing index");
            break;
        }

        if (expr.first) {
            validate_expr(*expr.first);
        }
        if (expr.second) {
            validate_expr(*expr.second);
        }
        if (expr.path) {
            require(
                !expr.path->root_name.empty(), expr.path->range, "PathSyntax is missing root_name");
        }
        for (const auto &item : expr.items) {
            require(item != nullptr, expr.range, "ExprSyntax.items contains null");
            if (item) {
                validate_expr(*item);
            }
        }
        for (const auto &entry : expr.map_entries) {
            require(entry != nullptr, expr.range, "ExprSyntax.map_entries contains null");
            if (!entry) {
                continue;
            }
            require(entry->key != nullptr, entry->range, "MapEntrySyntax is missing key");
            require(entry->value != nullptr, entry->range, "MapEntrySyntax is missing value");
            if (entry->key) {
                validate_expr(*entry->key);
            }
            if (entry->value) {
                validate_expr(*entry->value);
            }
        }
        for (const auto &field : expr.struct_fields) {
            require(field != nullptr, expr.range, "ExprSyntax.struct_fields contains null");
            if (!field) {
                continue;
            }
            require(
                !field->field_name.empty(), field->range, "StructInitSyntax is missing field_name");
            require(field->value != nullptr, field->range, "StructInitSyntax is missing value");
            if (field->value) {
                validate_expr(*field->value);
            }
        }
    }

    void validate_statement(const StatementSyntax &statement) {
        int active_count = 0;
        active_count += statement.let_stmt != nullptr;
        active_count += statement.assign_stmt != nullptr;
        active_count += statement.if_stmt != nullptr;
        active_count += statement.goto_stmt != nullptr;
        active_count += statement.return_stmt != nullptr;
        active_count += statement.assert_stmt != nullptr;
        active_count += statement.expr_stmt != nullptr;
        require(active_count == 1,
                statement.range,
                "StatementSyntax must have exactly one active payload");

        switch (statement.kind) {
        case StatementSyntaxKind::Let:
            require(statement.let_stmt != nullptr,
                    statement.range,
                    "Let StatementSyntax is missing let_stmt");
            if (statement.let_stmt) {
                require(!statement.let_stmt->name.empty(),
                        statement.let_stmt->range,
                        "LetStmtSyntax is missing name");
                if (statement.let_stmt->type) {
                    validate_type(*statement.let_stmt->type);
                }
                require(statement.let_stmt->initializer != nullptr,
                        statement.let_stmt->range,
                        "LetStmtSyntax is missing initializer");
                if (statement.let_stmt->initializer) {
                    validate_expr(*statement.let_stmt->initializer);
                }
            }
            break;
        case StatementSyntaxKind::Assign:
            require(statement.assign_stmt != nullptr,
                    statement.range,
                    "Assign StatementSyntax is missing assign_stmt");
            if (statement.assign_stmt) {
                require(statement.assign_stmt->target != nullptr,
                        statement.assign_stmt->range,
                        "AssignStmtSyntax is missing target");
                require(statement.assign_stmt->value != nullptr,
                        statement.assign_stmt->range,
                        "AssignStmtSyntax is missing value");
                if (statement.assign_stmt->value) {
                    validate_expr(*statement.assign_stmt->value);
                }
            }
            break;
        case StatementSyntaxKind::If:
            require(statement.if_stmt != nullptr,
                    statement.range,
                    "If StatementSyntax is missing if_stmt");
            if (statement.if_stmt) {
                require(statement.if_stmt->condition != nullptr,
                        statement.if_stmt->range,
                        "IfStmtSyntax is missing condition");
                require(statement.if_stmt->then_block != nullptr,
                        statement.if_stmt->range,
                        "IfStmtSyntax is missing then_block");
                if (statement.if_stmt->condition) {
                    validate_expr(*statement.if_stmt->condition);
                }
                if (statement.if_stmt->then_block) {
                    validate_block(*statement.if_stmt->then_block);
                }
                if (statement.if_stmt->else_block) {
                    validate_block(*statement.if_stmt->else_block);
                }
            }
            break;
        case StatementSyntaxKind::Goto:
            require(statement.goto_stmt != nullptr,
                    statement.range,
                    "Goto StatementSyntax is missing goto_stmt");
            if (statement.goto_stmt) {
                require(!statement.goto_stmt->target_state.empty(),
                        statement.goto_stmt->range,
                        "GotoStmtSyntax is missing target_state");
            }
            break;
        case StatementSyntaxKind::Return:
            require(statement.return_stmt != nullptr,
                    statement.range,
                    "Return StatementSyntax is missing return_stmt");
            if (statement.return_stmt && statement.return_stmt->value) {
                validate_expr(*statement.return_stmt->value);
            }
            break;
        case StatementSyntaxKind::Assert:
            require(statement.assert_stmt != nullptr,
                    statement.range,
                    "Assert StatementSyntax is missing assert_stmt");
            if (statement.assert_stmt) {
                require(statement.assert_stmt->condition != nullptr,
                        statement.assert_stmt->range,
                        "AssertStmtSyntax is missing condition");
                if (statement.assert_stmt->condition) {
                    validate_expr(*statement.assert_stmt->condition);
                }
            }
            break;
        case StatementSyntaxKind::Expr:
            require(statement.expr_stmt != nullptr,
                    statement.range,
                    "Expr StatementSyntax is missing expr_stmt");
            if (statement.expr_stmt) {
                require(statement.expr_stmt->expr != nullptr,
                        statement.expr_stmt->range,
                        "ExprStmtSyntax is missing expr");
                if (statement.expr_stmt->expr) {
                    validate_expr(*statement.expr_stmt->expr);
                }
            }
            break;
        }
    }

    void validate_block(const BlockSyntax &block) {
        for (const auto &statement : block.statements) {
            require(statement != nullptr, block.range, "BlockSyntax.statements contains null");
            if (statement) {
                validate_statement(*statement);
            }
        }
    }

    void validate_temporal(const TemporalExprSyntax &expr) {
        switch (expr.kind) {
        case TemporalExprSyntaxKind::EmbeddedExpr:
            require(expr.expr != nullptr,
                    expr.range,
                    "EmbeddedExpr TemporalExprSyntax is missing expr");
            if (expr.expr) {
                validate_expr(*expr.expr);
            }
            break;
        case TemporalExprSyntaxKind::Called:
        case TemporalExprSyntaxKind::InState:
        case TemporalExprSyntaxKind::Running:
        case TemporalExprSyntaxKind::Completed:
            require(!expr.name.empty(),
                    expr.range,
                    std::string(temporal_kind_name(expr.kind)) +
                        " TemporalExprSyntax is missing name");
            break;
        case TemporalExprSyntaxKind::Unary:
            require(
                expr.first != nullptr, expr.range, "Unary TemporalExprSyntax is missing operand");
            if (expr.first) {
                validate_temporal(*expr.first);
            }
            break;
        case TemporalExprSyntaxKind::Binary:
            require(expr.first != nullptr, expr.range, "Binary TemporalExprSyntax is missing lhs");
            require(expr.second != nullptr, expr.range, "Binary TemporalExprSyntax is missing rhs");
            if (expr.first) {
                validate_temporal(*expr.first);
            }
            if (expr.second) {
                validate_temporal(*expr.second);
            }
            break;
        }
    }

    void validate_params(const std::vector<Owned<ParamDeclSyntax>> &params, SourceRange range) {
        for (const auto &param : params) {
            require(param != nullptr, range, "parameter list contains null");
            if (!param) {
                continue;
            }
            require(!param->name.empty(), param->range, "ParamDeclSyntax is missing name");
            require(param->type != nullptr, param->range, "ParamDeclSyntax is missing type");
            if (param->type) {
                validate_type(*param->type);
            }
        }
    }

    void validate_declaration(const Decl &declaration) {
        switch (declaration.kind) {
        case NodeKind::Program:
            fail(declaration.range, "Program node cannot appear in Program.declarations");
            break;
        case NodeKind::ModuleDecl: {
            const auto &node = static_cast<const ModuleDecl &>(declaration);
            validate_qualified_name(node.name.get(), node.range, "ModuleDecl.name");
            break;
        }
        case NodeKind::ImportDecl: {
            const auto &node = static_cast<const ImportDecl &>(declaration);
            validate_qualified_name(node.path.get(), node.range, "ImportDecl.path");
            break;
        }
        case NodeKind::ConstDecl: {
            const auto &node = static_cast<const ConstDecl &>(declaration);
            require(!node.name.empty(), node.range, "ConstDecl is missing name");
            require(node.type != nullptr, node.range, "ConstDecl is missing type");
            require(node.value != nullptr, node.range, "ConstDecl is missing value");
            if (node.type) {
                validate_type(*node.type);
            }
            if (node.value) {
                validate_expr(*node.value);
            }
            break;
        }
        case NodeKind::TypeAliasDecl: {
            const auto &node = static_cast<const TypeAliasDecl &>(declaration);
            require(!node.name.empty(), node.range, "TypeAliasDecl is missing name");
            require(
                node.aliased_type != nullptr, node.range, "TypeAliasDecl is missing aliased_type");
            if (node.aliased_type) {
                validate_type(*node.aliased_type);
            }
            break;
        }
        case NodeKind::StructDecl: {
            const auto &node = static_cast<const StructDecl &>(declaration);
            require(!node.name.empty(), node.range, "StructDecl is missing name");
            for (const auto &field : node.fields) {
                require(field != nullptr, node.range, "StructDecl.fields contains null");
                if (!field) {
                    continue;
                }
                require(
                    !field->name.empty(), field->range, "StructFieldDeclSyntax is missing name");
                require(
                    field->type != nullptr, field->range, "StructFieldDeclSyntax is missing type");
                if (field->type) {
                    validate_type(*field->type);
                }
                if (field->default_value) {
                    validate_expr(*field->default_value);
                }
            }
            break;
        }
        case NodeKind::EnumDecl: {
            const auto &node = static_cast<const EnumDecl &>(declaration);
            require(!node.name.empty(), node.range, "EnumDecl is missing name");
            for (const auto &variant : node.variants) {
                require(variant != nullptr, node.range, "EnumDecl.variants contains null");
                if (variant) {
                    require(!variant->name.empty(),
                            variant->range,
                            "EnumVariantDeclSyntax is missing name");
                }
            }
            break;
        }
        case NodeKind::CapabilityDecl: {
            const auto &node = static_cast<const CapabilityDecl &>(declaration);
            require(!node.name.empty(), node.range, "CapabilityDecl is missing name");
            validate_params(node.params, node.range);
            require(
                node.return_type != nullptr, node.range, "CapabilityDecl is missing return_type");
            if (node.return_type) {
                validate_type(*node.return_type);
            }
            break;
        }
        case NodeKind::PredicateDecl: {
            const auto &node = static_cast<const PredicateDecl &>(declaration);
            require(!node.name.empty(), node.range, "PredicateDecl is missing name");
            validate_params(node.params, node.range);
            break;
        }
        case NodeKind::AgentDecl: {
            const auto &node = static_cast<const AgentDecl &>(declaration);
            require(!node.name.empty(), node.range, "AgentDecl is missing name");
            require(node.input_type != nullptr, node.range, "AgentDecl is missing input_type");
            require(node.context_type != nullptr, node.range, "AgentDecl is missing context_type");
            require(node.output_type != nullptr, node.range, "AgentDecl is missing output_type");
            if (node.input_type) {
                validate_type(*node.input_type);
            }
            if (node.context_type) {
                validate_type(*node.context_type);
            }
            if (node.output_type) {
                validate_type(*node.output_type);
            }
            if (node.quota) {
                for (const auto &item : node.quota->items) {
                    require(
                        item != nullptr, node.quota->range, "AgentQuotaSyntax.items contains null");
                    if (!item) {
                        continue;
                    }
                    const auto has_integer = item->integer_value != nullptr;
                    const auto has_duration = item->duration_value != nullptr;
                    require(has_integer != has_duration,
                            item->range,
                            "AgentQuotaItemSyntax must have exactly one value payload");
                }
            }
            for (const auto &transition : node.transitions) {
                require(transition != nullptr, node.range, "AgentDecl.transitions contains null");
                if (transition) {
                    require(!transition->from_state.empty(),
                            transition->range,
                            "TransitionSyntax is missing from_state");
                    require(!transition->to_state.empty(),
                            transition->range,
                            "TransitionSyntax is missing to_state");
                }
            }
            break;
        }
        case NodeKind::ContractDecl: {
            const auto &node = static_cast<const ContractDecl &>(declaration);
            validate_qualified_name(node.target.get(), node.range, "ContractDecl.target");
            for (const auto &clause : node.clauses) {
                require(clause != nullptr, node.range, "ContractDecl.clauses contains null");
                if (!clause) {
                    continue;
                }
                const auto has_expr = clause->expr != nullptr;
                const auto has_temporal = clause->temporal_expr != nullptr;
                require(has_expr != has_temporal,
                        clause->range,
                        "ContractClauseSyntax must have exactly one expression payload");
                if (clause->expr) {
                    validate_expr(*clause->expr);
                }
                if (clause->temporal_expr) {
                    validate_temporal(*clause->temporal_expr);
                }
            }
            break;
        }
        case NodeKind::FlowDecl: {
            const auto &node = static_cast<const FlowDecl &>(declaration);
            validate_qualified_name(node.target.get(), node.range, "FlowDecl.target");
            for (const auto &handler : node.state_handlers) {
                require(handler != nullptr, node.range, "FlowDecl.state_handlers contains null");
                if (!handler) {
                    continue;
                }
                require(!handler->state_name.empty(),
                        handler->range,
                        "StateHandlerSyntax is missing state_name");
                require(
                    handler->body != nullptr, handler->range, "StateHandlerSyntax is missing body");
                if (handler->body) {
                    validate_block(*handler->body);
                }
            }
            break;
        }
        case NodeKind::WorkflowDecl: {
            const auto &node = static_cast<const WorkflowDecl &>(declaration);
            require(!node.name.empty(), node.range, "WorkflowDecl is missing name");
            require(node.input_type != nullptr, node.range, "WorkflowDecl is missing input_type");
            require(node.output_type != nullptr, node.range, "WorkflowDecl is missing output_type");
            require(
                node.return_value != nullptr, node.range, "WorkflowDecl is missing return_value");
            if (node.input_type) {
                validate_type(*node.input_type);
            }
            if (node.output_type) {
                validate_type(*node.output_type);
            }
            for (const auto &workflow_node : node.nodes) {
                require(workflow_node != nullptr, node.range, "WorkflowDecl.nodes contains null");
                if (!workflow_node) {
                    continue;
                }
                require(!workflow_node->name.empty(),
                        workflow_node->range,
                        "WorkflowNodeDeclSyntax is missing name");
                validate_qualified_name(workflow_node->target.get(),
                                        workflow_node->range,
                                        "WorkflowNodeDeclSyntax.target");
                require(workflow_node->input != nullptr,
                        workflow_node->range,
                        "WorkflowNodeDeclSyntax is missing input");
                if (workflow_node->input) {
                    validate_expr(*workflow_node->input);
                }
            }
            for (const auto &formula : node.safety) {
                require(formula != nullptr, node.range, "WorkflowDecl.safety contains null");
                if (formula) {
                    validate_temporal(*formula);
                }
            }
            for (const auto &formula : node.liveness) {
                require(formula != nullptr, node.range, "WorkflowDecl.liveness contains null");
                if (formula) {
                    validate_temporal(*formula);
                }
            }
            if (node.return_value) {
                validate_expr(*node.return_value);
            }
            break;
        }
        }
    }
};

} // namespace

std::string_view to_string(NodeKind kind) noexcept {
    switch (kind) {
    case NodeKind::Program:
        return "Program";
    case NodeKind::ModuleDecl:
        return "ModuleDecl";
    case NodeKind::ImportDecl:
        return "ImportDecl";
    case NodeKind::ConstDecl:
        return "ConstDecl";
    case NodeKind::TypeAliasDecl:
        return "TypeAliasDecl";
    case NodeKind::StructDecl:
        return "StructDecl";
    case NodeKind::EnumDecl:
        return "EnumDecl";
    case NodeKind::CapabilityDecl:
        return "CapabilityDecl";
    case NodeKind::PredicateDecl:
        return "PredicateDecl";
    case NodeKind::AgentDecl:
        return "AgentDecl";
    case NodeKind::ContractDecl:
        return "ContractDecl";
    case NodeKind::FlowDecl:
        return "FlowDecl";
    case NodeKind::WorkflowDecl:
        return "WorkflowDecl";
    }

    return "Unknown";
}

std::string_view to_string(ContractClauseKind kind) noexcept {
    switch (kind) {
    case ContractClauseKind::Requires:
        return "requires";
    case ContractClauseKind::Ensures:
        return "ensures";
    case ContractClauseKind::Invariant:
        return "invariant";
    case ContractClauseKind::Forbid:
        return "forbid";
    }

    return "unknown";
}

std::string_view to_string(TypeSyntaxKind kind) noexcept {
    switch (kind) {
    case TypeSyntaxKind::Unit:
        return "Unit";
    case TypeSyntaxKind::Bool:
        return "Bool";
    case TypeSyntaxKind::Int:
        return "Int";
    case TypeSyntaxKind::Float:
        return "Float";
    case TypeSyntaxKind::String:
        return "String";
    case TypeSyntaxKind::BoundedString:
        return "BoundedString";
    case TypeSyntaxKind::UUID:
        return "UUID";
    case TypeSyntaxKind::Timestamp:
        return "Timestamp";
    case TypeSyntaxKind::Duration:
        return "Duration";
    case TypeSyntaxKind::Decimal:
        return "Decimal";
    case TypeSyntaxKind::Named:
        return "Named";
    case TypeSyntaxKind::Optional:
        return "Optional";
    case TypeSyntaxKind::List:
        return "List";
    case TypeSyntaxKind::Set:
        return "Set";
    case TypeSyntaxKind::Map:
        return "Map";
    }

    return "Unknown";
}

std::string QualifiedName::spelling() const {
    return join_segments(segments);
}

std::string PathSyntax::spelling() const {
    std::string text = root_name;

    for (const auto &member : members) {
        text += ".";
        text += member;
    }

    return text;
}

std::string TypeSyntax::spelling() const {
    switch (kind) {
    case TypeSyntaxKind::Unit:
    case TypeSyntaxKind::Bool:
    case TypeSyntaxKind::Int:
    case TypeSyntaxKind::Float:
    case TypeSyntaxKind::String:
    case TypeSyntaxKind::UUID:
    case TypeSyntaxKind::Timestamp:
    case TypeSyntaxKind::Duration:
        return std::string(to_string(kind));
    case TypeSyntaxKind::BoundedString: {
        std::ostringstream builder;
        builder << "String(" << string_bounds->first << ", " << string_bounds->second << ")";
        return builder.str();
    }
    case TypeSyntaxKind::Decimal: {
        std::ostringstream builder;
        builder << "Decimal(" << *decimal_scale << ")";
        return builder.str();
    }
    case TypeSyntaxKind::Named:
        return name->spelling();
    case TypeSyntaxKind::Optional:
        return "Optional<" + first->spelling() + ">";
    case TypeSyntaxKind::List:
        return "List<" + first->spelling() + ">";
    case TypeSyntaxKind::Set:
        return "Set<" + first->spelling() + ">";
    case TypeSyntaxKind::Map:
        return "Map<" + first->spelling() + ", " + second->spelling() + ">";
    }

    return "<invalid-type>";
}

std::vector<AstInvariantViolation> validate_program_invariants(const Program &program) {
    return AstInvariantValidator{}.validate(program);
}

Node::Node(NodeKind kind, ahfl::SourceRange range) : kind(kind), range(range) {}

Decl::Decl(NodeKind kind, std::string raw_text, ahfl::SourceRange range)
    : Node(kind, range), raw_text(std::move(raw_text)) {}

Program::Program(std::string source_name, ahfl::SourceRange range)
    : Node(NodeKind::Program, range), source_name(std::move(source_name)) {}

void Program::accept(Visitor &visitor) {
    visitor.visit(*this);
}

ModuleDecl::ModuleDecl(Owned<QualifiedName> name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ModuleDecl, std::move(raw_text), range), name(std::move(name)) {}

void ModuleDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ModuleDecl::headline() const {
    return with_name("module ", name->spelling());
}

ImportDecl::ImportDecl(Owned<QualifiedName> path,
                       std::string alias,
                       std::string raw_text,
                       ahfl::SourceRange range)
    : Decl(NodeKind::ImportDecl, std::move(raw_text), range), path(std::move(path)),
      alias(std::move(alias)) {}

void ImportDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ImportDecl::headline() const {
    if (alias.empty()) {
        return with_name("import ", path->spelling());
    }

    return "import " + path->spelling() + " as " + alias;
}

ConstDecl::ConstDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::ConstDecl, std::move(raw_text), range), name(std::move(name)) {}

void ConstDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ConstDecl::headline() const {
    if (!type) {
        return with_name("const ", name);
    }

    return "const " + name + " : " + type->spelling();
}

TypeAliasDecl::TypeAliasDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::TypeAliasDecl, std::move(raw_text), range), name(std::move(name)) {}

void TypeAliasDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string TypeAliasDecl::headline() const {
    if (!aliased_type) {
        return with_name("type ", name);
    }

    return "type " + name + " = " + aliased_type->spelling();
}

StructDecl::StructDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::StructDecl, std::move(raw_text), range), name(std::move(name)) {}

void StructDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string StructDecl::headline() const {
    return with_count("struct " + name, fields.size(), "field");
}

EnumDecl::EnumDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::EnumDecl, std::move(raw_text), range), name(std::move(name)) {}

void EnumDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string EnumDecl::headline() const {
    return with_count("enum " + name, variants.size(), "variant");
}

CapabilityDecl::CapabilityDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::CapabilityDecl, std::move(raw_text), range), name(std::move(name)) {}

void CapabilityDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string CapabilityDecl::headline() const {
    std::ostringstream builder;
    builder << "capability " << name << " (" << params.size() << " param";
    builder << (params.size() == 1 ? "" : "s") << ")";

    if (return_type) {
        builder << " -> " << return_type->spelling();
    }

    return builder.str();
}

PredicateDecl::PredicateDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::PredicateDecl, std::move(raw_text), range), name(std::move(name)) {}

void PredicateDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string PredicateDecl::headline() const {
    return with_count("predicate " + name, params.size(), "param");
}

AgentDecl::AgentDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::AgentDecl, std::move(raw_text), range), name(std::move(name)) {}

void AgentDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string AgentDecl::headline() const {
    std::ostringstream builder;
    builder << "agent " << name << " (" << states.size() << " states, " << transitions.size()
            << " transitions)";
    return builder.str();
}

ContractDecl::ContractDecl(Owned<QualifiedName> target,
                           std::string raw_text,
                           ahfl::SourceRange range)
    : Decl(NodeKind::ContractDecl, std::move(raw_text), range), target(std::move(target)) {}

void ContractDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string ContractDecl::headline() const {
    return with_count("contract for " + target->spelling(), clauses.size(), "clause");
}

FlowDecl::FlowDecl(Owned<QualifiedName> target, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::FlowDecl, std::move(raw_text), range), target(std::move(target)) {}

void FlowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string FlowDecl::headline() const {
    return with_count("flow for " + target->spelling(), state_handlers.size(), "handler");
}

WorkflowDecl::WorkflowDecl(std::string name, std::string raw_text, ahfl::SourceRange range)
    : Decl(NodeKind::WorkflowDecl, std::move(raw_text), range), name(std::move(name)) {}

void WorkflowDecl::accept(Visitor &visitor) {
    visitor.visit(*this);
}

std::string WorkflowDecl::headline() const {
    return with_count("workflow " + name, nodes.size(), "node");
}

void RecursiveVisitor::visit(Program &node) {
    for (auto &declaration : node.declarations) {
        declaration->accept(*this);
    }
}

void RecursiveVisitor::visit(ModuleDecl &) {}

void RecursiveVisitor::visit(ImportDecl &) {}

void RecursiveVisitor::visit(ConstDecl &) {}

void RecursiveVisitor::visit(TypeAliasDecl &) {}

void RecursiveVisitor::visit(StructDecl &) {}

void RecursiveVisitor::visit(EnumDecl &) {}

void RecursiveVisitor::visit(CapabilityDecl &) {}

void RecursiveVisitor::visit(PredicateDecl &) {}

void RecursiveVisitor::visit(AgentDecl &) {}

void RecursiveVisitor::visit(ContractDecl &) {}

void RecursiveVisitor::visit(FlowDecl &) {}

void RecursiveVisitor::visit(WorkflowDecl &) {}

} // namespace ahfl::ast
