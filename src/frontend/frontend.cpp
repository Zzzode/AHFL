#include "ahfl/frontend/frontend.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AHFLLexer.h"
#include "AHFLParser.h"
#include "antlr4-runtime.h"

namespace ahfl {

namespace {

template <typename NodeT> [[nodiscard]] std::string text_of(NodeT &node) {
    return node.getText();
}

template <typename NodeT> [[nodiscard]] std::string text_or_empty(MaybeRef<NodeT> node) {
    return node.has_value() ? node->get().getText() : "";
}

[[nodiscard]] SourceRange
clamp_range(std::size_t begin_offset, std::size_t end_offset, const SourceFile &source) {
    const auto bounded_begin = std::min(begin_offset, source.content.size());
    const auto bounded_end = std::max(bounded_begin, std::min(end_offset, source.content.size()));

    return SourceRange{
        .begin_offset = bounded_begin,
        .end_offset = bounded_end,
    };
}

[[nodiscard]] SourceRange
token_range(const antlr4::Token &start, MaybeCRef<antlr4::Token> stop, const SourceFile &source) {
    const auto begin_offset =
        start.getStartIndex() == INVALID_INDEX ? source.content.size() : start.getStartIndex();

    std::size_t end_offset = begin_offset;
    if (stop.has_value() && stop->get().getStopIndex() != INVALID_INDEX) {
        end_offset = stop->get().getStopIndex() + 1;
    } else if (start.getStopIndex() != INVALID_INDEX) {
        end_offset = start.getStopIndex() + 1;
    }

    return clamp_range(begin_offset, end_offset, source);
}

template <typename ContextT>
[[nodiscard]] SourceRange context_range(ContextT &context, const SourceFile &source) {
    return token_range(require(context.getStart(), "parser context start token is missing"),
                       borrow(context.getStop()),
                       source);
}

[[nodiscard]] SourceRange terminal_range(antlr4::tree::TerminalNode &node,
                                         const SourceFile &source) {
    const auto &symbol = require(node.getSymbol(), "terminal node symbol is missing");
    return token_range(symbol, borrow(node.getSymbol()), source);
}

[[nodiscard]] std::string source_text(const SourceFile &source, SourceRange range) {
    return std::string(source.slice(range));
}

template <typename DeclT, typename ContextT, typename... Args>
[[nodiscard]] Owned<DeclT> make_decl(const SourceFile &source, ContextT &context, Args &&...args) {
    const auto range = context_range(context, source);
    return make_owned<DeclT>(std::forward<Args>(args)..., source_text(source, range), range);
}

[[nodiscard]] std::int64_t parse_integer_literal(std::string_view text) {
    std::int64_t value{0};
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::logic_error("invalid integer literal: " + std::string(text));
    }

    return value;
}

[[nodiscard]] SourceRange span_range(const SourceRange &first, const SourceRange &second) {
    return SourceRange{
        .begin_offset = std::min(first.begin_offset, second.begin_offset),
        .end_offset = std::max(first.end_offset, second.end_offset),
    };
}

[[nodiscard]] ast::ExprUnaryOp expr_unary_op_from(std::string_view op) {
    if (op == "not" || op == "!") {
        return ast::ExprUnaryOp::Not;
    }

    if (op == "-") {
        return ast::ExprUnaryOp::Negate;
    }

    if (op == "+") {
        return ast::ExprUnaryOp::Positive;
    }

    throw std::logic_error("unsupported expression unary operator: " + std::string(op));
}

[[nodiscard]] ast::ExprBinaryOp expr_binary_op_from(std::string_view op) {
    if (op == "=>") {
        return ast::ExprBinaryOp::Implies;
    }

    if (op == "or" || op == "||") {
        return ast::ExprBinaryOp::Or;
    }

    if (op == "and" || op == "&&") {
        return ast::ExprBinaryOp::And;
    }

    if (op == "==") {
        return ast::ExprBinaryOp::Equal;
    }

    if (op == "!=") {
        return ast::ExprBinaryOp::NotEqual;
    }

    if (op == "<") {
        return ast::ExprBinaryOp::Less;
    }

    if (op == "<=") {
        return ast::ExprBinaryOp::LessEqual;
    }

    if (op == ">") {
        return ast::ExprBinaryOp::Greater;
    }

    if (op == ">=") {
        return ast::ExprBinaryOp::GreaterEqual;
    }

    if (op == "+") {
        return ast::ExprBinaryOp::Add;
    }

    if (op == "-") {
        return ast::ExprBinaryOp::Subtract;
    }

    if (op == "*") {
        return ast::ExprBinaryOp::Multiply;
    }

    if (op == "/") {
        return ast::ExprBinaryOp::Divide;
    }

    if (op == "%") {
        return ast::ExprBinaryOp::Modulo;
    }

    throw std::logic_error("unsupported expression binary operator: " + std::string(op));
}

[[nodiscard]] ast::TemporalUnaryOp temporal_unary_op_from(std::string_view op) {
    if (op == "always") {
        return ast::TemporalUnaryOp::Always;
    }

    if (op == "eventually") {
        return ast::TemporalUnaryOp::Eventually;
    }

    if (op == "next") {
        return ast::TemporalUnaryOp::Next;
    }

    if (op == "not" || op == "!") {
        return ast::TemporalUnaryOp::Not;
    }

    throw std::logic_error("unsupported temporal unary operator: " + std::string(op));
}

[[nodiscard]] ast::TemporalBinaryOp temporal_binary_op_from(std::string_view op) {
    if (op == "=>") {
        return ast::TemporalBinaryOp::Implies;
    }

    if (op == "or" || op == "||") {
        return ast::TemporalBinaryOp::Or;
    }

    if (op == "and" || op == "&&") {
        return ast::TemporalBinaryOp::And;
    }

    if (op == "until") {
        return ast::TemporalBinaryOp::Until;
    }

    throw std::logic_error("unsupported temporal binary operator: " + std::string(op));
}

template <typename RecognizerT>
void install_error_listener(RecognizerT &recognizer, antlr4::ANTLRErrorListener &listener) {
    recognizer.removeErrorListeners();
    recognizer.addErrorListener(std::addressof(listener));
}

[[nodiscard]] antlr4::CommonTokenStream make_token_stream(antlr4::TokenSource &source) {
    return antlr4::CommonTokenStream(std::addressof(source));
}

[[nodiscard]] AHFLParser make_parser(antlr4::TokenStream &tokens) {
    return AHFLParser(std::addressof(tokens));
}

struct OrderedDecl final {
    std::size_t begin_offset{0};
    Owned<ast::Decl> declaration;
};

void append_decl(std::vector<OrderedDecl> &declarations, Owned<ast::Decl> declaration) {
    declarations.push_back(OrderedDecl{
        .begin_offset = declaration->range.begin_offset,
        .declaration = std::move(declaration),
    });
}

class DiagnosticErrorListener final : public antlr4::BaseErrorListener {
  public:
    DiagnosticErrorListener(const SourceFile &source, DiagnosticBag &diagnostics)
        : source_(source), diagnostics_(diagnostics) {}

