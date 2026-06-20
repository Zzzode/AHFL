#include "tooling/lsp/semantic_tokens.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/base/support/source.hpp"
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
[[nodiscard]] SourceRange find_name_in_range(const SourceFile &source,
                                             SourceRange outer_range,
                                             std::string_view name) {
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

// Forward declarations
void collect_type_syntax_tokens(const ast::TypeSyntax &type,
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

// ---- TypeSyntax ----

void collect_type_syntax_tokens(const ast::TypeSyntax &type,
                                const SourceFile &source,
                                std::vector<TokenEntry> &tokens) {
    switch (type.kind) {
    case ast::TypeSyntaxKind::Named:
        if (type.name != nullptr) {
            collect_qualified_name_tokens(*type.name, source, tokens, SemanticTokenType::Type);
        }
        break;
    case ast::TypeSyntaxKind::Optional:
        if (type.first != nullptr) {
            collect_type_syntax_tokens(*type.first, source, tokens);
        }
        break;
    case ast::TypeSyntaxKind::List:
    case ast::TypeSyntaxKind::Set:
        if (type.first != nullptr) {
            collect_type_syntax_tokens(*type.first, source, tokens);
        }
        break;
    case ast::TypeSyntaxKind::Map:
        if (type.first != nullptr) {
            collect_type_syntax_tokens(*type.first, source, tokens);
        }
        if (type.second != nullptr) {
            collect_type_syntax_tokens(*type.second, source, tokens);
        }
        break;
    case ast::TypeSyntaxKind::Unit:
    case ast::TypeSyntaxKind::Bool:
    case ast::TypeSyntaxKind::Int:
    case ast::TypeSyntaxKind::Float:
    case ast::TypeSyntaxKind::String:
    case ast::TypeSyntaxKind::BoundedString:
    case ast::TypeSyntaxKind::UUID:
    case ast::TypeSyntaxKind::Timestamp:
    case ast::TypeSyntaxKind::Duration:
    case ast::TypeSyntaxKind::Decimal:
        // Built-in types — no tokens for now (could add Keyword/Type later)
        break;
    }
}

// ---- Parameters ----

void collect_param_tokens(const ast::ParamDeclSyntax &param,
                          const SourceFile &source,
                          std::vector<TokenEntry> &tokens) {
    add_token_for_name(tokens, source, param.range, param.name, SemanticTokenType::Parameter);
    if (param.type != nullptr) {
        collect_type_syntax_tokens(*param.type, source, tokens);
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
    add_token_for_name(
        tokens, source, variant.range, variant.name, SemanticTokenType::EnumMember);
}

// ---- Expressions ----

void collect_expr_tokens(const ast::ExprSyntax &expr,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::Path:
        // Path root identifier — treated as variable reference
        if (expr.path != nullptr) {
            add_token_for_name(tokens, source, expr.path->range, expr.path->root_name,
                               SemanticTokenType::Variable);
        }
        break;
    case ast::ExprSyntaxKind::QualifiedValue:
        if (expr.qualified_name != nullptr) {
            collect_qualified_name_tokens(
                *expr.qualified_name, source, tokens, SemanticTokenType::EnumMember);
        }
        break;
    case ast::ExprSyntaxKind::Call: {
        // Call target name — could be a capability or predicate
        // Without semantic resolution we mark it as Function; the analysis
        // service could refine this later.
        add_token_for_name(tokens, source, expr.range, expr.name, SemanticTokenType::Function);
        for (const auto &arg : expr.items) {
            if (arg != nullptr) {
                collect_expr_tokens(*arg, source, tokens);
            }
        }
        break;
    }
    case ast::ExprSyntaxKind::StructLiteral: {
        add_token_for_name(tokens, source, expr.range, expr.name, SemanticTokenType::Type);
        for (const auto &field : expr.struct_fields) {
            if (field != nullptr) {
                add_token_for_name(tokens, source, field->range, field->field_name,
                                   SemanticTokenType::Property);
                if (field->value != nullptr) {
                    collect_expr_tokens(*field->value, source, tokens);
                }
            }
        }
        break;
    }
    case ast::ExprSyntaxKind::ListLiteral:
    case ast::ExprSyntaxKind::SetLiteral:
        for (const auto &item : expr.items) {
            if (item != nullptr) {
                collect_expr_tokens(*item, source, tokens);
            }
        }
        break;
    case ast::ExprSyntaxKind::MapLiteral:
        for (const auto &entry : expr.map_entries) {
            if (entry != nullptr) {
                if (entry->key != nullptr) {
                    collect_expr_tokens(*entry->key, source, tokens);
                }
                if (entry->value != nullptr) {
                    collect_expr_tokens(*entry->value, source, tokens);
                }
            }
        }
        break;
    case ast::ExprSyntaxKind::Unary:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        break;
    case ast::ExprSyntaxKind::Binary:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        if (expr.second != nullptr) {
            collect_expr_tokens(*expr.second, source, tokens);
        }
        break;
    case ast::ExprSyntaxKind::MemberAccess:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        add_token_for_name(
            tokens, source, expr.range, expr.name, SemanticTokenType::Property);
        break;
    case ast::ExprSyntaxKind::IndexAccess:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        if (expr.second != nullptr) {
            collect_expr_tokens(*expr.second, source, tokens);
        }
        break;
    case ast::ExprSyntaxKind::Group:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        break;
    case ast::ExprSyntaxKind::Some:
        if (expr.first != nullptr) {
            collect_expr_tokens(*expr.first, source, tokens);
        }
        break;
    case ast::ExprSyntaxKind::BoolLiteral:
    case ast::ExprSyntaxKind::IntegerLiteral:
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::StringLiteral:
    case ast::ExprSyntaxKind::DurationLiteral:
    case ast::ExprSyntaxKind::NoneLiteral:
        // Literals — could add Number/String tokens later
        break;
    }
}

// ---- Statements ----

void collect_statement_tokens(const ast::StatementSyntax &stmt,
                              const SourceFile &source,
                              std::vector<TokenEntry> &tokens) {
    switch (stmt.kind) {
    case ast::StatementSyntaxKind::Let:
        if (stmt.let_stmt != nullptr) {
            add_token_for_name(tokens, source, stmt.let_stmt->range, stmt.let_stmt->name,
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
                add_token_for_name(tokens, source, stmt.assign_stmt->target->range,
                                   stmt.assign_stmt->target->root_name,
                                   SemanticTokenType::Variable);
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
    case ast::StatementSyntaxKind::Goto:
        if (stmt.goto_stmt != nullptr) {
            add_token_for_name(tokens, source, stmt.goto_stmt->range,
                               stmt.goto_stmt->target_state, SemanticTokenType::Variable);
        }
        break;
    case ast::StatementSyntaxKind::Return:
        if (stmt.return_stmt != nullptr && stmt.return_stmt->value != nullptr) {
            collect_expr_tokens(*stmt.return_stmt->value, source, tokens);
        }
        break;
    case ast::StatementSyntaxKind::Assert:
        if (stmt.assert_stmt != nullptr && stmt.assert_stmt->condition != nullptr) {
            collect_expr_tokens(*stmt.assert_stmt->condition, source, tokens);
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

// ---- Declarations ----

void collect_decl_tokens(const ast::Decl &decl,
                         const SourceFile &source,
                         std::vector<TokenEntry> &tokens) {
    switch (decl.kind) {
    case ast::NodeKind::ModuleDecl: {
        const auto &mod = static_cast<const ast::ModuleDecl &>(decl);
        if (mod.name != nullptr) {
            collect_qualified_name_tokens(
                *mod.name, source, tokens, SemanticTokenType::Namespace);
        }
        break;
    }
    case ast::NodeKind::ImportDecl: {
        const auto &imp = static_cast<const ast::ImportDecl &>(decl);
        if (imp.path != nullptr) {
            collect_qualified_name_tokens(
                *imp.path, source, tokens, SemanticTokenType::Namespace);
        }
        if (!imp.alias.empty()) {
            add_token_for_name(tokens, source, imp.range, imp.alias,
                               SemanticTokenType::Namespace);
        }
        break;
    }
    case ast::NodeKind::ConstDecl: {
        const auto &con = static_cast<const ast::ConstDecl &>(decl);
        const auto readonly_mod =
            static_cast<std::uint32_t>(SemanticTokenModifier::Readonly);
        add_token_for_name(tokens, source, con.range, con.name, SemanticTokenType::Variable,
                           readonly_mod);
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
        for (const auto &field : str.fields) {
            if (field != nullptr) {
                collect_struct_field_tokens(*field, source, tokens);
            }
        }
        break;
    }
    case ast::NodeKind::EnumDecl: {
        const auto &en = static_cast<const ast::EnumDecl &>(decl);
        add_token_for_name(tokens, source, en.range, en.name, SemanticTokenType::Enum);
        for (const auto &variant : en.variants) {
            if (variant != nullptr) {
                collect_enum_variant_tokens(*variant, source, tokens);
            }
        }
        break;
    }
    case ast::NodeKind::CapabilityDecl: {
        const auto &cap = static_cast<const ast::CapabilityDecl &>(decl);
        add_token_for_name(tokens, source, cap.range, cap.name, SemanticTokenType::Interface);
        for (const auto &param : cap.params) {
            if (param != nullptr) {
                collect_param_tokens(*param, source, tokens);
            }
        }
        if (cap.return_type != nullptr) {
            collect_type_syntax_tokens(*cap.return_type, source, tokens);
        }
        break;
    }
    case ast::NodeKind::PredicateDecl: {
        const auto &pred = static_cast<const ast::PredicateDecl &>(decl);
        add_token_for_name(tokens, source, pred.range, pred.name, SemanticTokenType::Function);
        for (const auto &param : pred.params) {
            if (param != nullptr) {
                collect_param_tokens(*param, source, tokens);
            }
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
                add_token_for_name(tokens, source, node->range, node->name,
                                   SemanticTokenType::Variable);
                if (node->target != nullptr) {
                    collect_qualified_name_tokens(
                        *node->target, source, tokens, SemanticTokenType::Class);
                }
                if (node->input != nullptr) {
                    collect_expr_tokens(*node->input, source, tokens);
                }
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
        break;
    }
    case ast::NodeKind::FlowDecl: {
        const auto &flow = static_cast<const ast::FlowDecl &>(decl);
        if (flow.target != nullptr) {
            collect_qualified_name_tokens(
                *flow.target, source, tokens, SemanticTokenType::Class);
        }
        for (const auto &handler : flow.state_handlers) {
            if (handler != nullptr) {
                add_token_for_name(tokens, source, handler->range, handler->state_name,
                                   SemanticTokenType::Variable);
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
    }
}

// ---------------------------------------------------------------------------
// Delta encoding
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<SemanticToken>
delta_encode_tokens(std::vector<TokenEntry> &tokens) {
    std::sort(tokens.begin(), tokens.end(), compare_tokens);

    std::vector<SemanticToken> result;
    result.reserve(tokens.size());

    std::uint32_t prev_line = 0;
    std::uint32_t prev_start = 0;

    for (const auto &entry : tokens) {
        const std::uint32_t delta_line = entry.line - prev_line;
        const std::uint32_t delta_start =
            delta_line == 0 ? entry.start - prev_start : entry.start;

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

SemanticTokens compute_semantic_tokens(const std::string &source,
                                       const AnalysisService &analysis) {
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
