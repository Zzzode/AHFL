#include "ahfl/compiler/frontend/frontend.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ahfl {

namespace {

[[nodiscard]] bool is_std_module_name(std::string_view module_name) noexcept {
    return module_name == "std" || module_name.starts_with("std::");
}

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

    // Standalone hook for DecreasesClauseSyntax.  Public so the free-function
    // wrapper and future consumers (debug printers, LSP hover) can reuse the
    // same indent + expr formatting helpers without duplicating them.  This is
    // a deliberately narrow entry point: DecreasesClauseSyntax is NOT a Node
    // and MUST NOT be dispatched through visit_declaration / NodeKind (R-09).
    void print_decreases_clause(const ast::DecreasesClauseSyntax &clause,
                                int indent_level) {
        line(indent_level, "decreases");
        line(indent_level + 1,
             clause.is_wildcard ? "wildcard: true" : "wildcard: false");
        for (std::size_t i = 0; i < clause.terms.size(); ++i) {
            const auto *term = clause.terms[i].get();
            line(indent_level + 1, "term " + std::to_string(i));
            if (term != nullptr) {
                print_expr(*term, indent_level + 2);
            } else {
                line(indent_level + 2, "<null>");
            }
        }
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
        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }
        print_type_field("aliased_type", node.aliased_type.get(), 2);
    }

    void visit(const ast::StructDecl &node) {
        line(1, "- " + node.headline());

        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }

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

        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }

        for (const auto &variant : node.variants) {
            std::string label = "variant " + variant->name;
            if (!variant->payload.empty()) {
                label += " (";
                for (std::size_t i = 0; i < variant->payload.size(); ++i) {
                    if (i != 0) {
                        label += ", ";
                    }
                    label += variant->payload[i]->spelling();
                }
                label += ")";
            } else if (!variant->named_fields.empty()) {
                label += " (";
                for (std::size_t i = 0; i < variant->named_fields.size(); ++i) {
                    if (i != 0) {
                        label += ", ";
                    }
                    const auto &f = variant->named_fields[i];
                    label += f->name + ": " + (f->type ? f->type->spelling() : std::string("?"));
                    if (f->default_value) {
                        label += " = " + f->default_value->text;
                    }
                }
                label += ")";
            }
            line(2, label);
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

    void visit(const ast::FnDecl &node) {
        line(1, "- " + node.headline());

        if (node.builtin_name.has_value()) {
            line(2, "builtin: \"" + *node.builtin_name + "\"");
        }

        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }

        for (const auto &param : node.params) {
            line(2, "param " + param->name);
            print_type_field("type", param->type.get(), 3);
        }

        print_type_field("return", node.return_type.get(), 2);

        if (node.effect_clause) {
            print_effect_clause(*node.effect_clause, 2);
        }

        if (node.where_clause) {
            print_where_clause(*node.where_clause, 2);
        }

        if (node.body) {
            print_block(*node.body, 2);
        } else {
            line(2, "(prototype)");
        }
    }

    void visit(const ast::TraitDecl &node) {
        line(1, "- " + node.headline());

        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }

        if (!node.super_traits.empty()) {
            line(2, "super_traits");
            for (const auto &super_trait : node.super_traits) {
                line(3, super_trait ? super_trait->spelling() : std::string{});
            }
        }

        if (node.where_clause) {
            print_where_clause(*node.where_clause, 2);
        }

        for (const auto &item : node.items) {
            if (item->kind == ast::TraitItemKind::Fn) {
                line(2, "fn " + item->name);
                for (const auto &param : item->params) {
                    line(3, "param " + param->name);
                    print_type_field("type", param->type.get(), 4);
                }
                print_type_field("return", item->return_type.get(), 3);
                if (item->effect_clause) {
                    print_effect_clause(*item->effect_clause, 3);
                }
                if (item->where_clause) {
                    print_where_clause(*item->where_clause, 3);
                }
            } else if (item->kind == ast::TraitItemKind::AssocType) {
                const auto &assoc = *item->assoc_type;
                line(2, "type " + assoc.name);
                if (!assoc.bounds.empty()) {
                    std::string entry;
                    for (std::size_t index = 0; index < assoc.bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (assoc.bounds[index]) {
                            entry += assoc.bounds[index]->spelling();
                        }
                    }
                    line(3, "bounds: " + entry);
                }
                print_type_field("default", assoc.default_type.get(), 3);
            } else if (item->kind == ast::TraitItemKind::AssocConst) {
                const auto &assoc = *item->assoc_const;
                line(2, "const " + assoc.name);
                print_type_field("type", assoc.type.get(), 3);
                if (assoc.default_value) {
                    print_expr_field("default", assoc.default_value.get(), 3);
                }
            }
        }
    }

    void visit(const ast::ImplDecl &node) {
        line(1, "- " + node.headline());

        if (!node.type_params.empty()) {
            line(2, "type_params");
            for (const auto &type_param : node.type_params) {
                std::string entry = type_param->name;
                if (!type_param->bounds.empty()) {
                    entry += ": <";
                    for (std::size_t index = 0; index < type_param->bounds.size(); ++index) {
                        if (index != 0) {
                            entry += " + ";
                        }
                        if (type_param->bounds[index]) {
                            entry += type_param->bounds[index]->spelling();
                        }
                    }
                    entry += ">";
                }
                line(3, entry);
            }
        }

        print_type_field("trait_ref", node.trait_ref.get(), 2);
        print_type_field("target", node.target_type.get(), 2);

        if (node.where_clause) {
            print_where_clause(*node.where_clause, 2);
        }

        for (const auto &method : node.methods) {
            line(2, "fn " + method->name);
            for (const auto &param : method->params) {
                line(3, "param " + param->name);
                print_type_field("type", param->type.get(), 4);
            }
            print_type_field("return", method->return_type.get(), 3);
            if (method->effect_clause) {
                print_effect_clause(*method->effect_clause, 3);
            }
            if (method->where_clause) {
                print_where_clause(*method->where_clause, 3);
            }
            if (method->body) {
                print_block(*method->body, 3);
            }
        }

        for (const auto &assoc : node.assoc_items) {
            line(2, "type " + assoc->name);
            print_type_field("value", assoc->type.get(), 3);
        }

        for (const auto &assoc_const : node.const_items) {
            line(2, "const " + assoc_const->name);
            print_type_field("type", assoc_const->type.get(), 3);
            if (assoc_const->value) {
                print_expr_field("value", assoc_const->value.get(), 3);
            }
        }
    }

    void print_effect_clause(const ast::EffectClauseSyntax &clause, int indent_level) {
        line(indent_level, "effect");
        line(indent_level + 1, "kind: " + std::string(ast::to_string(clause.kind)));
        if (clause.kind == ast::EffectClauseKind::Capability) {
            std::vector<std::string> capabilities;
            capabilities.reserve(clause.capabilities.size());
            for (const auto &capability : clause.capabilities) {
                capabilities.push_back(capability ? capability->spelling() : std::string{});
            }
            print_string_list("capabilities", capabilities, indent_level + 1);
        }
        if (clause.decreases_expr) {
            print_expr_field("decreases", clause.decreases_expr.get(), indent_level + 1);
        }
    }

    void print_where_clause(const ast::WhereClauseSyntax &clause, int indent_level) {
        line(indent_level, "where");
        for (const auto &constraint : clause.constraints) {
            std::string entry;
            if (constraint->subject) {
                entry += constraint->subject->spelling();
            }
            if (constraint->is_predicate) {
                entry += "::" + constraint->trait_name;
                entry += "(";
                for (std::size_t index = 0; index < constraint->arguments.size(); ++index) {
                    if (index != 0) {
                        entry += ", ";
                    }
                    if (constraint->arguments[index]) {
                        entry += constraint->arguments[index]->spelling();
                    }
                }
                entry += ")";
            } else {
                entry += ": ";
                for (std::size_t index = 0; index < constraint->bounds.size(); ++index) {
                    if (index != 0) {
                        entry += " + ";
                    }
                    if (constraint->bounds[index]) {
                        entry += constraint->bounds[index]->spelling();
                    }
                }
            }
            line(indent_level + 1, entry);
        }
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
        case ast::NodeKind::FnDecl:
            visit(static_cast<const ast::FnDecl &>(declaration));
            return;
        case ast::NodeKind::TraitDecl:
            visit(static_cast<const ast::TraitDecl &>(declaration));
            return;
        case ast::NodeKind::ImplDecl:
            visit(static_cast<const ast::ImplDecl &>(declaration));
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
        std::visit(
            Overloaded{
                [&](const ast::UnitType &) { line(indent_level, "primitive ()"); },
                [&](const ast::BoolType &) { line(indent_level, "primitive Bool"); },
                [&](const ast::IntType &) { line(indent_level, "primitive Int"); },
                [&](const ast::FloatType &) { line(indent_level, "primitive Float"); },
                [&](const ast::StringType &) { line(indent_level, "primitive String"); },
                [&](const ast::BoundedStringType &t) {
                    std::ostringstream ss;
                    ss << "bounded_string String(" << t.min_length << ", " << t.max_length << ")";
                    line(indent_level, ss.str());
                },
                [&](const ast::UuidType &) { line(indent_level, "primitive UUID"); },
                [&](const ast::TimestampType &) { line(indent_level, "primitive Timestamp"); },
                [&](const ast::DurationType &) { line(indent_level, "primitive Duration"); },
                [&](const ast::DecimalType &t) {
                    line(indent_level, "decimal Decimal(" + std::to_string(t.scale) + ")");
                },
                [&](const ast::NamedType &t) {
                    line(indent_level, "named " + t.name->spelling());
                    if (!t.type_args.empty()) {
                        line(indent_level, "type_arguments(" + std::to_string(t.type_args.size()) + ")");
                        for (const auto &arg : t.type_args) {
                            print_type(*arg, indent_level + 1);
                        }
                    }
                },
                [&](const ast::FnType &t) {
                    line(indent_level, "fn");
                    line(indent_level + 1, "params (" + std::to_string(t.params.size()) + ")");
                    for (const auto &param : t.params) {
                        if (param) {
                            print_type(*param, indent_level + 2);
                        }
                    }
                    if (t.return_type) {
                        line(indent_level + 1, "return");
                        print_type(*t.return_type, indent_level + 2);
                    }
                    if (t.has_effect_clause) {
                        line(indent_level + 1, "effect " + std::string(to_string(t.effect_kind)));
                    }
                },
                [&](const ast::AppType &t) {
                    line(indent_level, "app_type " + t.name->spelling());
                    for (std::size_t i = 0; i < t.arguments.size(); ++i) {
                        line(indent_level + 1, "arg " + std::to_string(i));
                        if (t.arguments[i]) {
                            print_type(*t.arguments[i], indent_level + 2);
                        }
                    }
                },
            },
            type.node);
    }

    void print_expr_field(std::string_view label, const ast::ExprSyntax *expr, int indent_level) {
        if (expr == nullptr) {
            return;
        }

        line(indent_level, std::string(label));
        print_expr(*expr, indent_level + 1);
    }

    void print_expr(const ast::ExprSyntax &expr, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ast::BoolLiteralExpr &e) {
                    line(indent_level, std::string("bool ") + (e.value ? "true" : "false"));
                },
                [&](const ast::IntegerLiteralExpr &e) {
                    line(indent_level, "integer " + e.literal->spelling);
                },
                [&](const ast::FloatLiteralExpr &e) { line(indent_level, "float " + e.spelling); },
                [&](const ast::DecimalLiteralExpr &e) {
                    line(indent_level, "decimal " + e.spelling);
                },
                [&](const ast::StringLiteralExpr &e) {
                    line(indent_level, "string " + e.spelling);
                },
                [&](const ast::DurationLiteralExpr &e) {
                    line(indent_level, "duration " + e.literal->spelling);
                },
                [&](const ast::PathExpr &e) { line(indent_level, "path " + e.path->spelling()); },
                [&](const ast::QualifiedValueExpr &e) {
                    line(indent_level, "qualified_value " + e.name->spelling());
                },
                [&](const ast::CallExpr &e) {
                    line(indent_level, "call " + e.callee->spelling());
                    for (std::size_t index = 0; index < e.type_args.size(); ++index) {
                        line(indent_level + 1, "type_arg " + std::to_string(index));
                        print_type(*e.type_args[index], indent_level + 2);
                    }
                    for (std::size_t index = 0; index < e.arguments.size(); ++index) {
                        line(indent_level + 1, "arg " + std::to_string(index));
                        print_expr(*e.arguments[index], indent_level + 2);
                    }
                },
                [&](const ast::MethodCallExpr &e) {
                    line(indent_level, "method_call ." + e.method);
                    line(indent_level + 1, "receiver");
                    print_expr(*e.receiver, indent_level + 2);
                    for (std::size_t index = 0; index < e.type_args.size(); ++index) {
                        line(indent_level + 1, "type_arg " + std::to_string(index));
                        print_type(*e.type_args[index], indent_level + 2);
                    }
                    for (std::size_t index = 0; index < e.arguments.size(); ++index) {
                        line(indent_level + 1, "arg " + std::to_string(index));
                        print_expr(*e.arguments[index], indent_level + 2);
                    }
                },
                [&](const ast::StructLiteralExpr &e) {
                    line(indent_level, "struct_literal " + e.type_name->spelling());
                    for (const auto &field : e.fields) {
                        line(indent_level + 1, "field " + field->field_name);
                        print_expr(*field->value, indent_level + 2);
                    }
                },
                [&](const ast::UnaryExpr &e) {
                    line(indent_level, "unary " + expr_unary_name(e.op));
                    print_expr(*e.operand, indent_level + 1);
                },
                [&](const ast::BinaryExpr &e) {
                    line(indent_level, "binary " + expr_binary_name(e.op));
                    line(indent_level + 1, "lhs");
                    print_expr(*e.lhs, indent_level + 2);
                    line(indent_level + 1, "rhs");
                    print_expr(*e.rhs, indent_level + 2);
                },
                [&](const ast::MemberAccessExpr &e) {
                    line(indent_level, "member_access ." + e.member);
                    print_expr(*e.base, indent_level + 1);
                },
                [&](const ast::IndexAccessExpr &e) {
                    line(indent_level, "index_access");
                    line(indent_level + 1, "base");
                    print_expr(*e.base, indent_level + 2);
                    line(indent_level + 1, "index");
                    print_expr(*e.index, indent_level + 2);
                },
                [&](const ast::GroupExpr &e) {
                    line(indent_level, "group");
                    print_expr(*e.inner, indent_level + 1);
                },
                [&](const ast::MatchExpr &e) {
                    line(indent_level, "match");
                    line(indent_level + 1, "scrutinee");
                    print_expr(*e.scrutinee, indent_level + 2);
                    for (std::size_t index = 0; index < e.arms.size(); ++index) {
                        line(indent_level + 1, "arm " + std::to_string(index));
                        const auto &arm = *e.arms[index];
                        print_pattern(*arm.pattern, indent_level + 2);
                        if (arm.guard) {
                            line(indent_level + 2, "guard");
                            print_expr(*arm.guard, indent_level + 3);
                        }
                        line(indent_level + 2, "body");
                        print_expr(*arm.body, indent_level + 3);
                    }
                },
                [&](const ast::LambdaExpr &e) {
                    line(indent_level, "lambda");
                    if (!e.capture_list.empty()) {
                        line(indent_level + 1, "captures");
                        for (const auto &capture : e.capture_list) {
                            line(indent_level + 2, capture);
                        }
                    }
                    for (const auto &param : e.params) {
                        std::string entry = "param " + param->name;
                        if (param->type) {
                            entry += ": " + param->type->spelling();
                        }
                        line(indent_level + 1, entry);
                    }
                    line(indent_level + 1, "body");
                    print_expr(*e.body, indent_level + 2);
                },
                // P4-02: unwrap(e) expression printer — mirrors the statement
                // form's "unwrap" keyword and operand layout so diffs stay small.
                [&](const ast::UnwrapExprSyntax &e) {
                    line(indent_level, "unwrap_expr");
                    print_expr_field("operand", e.operand.get(), indent_level + 1);
                },
            },
            expr.node);
    }

    void print_pattern(const ast::PatternSyntax &pattern, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ast::LiteralPattern &p) {
                    line(indent_level, "pattern_literal " + p.spelling);
                },
                [&](const ast::VariantPattern &p) {
                    line(indent_level, "pattern_variant " + p.path->spelling());
                    for (std::size_t index = 0; index < p.subpatterns.size(); ++index) {
                        line(indent_level + 1, "sub " + std::to_string(index));
                        print_pattern(*p.subpatterns[index], indent_level + 2);
                    }
                },
                [&](const ast::WildcardPattern &) { line(indent_level, "pattern_wildcard"); },
                [&](const ast::BindingPattern &p) {
                    line(indent_level,
                         std::string("pattern_binding ") + (p.is_mut ? "mut " : "") + p.name);
                    if (p.nested) {
                        line(indent_level + 1, "nested");
                        print_pattern(*p.nested, indent_level + 2);
                    }
                },
                [&](const ast::TuplePattern &p) {
                    line(indent_level, "pattern_tuple");
                    for (std::size_t index = 0; index < p.elements.size(); ++index) {
                        line(indent_level + 1, "elem " + std::to_string(index));
                        print_pattern(*p.elements[index], indent_level + 2);
                    }
                },
                [&](const ast::OrPattern &p) {
                    line(indent_level, "pattern_or");
                    for (std::size_t index = 0; index < p.branches.size(); ++index) {
                        line(indent_level + 1, "branch " + std::to_string(index));
                        print_pattern(*p.branches[index], indent_level + 2);
                    }
                },
            },
            pattern.node);
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
        case ast::StatementSyntaxKind::IfLet: {
            // RFC e-1 minimal POC: mirrors the `if` printer shape with an
            // additional `pattern` section that serializes variant(binding*).
            // Roundtripping the textual surface is handled by the formatter;
            // this printer is for developer/debug consumption.
            line(indent_level, "if_let");
            std::ostringstream pattern_builder;
            if (statement.if_let_stmt && statement.if_let_stmt->pattern) {
                const auto &p = *statement.if_let_stmt->pattern;
                pattern_builder << p.variant_name;
                if (!p.bindings.empty()) {
                    pattern_builder << "(";
                    for (std::size_t i = 0; i < p.bindings.size(); ++i) {
                        if (i != 0) {
                            pattern_builder << ", ";
                        }
                        pattern_builder << p.bindings[i];
                    }
                    pattern_builder << ")";
                }
            }
            line(indent_level + 1, "pattern " + pattern_builder.str());
            if (statement.if_let_stmt) {
                print_expr_field(
                    "scrutinee", statement.if_let_stmt->scrutinee.get(), indent_level + 1);
                line(indent_level + 1, "then");
                if (statement.if_let_stmt->then_block) {
                    print_block(*statement.if_let_stmt->then_block, indent_level + 2);
                }
                if (statement.if_let_stmt->else_block) {
                    line(indent_level + 1, "else");
                    print_block(*statement.if_let_stmt->else_block, indent_level + 2);
                }
            }
            break;
        }
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
            if (statement.assert_stmt->message) {
                print_expr_field("message", statement.assert_stmt->message.get(), indent_level + 1);
            }
            break;
        case ast::StatementSyntaxKind::Unwrap:
            line(indent_level, "unwrap");
            print_expr_field("operand", statement.unwrap_stmt->operand.get(), indent_level + 1);
            break;
        case ast::StatementSyntaxKind::Requires:
            line(indent_level, "requires");
            print_expr_field("condition", statement.requires_stmt->condition.get(), indent_level + 1);
            if (statement.requires_stmt->message) {
                print_expr_field("message", statement.requires_stmt->message.get(), indent_level + 1);
            }
            break;
        case ast::StatementSyntaxKind::Unreachable:
            line(indent_level, "unreachable");
            if (statement.unreachable_stmt && statement.unreachable_stmt->message) {
                print_expr_field("message", statement.unreachable_stmt->message.get(), indent_level + 1);
            }
            break;
        case ast::StatementSyntaxKind::Expr:
            line(indent_level, "expr_stmt");
            print_expr_field("expr", statement.expr_stmt->expr.get(), indent_level + 1);
            break;
        }
    }

    void print_temporal_expr(const ast::TemporalExprSyntax &expr, int indent_level) {
        std::visit(
            Overloaded{
                [&](const ast::EmbeddedTemporalExpr &e) {
                    line(indent_level, "embedded_expr");
                    if (e.expr) {
                        print_expr(*e.expr, indent_level + 1);
                    }
                },
                [&](const ast::CalledTemporalExpr &e) { line(indent_level, "called " + e.name); },
                [&](const ast::InStateTemporalExpr &e) {
                    line(indent_level, "in_state " + e.name);
                },
                [&](const ast::RunningTemporalExpr &e) { line(indent_level, "running " + e.name); },
                [&](const ast::CompletedTemporalExpr &e) {
                    if (e.state_name.has_value()) {
                        line(indent_level, "completed " + e.name + ", " + *e.state_name);
                    } else {
                        line(indent_level, "completed " + e.name);
                    }
                },
                [&](const ast::UnaryTemporalExpr &e) {
                    line(indent_level, "temporal_unary " + temporal_unary_name(e.op));
                    if (e.operand) {
                        print_temporal_expr(*e.operand, indent_level + 1);
                    }
                },
                [&](const ast::BinaryTemporalExpr &e) {
                    line(indent_level, "temporal_binary " + temporal_binary_name(e.op));
                    line(indent_level + 1, "lhs");
                    if (e.lhs) {
                        print_temporal_expr(*e.lhs, indent_level + 2);
                    }
                    line(indent_level + 1, "rhs");
                    if (e.rhs) {
                        print_temporal_expr(*e.rhs, indent_level + 2);
                    }
                },
            },
            expr.node);
    }
};

} // namespace

