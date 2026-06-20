#include "tooling/formatter/formatter.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <variant>

namespace ahfl::formatter {

namespace {

std::string make_indent(int level, const FormatOptions &opts) {
    if (opts.use_tabs) {
        return std::string(static_cast<std::size_t>(level), '\t');
    }
    return std::string(static_cast<std::size_t>(level * opts.indent_width), ' ');
}

// Fallback formatter: brace-counting for unparseable input
FormatResult format_source_fallback(const std::string &source, const FormatOptions &options) {
    FormatResult result;
    std::istringstream stream(source);
    std::ostringstream out;
    std::string line;
    int indent_level = 0;

    while (std::getline(stream, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        // Trim leading whitespace for re-indentation
        std::string_view trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
            trimmed.remove_prefix(1);
        }

        // Adjust indent for closing braces
        int close_count = 0;
        for (char c : trimmed) {
            if (c == '}')
                ++close_count;
        }
        (void)close_count;
        // If line starts with closing brace, dedent first
        if (!trimmed.empty() && trimmed.front() == '}') {
            indent_level = std::max(0, indent_level - 1);
        }

        if (trimmed.empty()) {
            out << "\n";
        } else {
            out << make_indent(indent_level, options) << trimmed << "\n";
        }

        // Count braces for next line indent
        int open_count = 0;
        for (char c : trimmed) {
            if (c == '{')
                ++open_count;
            else if (c == '}')
                --open_count;
        }
        // If line starts with }, we already decremented
        if (!trimmed.empty() && trimmed.front() == '}') {
            indent_level += open_count + 1; // undo the front-} we already handled
        } else {
            indent_level = std::max(0, indent_level + open_count);
        }
    }

    result.formatted = out.str();
    if (options.trailing_newline && !result.formatted.empty() && result.formatted.back() != '\n') {
        result.formatted += "\n";
    }
    result.success = true;
    return result;
}

// AST-aware formatter
class AstFormatter {
  public:
    explicit AstFormatter(const FormatOptions &opts) : opts_(opts) {}

    std::string format(const ahfl::ast::Program &program) {
        // Separate imports from other declarations
        std::vector<const ahfl::ast::ImportDecl *> imports;
        std::vector<const ahfl::ast::Decl *> others;

        for (const auto &decl : program.declarations) {
            if (auto *imp = dynamic_cast<const ahfl::ast::ImportDecl *>(decl.get())) {
                imports.push_back(imp);
            } else {
                others.push_back(decl.get());
            }
        }

        // Imports (sorted if requested)
        if (opts_.sort_imports && imports.size() > 1) {
            std::sort(imports.begin(),
                      imports.end(),
                      [](const ahfl::ast::ImportDecl *a, const ahfl::ast::ImportDecl *b) {
                          return a->path->spelling() < b->path->spelling();
                      });
        }
        for (const auto *imp : imports) {
            write("import ");
            write(imp->path->spelling());
            if (!imp->alias.empty()) {
                write(" as " + imp->alias);
            }
            write(";");
            newline();
        }

        // Top-level declarations
        bool needs_separator = !imports.empty() && !others.empty();
        for (const auto *decl : others) {
            if (needs_separator) {
                newline();
                needs_separator = false;
            }
            format_declaration(*decl);
        }

        return out_.str();
    }

