#include "tooling/lsp/semantic_tokens.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "tooling/lsp/analysis_service.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::lsp {

namespace {

// ---------------------------------------------------------------------------
// Position helpers
// ---------------------------------------------------------------------------

[[nodiscard]] Position to_lsp_position(const SourceFile &source, std::size_t offset) {
    const auto pos = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(pos.line > 0 ? pos.line - 1 : 0),
        .character = static_cast<std::uint32_t>(pos.column > 0 ? pos.column - 1 : 0),
    };
}

[[nodiscard]] bool is_identifier_char(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

/// Find the exact range of an identifier within a larger source range.
/// Returns an empty range if the name cannot be found.
[[nodiscard]] SourceRange
find_name_in_range(const SourceFile &source, SourceRange outer_range, std::string_view name) {
    if (name.empty() || outer_range.empty()) {
        return {};
    }

    const auto &content = source.content;
    const auto begin = std::min(outer_range.begin_offset, content.size());
    const auto end = std::max(begin, std::min(outer_range.end_offset, content.size()));

    std::size_t cursor = begin;
    while (cursor < end) {
        const auto found = content.find(name, cursor);
        if (found == std::string::npos || found + name.size() > end) {
            return {};
        }

        const auto before_ok = found == 0 || !is_identifier_char(content[found - 1]);
        const auto after = found + name.size();
        const auto after_ok = after >= content.size() || !is_identifier_char(content[after]);
        if (before_ok && after_ok) {
            return SourceRange{
                .begin_offset = found,
                .end_offset = after,
            };
        }

        cursor = found + 1;
    }

    return {};
}

[[nodiscard]] SourceRange find_name_in_range_from(const SourceFile &source,
                                                  SourceRange outer_range,
                                                  std::string_view name,
                                                  std::size_t &cursor) {
    if (name.empty() || outer_range.empty()) {
        return {};
    }

    const auto &content = source.content;
    const auto begin = std::min(outer_range.begin_offset, content.size());
    const auto end = std::max(begin, std::min(outer_range.end_offset, content.size()));
    cursor = std::max(cursor, begin);

    while (cursor < end) {
        const auto found = content.find(name, cursor);
        if (found == std::string::npos || found + name.size() > end) {
            cursor = end;
            return {};
        }

        const auto before_ok = found == 0 || !is_identifier_char(content[found - 1]);
        const auto after = found + name.size();
        const auto after_ok = after >= content.size() || !is_identifier_char(content[after]);
        cursor = found + name.size();
        if (before_ok && after_ok) {
            return SourceRange{
                .begin_offset = found,
                .end_offset = after,
            };
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// Token entry (absolute position, pre-sort)
// ---------------------------------------------------------------------------

struct TokenEntry {
    std::uint32_t line;
    std::uint32_t start;
    std::uint32_t length;
    SemanticTokenType token_type;
    std::uint32_t modifiers;
};

[[nodiscard]] bool compare_tokens(const TokenEntry &a, const TokenEntry &b) noexcept {
    if (a.line != b.line) {
        return a.line < b.line;
    }
    return a.start < b.start;
}

// ---------------------------------------------------------------------------
// AST traversal — token collection
// ---------------------------------------------------------------------------

void add_token_from_range(std::vector<TokenEntry> &tokens,
                          const SourceFile &source,
                          SourceRange range,
                          SemanticTokenType type,
                          std::uint32_t modifiers = 0) {
    if (range.empty()) {
        return;
    }
    const auto start_pos = to_lsp_position(source, range.begin_offset);
    const auto end_pos = to_lsp_position(source, range.end_offset);
    const auto length =
        start_pos.line == end_pos.line ? end_pos.character - start_pos.character : 0;
    if (length == 0) {
        return;
    }
    tokens.push_back(TokenEntry{
        .line = start_pos.line,
        .start = start_pos.character,
        .length = length,
        .token_type = type,
        .modifiers = modifiers,
    });
}

void add_token_for_name(std::vector<TokenEntry> &tokens,
                        const SourceFile &source,
                        SourceRange decl_range,
                        std::string_view name,
                        SemanticTokenType type,
                        std::uint32_t modifiers = 0) {
    const auto name_range = find_name_in_range(source, decl_range, name);
    add_token_from_range(tokens, source, name_range, type, modifiers);
}

void add_token_for_name_in_order(std::vector<TokenEntry> &tokens,
                                 const SourceFile &source,
                                 SourceRange range,
                                 std::size_t &cursor,
                                 std::string_view name,
                                 SemanticTokenType type,
                                 std::uint32_t modifiers = 0) {
    const auto name_range = find_name_in_range_from(source, range, name, cursor);
    add_token_from_range(tokens, source, name_range, type, modifiers);
}

// Forward declarations
void collect_type_syntax_tokens(const ast::TypeSyntax &type,
                                const SourceFile &source,
                                std::vector<TokenEntry> &tokens);
void collect_type_param_tokens(const ast::TypeParamSyntax &param,
                               const SourceFile &source,
                               std::vector<TokenEntry> &tokens);
void collect_where_clause_tokens(const ast::WhereClauseSyntax &where,
                                 const SourceFile &source,
                                 std::vector<TokenEntry> &tokens);
void collect_effect_clause_tokens(const ast::EffectClauseSyntax &effect,
                                  const SourceFile &source,
                                  std::vector<TokenEntry> &tokens);
void collect_pattern_tokens(const ast::PatternSyntax &pattern,
                            const SourceFile &source,
                            std::vector<TokenEntry> &tokens);
void collect_temporal_tokens(const ast::TemporalExprSyntax &expr,
                             const SourceFile &source,
                             std::vector<TokenEntry> &tokens);
void collect_expr_tokens(const ast::ExprSyntax &expr,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens);
void collect_statement_tokens(const ast::StatementSyntax &stmt,
                              const SourceFile &source,
                              std::vector<TokenEntry> &tokens);
void collect_block_tokens(const ast::BlockSyntax &block,
                          const SourceFile &source,
                          std::vector<TokenEntry> &tokens);

// ---- QualifiedName ----

void collect_qualified_name_tokens(const ast::QualifiedName &qname,
                                   const SourceFile &source,
                                   std::vector<TokenEntry> &tokens,
                                   SemanticTokenType type) {
    // Emit a token for the full qualified name.  Semantic token consumers
    // typically highlight the entire identifier span as one token.
    add_token_from_range(tokens, source, qname.range, type);
}

void collect_path_tokens(const ast::PathSyntax &path,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens) {
    std::size_t cursor = path.range.begin_offset;
    add_token_for_name_in_order(
        tokens, source, path.range, cursor, path.root_name, SemanticTokenType::Variable);
    for (const auto &member : path.members) {
        add_token_for_name_in_order(
            tokens, source, path.range, cursor, member, SemanticTokenType::Property);
    }
}

void collect_integer_tokens(const ast::IntegerSyntax &integer,
                            const SourceFile &source,
                            std::vector<TokenEntry> &tokens) {
    add_token_from_range(tokens, source, integer.range, SemanticTokenType::Number);
}

void collect_duration_tokens(const ast::DurationSyntax &duration,
                             const SourceFile &source,
                             std::vector<TokenEntry> &tokens) {
    add_token_from_range(tokens, source, duration.range, SemanticTokenType::Number);
}

void collect_type_list_tokens(const std::vector<Owned<ast::TypeSyntax>> &types,
                              const SourceFile &source,
                              std::vector<TokenEntry> &tokens) {
    for (const auto &type : types) {
        if (type != nullptr) {
            collect_type_syntax_tokens(*type, source, tokens);
        }
    }
}

void collect_param_list_tokens(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                               const SourceFile &source,
                               std::vector<TokenEntry> &tokens) {
    for (const auto &param : params) {
        if (param != nullptr) {
            add_token_for_name(
                tokens, source, param->range, param->name, SemanticTokenType::Parameter);
            if (param->is_self_mut) {
                add_token_for_name(
                    tokens, source, param->range, "mut", SemanticTokenType::Modifier);
            }
            if (param->type != nullptr) {
                collect_type_syntax_tokens(*param->type, source, tokens);
            }
        }
    }
}

// ---- TypeSyntax ----

void collect_primitive_type_token(const ast::TypeSyntax &type,
                                  const SourceFile &source,
                                  std::vector<TokenEntry> &tokens,
                                  std::string_view spelling) {
    add_token_for_name(tokens, source, type.range, spelling, SemanticTokenType::Type);
}

void collect_type_syntax_tokens(const ast::TypeSyntax &type,
                                const SourceFile &source,
                                std::vector<TokenEntry> &tokens) {
    std::visit(
        Overloaded{
            [&](const ast::NamedType &t) {
                if (t.name != nullptr) {
                    collect_qualified_name_tokens(*t.name, source, tokens, SemanticTokenType::Type);
                }
                for (const auto &arg : t.type_args) {
                    if (arg != nullptr) {
                        collect_type_syntax_tokens(*arg, source, tokens);
                    }
                }
            },
            [&](const ast::FnType &t) {
                add_token_for_name(tokens, source, type.range, "Fn", SemanticTokenType::Type);
                for (const auto &param : t.params) {
                    if (param != nullptr) {
                        collect_type_syntax_tokens(*param, source, tokens);
                    }
                }
                if (t.return_type != nullptr) {
                    collect_type_syntax_tokens(*t.return_type, source, tokens);
                }
                if (t.has_effect_clause) {
                    if (t.effect_kind == ast::EffectClauseKind::Pure) {
                        add_token_for_name(
                            tokens, source, type.range, "Pure", SemanticTokenType::Modifier);
                    } else if (t.effect_kind == ast::EffectClauseKind::Nondet) {
                        add_token_for_name(
                            tokens, source, type.range, "Nondet", SemanticTokenType::Modifier);
                    } else {
                        for (const auto &capability : t.effect_capabilities) {
                            if (capability != nullptr) {
                                collect_qualified_name_tokens(
                                    *capability, source, tokens, SemanticTokenType::Interface);
                            }
                        }
                    }
                }
            },
            [&](const ast::AppType &t) {
                if (t.name != nullptr) {
                    collect_qualified_name_tokens(*t.name, source, tokens, SemanticTokenType::Type);
                }
                for (const auto &arg : t.arguments) {
                    if (arg != nullptr) {
                        collect_type_syntax_tokens(*arg, source, tokens);
                    }
                }
            },
            [&](const ast::UnitType &) {
                collect_primitive_type_token(type, source, tokens, "Unit");
            },
            [&](const ast::BoolType &) {
                collect_primitive_type_token(type, source, tokens, "Bool");
            },
            [&](const ast::IntType &) {
                collect_primitive_type_token(type, source, tokens, "Int");
            },
            [&](const ast::BoundedIntType &) {
                collect_primitive_type_token(type, source, tokens, "Int");
            },
            [&](const ast::FloatType &) {
                collect_primitive_type_token(type, source, tokens, "Float");
            },
            [&](const ast::StringType &) {
                collect_primitive_type_token(type, source, tokens, "String");
            },
            [&](const ast::BoundedStringType &) {
                collect_primitive_type_token(type, source, tokens, "String");
            },
            [&](const ast::UuidType &) {
                collect_primitive_type_token(type, source, tokens, "UUID");
            },
            [&](const ast::TimestampType &) {
                collect_primitive_type_token(type, source, tokens, "Timestamp");
            },
            [&](const ast::DurationType &) {
                collect_primitive_type_token(type, source, tokens, "Duration");
            },
            [&](const ast::DecimalType &) {
                collect_primitive_type_token(type, source, tokens, "Decimal");
            },
        },
        type.node);
}

void collect_type_param_tokens(const ast::TypeParamSyntax &param,
                               const SourceFile &source,
                               std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens, source, param.range, param.name, SemanticTokenType::TypeParameter);
    collect_type_list_tokens(param.bounds, source, tokens);
}

void collect_type_params_tokens(const std::vector<Owned<ast::TypeParamSyntax>> &params,
                                const SourceFile &source,
                                std::vector<TokenEntry> &tokens) {
    for (const auto &param : params) {
        if (param != nullptr) {
            collect_type_param_tokens(*param, source, tokens);
        }
    }
}

void collect_where_clause_tokens(const ast::WhereClauseSyntax &where,
                                 const SourceFile &source,
                                 std::vector<TokenEntry> &tokens) {
    for (const auto &constraint : where.constraints) {
        if (constraint == nullptr) {
            continue;
        }
        if (constraint->subject != nullptr) {
            collect_type_syntax_tokens(*constraint->subject, source, tokens);
        }
        if (constraint->is_predicate) {
            add_token_for_name(tokens,
                               source,
                               constraint->range,
                               constraint->trait_name,
                               SemanticTokenType::Interface);
            collect_type_list_tokens(constraint->arguments, source, tokens);
        } else {
            collect_type_list_tokens(constraint->bounds, source, tokens);
        }
    }
}

void collect_effect_clause_tokens(const ast::EffectClauseSyntax &effect,
                                  const SourceFile &source,
                                  std::vector<TokenEntry> &tokens) {
    if (effect.kind == ast::EffectClauseKind::Pure) {
        add_token_for_name(tokens, source, effect.range, "Pure", SemanticTokenType::Modifier);
    } else if (effect.kind == ast::EffectClauseKind::Nondet) {
        add_token_for_name(tokens, source, effect.range, "Nondet", SemanticTokenType::Modifier);
    } else {
        for (const auto &capability : effect.capabilities) {
            if (capability != nullptr) {
                collect_qualified_name_tokens(
                    *capability, source, tokens, SemanticTokenType::Interface);
            }
        }
    }
    if (effect.decreases_expr != nullptr) {
        collect_expr_tokens(*effect.decreases_expr, source, tokens);
    }
}

// ---- Struct fields ----

void collect_struct_field_tokens(const ast::StructFieldDeclSyntax &field,
                                 const SourceFile &source,
                                 std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens, source, field.range, field.name, SemanticTokenType::Property);
    if (field.type != nullptr) {
        collect_type_syntax_tokens(*field.type, source, tokens);
    }
    if (field.default_value != nullptr) {
        collect_expr_tokens(*field.default_value, source, tokens);
    }
}

// ---- Enum variants ----

void collect_enum_variant_tokens(const ast::EnumVariantDeclSyntax &variant,
                                 const SourceFile &source,
                                 std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens, source, variant.range, variant.name, SemanticTokenType::EnumMember);
    collect_type_list_tokens(variant.payload, source, tokens);
    for (const auto &field : variant.named_fields) {
        if (field == nullptr) {
            continue;
        }
        add_token_for_name(tokens, source, field->range, field->name, SemanticTokenType::Property);
        if (field->type != nullptr) {
            collect_type_syntax_tokens(*field->type, source, tokens);
        }
        if (field->default_value != nullptr) {
            collect_expr_tokens(*field->default_value, source, tokens);
        }
    }
}

void collect_pattern_tokens(const ast::PatternSyntax &pattern,
                            const SourceFile &source,
                            std::vector<TokenEntry> &tokens) {
    std::visit(
        Overloaded{
            [&](const ast::LiteralPattern &p) {
                if (p.spelling == "true" || p.spelling == "false" || p.spelling == "none") {
                    add_token_from_range(tokens, source, pattern.range, SemanticTokenType::Keyword);
                } else if (!p.spelling.empty() && p.spelling.front() == '"') {
                    add_token_from_range(tokens, source, pattern.range, SemanticTokenType::String);
                } else {
                    add_token_from_range(tokens, source, pattern.range, SemanticTokenType::Number);
                }
            },
            [&](const ast::IntRangePattern &p) {
                add_token_from_range(tokens, source, p.start_range, SemanticTokenType::Number);
                add_token_from_range(tokens, source, p.end_range, SemanticTokenType::Number);
            },
            [&](const ast::VariantPattern &p) {
                if (p.path != nullptr) {
                    collect_qualified_name_tokens(
                        *p.path, source, tokens, SemanticTokenType::EnumMember);
                }
                for (const auto &subpattern : p.subpatterns) {
                    if (subpattern != nullptr) {
                        collect_pattern_tokens(*subpattern, source, tokens);
                    }
                }
                for (const auto &field : p.fields) {
                    if (field == nullptr) {
                        continue;
                    }
                    if (!field->is_rest) {
                        add_token_for_name(
                            tokens, source, field->range, field->name, SemanticTokenType::Property);
                    }
                    if (field->pattern != nullptr) {
                        collect_pattern_tokens(*field->pattern, source, tokens);
                    }
                }
            },
            [&](const ast::WildcardPattern &) {
                add_token_for_name(tokens, source, pattern.range, "_", SemanticTokenType::Variable);
            },
            [&](const ast::BindingPattern &p) {
                if (p.is_mut) {
                    add_token_for_name(
                        tokens, source, pattern.range, "mut", SemanticTokenType::Modifier);
                }
                add_token_for_name(
                    tokens, source, pattern.range, p.name, SemanticTokenType::Variable);
                if (p.nested != nullptr) {
                    collect_pattern_tokens(*p.nested, source, tokens);
                }
            },
            [&](const ast::TuplePattern &p) {
                for (const auto &element : p.elements) {
                    if (element != nullptr) {
                        collect_pattern_tokens(*element, source, tokens);
                    }
                }
            },
            [&](const ast::OrPattern &p) {
                for (const auto &branch : p.branches) {
                    if (branch != nullptr) {
                        collect_pattern_tokens(*branch, source, tokens);
                    }
                }
            },
        },
        pattern.node);
}

void collect_temporal_tokens(const ast::TemporalExprSyntax &expr,
                             const SourceFile &source,
                             std::vector<TokenEntry> &tokens) {
    std::visit(
        Overloaded{
            [&](const ast::EmbeddedTemporalExpr &e) {
                if (e.expr != nullptr) {
                    collect_expr_tokens(*e.expr, source, tokens);
                }
            },
            [&](const ast::CalledTemporalExpr &e) {
                add_token_for_name(
                    tokens, source, expr.range, e.name, SemanticTokenType::Interface);
            },
            [&](const ast::InStateTemporalExpr &e) {
                add_token_for_name(tokens, source, expr.range, e.name, SemanticTokenType::Variable);
            },
            [&](const ast::RunningTemporalExpr &e) {
                add_token_for_name(tokens, source, expr.range, e.name, SemanticTokenType::Variable);
            },
            [&](const ast::CompletedTemporalExpr &e) {
                std::size_t cursor = expr.range.begin_offset;
                add_token_for_name_in_order(
                    tokens, source, expr.range, cursor, e.name, SemanticTokenType::Variable);
                if (e.state_name.has_value()) {
                    add_token_for_name_in_order(tokens,
                                                source,
                                                expr.range,
                                                cursor,
                                                *e.state_name,
                                                SemanticTokenType::Variable);
                }
            },
            [&](const ast::UnaryTemporalExpr &e) {
                const auto op = [&] {
                    switch (e.op) {
                    case ast::TemporalUnaryOp::Always:
                        return std::string_view{"always"};
                    case ast::TemporalUnaryOp::Eventually:
                        return std::string_view{"eventually"};
                    case ast::TemporalUnaryOp::Next:
                        return std::string_view{"next"};
                    case ast::TemporalUnaryOp::Not:
                        return std::string_view{"not"};
                    }
                    return std::string_view{};
                }();
                add_token_for_name(tokens, source, expr.range, op, SemanticTokenType::Keyword);
                if (e.operand != nullptr) {
                    collect_temporal_tokens(*e.operand, source, tokens);
                }
            },
            [&](const ast::BinaryTemporalExpr &e) {
                if (e.op == ast::TemporalBinaryOp::Until) {
                    add_token_for_name(
                        tokens, source, expr.range, "until", SemanticTokenType::Keyword);
                }
                if (e.lhs != nullptr) {
                    collect_temporal_tokens(*e.lhs, source, tokens);
                }
                if (e.rhs != nullptr) {
                    collect_temporal_tokens(*e.rhs, source, tokens);
                }
            },
        },
        expr.node);
}

// ---- Expressions ----

void collect_expr_tokens(const ast::ExprSyntax &expr,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens) {
    std::visit(
        Overloaded{
            [&](const ast::BoolLiteralExpr &) {
                add_token_from_range(tokens, source, expr.range, SemanticTokenType::Keyword);
            },
            [&](const ast::IntegerLiteralExpr &e) {
                if (e.literal != nullptr) {
                    collect_integer_tokens(*e.literal, source, tokens);
                } else {
                    add_token_from_range(tokens, source, expr.range, SemanticTokenType::Number);
                }
            },
            [&](const ast::FloatLiteralExpr &) {
                add_token_from_range(tokens, source, expr.range, SemanticTokenType::Number);
            },
            [&](const ast::DecimalLiteralExpr &) {
                add_token_from_range(tokens, source, expr.range, SemanticTokenType::Number);
            },
            [&](const ast::StringLiteralExpr &) {
                add_token_from_range(tokens, source, expr.range, SemanticTokenType::String);
            },
            [&](const ast::DurationLiteralExpr &e) {
                if (e.literal != nullptr) {
                    collect_duration_tokens(*e.literal, source, tokens);
                } else {
                    add_token_from_range(tokens, source, expr.range, SemanticTokenType::Number);
                }
            },
            [&](const ast::PathExpr &e) {
                if (e.path != nullptr) {
                    collect_path_tokens(*e.path, source, tokens);
                }
            },
            [&](const ast::QualifiedValueExpr &e) {
                if (e.name != nullptr) {
                    collect_qualified_name_tokens(
                        *e.name, source, tokens, SemanticTokenType::EnumMember);
                }
            },
            [&](const ast::CallExpr &e) {
                // Call target name — could be a capability or predicate
                // Without semantic resolution we mark it as Function; the analysis
                // service could refine this later.
                if (e.callee != nullptr) {
                    collect_qualified_name_tokens(
                        *e.callee, source, tokens, SemanticTokenType::Function);
                }
                for (const auto &type_arg : e.type_args) {
                    if (type_arg != nullptr) {
                        collect_type_syntax_tokens(*type_arg, source, tokens);
                    }
                }
                for (const auto &arg : e.arguments) {
                    if (arg != nullptr) {
                        collect_expr_tokens(*arg, source, tokens);
                    }
                }
            },
            [&](const ast::MethodCallExpr &e) {
                if (e.receiver != nullptr) {
                    collect_expr_tokens(*e.receiver, source, tokens);
                }
                add_token_for_name(tokens, source, expr.range, e.method, SemanticTokenType::Method);
                for (const auto &type_arg : e.type_args) {
                    if (type_arg != nullptr) {
                        collect_type_syntax_tokens(*type_arg, source, tokens);
                    }
                }
                for (const auto &arg : e.arguments) {
                    if (arg != nullptr) {
                        collect_expr_tokens(*arg, source, tokens);
                    }
                }
            },
            [&](const ast::StructLiteralExpr &e) {
                if (e.type_name != nullptr) {
                    collect_qualified_name_tokens(
                        *e.type_name, source, tokens, SemanticTokenType::Type);
                }
                for (const auto &field : e.fields) {
                    if (field != nullptr) {
                        add_token_for_name(tokens,
                                           source,
                                           field->range,
                                           field->field_name,
                                           SemanticTokenType::Property);
                        if (field->value != nullptr) {
                            collect_expr_tokens(*field->value, source, tokens);
                        }
                    }
                }
            },
            [&](const ast::UnaryExpr &e) {
                if (e.operand != nullptr) {
                    collect_expr_tokens(*e.operand, source, tokens);
                }
            },
            [&](const ast::BinaryExpr &e) {
                if (e.lhs != nullptr) {
                    collect_expr_tokens(*e.lhs, source, tokens);
                }
                if (e.rhs != nullptr) {
                    collect_expr_tokens(*e.rhs, source, tokens);
                }
            },
            [&](const ast::MemberAccessExpr &e) {
                if (e.base != nullptr) {
                    collect_expr_tokens(*e.base, source, tokens);
                }
                add_token_for_name(
                    tokens, source, expr.range, e.member, SemanticTokenType::Property);
            },
            [&](const ast::IndexAccessExpr &e) {
                if (e.base != nullptr) {
                    collect_expr_tokens(*e.base, source, tokens);
                }
                if (e.index != nullptr) {
                    collect_expr_tokens(*e.index, source, tokens);
                }
            },
            [&](const ast::GroupExpr &e) {
                if (e.inner != nullptr) {
                    collect_expr_tokens(*e.inner, source, tokens);
                }
            },
            [&](const ast::MatchExpr &e) {
                if (e.scrutinee != nullptr) {
                    collect_expr_tokens(*e.scrutinee, source, tokens);
                }
                for (const auto &arm : e.arms) {
                    if (arm == nullptr) {
                        continue;
                    }
                    if (arm->pattern != nullptr) {
                        collect_pattern_tokens(*arm->pattern, source, tokens);
                    }
                    if (arm->guard != nullptr) {
                        collect_expr_tokens(*arm->guard, source, tokens);
                    }
                    if (arm->body != nullptr) {
                        collect_expr_tokens(*arm->body, source, tokens);
                    }
                }
            },
            [&](const ast::LambdaExpr &e) {
                // P2 (RFC §6): tokenize lambda params (names) and walk
                // the body. Param type annotations are tokenized via
                // collect_type_syntax_tokens.
                //
                // C-4 (Wave-24): explicit capture list entries are
                // outer-variable references bound by name, so they
                // render as VARIABLE tokens anchored to the
                // per-capture SourceRange stored on the AST node.
                for (std::size_t i = 0; i < e.capture_list.size(); ++i) {
                    const auto range =
                        i < e.capture_ranges.size() ? e.capture_ranges[i] : SourceRange{};
                    add_token_for_name(
                        tokens, source, range, e.capture_list[i], SemanticTokenType::Variable);
                }
                for (const auto &param : e.params) {
                    if (param != nullptr) {
                        add_token_for_name(tokens,
                                           source,
                                           param->range,
                                           param->name,
                                           SemanticTokenType::Parameter);
                        if (param->type != nullptr) {
                            collect_type_syntax_tokens(*param->type, source, tokens);
                        }
                    }
                }
                if (e.body != nullptr) {
                    collect_expr_tokens(*e.body, source, tokens);
                }
            },
            [&](const ast::UnwrapExprSyntax &e) {
                if (e.operand != nullptr) {
                    collect_expr_tokens(*e.operand, source, tokens);
                }
            },
        },
        expr.node);
}

// ---- Statements ----

void collect_statement_tokens(const ast::StatementSyntax &stmt,
                              const SourceFile &source,
                              std::vector<TokenEntry> &tokens) {
    switch (stmt.kind) {
    case ast::StatementSyntaxKind::Let:
        if (stmt.let_stmt != nullptr) {
            add_token_for_name(tokens,
                               source,
                               stmt.let_stmt->range,
                               stmt.let_stmt->name,
                               SemanticTokenType::Variable);
            if (stmt.let_stmt->type != nullptr) {
                collect_type_syntax_tokens(*stmt.let_stmt->type, source, tokens);
            }
            if (stmt.let_stmt->initializer != nullptr) {
                collect_expr_tokens(*stmt.let_stmt->initializer, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::Assign:
        if (stmt.assign_stmt != nullptr) {
            if (stmt.assign_stmt->target != nullptr) {
                collect_path_tokens(*stmt.assign_stmt->target, source, tokens);
            }
            if (stmt.assign_stmt->value != nullptr) {
                collect_expr_tokens(*stmt.assign_stmt->value, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::If:
        if (stmt.if_stmt != nullptr) {
            if (stmt.if_stmt->condition != nullptr) {
                collect_expr_tokens(*stmt.if_stmt->condition, source, tokens);
            }
            if (stmt.if_stmt->then_block != nullptr) {
                collect_block_tokens(*stmt.if_stmt->then_block, source, tokens);
            }
            if (stmt.if_stmt->else_block != nullptr) {
                collect_block_tokens(*stmt.if_stmt->else_block, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::IfLet:
        if (stmt.if_let_stmt != nullptr) {
            if (stmt.if_let_stmt->pattern != nullptr) {
                collect_pattern_tokens(*stmt.if_let_stmt->pattern, source, tokens);
            }
            if (stmt.if_let_stmt->scrutinee != nullptr) {
                collect_expr_tokens(*stmt.if_let_stmt->scrutinee, source, tokens);
            }
            if (stmt.if_let_stmt->then_block != nullptr) {
                collect_block_tokens(*stmt.if_let_stmt->then_block, source, tokens);
            }
            if (stmt.if_let_stmt->else_block != nullptr) {
                collect_block_tokens(*stmt.if_let_stmt->else_block, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::Goto:
        if (stmt.goto_stmt != nullptr) {
            add_token_for_name(tokens,
                               source,
                               stmt.goto_stmt->range,
                               stmt.goto_stmt->target_state,
                               SemanticTokenType::Variable);
        }
        break;
    case ast::StatementSyntaxKind::Return:
        if (stmt.return_stmt != nullptr && stmt.return_stmt->value != nullptr) {
            collect_expr_tokens(*stmt.return_stmt->value, source, tokens);
        }
        break;
    case ast::StatementSyntaxKind::Assert:
        if (stmt.assert_stmt != nullptr) {
            if (stmt.assert_stmt->condition != nullptr) {
                collect_expr_tokens(*stmt.assert_stmt->condition, source, tokens);
            }
            if (stmt.assert_stmt->message != nullptr) {
                collect_expr_tokens(*stmt.assert_stmt->message, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::Unwrap:
        if (stmt.unwrap_stmt != nullptr && stmt.unwrap_stmt->operand != nullptr) {
            collect_expr_tokens(*stmt.unwrap_stmt->operand, source, tokens);
        }
        break;
    case ast::StatementSyntaxKind::Requires:
        if (stmt.requires_stmt != nullptr) {
            if (stmt.requires_stmt->condition != nullptr) {
                collect_expr_tokens(*stmt.requires_stmt->condition, source, tokens);
            }
            if (stmt.requires_stmt->message != nullptr) {
                collect_expr_tokens(*stmt.requires_stmt->message, source, tokens);
            }
        }
        break;
    case ast::StatementSyntaxKind::Unreachable:
        if (stmt.unreachable_stmt != nullptr && stmt.unreachable_stmt->message != nullptr) {
            collect_expr_tokens(*stmt.unreachable_stmt->message, source, tokens);
        }
        break;
    case ast::StatementSyntaxKind::Expr:
        if (stmt.expr_stmt != nullptr && stmt.expr_stmt->expr != nullptr) {
            collect_expr_tokens(*stmt.expr_stmt->expr, source, tokens);
        }
        break;
    }
}

void collect_block_tokens(const ast::BlockSyntax &block,
                          const SourceFile &source,
                          std::vector<TokenEntry> &tokens) {
    for (const auto &stmt : block.statements) {
        if (stmt != nullptr) {
            collect_statement_tokens(*stmt, source, tokens);
        }
    }
}

void collect_fn_tokens(const ast::FnDecl &fn,
                       const SourceFile &source,
                       std::vector<TokenEntry> &tokens,
                       SemanticTokenType name_type) {
    if (fn.builtin_name.has_value()) {
        add_token_for_name(tokens, source, fn.range, "builtin", SemanticTokenType::Decorator);
        add_token_for_name(tokens, source, fn.range, *fn.builtin_name, SemanticTokenType::String);
    }
    add_token_for_name(tokens, source, fn.range, fn.name, name_type);
    collect_type_params_tokens(fn.type_params, source, tokens);
    collect_param_list_tokens(fn.params, source, tokens);
    if (fn.return_type != nullptr) {
        collect_type_syntax_tokens(*fn.return_type, source, tokens);
    }
    if (fn.effect_clause != nullptr) {
        collect_effect_clause_tokens(*fn.effect_clause, source, tokens);
    }
    if (fn.where_clause != nullptr) {
        collect_where_clause_tokens(*fn.where_clause, source, tokens);
    }
    if (fn.body != nullptr) {
        collect_block_tokens(*fn.body, source, tokens);
    }
}

void collect_capability_effect_tokens(const ast::CapabilityEffectSyntax &effect,
                                      const SourceFile &source,
                                      std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens,
                       source,
                       effect.range,
                       std::string(to_string(effect.effect_kind)),
                       SemanticTokenType::Keyword);
    if (effect.domain != nullptr) {
        collect_qualified_name_tokens(*effect.domain, source, tokens, SemanticTokenType::Namespace);
    }
    if (effect.idempotency_key != nullptr) {
        collect_path_tokens(*effect.idempotency_key, source, tokens);
    }
    add_token_for_name(tokens,
                       source,
                       effect.range,
                       std::string(to_string(effect.receipt_mode)),
                       SemanticTokenType::Keyword);
    add_token_for_name(tokens,
                       source,
                       effect.range,
                       std::string(to_string(effect.retry_mode)),
                       SemanticTokenType::Keyword);
    if (effect.timeout != nullptr) {
        collect_duration_tokens(*effect.timeout, source, tokens);
    }
    if (effect.compensation != nullptr) {
        collect_qualified_name_tokens(
            *effect.compensation, source, tokens, SemanticTokenType::Function);
    }
    for (const auto &policy : effect.policies) {
        if (policy != nullptr) {
            collect_qualified_name_tokens(*policy, source, tokens, SemanticTokenType::Namespace);
        }
    }
}

void collect_contract_decreases_tokens(const ast::ContractDecreasesSyntax &decreases,
                                       const SourceFile &source,
                                       std::vector<TokenEntry> &tokens) {
    for (const auto &term : decreases.decreases_exprs) {
        if (term != nullptr) {
            collect_expr_tokens(*term, source, tokens);
        }
    }
}

void collect_contract_clause_tokens(const ast::ContractClauseSyntax &clause,
                                    const SourceFile &source,
                                    std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens,
                       source,
                       clause.range,
                       std::string(to_string(clause.kind)),
                       SemanticTokenType::Keyword);
    if (clause.expr != nullptr) {
        collect_expr_tokens(*clause.expr, source, tokens);
    }
    if (clause.temporal_expr != nullptr) {
        collect_temporal_tokens(*clause.temporal_expr, source, tokens);
    }
    if (clause.decreases != nullptr) {
        collect_contract_decreases_tokens(*clause.decreases, source, tokens);
    }
}

void collect_agent_quota_tokens(const ast::AgentQuotaSyntax &quota,
                                const SourceFile &source,
                                std::vector<TokenEntry> &tokens) {
    for (const auto &item : quota.items) {
        if (item == nullptr) {
            continue;
        }
        if (item->integer_value != nullptr) {
            collect_integer_tokens(*item->integer_value, source, tokens);
        }
        if (item->duration_value != nullptr) {
            collect_duration_tokens(*item->duration_value, source, tokens);
        }
    }
}

void collect_state_policy_tokens(const ast::StatePolicySyntax &policy,
                                 const SourceFile &source,
                                 std::vector<TokenEntry> &tokens) {
    for (const auto &item : policy.items) {
        if (item == nullptr) {
            continue;
        }
        if (item->retry_limit != nullptr) {
            collect_integer_tokens(*item->retry_limit, source, tokens);
        }
        for (const auto &retry_on : item->retry_on) {
            if (retry_on != nullptr) {
                collect_qualified_name_tokens(*retry_on, source, tokens, SemanticTokenType::Type);
            }
        }
        if (item->timeout != nullptr) {
            collect_duration_tokens(*item->timeout, source, tokens);
        }
    }
}

void collect_trait_item_tokens(const ast::TraitItemSyntax &item,
                               const SourceFile &source,
                               std::vector<TokenEntry> &tokens) {
    switch (item.kind) {
    case ast::TraitItemKind::Fn:
        add_token_for_name(tokens, source, item.range, item.name, SemanticTokenType::Method);
        collect_type_params_tokens(item.type_params, source, tokens);
        collect_param_list_tokens(item.params, source, tokens);
        if (item.return_type != nullptr) {
            collect_type_syntax_tokens(*item.return_type, source, tokens);
        }
        if (item.effect_clause != nullptr) {
            collect_effect_clause_tokens(*item.effect_clause, source, tokens);
        }
        if (item.where_clause != nullptr) {
            collect_where_clause_tokens(*item.where_clause, source, tokens);
        }
        break;
    case ast::TraitItemKind::AssocType:
        if (item.assoc_type != nullptr) {
            add_token_for_name(tokens,
                               source,
                               item.assoc_type->range,
                               item.assoc_type->name,
                               SemanticTokenType::Type);
            collect_type_params_tokens(item.assoc_type->type_params, source, tokens);
            collect_type_list_tokens(item.assoc_type->bounds, source, tokens);
            if (item.assoc_type->default_type != nullptr) {
                collect_type_syntax_tokens(*item.assoc_type->default_type, source, tokens);
            }
        }
        break;
    case ast::TraitItemKind::AssocConst:
        if (item.assoc_const != nullptr) {
            const auto readonly_mod = static_cast<std::uint32_t>(SemanticTokenModifier::Readonly);
            add_token_for_name(tokens,
                               source,
                               item.assoc_const->range,
                               item.assoc_const->name,
                               SemanticTokenType::Variable,
                               readonly_mod);
            if (item.assoc_const->type != nullptr) {
                collect_type_syntax_tokens(*item.assoc_const->type, source, tokens);
            }
            if (item.assoc_const->default_value != nullptr) {
                collect_expr_tokens(*item.assoc_const->default_value, source, tokens);
            }
        }
        break;
    }
}

void collect_impl_item_tokens(const ast::ImplItemSyntax &item,
                              const SourceFile &source,
                              std::vector<TokenEntry> &tokens) {
    switch (item.kind) {
    case ast::ImplItemKind::Fn:
        if (item.fn_def != nullptr) {
            collect_fn_tokens(*item.fn_def, source, tokens, SemanticTokenType::Method);
        }
        break;
    case ast::ImplItemKind::AssocType:
        if (item.assoc_type != nullptr) {
            add_token_for_name(tokens,
                               source,
                               item.assoc_type->range,
                               item.assoc_type->name,
                               SemanticTokenType::Type);
            if (item.assoc_type->type != nullptr) {
                collect_type_syntax_tokens(*item.assoc_type->type, source, tokens);
            }
        }
        break;
    case ast::ImplItemKind::AssocConst:
        if (item.assoc_const != nullptr) {
            const auto readonly_mod = static_cast<std::uint32_t>(SemanticTokenModifier::Readonly);
            add_token_for_name(tokens,
                               source,
                               item.assoc_const->range,
                               item.assoc_const->name,
                               SemanticTokenType::Variable,
                               readonly_mod);
            if (item.assoc_const->type != nullptr) {
                collect_type_syntax_tokens(*item.assoc_const->type, source, tokens);
            }
            if (item.assoc_const->value != nullptr) {
                collect_expr_tokens(*item.assoc_const->value, source, tokens);
            }
        }
        break;
    }
}

// ---- Declarations ----

void collect_decl_tokens(const ast::Decl &decl,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens) {
    switch (decl.kind) {
    case ast::NodeKind::ModuleDecl: {
        const auto &mod = static_cast<const ast::ModuleDecl &>(decl);
        if (mod.name != nullptr) {
            collect_qualified_name_tokens(*mod.name, source, tokens, SemanticTokenType::Namespace);
        }
        break;
    }
    case ast::NodeKind::ImportDecl: {
        const auto &imp = static_cast<const ast::ImportDecl &>(decl);
        if (imp.path != nullptr) {
            collect_qualified_name_tokens(*imp.path, source, tokens, SemanticTokenType::Namespace);
        }
        if (!imp.alias.empty()) {
            add_token_for_name(tokens, source, imp.range, imp.alias, SemanticTokenType::Namespace);
        }
        break;
    }
    case ast::NodeKind::UseDecl: {
        const auto &use = static_cast<const ast::UseDecl &>(decl);
        if (use.path != nullptr) {
            collect_qualified_name_tokens(*use.path, source, tokens, SemanticTokenType::Namespace);
        }
        if (!use.alias.empty()) {
            add_token_for_name(tokens, source, use.range, use.alias, SemanticTokenType::Namespace);
        }
        break;
    }
    case ast::NodeKind::ConstDecl: {
        const auto &con = static_cast<const ast::ConstDecl &>(decl);
        const auto readonly_mod = static_cast<std::uint32_t>(SemanticTokenModifier::Readonly);
        add_token_for_name(
            tokens, source, con.range, con.name, SemanticTokenType::Variable, readonly_mod);
        if (con.type != nullptr) {
            collect_type_syntax_tokens(*con.type, source, tokens);
        }
        if (con.value != nullptr) {
            collect_expr_tokens(*con.value, source, tokens);
        }
        break;
    }
    case ast::NodeKind::TypeAliasDecl: {
        const auto &alias = static_cast<const ast::TypeAliasDecl &>(decl);
        add_token_for_name(tokens, source, alias.range, alias.name, SemanticTokenType::Type);
        if (alias.aliased_type != nullptr) {
            collect_type_syntax_tokens(*alias.aliased_type, source, tokens);
        }
        break;
    }
    case ast::NodeKind::StructDecl: {
        const auto &str = static_cast<const ast::StructDecl &>(decl);
        add_token_for_name(tokens, source, str.range, str.name, SemanticTokenType::Struct);
        collect_type_params_tokens(str.type_params, source, tokens);
        for (const auto &field : str.fields) {
            if (field != nullptr) {
                collect_struct_field_tokens(*field, source, tokens);
            }
        }
        if (str.where_clause != nullptr) {
            collect_where_clause_tokens(*str.where_clause, source, tokens);
        }
        break;
    }
    case ast::NodeKind::EnumDecl: {
        const auto &en = static_cast<const ast::EnumDecl &>(decl);
        add_token_for_name(tokens, source, en.range, en.name, SemanticTokenType::Enum);
        collect_type_params_tokens(en.type_params, source, tokens);
        for (const auto &variant : en.variants) {
            if (variant != nullptr) {
                collect_enum_variant_tokens(*variant, source, tokens);
            }
        }
        if (en.where_clause != nullptr) {
            collect_where_clause_tokens(*en.where_clause, source, tokens);
        }
        break;
    }
    case ast::NodeKind::CapabilityDecl: {
        const auto &cap = static_cast<const ast::CapabilityDecl &>(decl);
        add_token_for_name(tokens, source, cap.range, cap.name, SemanticTokenType::Interface);
        collect_param_list_tokens(cap.params, source, tokens);
        if (cap.return_type != nullptr) {
            collect_type_syntax_tokens(*cap.return_type, source, tokens);
        }
        if (cap.effect != nullptr) {
            collect_capability_effect_tokens(*cap.effect, source, tokens);
        }
        if (cap.where_clause != nullptr) {
            collect_where_clause_tokens(*cap.where_clause, source, tokens);
        }
        break;
    }
    case ast::NodeKind::PredicateDecl: {
        const auto &pred = static_cast<const ast::PredicateDecl &>(decl);
        add_token_for_name(tokens, source, pred.range, pred.name, SemanticTokenType::Function);
        collect_param_list_tokens(pred.params, source, tokens);
        if (pred.effect_clause != nullptr) {
            collect_effect_clause_tokens(*pred.effect_clause, source, tokens);
        }
        break;
    }
    case ast::NodeKind::AgentDecl: {
        const auto &agent = static_cast<const ast::AgentDecl &>(decl);
        add_token_for_name(tokens, source, agent.range, agent.name, SemanticTokenType::Class);
        if (agent.input_type != nullptr) {
            collect_type_syntax_tokens(*agent.input_type, source, tokens);
        }
        if (agent.context_type != nullptr) {
            collect_type_syntax_tokens(*agent.context_type, source, tokens);
        }
        if (agent.output_type != nullptr) {
            collect_type_syntax_tokens(*agent.output_type, source, tokens);
        }
        for (const auto &state : agent.states) {
            add_token_for_name(
                tokens, source, agent.states_range, state, SemanticTokenType::Variable);
        }
        add_token_for_name(tokens,
                           source,
                           agent.initial_state_range,
                           agent.initial_state,
                           SemanticTokenType::Variable);
        for (const auto &state : agent.final_states) {
            add_token_for_name(
                tokens, source, agent.final_states_range, state, SemanticTokenType::Variable);
        }
        for (const auto &capability : agent.capabilities) {
            add_token_for_name(
                tokens, source, agent.capabilities_range, capability, SemanticTokenType::Interface);
        }
        if (agent.quota != nullptr) {
            collect_agent_quota_tokens(*agent.quota, source, tokens);
        }
        for (const auto &transition : agent.transitions) {
            if (transition == nullptr) {
                continue;
            }
            std::size_t cursor = transition->range.begin_offset;
            add_token_for_name_in_order(tokens,
                                        source,
                                        transition->range,
                                        cursor,
                                        transition->from_state,
                                        SemanticTokenType::Variable);
            add_token_for_name_in_order(tokens,
                                        source,
                                        transition->range,
                                        cursor,
                                        transition->to_state,
                                        SemanticTokenType::Variable);
        }
        break;
    }
    case ast::NodeKind::WorkflowDecl: {
        const auto &wf = static_cast<const ast::WorkflowDecl &>(decl);
        add_token_for_name(tokens, source, wf.range, wf.name, SemanticTokenType::Class);
        if (wf.input_type != nullptr) {
            collect_type_syntax_tokens(*wf.input_type, source, tokens);
        }
        if (wf.output_type != nullptr) {
            collect_type_syntax_tokens(*wf.output_type, source, tokens);
        }
        for (const auto &node : wf.nodes) {
            if (node != nullptr) {
                add_token_for_name(
                    tokens, source, node->range, node->name, SemanticTokenType::Variable);
                if (node->target != nullptr) {
                    collect_qualified_name_tokens(
                        *node->target, source, tokens, SemanticTokenType::Class);
                }
                if (node->input != nullptr) {
                    collect_expr_tokens(*node->input, source, tokens);
                }
                for (const auto &dep : node->after) {
                    add_token_for_name(
                        tokens, source, node->range, dep, SemanticTokenType::Variable);
                }
            }
        }
        for (const auto &formula : wf.safety) {
            if (formula != nullptr) {
                collect_temporal_tokens(*formula, source, tokens);
            }
        }
        for (const auto &formula : wf.liveness) {
            if (formula != nullptr) {
                collect_temporal_tokens(*formula, source, tokens);
            }
        }
        if (wf.return_value != nullptr) {
            collect_expr_tokens(*wf.return_value, source, tokens);
        }
        break;
    }
    case ast::NodeKind::ContractDecl: {
        const auto &contract = static_cast<const ast::ContractDecl &>(decl);
        if (contract.target != nullptr) {
            collect_qualified_name_tokens(
                *contract.target, source, tokens, SemanticTokenType::Class);
        }
        for (const auto &clause : contract.clauses) {
            if (clause != nullptr) {
                collect_contract_clause_tokens(*clause, source, tokens);
            }
        }
        break;
    }
    case ast::NodeKind::FlowDecl: {
        const auto &flow = static_cast<const ast::FlowDecl &>(decl);
        if (flow.target != nullptr) {
            collect_qualified_name_tokens(*flow.target, source, tokens, SemanticTokenType::Class);
        }
        for (const auto &handler : flow.state_handlers) {
            if (handler != nullptr) {
                add_token_for_name(tokens,
                                   source,
                                   handler->range,
                                   handler->state_name,
                                   SemanticTokenType::Variable);
                if (handler->policy != nullptr) {
                    collect_state_policy_tokens(*handler->policy, source, tokens);
                }
                if (handler->body != nullptr) {
                    collect_block_tokens(*handler->body, source, tokens);
                }
            }
        }
        break;
    }
    case ast::NodeKind::Program:
        // Not a decl — should not reach here
        break;
    case ast::NodeKind::FnDecl: {
        const auto &fn = static_cast<const ast::FnDecl &>(decl);
        collect_fn_tokens(fn, source, tokens, SemanticTokenType::Function);
        break;
    }
    case ast::NodeKind::TraitDecl: {
        const auto &trait = static_cast<const ast::TraitDecl &>(decl);
        add_token_for_name(tokens, source, trait.range, trait.name, SemanticTokenType::Interface);
        collect_type_params_tokens(trait.type_params, source, tokens);
        collect_type_list_tokens(trait.super_traits, source, tokens);
        if (trait.where_clause != nullptr) {
            collect_where_clause_tokens(*trait.where_clause, source, tokens);
        }
        for (const auto &item : trait.items) {
            if (item != nullptr) {
                collect_trait_item_tokens(*item, source, tokens);
            }
        }
        break;
    }
    case ast::NodeKind::ImplDecl: {
        const auto &impl = static_cast<const ast::ImplDecl &>(decl);
        collect_type_params_tokens(impl.type_params, source, tokens);
        if (impl.trait_ref != nullptr) {
            collect_type_syntax_tokens(*impl.trait_ref, source, tokens);
        }
        if (impl.target_type != nullptr) {
            collect_type_syntax_tokens(*impl.target_type, source, tokens);
        }
        if (impl.where_clause != nullptr) {
            collect_where_clause_tokens(*impl.where_clause, source, tokens);
        }
        for (const auto &item : impl.items) {
            if (item != nullptr) {
                collect_impl_item_tokens(*item, source, tokens);
            }
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Delta encoding
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<SemanticToken> delta_encode_tokens(std::vector<TokenEntry> &tokens) {
    std::sort(tokens.begin(), tokens.end(), compare_tokens);

    std::vector<SemanticToken> result;
    result.reserve(tokens.size());

    std::uint32_t prev_line = 0;
    std::uint32_t prev_start = 0;

    for (const auto &entry : tokens) {
        const std::uint32_t delta_line = entry.line - prev_line;
        const std::uint32_t delta_start = delta_line == 0 ? entry.start - prev_start : entry.start;

        result.push_back(SemanticToken{
            .delta_line = delta_line,
            .delta_start = delta_start,
            .length = entry.length,
            .token_type = static_cast<std::uint32_t>(entry.token_type),
            .token_modifiers = entry.modifiers,
        });

        prev_line = entry.line;
        prev_start = entry.start;
    }

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SemanticTokens compute_semantic_tokens(const std::string &source, const AnalysisService &analysis) {
    SemanticTokens result;

    // snapshot_for_uri performs lazy computation with internal caching.
    // It is not marked const on AnalysisService, but logically this function
    // is a read-only consumer of analysis results.
    auto &mutable_analysis = const_cast<AnalysisService &>(analysis);
    const auto *snapshot = mutable_analysis.snapshot_for_uri(source);
    if (snapshot == nullptr) {
        return result;
    }

    const auto *src_snapshot = snapshot->source_for_uri(source);
    if (src_snapshot == nullptr || src_snapshot->source == nullptr ||
        src_snapshot->program == nullptr) {
        return result;
    }

    const auto &src_file = *src_snapshot->source;
    const auto &program = *src_snapshot->program;

    std::vector<TokenEntry> tokens;

    for (const auto &decl : program.declarations) {
        if (decl != nullptr) {
            collect_decl_tokens(*decl, src_file, tokens);
        }
    }

    result.data = delta_encode_tokens(tokens);

    return result;
}

} // namespace ahfl::lsp