void dump_program_outline(const ast::Program &program, std::ostream &out) {
    AstPrinter printer(out);
    printer.print(program);
}

void dump_project_ast_outline(const SourceGraph &graph, std::ostream &out) {
    const auto visible_source = [](const SourceUnit &source) {
        return !is_std_module_name(source.module_name);
    };
    const auto source_by_id = [&](SourceId id) -> const SourceUnit * {
        for (const auto &source : graph.sources) {
            if (source.id == id) {
                return &source;
            }
        }
        return nullptr;
    };
    const auto visible_import = [&](const ImportEdge &edge) {
        const auto *importer = source_by_id(edge.importer);
        const auto *imported = source_by_id(edge.imported);
        return importer != nullptr && imported != nullptr && visible_source(*importer) &&
               visible_source(*imported);
    };
    const auto source_count =
        std::count_if(graph.sources.begin(), graph.sources.end(), visible_source);
    const auto import_count =
        std::count_if(graph.import_edges.begin(), graph.import_edges.end(), visible_import);

    out << "project_ast (" << graph.entry_sources.size() << " entry, " << source_count
        << " sources, " << import_count << " import" << (import_count == 1 ? "" : "s")
        << ")\n";

    std::unordered_set<std::size_t> entry_ids;
    entry_ids.reserve(graph.entry_sources.size());
    for (const auto entry_source : graph.entry_sources) {
        entry_ids.insert(entry_source.value);
    }

    for (const auto &source : graph.sources) {
        if (!visible_source(source)) {
            continue;
        }
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

// ---------------------------------------------------------------------------
// DecreasesClauseSyntax – standalone printer (R-09: no DeclKind dispatch).
// ---------------------------------------------------------------------------

void print_decreases_clause(const ast::DecreasesClauseSyntax &clause,
                            std::ostream &out,
                            int base_indent) {
    // base_indent is consumed by AstPrinter's line() helper so callers get
    // predictable column alignment when embedding clause output inside a
    // larger debug dump.  indent_level=0 lets the header start at
    // `base_indent * 2` spaces.
    AstPrinter printer(out, base_indent);
    printer.print_decreases_clause(clause, 0);
}

} // namespace ahfl