  private:
    void format_declaration(const ahfl::ast::Decl &decl) {
        if (auto *s = dynamic_cast<const ahfl::ast::StructDecl *>(&decl)) {
            format_struct(*s);
        } else if (auto *e = dynamic_cast<const ahfl::ast::EnumDecl *>(&decl)) {
            format_enum(*e);
        } else if (auto *c = dynamic_cast<const ahfl::ast::CapabilityDecl *>(&decl)) {
            format_capability(*c);
        } else if (auto *a = dynamic_cast<const ahfl::ast::AgentDecl *>(&decl)) {
            format_agent(*a);
        } else if (auto *w = dynamic_cast<const ahfl::ast::WorkflowDecl *>(&decl)) {
            format_workflow(*w);
        } else if (auto *c = dynamic_cast<const ahfl::ast::ConstDecl *>(&decl)) {
            format_const(*c);
        } else if (auto *t = dynamic_cast<const ahfl::ast::TypeAliasDecl *>(&decl)) {
            format_type_alias(*t);
        } else if (auto *p = dynamic_cast<const ahfl::ast::PredicateDecl *>(&decl)) {
            format_predicate(*p);
        } else if (auto *c = dynamic_cast<const ahfl::ast::ContractDecl *>(&decl)) {
            format_contract(*c);
        } else if (auto *f = dynamic_cast<const ahfl::ast::FlowDecl *>(&decl)) {
            format_flow(*f);
        } else if (auto *m = dynamic_cast<const ahfl::ast::ModuleDecl *>(&decl)) {
            format_module(*m);
        } else {
            // For any other decl type, emit its headline as a comment
            write("// " + decl.headline());
            newline();
        }
    }

    void format_struct(const ahfl::ast::StructDecl &s) {
        write("struct " + s.name + " {");
        newline();
        indent_++;
        for (const auto &field : s.fields) {
            write(make_indent(indent_, opts_));
            out_ << field->name << ": " << field->type->spelling() << ";";
            newline();
        }
        indent_--;
        write("}");
        newline();
    }

    void format_enum(const ahfl::ast::EnumDecl &e) {
        write("enum " + e.name + " {");
        newline();
        indent_++;
        for (const auto &variant : e.variants) {
            write(make_indent(indent_, opts_) + variant->name + ",");
            newline();
        }
        indent_--;
        write("}");
        newline();
    }

    void format_capability(const ahfl::ast::CapabilityDecl &c) {
        write("capability " + c.name + "(");
        bool first = true;
        for (const auto &param : c.params) {
            if (!first)
                out_ << ", ";
            out_ << param->name << ": " << param->type->spelling();
            first = false;
        }
        out_ << ")";
        if (c.return_type) {
            out_ << " -> " << c.return_type->spelling();
        }
        out_ << ";";
        newline();
    }

    void format_agent(const ahfl::ast::AgentDecl &a) {
        write("agent " + a.name + " {");
        newline();
        indent_++;

        // Input/Output/Context types
        if (a.input_type) {
            write(make_indent(indent_, opts_) + "input: " + a.input_type->spelling() + ";");
            newline();
        }
        if (a.output_type) {
            write(make_indent(indent_, opts_) + "output: " + a.output_type->spelling() + ";");
            newline();
        }
        if (a.context_type) {
            write(make_indent(indent_, opts_) + "context: " + a.context_type->spelling() + ";");
            newline();
        }

        // States
        if (!a.states.empty()) {
            newline();
            write(make_indent(indent_, opts_) + "states {");
            newline();
            indent_++;
            for (const auto &state : a.states) {
                write(make_indent(indent_, opts_) + state + ",");
                newline();
            }
            indent_--;
            write(make_indent(indent_, opts_) + "}");
            newline();
        }

        // Initial state
        if (!a.initial_state.empty()) {
            write(make_indent(indent_, opts_) + "initial " + a.initial_state + ";");
            newline();
        }

        // Transitions
        for (const auto &t : a.transitions) {
            write(make_indent(indent_, opts_) + "transition " + t->from_state + " -> " +
                  t->to_state + ";");
            newline();
        }

        indent_--;
        write("}");
        newline();
    }

    void format_workflow(const ahfl::ast::WorkflowDecl &w) {
        write("workflow " + w.name + " {");
        newline();
        indent_++;
        for (const auto &node : w.nodes) {
            write(make_indent(indent_, opts_) + "node " + node->name);
            if (node->target) {
                out_ << " : " << node->target->spelling();
            }
            out_ << ";";
            newline();
        }
        indent_--;
        write("}");
        newline();
    }

    void format_const(const ahfl::ast::ConstDecl &c) {
        write("const " + c.name + ": ");
        if (c.type) {
            write(c.type->spelling());
        } else {
            write("auto");
        }
        write(" = ");
        if (c.value) {
            format_expr(*c.value);
        } else {
            write("/* no value */");
        }
        write(";");
        newline();
    }

