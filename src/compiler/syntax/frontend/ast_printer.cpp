#include "ahfl/compiler/frontend/frontend.hpp"

#include <ostream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace ahfl {

namespace {

[[nodiscard]] std::string expr_unary_name(ast::ExprUnaryOp op) {
    switch (op) {
    case ast::ExprUnaryOp::Not:
        return "not";
    case ast::ExprUnaryOp::Negate:
        return "negate";
    case ast::ExprUnaryOp::Positive:
        return "positive";
    }

    return "unknown";
}

[[nodiscard]] std::string expr_binary_name(ast::ExprBinaryOp op) {
    switch (op) {
    case ast::ExprBinaryOp::Implies:
        return "implies";
    case ast::ExprBinaryOp::Or:
        return "or";
    case ast::ExprBinaryOp::And:
        return "and";
    case ast::ExprBinaryOp::Equal:
        return "equal";
    case ast::ExprBinaryOp::NotEqual:
        return "not_equal";
    case ast::ExprBinaryOp::Less:
        return "less";
    case ast::ExprBinaryOp::LessEqual:
        return "less_equal";
    case ast::ExprBinaryOp::Greater:
        return "greater";
    case ast::ExprBinaryOp::GreaterEqual:
        return "greater_equal";
    case ast::ExprBinaryOp::Add:
        return "add";
    case ast::ExprBinaryOp::Subtract:
        return "subtract";
    case ast::ExprBinaryOp::Multiply:
        return "multiply";
    case ast::ExprBinaryOp::Divide:
        return "divide";
    case ast::ExprBinaryOp::Modulo:
        return "modulo";
    }

    return "unknown";
}

[[nodiscard]] std::string temporal_unary_name(ast::TemporalUnaryOp op) {
    switch (op) {
    case ast::TemporalUnaryOp::Always:
        return "always";
    case ast::TemporalUnaryOp::Eventually:
        return "eventually";
    case ast::TemporalUnaryOp::Next:
        return "next";
    case ast::TemporalUnaryOp::Not:
        return "not";
    }

    return "unknown";
}

[[nodiscard]] std::string temporal_binary_name(ast::TemporalBinaryOp op) {
    switch (op) {
    case ast::TemporalBinaryOp::Implies:
        return "implies";
    case ast::TemporalBinaryOp::Or:
        return "or";
    case ast::TemporalBinaryOp::And:
        return "and";
    case ast::TemporalBinaryOp::Until:
        return "until";
    }

    return "unknown";
}

class AstPrinter final {
  public:
    explicit AstPrinter(std::ostream &out, int base_indent = 0)
        : out_(out), base_indent_(base_indent) {}

    void print(const ast::Program &node) {
        visit(node);
    }

    void visit(const ast::Program &node) {
        line(0, "program " + node.source_name);

        for (const auto &declaration : node.declarations) {
            visit_declaration(*declaration);
        }
    }

    void visit(const ast::ModuleDecl &node) {
        line(1, "- " + node.headline());
    }

    void visit(const ast::ImportDecl &node) {
        line(1, "- " + node.headline());
    }

    void visit(const ast::ConstDecl &node) {
        line(1, "- " + node.headline());
        print_type_field("type", node.type.get(), 2);
        print_expr_field("value", node.value.get(), 2);
    }

    void visit(const ast::TypeAliasDecl &node) {
        line(1, "- " + node.headline());
        print_type_field("aliased_type", node.aliased_type.get(), 2);
    }

    void visit(const ast::StructDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &field : node.fields) {
            line(2, "field " + field->name);
            print_type_field("type", field->type.get(), 3);
            if (field->default_value) {
                print_expr_field("default", field->default_value.get(), 3);
            }
        }
    }

