#include "tooling/formatter/formatter.hpp"
#include "tooling/formatter/lossless_source_formatter.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <sstream>
#include <string_view>
#include <variant>

namespace ahfl::formatter {

namespace {

[[nodiscard]] std::string unary_operator(ast::ExprUnaryOp op) {
    switch (op) {
    case ast::ExprUnaryOp::Not:
        return "!";
    case ast::ExprUnaryOp::Negate:
        return "-";
    case ast::ExprUnaryOp::Positive:
        return "+";
    }
    return {};
}

[[nodiscard]] std::string binary_operator(ast::ExprBinaryOp op) {
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
    return {};
}

void append_type(std::ostringstream &out, const ast::TypeSyntax *type) {
    if (type != nullptr) {
        out << type->spelling();
    }
}

void append_expression(std::ostringstream &out, const ast::ExprSyntax &expr);

void append_arguments(std::ostringstream &out,
                      const std::vector<Owned<ast::ExprSyntax>> &arguments) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        if (arguments[index]) {
            append_expression(out, *arguments[index]);
        } else {
            out << "/* null */";
        }
    }
}

void append_type_arguments(std::ostringstream &out,
                           const std::vector<Owned<ast::TypeSyntax>> &arguments) {
    if (arguments.empty()) {
        return;
    }
    out << "<";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        append_type(out, arguments[index].get());
    }
    out << ">";
}

void append_pattern(std::ostringstream &out, const ast::PatternSyntax &pattern) {
    std::visit(
        Overloaded{
            [&](const ast::LiteralPattern &) { out << pattern.text; },
            [&](const ast::IntRangePattern &value) {
                out << value.start_spelling << ".." << value.end_spelling;
            },
            [&](const ast::VariantPattern &value) {
                if (value.path) {
                    out << value.path->spelling();
                }
                if (value.payload_kind == ast::EnumVariantPayloadKind::Tuple) {
                    out << "(";
                    for (std::size_t index = 0; index < value.subpatterns.size(); ++index) {
                        if (index != 0) {
                            out << ", ";
                        }
                        if (value.subpatterns[index]) {
                            append_pattern(out, *value.subpatterns[index]);
                        }
                    }
                    out << ")";
                } else if (value.payload_kind == ast::EnumVariantPayloadKind::Struct) {
                    out << " { ";
                    for (std::size_t index = 0; index < value.fields.size(); ++index) {
                        if (index != 0) {
                            out << ", ";
                        }
                        const auto &field = value.fields[index];
                        if (!field) {
                            out << "/* null */";
                        } else if (field->is_rest) {
                            out << "..";
                        } else {
                            out << field->name;
                            if (field->pattern) {
                                out << ": ";
                                append_pattern(out, *field->pattern);
                            }
                        }
                    }
                    out << " }";
                }
            },
            [&](const ast::WildcardPattern &) { out << "_"; },
            [&](const ast::BindingPattern &value) {
                out << value.name;
                if (value.nested) {
                    out << " @ ";
                    append_pattern(out, *value.nested);
                }
            },
            [&](const ast::TuplePattern &value) {
                out << "(";
                for (std::size_t index = 0; index < value.elements.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    if (value.elements[index]) {
                        append_pattern(out, *value.elements[index]);
                    }
                }
                out << ")";
            },
            [&](const ast::OrPattern &value) {
                for (std::size_t index = 0; index < value.branches.size(); ++index) {
                    if (index != 0) {
                        out << " | ";
                    }
                    if (value.branches[index]) {
                        append_pattern(out, *value.branches[index]);
                    }
                }
            },
        },
        pattern.node);
}