    void format_type_alias(const ahfl::ast::TypeAliasDecl &t) {
        write("type " + t.name + " = ");
        if (t.aliased_type) {
            write(t.aliased_type->spelling());
        } else {
            write("/* no type */");
        }
        write(";");
        newline();
    }

    void format_predicate(const ahfl::ast::PredicateDecl &p) {
        write("predicate " + p.name + "(");
        bool first = true;
        for (const auto &param : p.params) {
            if (!first)
                out_ << ", ";
            out_ << param->name << ": " << param->type->spelling();
            first = false;
        }
        out_ << ") -> Bool;";
        newline();
    }

    void format_contract(const ahfl::ast::ContractDecl &c) {
        write("contract for " + c.target->spelling() + " {");
        newline();
        indent_++;
        for (const auto &clause : c.clauses) {
            write(make_indent(indent_, opts_));
            format_contract_clause(*clause);
            newline();
        }
        indent_--;
        write("}");
        newline();
    }

    void format_contract_clause(const ahfl::ast::ContractClauseSyntax &clause) {
        using Kind = ahfl::ast::ContractClauseKind;
        switch (clause.kind) {
        case Kind::Requires:
            write("requires ");
            break;
        case Kind::Ensures:
            write("ensures ");
            break;
        case Kind::Invariant:
            write("invariant ");
            break;
        case Kind::Forbid:
            write("forbid ");
            break;
        }
        if (clause.temporal_expr) {
            format_temporal_expr(*clause.temporal_expr);
        } else if (clause.expr) {
            format_expr(*clause.expr);
        }
        write(";");
    }

    void format_flow(const ahfl::ast::FlowDecl &f) {
        write("flow for " + f.target->spelling() + " {");
        newline();
        indent_++;
        for (const auto &handler : f.state_handlers) {
            format_state_handler(*handler);
        }
        indent_--;
        write("}");
        newline();
    }

    void format_state_handler(const ahfl::ast::StateHandlerSyntax &handler) {
        write(make_indent(indent_, opts_) + "state " + handler.state_name);
        if (handler.policy && !handler.policy->items.empty()) {
            out_ << " policy { ";
            bool first = true;
            for (const auto &item : handler.policy->items) {
                if (!first)
                    out_ << ", ";
                format_state_policy_item(*item);
                first = false;
            }
            out_ << " }";
        }
        out_ << " {";
        newline();
        indent_++;
        if (handler.body) {
            format_block(*handler.body);
        }
        indent_--;
        write(make_indent(indent_, opts_) + "}");
        newline();
    }

    void format_state_policy_item(const ahfl::ast::StatePolicyItemSyntax &item) {
        using Kind = ahfl::ast::StatePolicyItemKind;
        switch (item.kind) {
        case Kind::Retry:
            if (item.retry_limit) {
                out_ << "retry: " << item.retry_limit->spelling;
            }
            break;
        case Kind::RetryOn:
            out_ << "retry_on: [";
            for (std::size_t i = 0; i < item.retry_on.size(); ++i) {
                if (i > 0)
                    out_ << ", ";
                out_ << item.retry_on[i]->spelling();
            }
            out_ << "]";
            break;
        case Kind::Timeout:
            if (item.timeout) {
                out_ << "timeout: " << item.timeout->spelling;
            }
            break;
        }
    }

    void format_module(const ahfl::ast::ModuleDecl &m) {
        write("module " + m.name->spelling() + ";");
        newline();
    }

    void format_block(const ahfl::ast::BlockSyntax &block) {
        for (const auto &stmt : block.statements) {
            format_statement(*stmt);
        }
    }