    void syntaxError(antlr4::Recognizer *,
                     antlr4::Token *offending_symbol,
                     size_t line,
                     size_t char_position_in_line,
                     const std::string &message,
                     std::exception_ptr) override {
        if (const auto offending = borrow(offending_symbol)) {
            diagnostics_.error(message, token_range(offending->get(), offending, source_));
            return;
        }

        const auto begin_offset = source_.offset_of(line, char_position_in_line + 1);
        diagnostics_.error(message, clamp_range(begin_offset, begin_offset, source_));
    }

  private:
    const SourceFile &source_;
    DiagnosticBag &diagnostics_;
};

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  SourceFile &source,
                                  DiagnosticBag &diagnostics,
                                  std::string_view kind) {
    source.display_name = display_path(path);

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.error("failed to open " + std::string(kind) + ": " + display_path(path));
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    source.content = buffer.str();
    return true;
}

class ProgramBuilder {
  public:
    explicit ProgramBuilder(const SourceFile &source) : source_(source) {}

    [[nodiscard]] Owned<ast::Program> build(AHFLParser::ProgramContext &context) const {
        auto program =
            make_owned<ast::Program>(source_.display_name, context_range(context, source_));

        const auto module_count = std::ranges::size(context.moduleDecl());
        const auto import_count = std::ranges::size(context.importDecl());
        const auto top_level_count = std::ranges::size(context.topLevelDecl());

        std::vector<OrderedDecl> declarations;
        declarations.reserve(module_count + import_count + top_level_count);

        for (std::size_t index = 0; index < module_count; ++index) {
            append_decl(declarations,
                        build_module_decl(
                            require(context.moduleDecl(index), "module declaration is missing")));
        }

        for (std::size_t index = 0; index < import_count; ++index) {
            append_decl(declarations,
                        build_import_decl(
                            require(context.importDecl(index), "import declaration is missing")));
        }

        for (std::size_t index = 0; index < top_level_count; ++index) {
            append_decl(declarations,
                        build_top_level_decl(require(context.topLevelDecl(index),
                                                     "top-level declaration is missing")));
        }

        std::ranges::sort(declarations, std::less{}, [](const OrderedDecl &declaration) {
            return declaration.begin_offset;
        });

        for (auto &declaration : declarations) {
            program->declarations.push_back(std::move(declaration.declaration));
        }

        return program;
    }

  private:
    const SourceFile &source_;

    // ProgramBuilder is the handwritten parse-tree lowering boundary. It is the
    // only place where later frontend work should routinely traverse generated
    // ANTLR contexts; resolver/checker stages must operate on ahfl::ast nodes.

    [[nodiscard]] Owned<ast::Decl> build_module_decl(AHFLParser::ModuleDeclContext &context) const {
        return make_decl<ast::ModuleDecl>(
            source_,
            context,
            build_qualified_name(require(context.qualifiedIdent(), "module name is missing")));
    }

    [[nodiscard]] Owned<ast::Decl> build_import_decl(AHFLParser::ImportDeclContext &context) const {
        return make_decl<ast::ImportDecl>(
            source_,
            context,
            build_qualified_name(require(context.qualifiedIdent(), "import path is missing")),
            text_or_empty(borrow(context.IDENT())));
    }