    void visit(const ast::EnumDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &variant : node.variants) {
            line(2, "variant " + variant->name);
        }
    }

    void visit(const ast::CapabilityDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &param : node.params) {
            line(2, "param " + param->name);
            print_type_field("type", param->type.get(), 3);
        }

        print_type_field("return", node.return_type.get(), 2);
        if (node.effect) {
            line(2, "effect");
            line(3, "kind: " + std::string(ast::to_string(node.effect->effect_kind)));
            line(3, "receipt: " + std::string(ast::to_string(node.effect->receipt_mode)));
            line(3, "retry: " + std::string(ast::to_string(node.effect->retry_mode)));
            if (node.effect->domain) {
                line(3, "domain: " + node.effect->domain->spelling());
            }
            if (node.effect->idempotency_key) {
                line(3, "idempotency: " + node.effect->idempotency_key->spelling());
            }
            if (node.effect->timeout) {
                line(3, "timeout: " + node.effect->timeout->spelling);
            }
            if (node.effect->compensation) {
                line(3, "compensation: " + node.effect->compensation->spelling());
            }
            std::vector<std::string> policies;
            policies.reserve(node.effect->policies.size());
            for (const auto &policy : node.effect->policies) {
                policies.push_back(policy->spelling());
            }
            print_string_list("policy", policies, 3);
        }
    }

    void visit(const ast::PredicateDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &param : node.params) {
            line(2, "param " + param->name);
            print_type_field("type", param->type.get(), 3);
        }
    }

    void visit(const ast::AgentDecl &node) {
        line(1, "- " + node.headline());
        print_type_field("input", node.input_type.get(), 2);
        print_type_field("context", node.context_type.get(), 2);
        print_type_field("output", node.output_type.get(), 2);
        print_string_list("states", node.states, 2);
        line(2, "initial: " + node.initial_state);
        print_string_list("final", node.final_states, 2);
        print_string_list("capabilities", node.capabilities, 2);

        if (node.quota) {
            line(2, "quota");
            for (const auto &item : node.quota->items) {
                switch (item->kind) {
                case ast::AgentQuotaItemKind::MaxToolCalls:
                    line(3, "max_tool_calls: " + item->integer_value->spelling);
                    break;
                case ast::AgentQuotaItemKind::MaxExecutionTime:
                    line(3, "max_execution_time: " + item->duration_value->spelling);
                    break;
                }
            }
        }

        for (const auto &transition : node.transitions) {
            line(2, "transition " + transition->from_state + " -> " + transition->to_state);
        }
    }

    void visit(const ast::ContractDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &clause : node.clauses) {
            line(2, std::string(to_string(clause->kind)));
            if (clause->expr) {
                print_expr(*clause->expr, 3);
            }

            if (clause->temporal_expr) {
                print_temporal_expr(*clause->temporal_expr, 3);
            }
        }
    }

    void visit(const ast::FlowDecl &node) {
        line(1, "- " + node.headline());

        for (const auto &handler : node.state_handlers) {
            line(2, "state " + handler->state_name);

            if (handler->policy) {
                line(3, "policy");
                for (const auto &item : handler->policy->items) {
                    switch (item->kind) {
                    case ast::StatePolicyItemKind::Retry:
                        line(4, "retry: " + item->retry_limit->spelling);
                        break;
                    case ast::StatePolicyItemKind::RetryOn:
                        line(4, "retry_on");
                        for (const auto &name : item->retry_on) {
                            line(5, name->spelling());
                        }
                        break;
                    case ast::StatePolicyItemKind::Timeout:
                        line(4, "timeout: " + item->timeout->spelling);
                        break;
                    }
                }
            }

            print_block(*handler->body, 3);
        }
    }

    void visit(const ast::WorkflowDecl &node) {
        line(1, "- " + node.headline());
        print_type_field("input", node.input_type.get(), 2);
        print_type_field("output", node.output_type.get(), 2);

        for (const auto &workflow_node : node.nodes) {
            line(2, "node " + workflow_node->name + " -> " + workflow_node->target->spelling());
            print_expr_field("input", workflow_node->input.get(), 3);
            if (!workflow_node->after.empty()) {
                print_string_list("after", workflow_node->after, 3);
            }
        }

        for (const auto &formula : node.safety) {
            line(2, "safety");
            print_temporal_expr(*formula, 3);
        }

        for (const auto &formula : node.liveness) {
            line(2, "liveness");
            print_temporal_expr(*formula, 3);
        }

        print_expr_field("return", node.return_value.get(), 2);
    }

  private:
    std::ostream &out_;
    int base_indent_{0};

    void visit_declaration(const ast::Decl &declaration) {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
            visit(static_cast<const ast::ModuleDecl &>(declaration));
            return;
        case ast::NodeKind::ImportDecl:
            visit(static_cast<const ast::ImportDecl &>(declaration));
            return;
        case ast::NodeKind::ConstDecl:
            visit(static_cast<const ast::ConstDecl &>(declaration));
            return;
        case ast::NodeKind::TypeAliasDecl:
            visit(static_cast<const ast::TypeAliasDecl &>(declaration));
            return;
        case ast::NodeKind::StructDecl:
            visit(static_cast<const ast::StructDecl &>(declaration));
            return;
        case ast::NodeKind::EnumDecl:
            visit(static_cast<const ast::EnumDecl &>(declaration));
            return;
        case ast::NodeKind::CapabilityDecl:
            visit(static_cast<const ast::CapabilityDecl &>(declaration));
            return;
        case ast::NodeKind::PredicateDecl:
            visit(static_cast<const ast::PredicateDecl &>(declaration));
            return;
        case ast::NodeKind::AgentDecl:
            visit(static_cast<const ast::AgentDecl &>(declaration));
            return;
        case ast::NodeKind::ContractDecl:
            visit(static_cast<const ast::ContractDecl &>(declaration));
            return;
        case ast::NodeKind::FlowDecl:
            visit(static_cast<const ast::FlowDecl &>(declaration));
            return;
        case ast::NodeKind::WorkflowDecl:
            visit(static_cast<const ast::WorkflowDecl &>(declaration));
            return;
        case ast::NodeKind::Program:
            return;
        }
    }

    void line(int indent_level, const std::string &text) {
        const auto effective_indent = std::max(0, base_indent_ + indent_level);
        out_ << std::string(static_cast<std::size_t>(effective_indent) * 2, ' ') << text << '\n';
    }

    void print_string_list(std::string_view label,
                           const std::vector<std::string> &values,
                           int indent_level) {
        if (values.empty()) {
            line(indent_level, std::string(label) + ": []");
            return;
        }

        std::ostringstream builder;
        builder << label << ": [";

        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) {
                builder << ", ";
            }

            builder << values[index];
        }

        builder << "]";
        line(indent_level, builder.str());
    }

    void print_type_field(std::string_view label, const ast::TypeSyntax *type, int indent_level) {
        if (type == nullptr) {
            return;
        }

        line(indent_level, std::string(label));
        print_type(*type, indent_level + 1);
    }

    void print_type(const ast::TypeSyntax &type, int indent_level) {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Unit:
        case ast::TypeSyntaxKind::Bool:
        case ast::TypeSyntaxKind::Int:
        case ast::TypeSyntaxKind::Float:
        case ast::TypeSyntaxKind::String:
        case ast::TypeSyntaxKind::UUID:
        case ast::TypeSyntaxKind::Timestamp:
        case ast::TypeSyntaxKind::Duration:
            line(indent_level, "primitive " + type.spelling());
            break;
        case ast::TypeSyntaxKind::BoundedString:
            line(indent_level, "bounded_string " + type.spelling());
            break;
        case ast::TypeSyntaxKind::Decimal:
            line(indent_level, "decimal " + type.spelling());
            break;
        case ast::TypeSyntaxKind::Named:
            line(indent_level, "named " + type.name->spelling());
            break;
        case ast::TypeSyntaxKind::Optional:
            line(indent_level, "optional");
            print_type(*type.first, indent_level + 1);
            break;
        case ast::TypeSyntaxKind::List:
            line(indent_level, "list");
            print_type(*type.first, indent_level + 1);
            break;
        case ast::TypeSyntaxKind::Set:
            line(indent_level, "set");
            print_type(*type.first, indent_level + 1);
            break;
        case ast::TypeSyntaxKind::Map:
            line(indent_level, "map");
            line(indent_level + 1, "key");
            print_type(*type.first, indent_level + 2);
            line(indent_level + 1, "value");
            print_type(*type.second, indent_level + 2);
            break;
        }
    }

    void print_expr_field(std::string_view label, const ast::ExprSyntax *expr, int indent_level) {
        if (expr == nullptr) {
            return;
        }

        line(indent_level, std::string(label));
        print_expr(*expr, indent_level + 1);
    }

    void print_expr(const ast::ExprSyntax &expr, int indent_level) {
        switch (expr.kind) {
        case ast::ExprSyntaxKind::BoolLiteral:
            line(indent_level, std::string("bool ") + (expr.bool_value ? "true" : "false"));
            break;
        case ast::ExprSyntaxKind::IntegerLiteral:
            line(indent_level, "integer " + expr.integer_literal->spelling);
            break;
        case ast::ExprSyntaxKind::FloatLiteral:
            line(indent_level, "float " + expr.text);
            break;
        case ast::ExprSyntaxKind::DecimalLiteral:
            line(indent_level, "decimal " + expr.text);
            break;
        case ast::ExprSyntaxKind::StringLiteral:
            line(indent_level, "string " + expr.text);
            break;
        case ast::ExprSyntaxKind::DurationLiteral:
            line(indent_level, "duration " + expr.duration_literal->spelling);
            break;
        case ast::ExprSyntaxKind::NoneLiteral:
            line(indent_level, "none");
            break;
        case ast::ExprSyntaxKind::Some:
            line(indent_level, "some");
            print_expr(*expr.first, indent_level + 1);
            break;
        case ast::ExprSyntaxKind::Path:
            line(indent_level, "path " + expr.path->spelling());
            break;
        case ast::ExprSyntaxKind::QualifiedValue:
            line(indent_level, "qualified_value " + expr.qualified_name->spelling());
            break;
        case ast::ExprSyntaxKind::Call:
            line(indent_level, "call " + expr.qualified_name->spelling());
            for (std::size_t index = 0; index < expr.items.size(); ++index) {
                line(indent_level + 1, "arg " + std::to_string(index));
                print_expr(*expr.items[index], indent_level + 2);
            }
            break;
        case ast::ExprSyntaxKind::StructLiteral:
            line(indent_level, "struct_literal " + expr.qualified_name->spelling());
            for (const auto &field : expr.struct_fields) {
                line(indent_level + 1, "field " + field->field_name);
                print_expr(*field->value, indent_level + 2);
            }
            break;
        case ast::ExprSyntaxKind::ListLiteral:
            line(indent_level, "list_literal");
            for (std::size_t index = 0; index < expr.items.size(); ++index) {
                line(indent_level + 1, "item " + std::to_string(index));
                print_expr(*expr.items[index], indent_level + 2);
            }
            break;
        case ast::ExprSyntaxKind::SetLiteral:
            line(indent_level, "set_literal");
            for (std::size_t index = 0; index < expr.items.size(); ++index) {
                line(indent_level + 1, "item " + std::to_string(index));
                print_expr(*expr.items[index], indent_level + 2);
            }
            break;
        case ast::ExprSyntaxKind::MapLiteral:
            line(indent_level, "map_literal");
            for (std::size_t index = 0; index < expr.map_entries.size(); ++index) {
                line(indent_level + 1, "entry " + std::to_string(index));
                line(indent_level + 2, "key");
                print_expr(*expr.map_entries[index]->key, indent_level + 3);
                line(indent_level + 2, "value");
                print_expr(*expr.map_entries[index]->value, indent_level + 3);
            }
            break;
        case ast::ExprSyntaxKind::Unary:
            line(indent_level, "unary " + expr_unary_name(expr.unary_op));
            print_expr(*expr.first, indent_level + 1);
            break;
        case ast::ExprSyntaxKind::Binary:
            line(indent_level, "binary " + expr_binary_name(expr.binary_op));
            line(indent_level + 1, "lhs");
            print_expr(*expr.first, indent_level + 2);
            line(indent_level + 1, "rhs");
            print_expr(*expr.second, indent_level + 2);
            break;
        case ast::ExprSyntaxKind::MemberAccess:
            line(indent_level, "member_access ." + expr.name);
            print_expr(*expr.first, indent_level + 1);
            break;
        case ast::ExprSyntaxKind::IndexAccess:
            line(indent_level, "index_access");
            line(indent_level + 1, "base");
            print_expr(*expr.first, indent_level + 2);
            line(indent_level + 1, "index");
            print_expr(*expr.second, indent_level + 2);
            break;
        case ast::ExprSyntaxKind::Group:
            line(indent_level, "group");
            print_expr(*expr.first, indent_level + 1);
            break;
        }
    }

    void print_block(const ast::BlockSyntax &block, int indent_level) {
        line(indent_level, "block");
        for (const auto &statement : block.statements) {
            print_statement(*statement, indent_level + 1);
        }
    }

    void print_statement(const ast::StatementSyntax &statement, int indent_level) {
        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
            line(indent_level, "let " + statement.let_stmt->name);
            if (statement.let_stmt->type) {
                print_type_field("type", statement.let_stmt->type.get(), indent_level + 1);
            }
            print_expr_field(
                "initializer", statement.let_stmt->initializer.get(), indent_level + 1);
            break;
        case ast::StatementSyntaxKind::Assign:
            line(indent_level, "assign " + statement.assign_stmt->target->spelling());
            print_expr_field("value", statement.assign_stmt->value.get(), indent_level + 1);
            break;
        case ast::StatementSyntaxKind::If:
            line(indent_level, "if");
            print_expr_field("condition", statement.if_stmt->condition.get(), indent_level + 1);
            line(indent_level + 1, "then");
            print_block(*statement.if_stmt->then_block, indent_level + 2);
            if (statement.if_stmt->else_block) {
                line(indent_level + 1, "else");
                print_block(*statement.if_stmt->else_block, indent_level + 2);
            }
            break;
        case ast::StatementSyntaxKind::Goto:
            line(indent_level, "goto " + statement.goto_stmt->target_state);
            break;
        case ast::StatementSyntaxKind::Return:
            line(indent_level, "return");
            print_expr_field("value", statement.return_stmt->value.get(), indent_level + 1);
            break;
        case ast::StatementSyntaxKind::Assert:
            line(indent_level, "assert");
            print_expr_field("condition", statement.assert_stmt->condition.get(), indent_level + 1);
            break;
        case ast::StatementSyntaxKind::Expr:
            line(indent_level, "expr_stmt");
            print_expr_field("expr", statement.expr_stmt->expr.get(), indent_level + 1);
            break;
        }
    }

    void print_temporal_expr(const ast::TemporalExprSyntax &expr, int indent_level) {
        switch (expr.kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            line(indent_level, "embedded_expr");
            print_expr(*expr.expr, indent_level + 1);
            break;
        case ast::TemporalExprSyntaxKind::Called:
            line(indent_level, "called " + expr.name);
            break;
        case ast::TemporalExprSyntaxKind::InState:
            line(indent_level, "in_state " + expr.name);
            break;
        case ast::TemporalExprSyntaxKind::Running:
            line(indent_level, "running " + expr.name);
            break;
        case ast::TemporalExprSyntaxKind::Completed:
            if (expr.state_name.has_value()) {
                line(indent_level, "completed " + expr.name + ", " + *expr.state_name);
            } else {
                line(indent_level, "completed " + expr.name);
            }
            break;
        case ast::TemporalExprSyntaxKind::Unary:
            line(indent_level, "temporal_unary " + temporal_unary_name(expr.unary_op));
            print_temporal_expr(*expr.first, indent_level + 1);
            break;
        case ast::TemporalExprSyntaxKind::Binary:
            line(indent_level, "temporal_binary " + temporal_binary_name(expr.binary_op));
            line(indent_level + 1, "lhs");
            print_temporal_expr(*expr.first, indent_level + 2);
            line(indent_level + 1, "rhs");
            print_temporal_expr(*expr.second, indent_level + 2);
            break;
        }
    }
};

} // namespace

void dump_program_outline(const ast::Program &program, std::ostream &out) {
    AstPrinter printer(out);
    printer.print(program);
}

void dump_project_ast_outline(const SourceGraph &graph, std::ostream &out) {
    out << "project_ast (" << graph.entry_sources.size() << " entry, " << graph.sources.size()
        << " sources, " << graph.import_edges.size() << " import"
        << (graph.import_edges.size() == 1 ? "" : "s") << ")\n";

    std::unordered_set<std::size_t> entry_ids;
    entry_ids.reserve(graph.entry_sources.size());
    for (const auto entry_source : graph.entry_sources) {
        entry_ids.insert(entry_source.value);
    }

    for (const auto &source : graph.sources) {
        out << "source " << source.source.display_name;
        if (entry_ids.contains(source.id.value)) {
            out << " [entry]";
        }
        out << '\n';
        out << "  module " << source.module_name << '\n';
        out << "  ast\n";
        AstPrinter printer(out, 2);
        printer.print(*source.program);
    }
}

} // namespace ahfl