    void format_statement(const ahfl::ast::StatementSyntax &stmt) {
        using Kind = ahfl::ast::StatementSyntaxKind;
        write(make_indent(indent_, opts_));
        switch (stmt.kind) {
        case Kind::Let:
            if (stmt.let_stmt) {
                write("let " + stmt.let_stmt->name);
                if (stmt.let_stmt->type) {
                    out_ << ": " << stmt.let_stmt->type->spelling();
                }
                if (stmt.let_stmt->initializer) {
                    out_ << " = ";
                    format_expr(*stmt.let_stmt->initializer);
                }
                write(";");
            }
            break;
        case Kind::Assign:
            if (stmt.assign_stmt) {
                write(stmt.assign_stmt->target->spelling());
                out_ << " = ";
                format_expr(*stmt.assign_stmt->value);
                write(";");
            }
            break;
        case Kind::If:
            if (stmt.if_stmt) {
                write("if (");
                format_expr(*stmt.if_stmt->condition);
                out_ << ") {";
                newline();
                indent_++;
                if (stmt.if_stmt->then_block) {
                    format_block(*stmt.if_stmt->then_block);
                }
                indent_--;
                write(make_indent(indent_, opts_) + "}");
                if (stmt.if_stmt->else_block) {
                    out_ << " else {";
                    newline();
                    indent_++;
                    format_block(*stmt.if_stmt->else_block);
                    indent_--;
                    write(make_indent(indent_, opts_) + "}");
                }
            }
            break;
        case Kind::Goto:
            if (stmt.goto_stmt) {
                write("goto " + stmt.goto_stmt->target_state + ";");
            }
            break;
        case Kind::Return:
            if (stmt.return_stmt && stmt.return_stmt->value) {
                write("return ");
                format_expr(*stmt.return_stmt->value);
                write(";");
            } else {
                write("return;");
            }
            break;
        case Kind::Assert:
            if (stmt.assert_stmt) {
                write("assert(");
                format_expr(*stmt.assert_stmt->condition);
                write(");");
            }
            break;
        case Kind::Expr:
            if (stmt.expr_stmt && stmt.expr_stmt->expr) {
                format_expr(*stmt.expr_stmt->expr);
                write(";");
            }
            break;
        }
        newline();
    }