void append_expression(std::ostringstream &out, const ast::ExprSyntax &expr) {
    std::visit(
        Overloaded{
            [&](const ast::BoolLiteralExpr &value) { out << (value.value ? "true" : "false"); },
            [&](const ast::IntegerLiteralExpr &value) {
                if (value.literal) {
                    out << value.literal->spelling;
                }
            },
            [&](const ast::FloatLiteralExpr &value) { out << value.spelling; },
            [&](const ast::DecimalLiteralExpr &value) { out << value.spelling; },
            [&](const ast::StringLiteralExpr &value) { out << value.spelling; },
            [&](const ast::DurationLiteralExpr &value) {
                if (value.literal) {
                    out << value.literal->spelling;
                }
            },
            [&](const ast::PathExpr &value) {
                if (value.path) {
                    out << value.path->spelling();
                }
            },
            [&](const ast::QualifiedValueExpr &value) {
                if (value.name) {
                    out << value.name->spelling();
                }
            },
            [&](const ast::CallExpr &value) {
                if (value.callee) {
                    out << value.callee->spelling();
                }
                append_type_arguments(out, value.type_args);
                out << "(";
                append_arguments(out, value.arguments);
                out << ")";
            },
            [&](const ast::MethodCallExpr &value) {
                if (value.receiver) {
                    append_expression(out, *value.receiver);
                }
                out << "." << value.method;
                append_type_arguments(out, value.type_args);
                out << "(";
                append_arguments(out, value.arguments);
                out << ")";
            },
            [&](const ast::StructLiteralExpr &value) {
                if (value.type_name) {
                    out << value.type_name->spelling();
                }
                out << " { ";
                for (std::size_t index = 0; index < value.fields.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    const auto &field = value.fields[index];
                    if (!field) {
                        out << "/* null */";
                        continue;
                    }
                    out << field->field_name << ": ";
                    if (field->value) {
                        append_expression(out, *field->value);
                    }
                }
                out << " }";
            },
            [&](const ast::UnaryExpr &value) {
                out << unary_operator(value.op);
                if (value.operand) {
                    append_expression(out, *value.operand);
                }
            },
            [&](const ast::BinaryExpr &value) {
                if (value.lhs) {
                    append_expression(out, *value.lhs);
                }
                out << " " << binary_operator(value.op) << " ";
                if (value.rhs) {
                    append_expression(out, *value.rhs);
                }
            },
            [&](const ast::MemberAccessExpr &value) {
                if (value.base) {
                    append_expression(out, *value.base);
                }
                out << "." << value.member;
            },
            [&](const ast::IndexAccessExpr &value) {
                if (value.base) {
                    append_expression(out, *value.base);
                }
                out << "[";
                if (value.index) {
                    append_expression(out, *value.index);
                }
                out << "]";
            },
            [&](const ast::GroupExpr &value) {
                out << "(";
                if (value.inner) {
                    append_expression(out, *value.inner);
                }
                out << ")";
            },
            [&](const ast::MatchExpr &value) {
                out << "match ";
                if (value.scrutinee) {
                    append_expression(out, *value.scrutinee);
                }
                out << " { ";
                for (std::size_t index = 0; index < value.arms.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    const auto &arm = value.arms[index];
                    if (!arm) {
                        out << "/* null */";
                        continue;
                    }
                    if (arm->pattern) {
                        append_pattern(out, *arm->pattern);
                    }
                    if (arm->guard) {
                        out << " if ";
                        append_expression(out, *arm->guard);
                    }
                    out << " => ";
                    if (arm->body) {
                        append_expression(out, *arm->body);
                    }
                }
                out << " }";
            },
            [&](const ast::LambdaExpr &value) {
                out << "\\";
                if (!value.capture_list.empty()) {
                    out << "[";
                    for (std::size_t index = 0; index < value.capture_list.size(); ++index) {
                        if (index != 0) {
                            out << ", ";
                        }
                        out << value.capture_list[index];
                    }
                    out << "] ";
                }
                for (std::size_t index = 0; index < value.params.size(); ++index) {
                    if (index != 0) {
                        out << ", ";
                    }
                    const auto &param = value.params[index];
                    if (!param) {
                        out << "/* null */";
                        continue;
                    }
                    out << param->name;
                    if (param->type) {
                        out << ": " << param->type->spelling();
                    }
                }
                out << " -> ";
                if (value.body) {
                    append_expression(out, *value.body);
                }
            },
            [&](const ast::UnwrapExprSyntax &value) {
                out << "unwrap(";
                if (value.operand) {
                    append_expression(out, *value.operand);
                }
                out << ")";
            },
        },
        expr.node);
}

} // namespace

FormatResult format_source(const std::string &source, const FormatOptions &options) {
    auto result = format_lossless_source(source, options);
    if (result.success) {
        result.lines_changed = static_cast<int>(compute_diff(source, result.formatted).size());
    }
    return result;
}

bool check_formatting(const std::string &source, const FormatOptions &options) {
    auto result = format_source(source, options);
    return result.success && result.formatted == source;
}

std::vector<FormatDiff> compute_diff(const std::string &original, const std::string &formatted) {
    std::vector<FormatDiff> diffs;
    std::istringstream original_stream(original);
    std::istringstream formatted_stream(formatted);
    std::string original_line;
    std::string formatted_line;
    int line = 1;

    while (true) {
        const bool have_original = static_cast<bool>(std::getline(original_stream, original_line));
        const bool have_formatted =
            static_cast<bool>(std::getline(formatted_stream, formatted_line));
        if (!have_original && !have_formatted) {
            break;
        }
        const std::string original_value = have_original ? original_line : "";
        const std::string formatted_value = have_formatted ? formatted_line : "";
        if (original_value != formatted_value) {
            diffs.push_back(FormatDiff{
                .line = line,
                .original = original_value,
                .formatted = formatted_value,
            });
        }
        ++line;
    }
    return diffs;
}

std::string format_decreases_clause(const ast::DecreasesClauseSyntax &clause) {
    if (clause.is_wildcard) {
        return "decreases *;";
    }

    std::ostringstream out;
    out << "decreases (";
    for (std::size_t index = 0; index < clause.terms.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        if (clause.terms[index]) {
            append_expression(out, *clause.terms[index]);
        } else {
            out << "/* null */";
        }
    }
    out << ");";
    return out.str();
}

} // namespace ahfl::formatter