    [[nodiscard]] Owned<ast::Decl>
    build_top_level_decl(AHFLParser::TopLevelDeclContext &context) const {
        if (const auto const_decl = borrow(context.constDecl())) {
            auto declaration = make_decl<ast::ConstDecl>(
                source_,
                const_decl->get(),
                text_of(require(const_decl->get().IDENT(), "const name is missing")));
            declaration->type =
                build_type_syntax(require(const_decl->get().type_(), "const type is missing"));
            declaration->value =
                build_expr_syntax(require(const_decl->get().constExpr(), "const value is missing"));
            return declaration;
        }

        if (const auto type_alias_decl = borrow(context.typeAliasDecl())) {
            auto declaration = make_decl<ast::TypeAliasDecl>(
                source_,
                type_alias_decl->get(),
                text_of(require(type_alias_decl->get().IDENT(), "type alias name is missing")));
            declaration->aliased_type = build_type_syntax(
                require(type_alias_decl->get().type_(), "type alias target is missing"));
            return declaration;
        }

        if (const auto struct_decl = borrow(context.structDecl())) {
            auto declaration = make_decl<ast::StructDecl>(
                source_,
                struct_decl->get(),
                text_of(require(struct_decl->get().IDENT(), "struct name is missing")));
            for (auto *field_context : struct_decl->get().structFieldDecl()) {
                declaration->fields.push_back(
                    build_struct_field_decl(require(field_context, "struct field is missing")));
            }
            return declaration;
        }

        if (const auto enum_decl = borrow(context.enumDecl())) {
            auto declaration = make_decl<ast::EnumDecl>(
                source_,
                enum_decl->get(),
                text_of(require(enum_decl->get().IDENT(), "enum name is missing")));
            for (auto *variant_context : enum_decl->get().enumVariant()) {
                declaration->variants.push_back(
                    build_enum_variant_decl(require(variant_context, "enum variant is missing")));
            }
            return declaration;
        }

        if (const auto capability_decl = borrow(context.capabilityDecl())) {
            auto declaration = make_decl<ast::CapabilityDecl>(
                source_,
                capability_decl->get(),
                text_of(require(capability_decl->get().IDENT(), "capability name is missing")));
            declaration->params = build_param_list(borrow(capability_decl->get().paramList()));
            declaration->return_type = build_type_syntax(
                require(capability_decl->get().type_(), "capability return type is missing"));
            return declaration;
        }

        if (const auto predicate_decl = borrow(context.predicateDecl())) {
            auto declaration = make_decl<ast::PredicateDecl>(
                source_,
                predicate_decl->get(),
                text_of(require(predicate_decl->get().IDENT(), "predicate name is missing")));
            declaration->params = build_param_list(borrow(predicate_decl->get().paramList()));
            return declaration;
        }

        if (const auto agent_decl = borrow(context.agentDecl())) {
            auto declaration = make_decl<ast::AgentDecl>(
                source_,
                agent_decl->get(),
                text_of(require(agent_decl->get().IDENT(), "agent name is missing")));
            declaration->input_type = build_type_syntax(
                require(require(agent_decl->get().inputDecl(), "agent input is missing").type_(),
                        "agent input type is missing"));
            declaration->context_type = build_type_syntax(require(
                require(agent_decl->get().contextDecl(), "agent context is missing").type_(),
                "agent context type is missing"));
            declaration->output_type = build_type_syntax(
                require(require(agent_decl->get().outputDecl(), "agent output is missing").type_(),
                        "agent output type is missing"));
            declaration->states = build_ident_list(require(
                require(agent_decl->get().statesDecl(), "agent states are missing").identList(),
                "agent states list is missing"));
            declaration->initial_state = text_of(require(
                require(agent_decl->get().initialDecl(), "agent initial state is missing").IDENT(),
                "agent initial state name is missing"));
            declaration->final_states = build_ident_list(
                require(require(agent_decl->get().finalDecl(), "agent final states are missing")
                            .identList(),
                        "agent final states list is missing"));
            declaration->capabilities = build_ident_list_opt(borrow(
                require(agent_decl->get().capabilitiesDecl(), "agent capabilities are missing")
                    .identListOpt()));

            if (const auto quota_decl = borrow(agent_decl->get().quotaDecl())) {
                declaration->quota = build_agent_quota(quota_decl->get());
            }

            for (auto *transition_context : agent_decl->get().transitionDecl()) {
                declaration->transitions.push_back(
                    build_transition_decl(require(transition_context, "transition is missing")));
            }
            return declaration;
        }

        if (const auto contract_decl = borrow(context.contractDecl())) {
            auto declaration = make_decl<ast::ContractDecl>(
                source_,
                contract_decl->get(),
                build_qualified_name(
                    require(contract_decl->get().qualifiedIdent(), "contract target is missing")));
            for (auto *item_context : contract_decl->get().contractItem()) {
                declaration->clauses.push_back(
                    build_contract_clause(require(item_context, "contract clause is missing")));
            }
            return declaration;
        }

        if (const auto flow_decl = borrow(context.flowDecl())) {
            auto declaration = make_decl<ast::FlowDecl>(
                source_,
                flow_decl->get(),
                build_qualified_name(
                    require(flow_decl->get().qualifiedIdent(), "flow target is missing")));
            for (auto *handler_context : flow_decl->get().stateHandler()) {
                declaration->state_handlers.push_back(
                    build_state_handler(require(handler_context, "state handler is missing")));
            }
            return declaration;
        }

        if (const auto workflow_decl = borrow(context.workflowDecl())) {
            auto declaration = make_decl<ast::WorkflowDecl>(
                source_,
                workflow_decl->get(),
                text_of(require(workflow_decl->get().IDENT(), "workflow name is missing")));
            declaration->input_type = build_type_syntax(require(
                require(workflow_decl->get().workflowInputDecl(), "workflow input is missing")
                    .type_(),
                "workflow input type is missing"));
            declaration->output_type = build_type_syntax(require(
                require(workflow_decl->get().workflowOutputDecl(), "workflow output is missing")
                    .type_(),
                "workflow output type is missing"));

            for (auto *item_context : workflow_decl->get().workflowItem()) {
                auto &item = require(item_context, "workflow item is missing");

                if (const auto node_decl = borrow(item.workflowNodeDecl())) {
                    declaration->nodes.push_back(build_workflow_node_decl(node_decl->get()));
                    continue;
                }

                if (const auto safety_decl = borrow(item.workflowSafetyDecl())) {
                    declaration->safety.push_back(build_temporal_expr_syntax(
                        require(safety_decl->get().workflowTemporalExpr(),
                                "workflow safety formula is missing")));
                    continue;
                }

                if (const auto liveness_decl = borrow(item.workflowLivenessDecl())) {
                    declaration->liveness.push_back(build_temporal_expr_syntax(
                        require(liveness_decl->get().workflowTemporalExpr(),
                                "workflow liveness formula is missing")));
                    continue;
                }

                throw std::logic_error("workflow item did not match any supported AHFL kind");
            }

            declaration->return_value = build_expr_syntax(require(
                require(workflow_decl->get().workflowReturnDecl(), "workflow return is missing")
                    .expr(),
                "workflow return expression is missing"));
            return declaration;
        }

        throw std::logic_error(
            "top-level declaration did not match any supported AHFL declaration kind");
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_expr_syntax(AHFLParser::ConstExprContext &context) const {
        return build_expr_syntax(require(context.expr(), "const expression is missing"));
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_expr_syntax(AHFLParser::ExprContext &context) const {
        return build_implies_expr(require(context.impliesExpr(), "expression body is missing"));
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_expr_syntax(AHFLParser::WorkflowTemporalExprContext &context) const {
        return build_temporal_expr_syntax(
            require(context.temporalExpr(), "workflow temporal expression is missing"));
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_expr_syntax(AHFLParser::TemporalExprContext &context) const {
        return build_temporal_implies_expr(
            require(context.temporalImpliesExpr(), "temporal expression body is missing"));
    }

    [[nodiscard]] Owned<ast::BlockSyntax>
    build_block_syntax(AHFLParser::BlockContext &context) const {
        auto block = make_owned<ast::BlockSyntax>();
        block->range = context_range(context, source_);

        for (auto *statement_context : context.statement()) {
            block->statements.push_back(
                build_statement_syntax(require(statement_context, "statement is missing")));
        }

        return block;
    }

    [[nodiscard]] Owned<ast::ExprSyntax> make_expr_syntax(ast::ExprSyntaxKind kind,
                                                          SourceRange range) const {
        auto expr = make_owned<ast::ExprSyntax>();
        expr->kind = kind;
        expr->range = range;
        expr->text = source_text(source_, range);
        return expr;
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    make_temporal_expr_syntax(ast::TemporalExprSyntaxKind kind, SourceRange range) const {
        auto expr = make_owned<ast::TemporalExprSyntax>();
        expr->kind = kind;
        expr->range = range;
        expr->text = source_text(source_, range);
        return expr;
    }

    [[nodiscard]] std::vector<std::string>
    build_infix_operator_texts(antlr4::ParserRuleContext &context) const {
        std::vector<std::string> operators;

        for (std::size_t index = 1; index < context.children.size(); index += 2) {
            operators.push_back(
                require(context.children[index], "operator child is missing").getText());
        }

        return operators;
    }

    template <typename ContextT, typename OperandContextT, typename BuilderT>
    [[nodiscard]] Owned<ast::ExprSyntax>
    build_expr_chain(ContextT &context,
                     const std::vector<OperandContextT *> &operands,
                     BuilderT build_operand,
                     bool right_associative = false) const {
        if (operands.empty()) {
            throw std::logic_error("expression chain is empty");
        }

        const auto operators = build_infix_operator_texts(context);
        if (operators.empty()) {
            return build_operand(require(operands.front(), "expression operand is missing"));
        }

        if (right_associative) {
            auto result = build_operand(require(operands.back(), "expression operand is missing"));

            for (std::size_t offset = operators.size(); offset > 0; --offset) {
                const auto index = offset - 1;
                auto lhs = build_operand(require(operands[index], "expression operand is missing"));
                auto binary = make_expr_syntax(ast::ExprSyntaxKind::Binary,
                                               span_range(lhs->range, result->range));
                binary->binary_op = expr_binary_op_from(operators[index]);
                binary->first = std::move(lhs);
                binary->second = std::move(result);
                result = std::move(binary);
            }

            return result;
        }

        auto result = build_operand(require(operands.front(), "expression operand is missing"));

        for (std::size_t index = 0; index < operators.size(); ++index) {
            auto rhs = build_operand(require(operands[index + 1], "expression operand is missing"));
            auto binary = make_expr_syntax(ast::ExprSyntaxKind::Binary,
                                           span_range(result->range, rhs->range));
            binary->binary_op = expr_binary_op_from(operators[index]);
            binary->first = std::move(result);
            binary->second = std::move(rhs);
            result = std::move(binary);
        }

        return result;
    }

    template <typename ContextT, typename OperandContextT, typename BuilderT>
    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_chain(ContextT &context,
                         const std::vector<OperandContextT *> &operands,
                         BuilderT build_operand,
                         bool right_associative = false) const {
        if (operands.empty()) {
            throw std::logic_error("temporal expression chain is empty");
        }

        const auto operators = build_infix_operator_texts(context);
        if (operators.empty()) {
            return build_operand(require(operands.front(), "temporal operand is missing"));
        }

        if (right_associative) {
            auto result = build_operand(require(operands.back(), "temporal operand is missing"));

            for (std::size_t offset = operators.size(); offset > 0; --offset) {
                const auto index = offset - 1;
                auto lhs = build_operand(require(operands[index], "temporal operand is missing"));
                auto binary = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Binary,
                                                        span_range(lhs->range, result->range));
                binary->binary_op = temporal_binary_op_from(operators[index]);
                binary->first = std::move(lhs);
                binary->second = std::move(result);
                result = std::move(binary);
            }

            return result;
        }

        auto result = build_operand(require(operands.front(), "temporal operand is missing"));

        for (std::size_t index = 0; index < operators.size(); ++index) {
            auto rhs = build_operand(require(operands[index + 1], "temporal operand is missing"));
            auto binary = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Binary,
                                                    span_range(result->range, rhs->range));
            binary->binary_op = temporal_binary_op_from(operators[index]);
            binary->first = std::move(result);
            binary->second = std::move(rhs);
            result = std::move(binary);
        }

        return result;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_implies_expr(AHFLParser::ImpliesExprContext &context) const {
        return build_expr_chain(
            context,
            context.orExpr(),
            [this](AHFLParser::OrExprContext &operand) { return build_or_expr(operand); },
            true);
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_or_expr(AHFLParser::OrExprContext &context) const {
        return build_expr_chain(
            context, context.andExpr(), [this](AHFLParser::AndExprContext &operand) {
                return build_and_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_and_expr(AHFLParser::AndExprContext &context) const {
        return build_expr_chain(
            context, context.equalityExpr(), [this](AHFLParser::EqualityExprContext &operand) {
                return build_equality_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_equality_expr(AHFLParser::EqualityExprContext &context) const {
        return build_expr_chain(
            context, context.compareExpr(), [this](AHFLParser::CompareExprContext &operand) {
                return build_compare_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_compare_expr(AHFLParser::CompareExprContext &context) const {
        return build_expr_chain(
            context, context.addExpr(), [this](AHFLParser::AddExprContext &operand) {
                return build_add_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_add_expr(AHFLParser::AddExprContext &context) const {
        return build_expr_chain(
            context, context.mulExpr(), [this](AHFLParser::MulExprContext &operand) {
                return build_mul_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_mul_expr(AHFLParser::MulExprContext &context) const {
        return build_expr_chain(
            context, context.unaryExpr(), [this](AHFLParser::UnaryExprContext &operand) {
                return build_unary_expr(operand);
            });
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_unary_expr(AHFLParser::UnaryExprContext &context) const {
        if (const auto inner = borrow(context.unaryExpr())) {
            auto expr =
                make_expr_syntax(ast::ExprSyntaxKind::Unary, context_range(context, source_));
            expr->unary_op = expr_unary_op_from(
                require(context.getStart(), "unary operator is missing").getText());
            expr->first = build_unary_expr(inner->get());
            return expr;
        }

        return build_postfix_expr(require(context.postfixExpr(), "postfix expression is missing"));
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_postfix_expr(AHFLParser::PostfixExprContext &context) const {
        auto result =
            build_primary_expr(require(context.primaryExpr(), "primary expression is missing"));

        std::size_t child_index = 1;
        std::size_t ident_index = 0;
        std::size_t expr_index = 0;

        while (child_index < context.children.size()) {
            const auto token_text =
                require(context.children[child_index], "postfix child is missing").getText();

            if (token_text == ".") {
                auto member =
                    make_expr_syntax(ast::ExprSyntaxKind::MemberAccess,
                                     span_range(result->range,
                                                terminal_range(require(context.IDENT(ident_index),
                                                                       "member name is missing"),
                                                               source_)));
                member->first = std::move(result);
                member->name =
                    text_of(require(context.IDENT(ident_index), "member name is missing"));
                result = std::move(member);
                ++ident_index;
                child_index += 2;
                continue;
            }

            if (token_text == "[") {
                auto index_expr = build_expr_syntax(
                    require(context.expr(expr_index), "index expression is missing"));
                auto index_access = make_expr_syntax(ast::ExprSyntaxKind::IndexAccess,
                                                     span_range(result->range, index_expr->range));
                index_access->first = std::move(result);
                index_access->second = std::move(index_expr);
                result = std::move(index_access);
                ++expr_index;
                child_index += 3;
                continue;
            }

            throw std::logic_error("unsupported postfix expression suffix");
        }

        return result;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_primary_expr(AHFLParser::PrimaryExprContext &context) const {
        if (const auto literal = borrow(context.literal())) {
            return build_literal_expr(literal->get());
        }

        if (const auto call = borrow(context.callExpr())) {
            return build_call_expr(call->get());
        }

        if (const auto struct_literal = borrow(context.structLiteral())) {
            return build_struct_literal_expr(struct_literal->get());
        }

        if (const auto qualified_value = borrow(context.qualifiedValueExpr())) {
            return build_qualified_value_expr(qualified_value->get());
        }

        if (const auto path = borrow(context.pathExpr())) {
            auto expr =
                make_expr_syntax(ast::ExprSyntaxKind::Path, context_range(context, source_));
            expr->path = build_path_syntax(path->get());
            return expr;
        }

        if (const auto list = borrow(context.listLiteral())) {
            return build_list_literal_expr(list->get());
        }

        if (const auto set = borrow(context.setLiteral())) {
            return build_set_literal_expr(set->get());
        }

        if (const auto map = borrow(context.mapLiteral())) {
            return build_map_literal_expr(map->get());
        }

        if (const auto inner_expr = borrow(context.expr())) {
            if (!context.children.empty() &&
                require(context.children[0], "primary expression child is missing").getText() ==
                    "some") {
                auto expr =
                    make_expr_syntax(ast::ExprSyntaxKind::Some, context_range(context, source_));
                expr->first = build_expr_syntax(inner_expr->get());
                return expr;
            }

            auto expr =
                make_expr_syntax(ast::ExprSyntaxKind::Group, context_range(context, source_));
            expr->first = build_expr_syntax(inner_expr->get());
            return expr;
        }

        if (context.children.size() == 1 &&
            require(context.children[0], "primary expression child is missing").getText() ==
                "none") {
            return make_expr_syntax(ast::ExprSyntaxKind::NoneLiteral,
                                    context_range(context, source_));
        }

        throw std::logic_error("primary expression did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_literal_expr(AHFLParser::LiteralContext &context) const {
        const auto range = context_range(context, source_);
        const auto literal_text =
            require(context.getStart(), "literal start token is missing").getText();

        if (literal_text == "true" || literal_text == "false") {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::BoolLiteral, range);
            expr->bool_value = literal_text == "true";
            return expr;
        }

        if (const auto integer_literal = borrow(context.integerLiteral())) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::IntegerLiteral, range);
            expr->integer_literal = build_integer_syntax(integer_literal->get());
            return expr;
        }

        if (borrow(context.floatLiteral()).has_value()) {
            return make_expr_syntax(ast::ExprSyntaxKind::FloatLiteral, range);
        }

        if (borrow(context.decimalLiteral()).has_value()) {
            return make_expr_syntax(ast::ExprSyntaxKind::DecimalLiteral, range);
        }

        if (borrow(context.stringLiteral()).has_value()) {
            return make_expr_syntax(ast::ExprSyntaxKind::StringLiteral, range);
        }

        if (const auto duration_literal = borrow(context.durationLiteral())) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::DurationLiteral, range);
            expr->duration_literal = build_duration_syntax(duration_literal->get());
            return expr;
        }

        throw std::logic_error("literal did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_call_expr(AHFLParser::CallExprContext &context) const {
        auto expr = make_expr_syntax(ast::ExprSyntaxKind::Call, context_range(context, source_));
        expr->qualified_name =
            build_qualified_name(require(context.qualifiedIdent(), "call target is missing"));

        if (const auto arguments = borrow(context.exprList())) {
            expr->items = build_expr_list(arguments->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_struct_literal_expr(AHFLParser::StructLiteralContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::StructLiteral, context_range(context, source_));
        expr->qualified_name = build_qualified_name(
            require(context.qualifiedIdent(), "struct literal type is missing"));

        if (const auto init_list = borrow(context.structInitList())) {
            expr->struct_fields = build_struct_init_list(init_list->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_qualified_value_expr(AHFLParser::QualifiedValueExprContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::QualifiedValue, context_range(context, source_));
        expr->qualified_name = build_qualified_name(context);
        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_list_literal_expr(AHFLParser::ListLiteralContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::ListLiteral, context_range(context, source_));

        if (const auto expr_list = borrow(context.exprList())) {
            expr->items = build_expr_list(expr_list->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_set_literal_expr(AHFLParser::SetLiteralContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::SetLiteral, context_range(context, source_));

        if (const auto expr_list = borrow(context.exprList())) {
            expr->items = build_expr_list(expr_list->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_map_literal_expr(AHFLParser::MapLiteralContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::MapLiteral, context_range(context, source_));

        if (const auto entry_list = borrow(context.mapEntryList())) {
            for (auto *entry_context : entry_list->get().mapEntry()) {
                expr->map_entries.push_back(
                    build_map_entry(require(entry_context, "map entry is missing")));
            }
        }

        return expr;
    }

    [[nodiscard]] std::vector<Owned<ast::ExprSyntax>>
    build_expr_list(AHFLParser::ExprListContext &context) const {
        std::vector<Owned<ast::ExprSyntax>> expressions;
        expressions.reserve(context.expr().size());

        for (auto *expr_context : context.expr()) {
            expressions.push_back(
                build_expr_syntax(require(expr_context, "expression is missing")));
        }

        return expressions;
    }

    [[nodiscard]] Owned<ast::MapEntrySyntax>
    build_map_entry(AHFLParser::MapEntryContext &context) const {
        auto entry = make_owned<ast::MapEntrySyntax>();
        entry->range = context_range(context, source_);
        entry->key = build_expr_syntax(require(context.expr(0), "map key is missing"));
        entry->value = build_expr_syntax(require(context.expr(1), "map value is missing"));
        return entry;
    }

    [[nodiscard]] std::vector<Owned<ast::StructInitSyntax>>
    build_struct_init_list(AHFLParser::StructInitListContext &context) const {
        std::vector<Owned<ast::StructInitSyntax>> fields;
        fields.reserve(context.structInit().size());

        for (auto *field_context : context.structInit()) {
            fields.push_back(
                build_struct_init(require(field_context, "struct initializer is missing")));
        }

        return fields;
    }

    [[nodiscard]] Owned<ast::StructInitSyntax>
    build_struct_init(AHFLParser::StructInitContext &context) const {
        auto field = make_owned<ast::StructInitSyntax>();
        field->range = context_range(context, source_);
        field->field_name = text_of(require(context.IDENT(), "struct initializer name is missing"));
        field->value =
            build_expr_syntax(require(context.expr(), "struct initializer value is missing"));
        return field;
    }

    [[nodiscard]] Owned<ast::PathSyntax>
    build_path_syntax(AHFLParser::PathExprContext &context) const {
        auto path = make_owned<ast::PathSyntax>();
        path->range = context_range(context, source_);

        const auto root_token_text =
            require(require(context.pathRoot(), "path root is missing").getStart(),
                    "path root token is missing")
                .getText();

        if (root_token_text == "input") {
            path->root_kind = ast::PathRootKind::Input;
        } else if (root_token_text == "output") {
            path->root_kind = ast::PathRootKind::Output;
        } else {
            path->root_kind = ast::PathRootKind::Identifier;
        }

        path->root_name = root_token_text;

        for (auto *segment_context : context.IDENT()) {
            path->members.push_back(text_of(require(segment_context, "path segment is missing")));
        }

        return path;
    }

    [[nodiscard]] Owned<ast::StatementSyntax>
    build_statement_syntax(AHFLParser::StatementContext &context) const {
        auto statement = make_owned<ast::StatementSyntax>();
        statement->range = context_range(context, source_);

        if (const auto let_stmt = borrow(context.letStmt())) {
            statement->kind = ast::StatementSyntaxKind::Let;
            statement->let_stmt = build_let_stmt(let_stmt->get());
            return statement;
        }

        if (const auto assign_stmt = borrow(context.assignStmt())) {
            statement->kind = ast::StatementSyntaxKind::Assign;
            statement->assign_stmt = build_assign_stmt(assign_stmt->get());
            return statement;
        }

        if (const auto if_stmt = borrow(context.ifStmt())) {
            statement->kind = ast::StatementSyntaxKind::If;
            statement->if_stmt = build_if_stmt(if_stmt->get());
            return statement;
        }

        if (const auto goto_stmt = borrow(context.gotoStmt())) {
            statement->kind = ast::StatementSyntaxKind::Goto;
            statement->goto_stmt = build_goto_stmt(goto_stmt->get());
            return statement;
        }

        if (const auto return_stmt = borrow(context.returnStmt())) {
            statement->kind = ast::StatementSyntaxKind::Return;
            statement->return_stmt = build_return_stmt(return_stmt->get());
            return statement;
        }

        if (const auto assert_stmt = borrow(context.assertStmt())) {
            statement->kind = ast::StatementSyntaxKind::Assert;
            statement->assert_stmt = build_assert_stmt(assert_stmt->get());
            return statement;
        }

        if (const auto expr_stmt = borrow(context.exprStmt())) {
            statement->kind = ast::StatementSyntaxKind::Expr;
            statement->expr_stmt = build_expr_stmt(expr_stmt->get());
            return statement;
        }

        throw std::logic_error("statement did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::LetStmtSyntax>
    build_let_stmt(AHFLParser::LetStmtContext &context) const {
        auto statement = make_owned<ast::LetStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->name = text_of(require(context.IDENT(), "let binding name is missing"));

        if (const auto type = borrow(context.type_())) {
            statement->type = build_type_syntax(type->get());
        }

        statement->initializer =
            build_expr_syntax(require(context.expr(), "let initializer is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::AssignStmtSyntax>
    build_assign_stmt(AHFLParser::AssignStmtContext &context) const {
        auto statement = make_owned<ast::AssignStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->target = build_path_syntax(
            require(require(context.lValue(), "assignment target is missing").pathExpr(),
                    "assignment path target is missing"));
        statement->value =
            build_expr_syntax(require(context.expr(), "assignment value is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::IfStmtSyntax> build_if_stmt(AHFLParser::IfStmtContext &context) const {
        auto statement = make_owned<ast::IfStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->condition =
            build_expr_syntax(require(context.expr(), "if condition is missing"));
        statement->then_block =
            build_block_syntax(require(context.block(0), "if then block is missing"));

        if (context.block().size() > 1) {
            statement->else_block =
                build_block_syntax(require(context.block(1), "if else block is missing"));
        }

        return statement;
    }

    [[nodiscard]] Owned<ast::GotoStmtSyntax>
    build_goto_stmt(AHFLParser::GotoStmtContext &context) const {
        auto statement = make_owned<ast::GotoStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->target_state = text_of(require(context.IDENT(), "goto target state is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::ReturnStmtSyntax>
    build_return_stmt(AHFLParser::ReturnStmtContext &context) const {
        auto statement = make_owned<ast::ReturnStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->value = build_expr_syntax(require(context.expr(), "return value is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::AssertStmtSyntax>
    build_assert_stmt(AHFLParser::AssertStmtContext &context) const {
        auto statement = make_owned<ast::AssertStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->condition =
            build_expr_syntax(require(context.expr(), "assert condition is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::ExprStmtSyntax>
    build_expr_stmt(AHFLParser::ExprStmtContext &context) const {
        auto statement = make_owned<ast::ExprStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->expr =
            build_expr_syntax(require(context.expr(), "expression statement is missing"));
        return statement;
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_implies_expr(AHFLParser::TemporalImpliesExprContext &context) const {
        return build_temporal_chain(
            context,
            context.temporalOrExpr(),
            [this](AHFLParser::TemporalOrExprContext &operand) {
                return build_temporal_or_expr(operand);
            },
            true);
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_or_expr(AHFLParser::TemporalOrExprContext &context) const {
        return build_temporal_chain(context,
                                    context.temporalAndExpr(),
                                    [this](AHFLParser::TemporalAndExprContext &operand) {
                                        return build_temporal_and_expr(operand);
                                    });
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_and_expr(AHFLParser::TemporalAndExprContext &context) const {
        return build_temporal_chain(context,
                                    context.temporalUntilExpr(),
                                    [this](AHFLParser::TemporalUntilExprContext &operand) {
                                        return build_temporal_until_expr(operand);
                                    });
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_until_expr(AHFLParser::TemporalUntilExprContext &context) const {
        return build_temporal_chain(context,
                                    context.temporalUnaryExpr(),
                                    [this](AHFLParser::TemporalUnaryExprContext &operand) {
                                        return build_temporal_unary_expr(operand);
                                    });
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_unary_expr(AHFLParser::TemporalUnaryExprContext &context) const {
        if (const auto inner = borrow(context.temporalUnaryExpr())) {
            auto expr = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Unary,
                                                  context_range(context, source_));
            expr->unary_op = temporal_unary_op_from(
                require(context.getStart(), "temporal unary operator is missing").getText());
            expr->first = build_temporal_unary_expr(inner->get());
            return expr;
        }

        return build_temporal_atom(require(context.temporalAtom(), "temporal atom is missing"));
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    build_temporal_atom(AHFLParser::TemporalAtomContext &context) const {
        if (const auto inner = borrow(context.temporalExpr())) {
            return build_temporal_expr_syntax(inner->get());
        }

        if (const auto expr = borrow(context.expr())) {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::EmbeddedExpr,
                                                      context_range(context, source_));
            temporal->expr = build_expr_syntax(expr->get());
            return temporal;
        }

        const auto keyword =
            require(context.getStart(), "temporal atom start token is missing").getText();

        if (keyword == "called") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Called,
                                                      context_range(context, source_));
            temporal->name = text_of(require(context.IDENT(0), "called() target is missing"));
            return temporal;
        }

        if (keyword == "in_state") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::InState,
                                                      context_range(context, source_));
            temporal->name = text_of(require(context.IDENT(0), "in_state() value is missing"));
            return temporal;
        }

        if (keyword == "running") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Running,
                                                      context_range(context, source_));
            temporal->name = text_of(require(context.IDENT(0), "running() value is missing"));
            return temporal;
        }

        if (keyword == "completed") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Completed,
                                                      context_range(context, source_));
            temporal->name = text_of(require(context.IDENT(0), "completed() target is missing"));

            if (context.IDENT().size() > 1) {
                temporal->state_name =
                    text_of(require(context.IDENT(1), "completed() state is missing"));
            }

            return temporal;
        }

        throw std::logic_error("temporal atom did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::IntegerSyntax>
    build_integer_syntax(AHFLParser::IntegerLiteralContext &context) const {
        const auto range = context_range(context, source_);
        auto syntax = make_owned<ast::IntegerSyntax>();
        syntax->range = range;
        syntax->spelling = source_text(source_, range);
        syntax->value = parse_integer_literal(syntax->spelling);
        return syntax;
    }

    [[nodiscard]] Owned<ast::DurationSyntax>
    build_duration_syntax(AHFLParser::DurationLiteralContext &context) const {
        const auto range = context_range(context, source_);
        auto syntax = make_owned<ast::DurationSyntax>();
        syntax->range = range;
        syntax->spelling = source_text(source_, range);
        return syntax;
    }

    [[nodiscard]] Owned<ast::TypeSyntax>
    build_type_syntax(AHFLParser::Type_Context &context) const {
        if (const auto primitive_type = borrow(context.primitiveType())) {
            return build_primitive_type_syntax(primitive_type->get());
        }

        auto type = make_owned<ast::TypeSyntax>();
        type->range = context_range(context, source_);

        if (const auto qualified_name = borrow(context.qualifiedIdent())) {
            type->kind = ast::TypeSyntaxKind::Named;
            type->name = build_qualified_name(qualified_name->get());
            return type;
        }

        const auto type_keyword =
            require(context.getStart(), "type start token is missing").getText();

        if (type_keyword == "Optional") {
            type->kind = ast::TypeSyntaxKind::Optional;
            type->first =
                build_type_syntax(require(context.type_(0), "optional element type is missing"));
            return type;
        }

        if (type_keyword == "List") {
            type->kind = ast::TypeSyntaxKind::List;
            type->first =
                build_type_syntax(require(context.type_(0), "list element type is missing"));
            return type;
        }

        if (type_keyword == "Set") {
            type->kind = ast::TypeSyntaxKind::Set;
            type->first =
                build_type_syntax(require(context.type_(0), "set element type is missing"));
            return type;
        }

        if (type_keyword == "Map") {
            type->kind = ast::TypeSyntaxKind::Map;
            type->first = build_type_syntax(require(context.type_(0), "map key type is missing"));
            type->second =
                build_type_syntax(require(context.type_(1), "map value type is missing"));
            return type;
        }

        throw std::logic_error("type did not match any supported AHFL type syntax kind");
    }

    [[nodiscard]] Owned<ast::TypeSyntax>
    build_primitive_type_syntax(AHFLParser::PrimitiveTypeContext &context) const {
        auto type = make_owned<ast::TypeSyntax>();
        type->range = context_range(context, source_);

        const auto primitive_keyword =
            require(context.getStart(), "primitive type start token is missing").getText();

        if (primitive_keyword == "Unit") {
            type->kind = ast::TypeSyntaxKind::Unit;
            return type;
        }

        if (primitive_keyword == "Bool") {
            type->kind = ast::TypeSyntaxKind::Bool;
            return type;
        }

        if (primitive_keyword == "Int") {
            type->kind = ast::TypeSyntaxKind::Int;
            return type;
        }

        if (primitive_keyword == "Float") {
            type->kind = ast::TypeSyntaxKind::Float;
            return type;
        }

        if (primitive_keyword == "String") {
            if (context.INT_LITERAL().empty()) {
                type->kind = ast::TypeSyntaxKind::String;
                return type;
            }

            type->kind = ast::TypeSyntaxKind::BoundedString;
            type->string_bounds = std::make_pair(
                parse_integer_literal(
                    text_of(require(context.INT_LITERAL(0), "string minimum length is missing"))),
                parse_integer_literal(
                    text_of(require(context.INT_LITERAL(1), "string maximum length is missing"))));
            return type;
        }

        if (primitive_keyword == "UUID") {
            type->kind = ast::TypeSyntaxKind::UUID;
            return type;
        }

        if (primitive_keyword == "Timestamp") {
            type->kind = ast::TypeSyntaxKind::Timestamp;
            return type;
        }

        if (primitive_keyword == "Duration") {
            type->kind = ast::TypeSyntaxKind::Duration;
            return type;
        }

        if (primitive_keyword == "Decimal") {
            type->kind = ast::TypeSyntaxKind::Decimal;
            type->decimal_scale = parse_integer_literal(
                text_of(require(context.INT_LITERAL(0), "decimal scale is missing")));
            return type;
        }

        throw std::logic_error("primitive type did not match any supported AHFL primitive");
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(AHFLParser::QualifiedIdentContext &context) const {
        return build_qualified_name(context.IDENT(), context_range(context, source_));
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(AHFLParser::QualifiedValueExprContext &context) const {
        return build_qualified_name(context.IDENT(), context_range(context, source_));
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(const std::vector<antlr4::tree::TerminalNode *> &segments,
                         SourceRange range) const {
        auto name = make_owned<ast::QualifiedName>();
        name->range = range;

        for (auto *segment_context : segments) {
            name->segments.push_back(
                text_of(require(segment_context, "qualified name segment is missing")));
        }

        return name;
    }

    [[nodiscard]] std::vector<std::string>
    build_ident_list(AHFLParser::IdentListContext &context) const {
        std::vector<std::string> names;
        names.reserve(context.IDENT().size());

        for (auto *name_context : context.IDENT()) {
            names.push_back(text_of(require(name_context, "identifier is missing")));
        }

        return names;
    }

    [[nodiscard]] std::vector<std::string>
    build_ident_list_opt(MaybeRef<AHFLParser::IdentListOptContext> context) const {
        if (!context.has_value()) {
            return {};
        }

        if (const auto ident_list = borrow(context->get().identList())) {
            return build_ident_list(ident_list->get());
        }

        return {};
    }

    [[nodiscard]] std::vector<Owned<ast::QualifiedName>> build_qualified_name_list_opt(
        MaybeRef<AHFLParser::QualifiedIdentListOptContext> context) const {
        std::vector<Owned<ast::QualifiedName>> names;

        if (!context.has_value()) {
            return names;
        }

        if (const auto qualified_list = borrow(context->get().qualifiedIdentList())) {
            for (auto *name_context : qualified_list->get().qualifiedIdent()) {
                names.push_back(
                    build_qualified_name(require(name_context, "qualified name is missing")));
            }
        }

        return names;
    }

    [[nodiscard]] std::vector<Owned<ast::ParamDeclSyntax>>
    build_param_list(MaybeRef<AHFLParser::ParamListContext> context) const {
        std::vector<Owned<ast::ParamDeclSyntax>> params;

        if (!context.has_value()) {
            return params;
        }

        params.reserve(context->get().param().size());
        for (auto *param_context : context->get().param()) {
            params.push_back(build_param_decl(require(param_context, "parameter is missing")));
        }

        return params;
    }

    [[nodiscard]] Owned<ast::ParamDeclSyntax>
    build_param_decl(AHFLParser::ParamContext &context) const {
        auto param = make_owned<ast::ParamDeclSyntax>();
        param->range = context_range(context, source_);
        param->name = text_of(require(context.IDENT(), "parameter name is missing"));
        param->type = build_type_syntax(require(context.type_(), "parameter type is missing"));
        return param;
    }

    [[nodiscard]] Owned<ast::StructFieldDeclSyntax>
    build_struct_field_decl(AHFLParser::StructFieldDeclContext &context) const {
        auto field = make_owned<ast::StructFieldDeclSyntax>();
        field->range = context_range(context, source_);
        field->name = text_of(require(context.IDENT(), "field name is missing"));
        field->type = build_type_syntax(require(context.type_(), "field type is missing"));

        if (const auto default_value = borrow(context.constExpr())) {
            field->default_value = build_expr_syntax(default_value->get());
        }

        return field;
    }

    [[nodiscard]] Owned<ast::EnumVariantDeclSyntax>
    build_enum_variant_decl(AHFLParser::EnumVariantContext &context) const {
        auto variant = make_owned<ast::EnumVariantDeclSyntax>();
        variant->range = context_range(context, source_);
        variant->name = text_of(require(context.IDENT(), "enum variant name is missing"));
        return variant;
    }

    [[nodiscard]] Owned<ast::TransitionSyntax>
    build_transition_decl(AHFLParser::TransitionDeclContext &context) const {
        auto transition = make_owned<ast::TransitionSyntax>();
        transition->range = context_range(context, source_);
        transition->from_state =
            text_of(require(context.IDENT(0), "transition source state is missing"));
        transition->to_state =
            text_of(require(context.IDENT(1), "transition target state is missing"));
        return transition;
    }

    [[nodiscard]] Owned<ast::AgentQuotaSyntax>
    build_agent_quota(AHFLParser::QuotaDeclContext &context) const {
        auto quota = make_owned<ast::AgentQuotaSyntax>();
        quota->range = context_range(context, source_);

        for (auto *item_context : context.quotaItem()) {
            auto item = make_owned<ast::AgentQuotaItemSyntax>();
            item->range = context_range(require(item_context, "quota item is missing"), source_);

            if (const auto integer_literal = borrow(item_context->integerLiteral())) {
                item->kind = ast::AgentQuotaItemKind::MaxToolCalls;
                item->integer_value = build_integer_syntax(integer_literal->get());
            } else if (const auto duration_literal = borrow(item_context->durationLiteral())) {
                item->kind = ast::AgentQuotaItemKind::MaxExecutionTime;
                item->duration_value = build_duration_syntax(duration_literal->get());
            } else {
                throw std::logic_error("quota item did not match any supported AHFL kind");
            }

            quota->items.push_back(std::move(item));
        }

        return quota;
    }

    [[nodiscard]] Owned<ast::ContractClauseSyntax>
    build_contract_clause(AHFLParser::ContractItemContext &context) const {
        auto clause = make_owned<ast::ContractClauseSyntax>();
        clause->range = context_range(context, source_);

        if (const auto requires_decl = borrow(context.requiresDecl())) {
            clause->kind = ast::ContractClauseKind::Requires;
            clause->expr =
                build_expr_syntax(require(requires_decl->get().expr(), "requires body is missing"));
            return clause;
        }

        if (const auto ensures_decl = borrow(context.ensuresDecl())) {
            clause->kind = ast::ContractClauseKind::Ensures;
            clause->expr =
                build_expr_syntax(require(ensures_decl->get().expr(), "ensures body is missing"));
            return clause;
        }

        if (const auto invariant_decl = borrow(context.invariantDecl())) {
            clause->kind = ast::ContractClauseKind::Invariant;
            clause->temporal_expr = build_temporal_expr_syntax(
                require(invariant_decl->get().temporalExpr(), "invariant body is missing"));
            return clause;
        }

        if (const auto forbid_decl = borrow(context.forbidDecl())) {
            clause->kind = ast::ContractClauseKind::Forbid;
            clause->temporal_expr = build_temporal_expr_syntax(
                require(forbid_decl->get().temporalExpr(), "forbid body is missing"));
            return clause;
        }

        throw std::logic_error("contract clause did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::StatePolicySyntax>
    build_state_policy(AHFLParser::StatePolicyContext &context) const {
        auto policy = make_owned<ast::StatePolicySyntax>();
        policy->range = context_range(context, source_);

        for (auto *item_context : context.statePolicyItem()) {
            auto item = make_owned<ast::StatePolicyItemSyntax>();
            item->range =
                context_range(require(item_context, "state policy item is missing"), source_);

            if (const auto retry_limit = borrow(item_context->integerLiteral())) {
                item->kind = ast::StatePolicyItemKind::Retry;
                item->retry_limit = build_integer_syntax(retry_limit->get());
            } else if (const auto retry_on = borrow(item_context->qualifiedIdentListOpt())) {
                item->kind = ast::StatePolicyItemKind::RetryOn;
                item->retry_on = build_qualified_name_list_opt(retry_on);
            } else if (const auto timeout = borrow(item_context->durationLiteral())) {
                item->kind = ast::StatePolicyItemKind::Timeout;
                item->timeout = build_duration_syntax(timeout->get());
            } else {
                throw std::logic_error("state policy item did not match any supported AHFL kind");
            }

            policy->items.push_back(std::move(item));
        }

        return policy;
    }

    [[nodiscard]] Owned<ast::StateHandlerSyntax>
    build_state_handler(AHFLParser::StateHandlerContext &context) const {
        auto handler = make_owned<ast::StateHandlerSyntax>();
        handler->range = context_range(context, source_);
        handler->state_name = text_of(require(context.IDENT(), "state handler name is missing"));
        handler->body =
            build_block_syntax(require(context.block(), "state handler body is missing"));

        if (const auto policy = borrow(context.statePolicy())) {
            handler->policy = build_state_policy(policy->get());
        }

        return handler;
    }

    [[nodiscard]] Owned<ast::WorkflowNodeDeclSyntax>
    build_workflow_node_decl(AHFLParser::WorkflowNodeDeclContext &context) const {
        auto node = make_owned<ast::WorkflowNodeDeclSyntax>();
        node->range = context_range(context, source_);
        node->name = text_of(require(context.IDENT(), "workflow node name is missing"));
        node->target = build_qualified_name(
            require(context.qualifiedIdent(), "workflow node target is missing"));
        node->input = build_expr_syntax(require(context.expr(), "workflow node input is missing"));
        node->after = build_ident_list_opt(borrow(context.identListOpt()));
        return node;
    }
};

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

Frontend::Frontend(FrontendOptions options) : options_(options) {}

ParseResult Frontend::parse_file(const std::filesystem::path &path) const {
    ParseResult result;
    if (!read_text_file(path, result.source, result.diagnostics, "source file")) {
        return result;
    }

    return parse_text(std::move(result.source.display_name), std::move(result.source.content));
}

ParseResult Frontend::parse_text(std::string display_name, std::string text) const {
    ParseResult result;
    result.source.display_name = std::move(display_name);
    result.source.content = std::move(text);

    antlr4::ANTLRInputStream input(result.source.content);
    AHFLLexer lexer(std::addressof(input));
    auto tokens = make_token_stream(lexer);
    auto parser = make_parser(tokens);

    DiagnosticErrorListener error_listener(result.source, result.diagnostics);
    install_error_listener(lexer, error_listener);
    install_error_listener(parser, error_listener);

    try {
        ProgramBuilder builder(result.source);
        result.program =
            builder.build(require(parser.program(), "parser.program() returned no parse tree"));
    } catch (const std::exception &exception) {
        result.diagnostics.error("parser failed: " + std::string(exception.what()));
    }

    if (result.program && options_.emit_parse_note && !result.has_errors()) {
        result.diagnostics.note("parsed with the generated ANTLR4 C++ parser from grammar/AHFL.g4");
    }

    return result;
}

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