    void format_expr(const ahfl::ast::ExprSyntax &expr) {
        std::visit(
            Overloaded{
                [&](const ahfl::ast::NoneLiteralExpr &) { write("none"); },
                [&](const ahfl::ast::BoolLiteralExpr &e) { write(e.value ? "true" : "false"); },
                [&](const ahfl::ast::IntegerLiteralExpr &e) {
                    if (e.literal) {
                        write(e.literal->spelling);
                    } else {
                        write(expr.text);
                    }
                },
                [&](const ahfl::ast::FloatLiteralExpr &e) { write(e.spelling); },
                [&](const ahfl::ast::DecimalLiteralExpr &e) { write(e.spelling); },
                [&](const ahfl::ast::StringLiteralExpr &e) { write(e.spelling); },
                [&](const ahfl::ast::DurationLiteralExpr &e) {
                    if (e.literal) {
                        write(e.literal->spelling);
                    } else {
                        write(expr.text);
                    }
                },
                [&](const ahfl::ast::SomeExpr &e) {
                    write("some(");
                    if (e.value) {
                        format_expr(*e.value);
                    }
                    write(")");
                },
                [&](const ahfl::ast::PathExpr &e) {
                    if (e.path) {
                        write(e.path->spelling());
                    }
                },
                [&](const ahfl::ast::QualifiedValueExpr &e) {
                    if (e.name) {
                        write(e.name->spelling());
                    }
                },
                [&](const ahfl::ast::CallExpr &e) {
                    if (e.callee) {
                        write(e.callee->spelling());
                    }
                    write("(");
                    for (std::size_t i = 0; i < e.arguments.size(); ++i) {
                        if (i > 0)
                            out_ << ", ";
                        if (e.arguments[i]) {
                            format_expr(*e.arguments[i]);
                        }
                    }
                    write(")");
                },
                [&](const ahfl::ast::StructLiteralExpr &e) {
                    if (e.type_name) {
                        write(e.type_name->spelling());
                    }
                    write(" { ");
                    for (std::size_t i = 0; i < e.fields.size(); ++i) {
                        if (i > 0)
                            out_ << ", ";
                        const auto &field = e.fields[i];
                        out_ << field->field_name << ": ";
                        if (field->value) {
                            format_expr(*field->value);
                        }
                    }
                    write(" }");
                },
                [&](const ahfl::ast::ListLiteralExpr &e) {
                    write("[");
                    for (std::size_t i = 0; i < e.items.size(); ++i) {
                        if (i > 0)
                            out_ << ", ";
                        if (e.items[i]) {
                            format_expr(*e.items[i]);
                        }
                    }
                    write("]");
                },
                [&](const ahfl::ast::SetLiteralExpr &e) {
                    write("{");
                    for (std::size_t i = 0; i < e.items.size(); ++i) {
                        if (i > 0)
                            out_ << ", ";
                        if (e.items[i]) {
                            format_expr(*e.items[i]);
                        }
                    }
                    write("}");
                },
                [&](const ahfl::ast::MapLiteralExpr &e) {
                    write("{");
                    for (std::size_t i = 0; i < e.entries.size(); ++i) {
                        if (i > 0)
                            out_ << ", ";
                        const auto &entry = e.entries[i];
                        if (entry->key) {
                            format_expr(*entry->key);
                        }
                        out_ << ": ";
                        if (entry->value) {
                            format_expr(*entry->value);
                        }
                    }
                    write("}");
                },
                [&](const ahfl::ast::UnaryExpr &e) {
                    format_unary_op(e.op);
                    if (e.operand) {
                        format_expr(*e.operand);
                    }
                },
                [&](const ahfl::ast::BinaryExpr &e) {
                    if (e.lhs) {
                        format_expr(*e.lhs);
                    }
                    out_ << " ";
                    format_binary_op(e.op);
                    out_ << " ";
                    if (e.rhs) {
                        format_expr(*e.rhs);
                    }
                },
                [&](const ahfl::ast::MemberAccessExpr &e) {
                    if (e.base) {
                        format_expr(*e.base);
                    }
                    out_ << "." << e.member;
                },
                [&](const ahfl::ast::IndexAccessExpr &e) {
                    if (e.base) {
                        format_expr(*e.base);
                    }
                    write("[");
                    if (e.index) {
                        format_expr(*e.index);
                    }
                    write("]");
                },
                [&](const ahfl::ast::GroupExpr &e) {
                    write("(");
                    if (e.inner) {
                        format_expr(*e.inner);
                    }
                    write(")");
                },
            },
            expr.node);
    }

    void format_unary_op(ahfl::ast::ExprUnaryOp op) {
        using Op = ahfl::ast::ExprUnaryOp;
        switch (op) {
        case Op::Not:
            write("!");
            break;
        case Op::Negate:
            write("-");
            break;
        case Op::Positive:
            write("+");
            break;
        }
    }

    void format_binary_op(ahfl::ast::ExprBinaryOp op) {
        using Op = ahfl::ast::ExprBinaryOp;
        switch (op) {
        case Op::Implies:
            write("=>");
            break;
        case Op::Or:
            write("||");
            break;
        case Op::And:
            write("&&");
            break;
        case Op::Equal:
            write("==");
            break;
        case Op::NotEqual:
            write("!=");
            break;
        case Op::Less:
            write("<");
            break;
        case Op::LessEqual:
            write("<=");
            break;
        case Op::Greater:
            write(">");
            break;
        case Op::GreaterEqual:
            write(">=");
            break;
        case Op::Add:
            write("+");
            break;
        case Op::Subtract:
            write("-");
            break;
        case Op::Multiply:
            write("*");
            break;
        case Op::Divide:
            write("/");
            break;
        case Op::Modulo:
            write("%");
            break;
        }
    }

    void format_temporal_expr(const ahfl::ast::TemporalExprSyntax &expr) {
        std::visit(
            Overloaded{
                [&](const ahfl::ast::EmbeddedTemporalExpr &e) {
                    if (e.expr) {
                        format_expr(*e.expr);
                    }
                },
                [&](const ahfl::ast::CalledTemporalExpr &e) { write("called(" + e.name + ")"); },
                [&](const ahfl::ast::InStateTemporalExpr &e) { write("in_state(" + e.name + ")"); },
                [&](const ahfl::ast::RunningTemporalExpr &e) { write("running(" + e.name + ")"); },
                [&](const ahfl::ast::CompletedTemporalExpr &e) {
                    write("completed(" + e.name);
                    if (e.state_name) {
                        out_ << ", " << *e.state_name;
                    }
                    write(")");
                },
                [&](const ahfl::ast::UnaryTemporalExpr &e) {
                    format_temporal_unary_op(e.op);
                    write(" ");
                    if (e.operand) {
                        format_temporal_expr(*e.operand);
                    }
                },
                [&](const ahfl::ast::BinaryTemporalExpr &e) {
                    if (e.lhs) {
                        format_temporal_expr(*e.lhs);
                    }
                    out_ << " ";
                    format_temporal_binary_op(e.op);
                    out_ << " ";
                    if (e.rhs) {
                        format_temporal_expr(*e.rhs);
                    }
                },
            },
            expr.node);
    }

    void format_temporal_unary_op(ahfl::ast::TemporalUnaryOp op) {
        using Op = ahfl::ast::TemporalUnaryOp;
        switch (op) {
        case Op::Always:
            write("always");
            break;
        case Op::Eventually:
            write("eventually");
            break;
        case Op::Next:
            write("next");
            break;
        case Op::Not:
            write("not");
            break;
        }
    }

    void format_temporal_binary_op(ahfl::ast::TemporalBinaryOp op) {
        using Op = ahfl::ast::TemporalBinaryOp;
        switch (op) {
        case Op::Implies:
            write("implies");
            break;
        case Op::Or:
            write("||");
            break;
        case Op::And:
            write("&&");
            break;
        case Op::Until:
            write("until");
            break;
        }
    }

    void write(const std::string &text) {
        out_ << text;
    }
    void newline() {
        out_ << "\n";
    }

    const FormatOptions &opts_;
    std::ostringstream out_;
    int indent_ = 0;
};

} // namespace

FormatResult format_source(const std::string &source, const FormatOptions &options) {
    FormatResult result;
    if (source.empty()) {
        result.success = true;
        result.formatted = options.trailing_newline ? "\n" : "";
        return result;
    }

    // Try AST-aware formatting
    ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text("format_input", source);

    if (!parse_result.has_errors() && parse_result.program) {
        AstFormatter formatter(options);
        result.formatted = formatter.format(*parse_result.program);
        if (options.trailing_newline && !result.formatted.empty() &&
            result.formatted.back() != '\n') {
            result.formatted += "\n";
        }
        result.success = true;

        // Count changed lines
        auto diffs = compute_diff(source, result.formatted);
        result.lines_changed = static_cast<int>(diffs.size());
        return result;
    }

    // Fallback to brace-counting for unparseable input
    return format_source_fallback(source, options);
}

bool check_formatting(const std::string &source, const FormatOptions &options) {
    auto result = format_source(source, options);
    return result.success && result.formatted == source;
}

std::vector<FormatDiff> compute_diff(const std::string &original, const std::string &formatted) {
    std::vector<FormatDiff> diffs;
    std::istringstream orig_stream(original);
    std::istringstream fmt_stream(formatted);
    std::string orig_line, fmt_line;
    int line_num = 1;

    while (true) {
        bool have_orig = static_cast<bool>(std::getline(orig_stream, orig_line));
        bool have_fmt = static_cast<bool>(std::getline(fmt_stream, fmt_line));

        if (!have_orig && !have_fmt)
            break;

        std::string o = have_orig ? orig_line : "";
        std::string f = have_fmt ? fmt_line : "";

        if (o != f) {
            diffs.push_back({line_num, std::move(o), std::move(f)});
        }
        ++line_num;
    }

    return diffs;
}

} // namespace ahfl::formatter
