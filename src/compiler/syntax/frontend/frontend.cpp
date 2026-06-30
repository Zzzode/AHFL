#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/frontend/desugar.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
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

// Wave-21 A-1: thrown by ProgramBuilder::RecursionDepthGuard when the ANTLR
// visitor descends past kMaxRecursionDepth (256). Caught by the existing
// try/catch in Frontend::parse_text (frontend.cpp ~line 3090) which converts
// it into a user-facing diagnostic.
class ParserStackOverflowException : public std::runtime_error {
  public:
    ParserStackOverflowException(std::string nesting_kind,
                                 std::size_t depth,
                                 std::size_t limit,
                                 SourceRange where)
        : std::runtime_error(build_what(nesting_kind, depth, limit)),
          nesting_kind_(std::move(nesting_kind)),
          depth_(depth),
          limit_(limit),
          where_(where) {}

    [[nodiscard]] const std::string &nesting_kind() const noexcept { return nesting_kind_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }
    [[nodiscard]] SourceRange where() const noexcept { return where_; }

  private:
    static std::string build_what(const std::string &nesting_kind,
                                  std::size_t depth,
                                  std::size_t limit) {
        std::ostringstream oss;
        oss << "parser recursion too deep (" << nesting_kind << " nesting = " << depth
            << "; hard limit = " << limit
            << "). Simplify this expression or split it into smaller declarations.";
        return oss.str();
    }

    std::string nesting_kind_;
    std::size_t depth_;
    std::size_t limit_;
    SourceRange where_;
};

template <typename NodeT> [[nodiscard]] std::string text_of(NodeT &node) {
    return node.getText();
}

[[nodiscard]] std::string identifier_text(AHFLParser::IdentifierContext &context) {
    return context.getText();
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

// ANTLR4 C++ runtime returns Unicode CODE POINT indices (not byte offsets) when
// the input contains UTF-8 multi-byte sequences (ANTLRInputStream advances a
// char32 cursor, not a byte cursor). Our SourceFile / SourceRange works in raw
// byte offsets, so every token position must be translated before it is stored
// into the AST. Otherwise source ranges silently drift by the number of UTF-8
// extension bytes that precede them, producing diagnostics that point at the
// wrong character and (worse) AST literal nodes whose `source_text(range)`
// returns garbled bytes.
[[nodiscard]] std::size_t codepoint_to_byte_offset(const SourceFile &source,
                                                   std::size_t codepoint_index) {
    const std::string_view content = source.content;
    if (codepoint_index == INVALID_INDEX) {
        return content.size();
    }

    std::size_t byte_index = 0;
    std::size_t codepoints_seen = 0;
    while (byte_index < content.size() && codepoints_seen < codepoint_index) {
        const unsigned char c = static_cast<unsigned char>(content[byte_index]);
        // A UTF-8 code point starts with anything except a 10xxxxxx continuation
        // byte; every start byte advances the codepoint counter by exactly one.
        if ((c & 0xC0u) != 0x80u) {
            ++codepoints_seen;
        }
        ++byte_index;
    }
    return byte_index;
}

[[nodiscard]] SourceRange
token_range(const antlr4::Token &start, MaybeCRef<antlr4::Token> stop, const SourceFile &source) {
    const auto begin_offset = codepoint_to_byte_offset(source, start.getStartIndex());

    std::size_t end_offset = begin_offset;
    if (stop.has_value() && stop->get().getStopIndex() != INVALID_INDEX) {
        // +1 because stopIndex points at the LAST CODE POINT of the token, so
        // a single-codepoint token produces the half-open range [begin, begin+1).
        end_offset = codepoint_to_byte_offset(source, stop->get().getStopIndex() + 1);
    } else if (start.getStopIndex() != INVALID_INDEX) {
        end_offset = codepoint_to_byte_offset(source, start.getStopIndex() + 1);
    }

    return clamp_range(begin_offset, end_offset, source);
}

// ── Helpers for the reference_wrapper unwrap in context_range below ───────
namespace detail_is_rw_impl_ {
template <typename T> struct is_reference_wrapper : std::false_type {};
template <typename T> struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type {};
} // namespace detail_is_rw_impl_
template <typename T>
struct detail_is_reference_wrapper
    : detail_is_rw_impl_::is_reference_wrapper<std::remove_cv_t<T>> {};

template <typename ContextT>
[[nodiscard]] SourceRange context_range(ContextT &context, const SourceFile &source) {
    // Unwrap std::reference_wrapper<U> if present (ANTLR optional rules return
    // reference_wrappers; call sites use borrow() which may yield wrappers).
    using Unwrapped = std::remove_cvref_t<ContextT>;
    if constexpr (detail_is_reference_wrapper<Unwrapped>::value) {
        return context_range(context.get(), source);
    } else {
        return token_range(require(context.getStart(), "parser context start token is missing"),
                           borrow(context.getStop()),
                           source);
    }
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
    return make_owned<DeclT>(std::forward<Args>(args)..., range);
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
            diagnostics_.error()
                .message(message)
                .range(token_range(offending->get(), offending, source_))
                .emit();
            return;
        }

        const auto begin_offset = source_.offset_of(line, char_position_in_line + 1);
        diagnostics_.error()
            .message(message)
            .range(clamp_range(begin_offset, begin_offset, source_))
            .emit();
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
        diagnostics.error()
            .message("failed to open " + std::string(kind) + ": " + display_path(path))
            .emit();
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    source.content = buffer.str();
    source.invalidate_line_starts_cache();
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
    // Monotonic counter used to assign stable ExprSyntax::node_id values during
    // the parse-tree lowering. Mutable so existing const builder methods remain
    // const while still able to mint fresh ids.
    mutable std::uint64_t next_expr_id_{0};

    // Wave-21 A-1: parser recursion guard. Incremented/decremented by
    // RecursionDepthGuard at every build_* method entry/exit. Exceeding
    // kMaxRecursionDepth throws ParserStackOverflowException which is caught
    // by Frontend::parse_text and converted into a PARSER_STACK_OVERFLOW
    // diagnostic. Hard-coded to 256 (conservative: macOS thread stacks are
    // typically ~8 MB, and each build_* frame is ~1-2 KB, so 256 gives a
    // generous 2x headroom).
    static constexpr std::size_t kMaxRecursionDepth = 256;
    mutable std::size_t recursion_depth_{0};

    // RAII wrapper: {guard constructor increments & checks, destructor decrements}.
    // One-liner at every build_* method entry:
    //   auto guard = RecursionDepthGuard{*this, "kind_label", context_range(ctx, source_)};
    class RecursionDepthGuard {
      public:
        RecursionDepthGuard(const ProgramBuilder &builder,
                            std::string nesting_kind,
                            SourceRange at_range)
            : builder_(builder), nesting_kind_(std::move(nesting_kind)) {
            ++builder_.recursion_depth_;
            if (builder_.recursion_depth_ > kMaxRecursionDepth) {
                throw ParserStackOverflowException(
                    nesting_kind_,
                    builder_.recursion_depth_,
                    kMaxRecursionDepth,
                    at_range);
            }
        }

        ~RecursionDepthGuard() { --builder_.recursion_depth_; }

        RecursionDepthGuard(const RecursionDepthGuard &) = delete;
        RecursionDepthGuard &operator=(const RecursionDepthGuard &) = delete;

      private:
        const ProgramBuilder &builder_;
        std::string nesting_kind_;
    };

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
            context.identifier() != nullptr
                ? text_of(*context.identifier())
                : std::string{});
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
                identifier_text(
                    require(type_alias_decl->get().identifier(), "type alias name is missing")));
            if (const auto params = borrow(type_alias_decl->get().typeParams())) {
                declaration->type_params = build_type_params(params->get());
            }
            declaration->aliased_type = build_type_syntax(
                require(type_alias_decl->get().type_(), "type alias target is missing"));
            return declaration;
        }

        if (const auto struct_decl = borrow(context.structDecl())) {
            auto declaration =
                make_decl<ast::StructDecl>(source_,
                                           struct_decl->get(),
                                           identifier_text(require(struct_decl->get().identifier(),
                                                                   "struct name is missing")));
            if (const auto params = borrow(struct_decl->get().typeParams())) {
                declaration->type_params = build_type_params(params->get());
            }
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
                identifier_text(require(enum_decl->get().identifier(), "enum name is missing")));
            if (const auto params = borrow(enum_decl->get().typeParams())) {
                declaration->type_params = build_type_params(params->get());
            }
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
            if (const auto effect = borrow(capability_decl->get().capabilityEffectBlock())) {
                declaration->effect = build_capability_effect(effect->get());
            }
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
            // Wave-20 QW-4: `context` clause is optional on the grammar. When the
            // user omits it (e.g. `agent A { input: X; output: Y; ... }`) we
            // leave `context_type == nullptr` and let the typechecker emit a
            // friendly "agent missing context type" diagnostic (see
            // build_agent_types) instead of the parser's unreadable
            // "mismatched input 'output' expecting 'context'" ANTLR message.
            if (const auto ctx_decl = borrow(agent_decl->get().contextDecl())) {
                declaration->context_type = build_type_syntax(
                    require(ctx_decl->get().type_(), "agent context type is missing"));
            } else {
                declaration->context_type = nullptr;
            }
            declaration->output_type = build_type_syntax(
                require(require(agent_decl->get().outputDecl(), "agent output is missing").type_(),
                        "agent output type is missing"));
            declaration->states = build_ident_list(require(
                require(agent_decl->get().statesDecl(), "agent states are missing").identList(),
                "agent states list is missing"));
            declaration->states_range = context_range(*agent_decl->get().statesDecl(), source_);
            declaration->initial_state = text_of(require(
                require(agent_decl->get().initialDecl(), "agent initial state is missing").IDENT(),
                "agent initial state name is missing"));
            declaration->initial_state_range =
                context_range(*agent_decl->get().initialDecl(), source_);
            declaration->final_states = build_ident_list(
                require(require(agent_decl->get().finalDecl(), "agent final states are missing")
                            .identList(),
                        "agent final states list is missing"));
            declaration->final_states_range =
                context_range(*agent_decl->get().finalDecl(), source_);
            // Wave-20 QW-4: `capabilities` clause is optional on the grammar.
            // When omitted, keep capabilities == empty vector and install a
            // zero-length range so downstream diagnostics (e.g. "agent
            // requires capability `Foo` but declares none") can point the
            // user at the agent's opening keyword range with a helpful note.
            if (const auto caps_decl = borrow(agent_decl->get().capabilitiesDecl())) {
                declaration->capabilities = build_ident_list_opt(borrow(caps_decl->get().identListOpt()));
                declaration->capabilities_range = context_range(*caps_decl, source_);
            } else {
                declaration->capabilities.clear();
                declaration->capabilities_range = SourceRange{};
            }

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

        if (const auto fn_decl = borrow(context.fnDecl())) {
            return build_fn_decl(fn_decl->get());
        }

        if (const auto trait_decl = borrow(context.traitDecl())) {
            return build_trait_decl(trait_decl->get());
        }

        if (const auto impl_decl = borrow(context.implDecl())) {
            return build_impl_decl(impl_decl->get());
        }

        throw std::logic_error(
            "top-level declaration did not match any supported AHFL declaration kind");
    }

    // ----------------------------------------------------------------------
    // P2 (RFC §3.2.2 / §3.2.3 / §2 / §6): function-declaration builders
    // ----------------------------------------------------------------------

    [[nodiscard]] Owned<ast::Decl> build_fn_decl(AHFLParser::FnDeclContext &context) const {
        auto declaration = make_decl<ast::FnDecl>(
            source_, context, identifier_text(require(context.identifier(), "fn name is missing")));

        // P5 (RFC §3.3): @builtin attribute. The builtin name is the string
        // literal content with surrounding quotes stripped.
        if (const auto builtin_attr = borrow(context.builtinAttr())) {
            if (const auto str_lit = borrow(builtin_attr->get().STRING_LITERAL())) {
                std::string raw = text_of(str_lit->get());
                // Strip surrounding double quotes
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    declaration->builtin_name = raw.substr(1, raw.size() - 2);
                } else {
                    declaration->builtin_name = raw;
                }
            }
        }

        if (const auto type_params = borrow(context.typeParams())) {
            declaration->type_params = build_type_params(type_params->get());
        }

        if (const auto param_list = borrow(context.paramList())) {
            declaration->params = build_param_list(param_list->get());
        }

        if (const auto return_type = borrow(context.type_())) {
            declaration->return_type = build_type_syntax(return_type->get());
        }

        if (const auto effect_clause = borrow(context.effectClause())) {
            declaration->effect_clause = build_effect_clause(effect_clause->get());
        }

        if (const auto where_clause = borrow(context.whereClause())) {
            declaration->where_clause = build_where_clause(where_clause->get());
        }

        if (const auto body = borrow(context.fnBody())) {
            declaration->body =
                build_block_syntax(require(body->get().block(), "fn body block is missing"));
        }

        return declaration;
    }

    // ------------------------------------------------------------------
    // P3 (RFC §3.2.2 / type-system §1.3 / §1.4): trait & impl builders
    // ------------------------------------------------------------------

    [[nodiscard]] Owned<ast::Decl> build_trait_decl(AHFLParser::TraitDeclContext &context) const {
        auto declaration = make_decl<ast::TraitDecl>(
            source_, context, text_of(require(context.IDENT(), "trait name is missing")));

        if (const auto type_params = borrow(context.typeParams())) {
            declaration->type_params = build_type_params(type_params->get());
        }

        // traitDecl: 'trait' IDENT typeParams? (':' typeBoundList)? '{' traitItem* '}'
        // The optional super-trait bound list carries each bound as a direct
        // type_ child (same shape as a type-param bound list).
        if (const auto bound_list = borrow(context.typeBoundList())) {
            for (auto *bound_context : bound_list->get().type_()) {
                declaration->super_traits.push_back(
                    build_type_syntax(require(bound_context, "super-trait bound is missing")));
            }
        }

        if (const auto where_clause = borrow(context.whereClause())) {
            declaration->where_clause = build_where_clause(where_clause->get());
        }

        for (auto *item_context : context.traitItem()) {
            declaration->items.push_back(
                build_trait_item(require(item_context, "trait item is missing")));
        }

        return declaration;
    }

    [[nodiscard]] Owned<ast::TraitItemSyntax>
    build_trait_item(AHFLParser::TraitItemContext &context) const {
        auto item = make_owned<ast::TraitItemSyntax>();
        item->range = context_range(context, source_);

        // traitItem: traitFnItem | assocTypeItem | assocConstItem
        if (const auto fn_item = borrow(context.traitFnItem())) {
            populate_trait_fn_item(*item, fn_item->get());
            return item;
        }

        if (const auto assoc_type_item = borrow(context.assocTypeItem())) {
            populate_assoc_type_item(*item, assoc_type_item->get());
            return item;
        }

        if (const auto assoc_const_item = borrow(context.assocConstItem())) {
            populate_assoc_const_item(*item, assoc_const_item->get());
            return item;
        }

        throw std::logic_error(
            "trait item did not match trait fn, assoc type, or assoc const");
    }

    void populate_trait_fn_item(ast::TraitItemSyntax &item,
                                AHFLParser::TraitFnItemContext &context) const {
        item.kind = ast::TraitItemKind::Fn;
        item.range = context_range(context, source_);
        item.name = identifier_text(require(context.identifier(), "trait fn name is missing"));

        if (const auto type_params = borrow(context.typeParams())) {
            item.type_params = build_type_params(type_params->get());
        }
        if (const auto param_list = borrow(context.paramList())) {
            item.params = build_param_list(param_list->get());
        }
        if (const auto return_type = borrow(context.type_())) {
            item.return_type = build_type_syntax(return_type->get());
        }
        if (const auto effect_clause = borrow(context.effectClause())) {
            item.effect_clause = build_effect_clause(effect_clause->get());
        }
        if (const auto where_clause = borrow(context.whereClause())) {
            item.where_clause = build_where_clause(where_clause->get());
        }
    }

    void populate_assoc_type_item(ast::TraitItemSyntax &item,
                                  AHFLParser::AssocTypeItemContext &context) const {
        item.kind = ast::TraitItemKind::AssocType;
        item.range = context_range(context, source_);
        item.name = identifier_text(require(context.identifier(), "assoc type name is missing"));

        auto assoc = make_owned<ast::TraitItemSyntax::AssocTypeDecl>();
        assoc->range = context_range(context, source_);
        assoc->name = item.name;
        if (const auto type_params = borrow(context.typeParams())) {
            assoc->type_params = build_type_params(type_params->get());
        }
        if (const auto bound_list = borrow(context.typeBoundList())) {
            for (auto *bound_context : bound_list->get().type_()) {
                assoc->bounds.push_back(
                    build_type_syntax(require(bound_context, "assoc type bound is missing")));
            }
        }
        if (const auto default_type = borrow(context.type_())) {
            assoc->default_type = build_type_syntax(default_type->get());
        }
        item.assoc_type = std::move(assoc);
    }

    // P3c.S1: associated constant trait item. Maps `const N: T [= expr];`
    // to `TraitItemSyntax::AssocConstDecl`. `default_value` is optional (null
    // when absent), forcing the implementing impl to provide a value.
    void populate_assoc_const_item(ast::TraitItemSyntax &item,
                                   AHFLParser::AssocConstItemContext &context) const {
        item.kind = ast::TraitItemKind::AssocConst;
        item.range = context_range(context, source_);
        item.name = identifier_text(require(context.identifier(), "assoc const name is missing"));

        auto assoc_const = make_owned<ast::TraitItemSyntax::AssocConstDecl>();
        assoc_const->range = context_range(context, source_);
        assoc_const->name = item.name;
        assoc_const->type = build_type_syntax(require(context.type_(), "assoc const type is missing"));
        if (const auto default_value = borrow(context.constExpr())) {
            // constExpr reuses the expr parser; build as plain ExprSyntax (const
            // validation is deferred to semantic analysis P3b).
            assoc_const->default_value = build_expr_syntax(default_value->get());
        }
        item.assoc_const = std::move(assoc_const);
    }

    [[nodiscard]] Owned<ast::Decl> build_impl_decl(AHFLParser::ImplDeclContext &context) const {
        auto declaration = make_decl<ast::ImplDecl>(source_, context);

        if (const auto type_params = borrow(context.typeParams())) {
            declaration->type_params = build_type_params(type_params->get());
        }

        // implDecl: 'impl' typeParams? (traitRef 'for')? type_ whereClause? '{' implItem* '}'
        // `traitRef 'for'` is present iff the optional traitRef child exists; in
        // that case the trailing `type_` is the target type. When absent, the
        // single `type_` child is the inherent-impl target.
        if (const auto trait_ref = borrow(context.traitRef())) {
            declaration->trait_ref = build_type_syntax(
                require(trait_ref->get().type_(), "impl trait reference is missing"));
        }
        declaration->target_type =
            build_type_syntax(require(context.type_(), "impl target type is missing"));

        if (const auto where_clause = borrow(context.whereClause())) {
            declaration->where_clause = build_where_clause(where_clause->get());
        }

        // P3c.S1: impl body is a flat list of implItem alternations. The
        // per-kind buckets (`methods` / `assoc_items` / `const_items`) are
        // the unique owners of each child; the unified `items` dispatcher
        // stores non-owning pointers into the same elements.
        for (auto *impl_item_context_raw : context.implItem()) {
            auto &impl_item_context = require(
                impl_item_context_raw, "impl item is missing");
            auto item = make_owned<ast::ImplItemSyntax>();
            item->range = context_range(impl_item_context, source_);

            if (const auto fn_ctx = borrow(impl_item_context.implFnItem())) {
                auto fn_def = build_impl_fn_item(fn_ctx->get());
                item->kind = ast::ImplItemKind::Fn;
                item->fn_def = fn_def.get();
                declaration->methods.push_back(std::move(fn_def));
            } else if (const auto atd_ctx = borrow(impl_item_context.assocTypeDef())) {
                auto assoc_type = make_owned<ast::ImplItemSyntax::AssocTypeDef>();
                assoc_type->range = context_range(atd_ctx->get(), source_);
                assoc_type->name = identifier_text(require(
                    atd_ctx->get().identifier(), "impl assoc type name is missing"));
                assoc_type->type = build_type_syntax(require(
                    atd_ctx->get().type_(), "impl assoc type is missing"));
                item->kind = ast::ImplItemKind::AssocType;
                item->assoc_type = assoc_type.get();
                declaration->assoc_items.push_back(std::move(assoc_type));
            } else if (const auto acd_ctx = borrow(impl_item_context.assocConstDef())) {
                auto assoc_const = make_owned<ast::ImplItemSyntax::AssocConstDef>();
                assoc_const->range = context_range(acd_ctx->get(), source_);
                assoc_const->name = identifier_text(require(
                    acd_ctx->get().identifier(), "impl assoc const name is missing"));
                assoc_const->type = build_type_syntax(require(
                    acd_ctx->get().type_(), "impl assoc const type is missing"));
                auto &def_value = require(
                    acd_ctx->get().constExpr(), "impl assoc const value is missing");
                assoc_const->value = build_expr_syntax(def_value);
                item->kind = ast::ImplItemKind::AssocConst;
                item->assoc_const = assoc_const.get();
                declaration->const_items.push_back(std::move(assoc_const));
            } else {
                throw std::logic_error(
                    "impl item did not match fn, assoc type, or assoc const");
            }

            declaration->items.push_back(std::move(item));
        }

        return declaration;
    }

    // Impl method definition. Surface identical to FnDecl but body is mandatory
    // (RFC §1.4). Reuses FnDecl so downstream passes treat impl methods
    // uniformly with top-level functions.
    [[nodiscard]] Owned<ast::FnDecl>
    build_impl_fn_item(AHFLParser::ImplFnItemContext &context) const {
        auto declaration = make_decl<ast::FnDecl>(
            source_,
            context,
            identifier_text(require(context.identifier(), "impl fn name is missing")));

        // P5 / BLK-01: @builtin attribute on impl methods. Semantics are
        // identical to the module-level @builtin attribute; the name maps to
        // a compiler/runtime builtin hook. Stored on the same FnDecl field so
        // downstream semantic passes can reuse a single inspection point.
        if (const auto builtin_attr = borrow(context.builtinAttr())) {
            if (const auto str_lit = borrow(builtin_attr->get().STRING_LITERAL())) {
                std::string raw = text_of(str_lit->get());
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    declaration->builtin_name = raw.substr(1, raw.size() - 2);
                } else {
                    declaration->builtin_name = raw;
                }
            }
        }

        if (const auto type_params = borrow(context.typeParams())) {
            declaration->type_params = build_type_params(type_params->get());
        }
        if (const auto param_list = borrow(context.paramList())) {
            declaration->params = build_param_list(param_list->get());
        }
        if (const auto return_type = borrow(context.type_())) {
            declaration->return_type = build_type_syntax(return_type->get());
        }
        if (const auto effect_clause = borrow(context.effectClause())) {
            declaration->effect_clause = build_effect_clause(effect_clause->get());
        }
        if (const auto where_clause = borrow(context.whereClause())) {
            declaration->where_clause = build_where_clause(where_clause->get());
        }

        // Body is mandatory for normal impl methods; the `;` prototype
        // shorthand is accepted only when the method carries a @builtin
        // attribute so the compiler can synthesise lowering to the C++ hook
        // directly (matching module-level `@builtin(...) fn name(...);`).
        if (const auto body = borrow(context.fnBody())) {
            declaration->body = build_block_syntax(
                require(body->get().block(), "impl fn body block is missing"));
        } else {
            // Optional semicolon alternative: body is left nullptr so the
            // semantic pass can unambiguously detect the prototype-shape
            // @builtin impl method and skip body typecheck.
            declaration->body = nullptr;
        }

        return declaration;
    }

    [[nodiscard]] std::vector<Owned<ast::TypeParamSyntax>>
    build_type_params(AHFLParser::TypeParamsContext &context) const {
        std::vector<Owned<ast::TypeParamSyntax>> type_params;
        type_params.reserve(context.typeParam().size());
        for (auto *param_context : context.typeParam()) {
            type_params.push_back(
                build_type_param(require(param_context, "type parameter is missing")));
        }
        return type_params;
    }

    [[nodiscard]] Owned<ast::TypeParamSyntax>
    build_type_param(AHFLParser::TypeParamContext &context) const {
        auto type_param = make_owned<ast::TypeParamSyntax>();
        type_param->range = context_range(context, source_);
        type_param->name = text_of(require(context.IDENT(), "type parameter name is missing"));
        if (const auto bound_list = borrow(context.typeBoundList())) {
            // typeBoundList: type_ ('+' type_)* — antlr generates a vector type_()
            // accessor (each bound is a direct type_ child), so we do not pass
            // an index (pitfall #1: vector accessors are index-free).
            for (auto *bound_context : bound_list->get().type_()) {
                type_param->bounds.push_back(
                    build_type_syntax(require(bound_context, "type bound is missing")));
            }
        }
        return type_param;
    }

    [[nodiscard]] Owned<ast::EffectClauseSyntax>
    build_effect_clause(AHFLParser::EffectClauseContext &context) const {
        auto clause = make_owned<ast::EffectClauseSyntax>();
        clause->range = context_range(context, source_);

        // effectClause delegates to a single effectSpec. effectSpec is one of:
        //   'Pure' | 'Nondet' | capabilityRef (',' capabilityRef)*
        // We discriminate by the spec's start token text rather than by
        // generated literal-token accessors (which would couple us to ANTLR's
        // implicit token naming).
        auto &spec = require(context.effectSpec(), "effect spec is missing");
        const auto spec_kind = effect_clause_kind_from(
            require(spec.getStart(), "effect spec start token is missing").getText());
        clause->kind = spec_kind;
        if (spec_kind == ast::EffectClauseKind::Capability) {
            for (auto *capability_context : spec.capabilityRef()) {
                clause->capabilities.push_back(build_qualified_name(require(
                    require(capability_context, "capability reference is missing").qualifiedIdent(),
                    "capability reference name is missing")));
            }
        }
        // P4a (RFC §3.1): optional `decreases` termination measure.
        if (const auto decreases_ctx = context.decreasesClause()) {
            clause->decreases_expr = build_expr_syntax(
                require(decreases_ctx->expr(), "decreases measure expression is missing"));
        }
        return clause;
    }

    [[nodiscard]] ast::EffectClauseKind effect_clause_kind_from(std::string_view spelling) const {
        if (spelling == "Pure") {
            return ast::EffectClauseKind::Pure;
        }
        if (spelling == "Nondet") {
            return ast::EffectClauseKind::Nondet;
        }
        return ast::EffectClauseKind::Capability;
    }

    [[nodiscard]] Owned<ast::WhereClauseSyntax>
    build_where_clause(AHFLParser::WhereClauseContext &context) const {
        auto clause = make_owned<ast::WhereClauseSyntax>();
        clause->range = context_range(context, source_);
        for (auto *constraint_context : context.whereConstraint()) {
            clause->constraints.push_back(
                build_where_constraint(require(constraint_context, "where constraint is missing")));
        }
        return clause;
    }

    [[nodiscard]] Owned<ast::WhereConstraintSyntax>
    build_where_constraint(AHFLParser::WhereConstraintContext &context) const {
        auto constraint = make_owned<ast::WhereConstraintSyntax>();
        constraint->range = context_range(context, source_);

        // whereConstraint has two alternatives:
        //   type_ '::' identifier ('(' typeList ')')?   — predicate
        //   type_ ':' typeBoundList                     — bound list
        // The bound list (`:` alt) carries a single typeBoundList child; the
        // predicate (`::` alt) carries an identifier and an optional typeList.
        if (const auto bound_list = borrow(context.typeBoundList())) {
            constraint->is_predicate = false;
            constraint->subject = build_type_syntax(
                require(context.type_(), "where-constraint subject type is missing"));
            for (auto *bound_context : bound_list->get().type_()) {
                constraint->bounds.push_back(
                    build_type_syntax(require(bound_context, "where bound type is missing")));
            }
        } else {
            constraint->is_predicate = true;
            constraint->subject = build_type_syntax(
                require(context.type_(), "where-constraint subject type is missing"));
            constraint->trait_name = identifier_text(
                require(context.identifier(), "where-predicate trait name is missing"));
            if (const auto type_list = borrow(context.typeList())) {
                for (auto *argument_context : type_list->get().type_()) {
                    constraint->arguments.push_back(build_type_syntax(
                        require(argument_context, "where-predicate argument is missing")));
                }
            }
        }
        return constraint;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_expr_syntax(AHFLParser::ConstExprContext &context) const {
        return build_expr_syntax(require(context.expr(), "const expression is missing"));
    }

    // P3 impl-migration: Disambiguate the `<`/`>` diamond at parse time.
    //
    // The grammar `postfixExpr: primaryExpr ('.' identifier ('<' typeList '>')? '(' exprList? ')')*`
    // and `compareExpr: addExpr (('<' | '>' | ...) addExpr)*` create a true 2-parse ambiguity
    // when the method has EXACTLY 1 generic type arg AND EXACTLY 1 value argument:
    //     receiver.method<SingleTypeArg>(singleValueArg)
    // parses as the comparison chain:
    //     (receiver.method < SingleTypeArg) > (singleValueArg)
    // because `(singleValueArg)` is itself a valid `(' expr ')'` parenthesised primary.
    //
    // Multi-type-arg and multi-arg calls never suffer this: `<T, U>` contains a comma that
    // breaks the comparison chain, and `(a, b)` is not a valid `(expr)` grouping. Nested
    // generics such as `<Wrap<U>>` are also not reachable as comparison chains because the
    // inner `<`/`>` consume the lookahead before the outer `>` can pair with the outer `<`.
    //
    // We repair this purely at AST-reconstruction time: a recursive post-order pass over
    // the just-built expression rewrites every `LT(_, GT(_, Group(_)))` shape into a
    // MethodCall with one type arg and one value arg whenever the left-operand chain is a
    // member-access (`.method`) on something.
    [[nodiscard]] Owned<ast::ExprSyntax>
    rewrite_diamond_ambiguity(Owned<ast::ExprSyntax> expr) const {
        if (expr == nullptr) return expr;

        // Recurse first so children are repaired before we inspect them.

        // Recurse first so children are repaired before we inspect them.
        std::visit(Overloaded{
                       [](auto &) { /* no-op for leaf nodes */ },
                       [this](ast::UnaryExpr &u) {
                           u.operand = rewrite_diamond_ambiguity(std::move(u.operand));
                       },
                       [this](ast::BinaryExpr &b) {
                           b.lhs = rewrite_diamond_ambiguity(std::move(b.lhs));
                           b.rhs = rewrite_diamond_ambiguity(std::move(b.rhs));
                       },
                       [this](ast::CallExpr &c) {
                           for (auto &a : c.arguments)
                               a = rewrite_diamond_ambiguity(std::move(a));
                       },
                       [this](ast::MethodCallExpr &m) {
                           m.receiver = rewrite_diamond_ambiguity(std::move(m.receiver));
                           for (auto &a : m.arguments)
                               a = rewrite_diamond_ambiguity(std::move(a));
                       },
                       [this](ast::MemberAccessExpr &ma) {
                           ma.base = rewrite_diamond_ambiguity(std::move(ma.base));
                       },
                       [this](ast::IndexAccessExpr &ia) {
                           ia.base = rewrite_diamond_ambiguity(std::move(ia.base));
                           ia.index = rewrite_diamond_ambiguity(std::move(ia.index));
                       },
                       [this](ast::GroupExpr &g) {
                           g.inner = rewrite_diamond_ambiguity(std::move(g.inner));
                       },
                   },
                   expr->node);

        // The `<` / `>` comparison chain is left-associative: for tokens
        //     operand_0 `<` operand_1 `>` operand_2
        // build_expr_chain produces:
        //     Binary(GT, Binary(LT, operand_0, operand_1), operand_2)
        //
        // So the diamond `receiver.method < TypeIdent > (value_arg)` is the shape
        //     outer = Binary(Greater, inner, Group(value))
        //     inner = Binary(Less,    lhs_member_or_path, type_expr)
        // which we rewrite into MethodCall(receiver, method, [Type], [value]).
        if (!expr->is<ast::BinaryExpr>()) return expr;
        auto &outer = expr->as<ast::BinaryExpr>();
        if (outer.op != ast::ExprBinaryOp::Greater) return expr;

        if (outer.lhs == nullptr || !outer.lhs->is<ast::BinaryExpr>()) return expr;
        auto &inner = outer.lhs->as<ast::BinaryExpr>();
        if (inner.op != ast::ExprBinaryOp::Less) return expr;

        if (outer.rhs == nullptr || !outer.rhs->is<ast::GroupExpr>()) return expr;
        auto &group = outer.rhs->as<ast::GroupExpr>();
        if (group.inner == nullptr) return expr;

        // The LHS of the inner `<` (the type argument expression) must be a PathExpr
        // (a simple identifier path, possibly qualified, that we can reinterpret as a
        // NamedType in the type-argument slot).
        if (inner.rhs == nullptr || !inner.rhs->is<ast::PathExpr>()) return expr;
        auto &type_path = inner.rhs->as<ast::PathExpr>();
        if (type_path.path == nullptr) return expr;

        // Convert the PathSyntax into a NamedType.
        auto type_syntax = make_owned<ast::TypeSyntax>();
        type_syntax->range = inner.rhs->range;
        ast::NamedType named;
        named.name = make_owned<ast::QualifiedName>();
        named.name->range = inner.rhs->range;
        named.name->segments.push_back(type_path.path->root_name);
        for (const auto &seg : type_path.path->members) {
            named.name->segments.push_back(seg);
        }
        type_syntax->node = std::move(named);

        // Case 1: LHS of the inner `<` is a MemberAccessExpr (postfix '.'
        // consumed the method name on the expression side).
        if (inner.lhs != nullptr && inner.lhs->is<ast::MemberAccessExpr>()) {
            auto &member = inner.lhs->as<ast::MemberAccessExpr>();
            if (member.base == nullptr) return expr;
            auto method_call = make_expr_syntax(ast::ExprSyntaxKind::MethodCall,
                                                span_range(inner.lhs->range, outer.rhs->range));
            auto &call = method_call->as<ast::MethodCallExpr>();
            call.receiver = std::move(member.base);
            call.method = member.member;
            call.type_args.push_back(std::move(type_syntax));
            call.arguments.push_back(std::move(group.inner));
            return method_call;
        }

        // Case 2: LHS of the inner `<` is a PathExpr whose `('.', IDENT)*`
        // loop greedily consumed `.method` (or `.field.method` etc.) before
        // the comparison layer ever got to see the `<` token. Peel the LAST
        // segment off the path as the method name and keep the prefix as the
        // receiver PathExpr.
        if (inner.lhs != nullptr && inner.lhs->is<ast::PathExpr>()) {
            auto &lhs_path = inner.lhs->as<ast::PathExpr>();
            if (lhs_path.path == nullptr) return expr;
            const std::size_t member_count = lhs_path.path->members.size();
            if (member_count == 0) return expr; // bare identifier: no `.` → not a method call

            Owned<ast::ExprSyntax> receiver;
            if (member_count == 1) {
                auto root_path = make_expr_syntax(ast::ExprSyntaxKind::Path, lhs_path.path->range);
                auto &root = root_path->as<ast::PathExpr>();
                root.path = make_owned<ast::PathSyntax>();
                root.path->range = lhs_path.path->range;
                root.path->root_kind = lhs_path.path->root_kind;
                root.path->root_name = lhs_path.path->root_name;
                receiver = std::move(root_path);
            } else {
                auto base_path = make_expr_syntax(ast::ExprSyntaxKind::Path, lhs_path.path->range);
                auto &base = base_path->as<ast::PathExpr>();
                base.path = make_owned<ast::PathSyntax>();
                base.path->range = lhs_path.path->range;
                base.path->root_kind = lhs_path.path->root_kind;
                base.path->root_name = lhs_path.path->root_name;
                base.path->members.insert(base.path->members.end(),
                                          lhs_path.path->members.begin(),
                                          lhs_path.path->members.end() - 1);
                receiver = std::move(base_path);
            }

            const std::string method = lhs_path.path->members.back();
            auto method_call = make_expr_syntax(ast::ExprSyntaxKind::MethodCall,
                                                span_range(inner.lhs->range, outer.rhs->range));
            auto &call = method_call->as<ast::MethodCallExpr>();
            call.receiver = std::move(receiver);
            call.method = method;
            call.type_args.push_back(std::move(type_syntax));
            call.arguments.push_back(std::move(group.inner));
            return method_call;
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax> build_expr_syntax(AHFLParser::ExprContext &context) const {
        auto guard = RecursionDepthGuard{*this, "expression", context_range(context, source_)};
        return rewrite_diamond_ambiguity(
            build_implies_expr(require(context.impliesExpr(), "expression body is missing")));
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
        auto guard = RecursionDepthGuard{*this, "block", context_range(context, source_)};
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
        expr->range = range;
        expr->node_id = ++next_expr_id_;
        expr->text = source_text(source_, range);

        // Initialize node variant with the correct default-constructed alternative
        switch (kind) {
        case ast::ExprSyntaxKind::BoolLiteral:
            expr->node = ast::BoolLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::IntegerLiteral:
            expr->node = ast::IntegerLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::FloatLiteral:
            expr->node = ast::FloatLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::DecimalLiteral:
            expr->node = ast::DecimalLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::StringLiteral:
            expr->node = ast::StringLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::DurationLiteral:
            expr->node = ast::DurationLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::Path:
            expr->node = ast::PathExpr{};
            break;
        case ast::ExprSyntaxKind::QualifiedValue:
            expr->node = ast::QualifiedValueExpr{};
            break;
        case ast::ExprSyntaxKind::Call:
            expr->node = ast::CallExpr{};
            break;
        case ast::ExprSyntaxKind::MethodCall:
            expr->node = ast::MethodCallExpr{};
            break;
        case ast::ExprSyntaxKind::StructLiteral:
            expr->node = ast::StructLiteralExpr{};
            break;
        case ast::ExprSyntaxKind::Unary:
            expr->node = ast::UnaryExpr{};
            break;
        case ast::ExprSyntaxKind::Binary:
            expr->node = ast::BinaryExpr{};
            break;
        case ast::ExprSyntaxKind::MemberAccess:
            expr->node = ast::MemberAccessExpr{};
            break;
        case ast::ExprSyntaxKind::IndexAccess:
            expr->node = ast::IndexAccessExpr{};
            break;
        case ast::ExprSyntaxKind::Group:
            expr->node = ast::GroupExpr{};
            break;
        case ast::ExprSyntaxKind::Match:
            expr->node = ast::MatchExpr{};
            break;
        case ast::ExprSyntaxKind::Lambda:
            expr->node = ast::LambdaExpr{};
            break;
        case ast::ExprSyntaxKind::UnwrapExpr:
            expr->node = ast::UnwrapExprSyntax{};
            break;
        default:
            throw std::logic_error("unhandled ExprSyntaxKind in make_expr_syntax");
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::TemporalExprSyntax>
    make_temporal_expr_syntax(ast::TemporalExprSyntaxKind kind, SourceRange range) const {
        auto expr = make_owned<ast::TemporalExprSyntax>();
        expr->range = range;

        // Initialize node variant with the correct default-constructed alternative
        switch (kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            expr->node = ast::EmbeddedTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::Called:
            expr->node = ast::CalledTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::InState:
            expr->node = ast::InStateTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::Running:
            expr->node = ast::RunningTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::Completed:
            expr->node = ast::CompletedTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::Unary:
            expr->node = ast::UnaryTemporalExpr{};
            break;
        case ast::TemporalExprSyntaxKind::Binary:
            expr->node = ast::BinaryTemporalExpr{};
            break;
        default:
            throw std::logic_error("unhandled TemporalExprSyntaxKind in make_temporal_expr_syntax");
        }

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
                auto &binary_node = std::get<ast::BinaryExpr>(binary->node);
                binary_node.op = expr_binary_op_from(operators[index]);
                binary_node.lhs = std::move(lhs);
                binary_node.rhs = std::move(result);
                result = std::move(binary);
            }

            return result;
        }

        auto result = build_operand(require(operands.front(), "expression operand is missing"));

        for (std::size_t index = 0; index < operators.size(); ++index) {
            auto rhs = build_operand(require(operands[index + 1], "expression operand is missing"));
            auto binary = make_expr_syntax(ast::ExprSyntaxKind::Binary,
                                           span_range(result->range, rhs->range));
            auto &binary_node = std::get<ast::BinaryExpr>(binary->node);
            binary_node.op = expr_binary_op_from(operators[index]);
            binary_node.lhs = std::move(result);
            binary_node.rhs = std::move(rhs);
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
                auto &binary_node = std::get<ast::BinaryTemporalExpr>(binary->node);
                binary_node.op = temporal_binary_op_from(operators[index]);
                binary_node.lhs = std::move(lhs);
                binary_node.rhs = std::move(result);
                result = std::move(binary);
            }

            return result;
        }

        auto result = build_operand(require(operands.front(), "temporal operand is missing"));

        for (std::size_t index = 0; index < operators.size(); ++index) {
            auto rhs = build_operand(require(operands[index + 1], "temporal operand is missing"));
            auto binary = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Binary,
                                                    span_range(result->range, rhs->range));
            auto &binary_node = std::get<ast::BinaryTemporalExpr>(binary->node);
            binary_node.op = temporal_binary_op_from(operators[index]);
            binary_node.lhs = std::move(result);
            binary_node.rhs = std::move(rhs);
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
            auto &unary_node = std::get<ast::UnaryExpr>(expr->node);
            unary_node.op = expr_unary_op_from(
                require(context.getStart(), "unary operator is missing").getText());
            unary_node.operand = build_unary_expr(inner->get());
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
        std::size_t type_list_index = 0;
        std::size_t expr_list_index = 0;

        while (child_index < context.children.size()) {
            const auto token_text =
                require(context.children[child_index], "postfix child is missing").getText();

            if (token_text == ".") {
                const auto member_name = identifier_text(
                    require(context.identifier(ident_index), "member name is missing"));
                const auto member_range = context_range(
                    require(context.identifier(ident_index), "member name is missing"), source_);
                ++ident_index;

                child_index += 2;
                std::vector<Owned<ast::TypeSyntax>> type_args;
                if (child_index < context.children.size() &&
                    require(context.children[child_index], "postfix child is missing").getText() ==
                        "<") {
                    auto &type_list =
                        require(context.typeList(type_list_index), "method type args are missing");
                    for (auto *type_context : type_list.type_()) {
                        type_args.push_back(build_type_syntax(
                            require(type_context, "method type argument is missing")));
                    }
                    ++type_list_index;
                    child_index += 3;
                }

                if (child_index < context.children.size() &&
                    require(context.children[child_index], "postfix child is missing").getText() ==
                        "(") {
                    const auto open_index = child_index;
                    const bool has_arguments =
                        open_index + 1 < context.children.size() &&
                        require(context.children[open_index + 1], "method call child is missing")
                                .getText() != ")";
                    std::vector<Owned<ast::ExprSyntax>> arguments;
                    std::size_t close_index = open_index + 1;
                    if (has_arguments) {
                        arguments = build_expr_list(require(context.exprList(expr_list_index),
                                                            "method arguments are missing"));
                        ++expr_list_index;
                        close_index = open_index + 2;
                    }

                    auto &close_child =
                        require(context.children[close_index], "method call close is missing");
                    auto *close_terminal = dynamic_cast<antlr4::tree::TerminalNode *>(&close_child);
                    const auto close_range = close_terminal != nullptr
                                                 ? terminal_range(*close_terminal, source_)
                                                 : result->range;
                    auto method_call = make_expr_syntax(ast::ExprSyntaxKind::MethodCall,
                                                        span_range(result->range, close_range));
                    auto &method_node = std::get<ast::MethodCallExpr>(method_call->node);
                    method_node.receiver = std::move(result);
                    method_node.method = member_name;
                    method_node.type_args = std::move(type_args);
                    method_node.arguments = std::move(arguments);
                    result = std::move(method_call);
                    child_index = close_index + 1;
                    continue;
                }

                auto member = make_expr_syntax(ast::ExprSyntaxKind::MemberAccess,
                                               span_range(result->range, member_range));
                auto &member_node = std::get<ast::MemberAccessExpr>(member->node);
                member_node.base = std::move(result);
                member_node.member = member_name;
                result = std::move(member);
                continue;
            }

            if (token_text == "[") {
                auto index_expr = build_expr_syntax(
                    require(context.expr(expr_index), "index expression is missing"));
                auto index_access = make_expr_syntax(ast::ExprSyntaxKind::IndexAccess,
                                                     span_range(result->range, index_expr->range));
                auto &idx_node = std::get<ast::IndexAccessExpr>(index_access->node);
                idx_node.base = std::move(result);
                idx_node.index = std::move(index_expr);
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

        if (const auto unwrap_expr = borrow(context.unwrapExpr())) {
            return build_unwrap_expr(unwrap_expr->get());
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
            std::get<ast::PathExpr>(expr->node).path = build_path_syntax(path->get());
            return expr;
        }

        if (const auto match = borrow(context.matchExpr())) {
            return build_match_expr(match->get());
        }

        if (const auto lambda = borrow(context.lambdaExpr())) {
            return build_lambda_expr(lambda->get());
        }

        if (const auto inner_expr = borrow(context.expr())) {
            auto expr =
                make_expr_syntax(ast::ExprSyntaxKind::Group, context_range(context, source_));
            std::get<ast::GroupExpr>(expr->node).inner = build_expr_syntax(inner_expr->get());
            return expr;
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
            std::get<ast::BoolLiteralExpr>(expr->node).value = (literal_text == "true");
            return expr;
        }

        if (const auto integer_literal = borrow(context.integerLiteral())) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::IntegerLiteral, range);
            std::get<ast::IntegerLiteralExpr>(expr->node).literal =
                build_integer_syntax(integer_literal->get());
            return expr;
        }

        if (borrow(context.floatLiteral()).has_value()) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::FloatLiteral, range);
            std::get<ast::FloatLiteralExpr>(expr->node).spelling = expr->text;
            return expr;
        }

        if (borrow(context.decimalLiteral()).has_value()) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::DecimalLiteral, range);
            std::get<ast::DecimalLiteralExpr>(expr->node).spelling = expr->text;
            return expr;
        }

        if (borrow(context.stringLiteral()).has_value()) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::StringLiteral, range);
            std::get<ast::StringLiteralExpr>(expr->node).spelling = expr->text;
            return expr;
        }

        if (const auto duration_literal = borrow(context.durationLiteral())) {
            auto expr = make_expr_syntax(ast::ExprSyntaxKind::DurationLiteral, range);
            std::get<ast::DurationLiteralExpr>(expr->node).literal =
                build_duration_syntax(duration_literal->get());
            return expr;
        }

        throw std::logic_error("literal did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_call_expr(AHFLParser::CallExprContext &context) const {
        auto expr = make_expr_syntax(ast::ExprSyntaxKind::Call, context_range(context, source_));
        auto &call_node = std::get<ast::CallExpr>(expr->node);
        call_node.callee =
            build_qualified_name(require(context.callableName(), "call target is missing"));

        if (const auto type_list = borrow(context.typeList())) {
            for (auto *type_context : type_list->get().type_()) {
                call_node.type_args.push_back(
                    build_type_syntax(require(type_context, "call type argument is missing")));
            }
        }

        if (const auto arguments = borrow(context.exprList())) {
            call_node.arguments = build_expr_list(arguments->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_struct_literal_expr(AHFLParser::StructLiteralContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::StructLiteral, context_range(context, source_));
        auto &struct_node = std::get<ast::StructLiteralExpr>(expr->node);
        struct_node.type_name = build_qualified_name(
            require(context.qualifiedIdent(), "struct literal type is missing"));

        if (const auto init_list = borrow(context.structInitList())) {
            struct_node.fields = build_struct_init_list(init_list->get());
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_qualified_value_expr(AHFLParser::QualifiedValueExprContext &context) const {
        auto expr =
            make_expr_syntax(ast::ExprSyntaxKind::QualifiedValue, context_range(context, source_));
        std::get<ast::QualifiedValueExpr>(expr->node).name = build_qualified_name(context);
        return expr;
    }

    // ----------------------------------------------------------------------
    // P1 (ADT, RFC §1.6): `match` expression + patterns
    // ----------------------------------------------------------------------

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_match_expr(AHFLParser::MatchExprContext &context) const {
        auto expr = make_expr_syntax(ast::ExprSyntaxKind::Match, context_range(context, source_));
        auto &match_node = std::get<ast::MatchExpr>(expr->node);

        // matchExpr: 'match' expr '{' matchArm* '}' — the single direct expr
        // child is the scrutinee (antlr generates a single expr() accessor).
        match_node.scrutinee =
            build_expr_syntax(require(context.expr(), "match scrutinee is missing"));

        for (auto *arm_context : context.matchArm()) {
            match_node.arms.push_back(
                build_match_arm(require(arm_context, "match arm is missing")));
        }

        return expr;
    }

    [[nodiscard]] Owned<ast::MatchArmSyntax>
    build_match_arm(AHFLParser::MatchArmContext &context) const {
        auto arm = make_owned<ast::MatchArmSyntax>();
        arm->range = context_range(context, source_);
        arm->pattern = build_pattern(require(context.pattern(), "match arm pattern is missing"));

        // Optional guard: `if expr`. The grammar emits at most one `expr`
        // before the `=>`; `context.expr()` returns [guard?, body] in source
        // order, so the guard (when present) is the first expr and the body
        // is the last.
        const auto exprs = context.expr();
        if (exprs.size() > 1) {
            arm->guard =
                build_expr_syntax(require(exprs.front(), "match arm guard expression is missing"));
        }

        arm->body =
            build_expr_syntax(require(exprs.back(), "match arm body expression is missing"));
        return arm;
    }

    // ----------------------------------------------------------------------
    // P2 (RFC §6): lambda (closure) expression builder
    // ----------------------------------------------------------------------

    [[nodiscard]] Owned<ast::ExprSyntax>
    build_lambda_expr(AHFLParser::LambdaExprContext &context) const {
        auto expr = make_expr_syntax(ast::ExprSyntaxKind::Lambda, context_range(context, source_));
        auto &lambda_node = std::get<ast::LambdaExpr>(expr->node);

        // lambdaExpr: BACKSLASH lambdaParamList? '->' expr
        // The single direct `expr` child is the body; lambdaParamList is the
        // optional parameter list (absent for a zero-arg thunk `\ -> expr`).
        if (const auto param_list = borrow(context.lambdaParamList())) {
            lambda_node.params = build_lambda_param_list(param_list->get());
        }

        lambda_node.body = build_expr_syntax(require(context.expr(), "lambda body is missing"));
        return expr;
    }

    [[nodiscard]] std::vector<Owned<ast::LambdaParamSyntax>>
    build_lambda_param_list(AHFLParser::LambdaParamListContext &context) const {
        std::vector<Owned<ast::LambdaParamSyntax>> params;

        // lambdaParamList: lambdaParam | '(' (lambdaParam (',' lambdaParam)*)? ')'
        // The parenthesised alt carries zero or more lambdaParam children; the
        // bare lambdaParam alt carries a single parameter with an optional type
        // annotation.
        const auto named_params = context.lambdaParam();
        if (!named_params.empty()) {
            params.reserve(named_params.size());
            for (auto *param_context : named_params) {
                params.push_back(
                    build_lambda_param(require(param_context, "lambda parameter is missing")));
            }
            return params;
        }

        return params;
    }

    [[nodiscard]] Owned<ast::LambdaParamSyntax>
    build_lambda_param(AHFLParser::LambdaParamContext &context) const {
        auto param = make_owned<ast::LambdaParamSyntax>();
        param->range = context_range(context, source_);
        param->name = text_of(require(context.IDENT(), "lambda parameter name is missing"));
        if (const auto type_annotation = borrow(context.type_())) {
            param->type = build_type_syntax(type_annotation->get());
        }
        return param;
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_pattern(AHFLParser::PatternContext &context) const {
        auto guard = RecursionDepthGuard{*this, "pattern", context_range(context, source_)};
        return build_or_pattern(require(context.orPattern(), "pattern body is missing"));
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_or_pattern(AHFLParser::OrPatternContext &context) const {
        const auto &concat_contexts = context.concatPattern();
        if (concat_contexts.size() == 1) {
            return build_concat_pattern(
                require(concat_contexts.front(), "pattern alternative is missing"));
        }

        auto pattern = make_owned<ast::PatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->text = source_text(source_, pattern->range);
        pattern->node = ast::OrPattern{};
        auto &or_node = std::get<ast::OrPattern>(pattern->node);
        for (auto *concat_context : concat_contexts) {
            or_node.branches.push_back(
                build_concat_pattern(require(concat_context, "or-pattern branch is missing")));
        }
        return pattern;
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_concat_pattern(AHFLParser::ConcatPatternContext &context) const {
        if (const auto literal = borrow(context.literalPattern())) {
            return build_literal_pattern(literal->get());
        }
        if (const auto variant = borrow(context.variantPattern())) {
            return build_variant_pattern(variant->get());
        }
        if (borrow(context.wildcardPattern()).has_value()) {
            auto pattern = make_owned<ast::PatternSyntax>();
            pattern->range = context_range(context, source_);
            pattern->text = source_text(source_, pattern->range);
            pattern->node = ast::WildcardPattern{};
            return pattern;
        }
        if (const auto binding = borrow(context.bindingPattern())) {
            return build_binding_pattern(binding->get());
        }
        if (const auto tuple = borrow(context.tuplePattern())) {
            return build_tuple_pattern(tuple->get());
        }

        throw std::logic_error("pattern did not match any supported AHFL kind");
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_literal_pattern(AHFLParser::LiteralPatternContext &context) const {
        auto pattern = make_owned<ast::PatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->text = source_text(source_, pattern->range);
        pattern->node = ast::LiteralPattern{.spelling = pattern->text};
        return pattern;
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_variant_pattern(AHFLParser::VariantPatternContext &context) const {
        auto pattern = make_owned<ast::PatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->text = source_text(source_, pattern->range);
        pattern->node = ast::VariantPattern{};
        auto &variant_node = std::get<ast::VariantPattern>(pattern->node);
        variant_node.path = build_qualified_name(context.IDENT(), pattern->range);

        // Optional positional sub-patterns: `Some(x, _)`.
        if (const auto pattern_list = borrow(context.patternList())) {
            for (auto *sub_context : pattern_list->get().pattern()) {
                variant_node.subpatterns.push_back(
                    build_pattern(require(sub_context, "variant sub-pattern is missing")));
            }
        }
        return pattern;
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_binding_pattern(AHFLParser::BindingPatternContext &context) const {
        auto pattern = make_owned<ast::PatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->text = source_text(source_, pattern->range);
        pattern->node = ast::BindingPattern{};
        auto &binding_node = std::get<ast::BindingPattern>(pattern->node);
        binding_node.is_mut =
            !context.children.empty() &&
            require(context.children.front(), "binding prefix is missing").getText() == "mut";
        binding_node.name = text_of(require(context.IDENT(), "binding name is missing"));

        // Optional `@`-bound nested pattern.
        if (const auto nested = borrow(context.concatPattern())) {
            binding_node.nested = build_concat_pattern(nested->get());
        }
        return pattern;
    }

    [[nodiscard]] Owned<ast::PatternSyntax>
    build_tuple_pattern(AHFLParser::TuplePatternContext &context) const {
        auto pattern = make_owned<ast::PatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->text = source_text(source_, pattern->range);
        pattern->node = ast::TuplePattern{};
        auto &tuple_node = std::get<ast::TuplePattern>(pattern->node);

        if (const auto pattern_list = borrow(context.patternList())) {
            for (auto *elem_context : pattern_list->get().pattern()) {
                tuple_node.elements.push_back(
                    build_pattern(require(elem_context, "tuple pattern element is missing")));
            }
        }
        return pattern;
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
        auto guard = RecursionDepthGuard{*this, "statement", context_range(context, source_)};
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

        if (const auto if_let_stmt = borrow(context.ifLetStmt())) {
            statement->kind = ast::StatementSyntaxKind::IfLet;
            statement->if_let_stmt = build_if_let_stmt(if_let_stmt->get());
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

        if (const auto unwrap_stmt = borrow(context.unwrapStmt())) {
            statement->kind = ast::StatementSyntaxKind::Unwrap;
            statement->unwrap_stmt = build_unwrap_stmt(unwrap_stmt->get());
            return statement;
        }

        if (const auto requires_stmt = borrow(context.requiresStmt())) {
            statement->kind = ast::StatementSyntaxKind::Requires;
            statement->requires_stmt = build_requires_stmt(requires_stmt->get());
            return statement;
        }

        if (const auto unreachable_stmt = borrow(context.unreachableStmt())) {
            statement->kind = ast::StatementSyntaxKind::Unreachable;
            statement->unreachable_stmt = build_unreachable_stmt(unreachable_stmt->get());
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

    [[nodiscard]] Owned<ast::IfLetPatternSyntax>
    build_if_let_pattern(AHFLParser::IfLetPatternContext &context) const {
        auto pattern = make_owned<ast::IfLetPatternSyntax>();
        pattern->range = context_range(context, source_);
        pattern->variant_name = text_of(require(context.variant, "if let pattern variant is missing"));
        for (const auto &var_ctx : context.ifLetPatternVar()) {
            if (!var_ctx) {
                continue;
            }
            auto &ident_token = require(var_ctx->IDENT(), "if let pattern binding is missing");
            pattern->bindings.push_back(text_of(ident_token));
        }
        return pattern;
    }

    [[nodiscard]] Owned<ast::IfLetStmtSyntax>
    build_if_let_stmt(AHFLParser::IfLetStmtContext &context) const {
        auto statement = make_owned<ast::IfLetStmtSyntax>();
        statement->range = context_range(context, source_);
        statement->pattern = build_if_let_pattern(
            require(context.iflet_pattern, "if let pattern section is missing"));
        statement->scrutinee =
            build_expr_syntax(require(context.expr(), "if let scrutinee is missing"));
        statement->then_block = build_block_syntax(
            require(context.thenBlock, "if let then block is missing"));
        if (context.elseBlock != nullptr) {
            statement->else_block = build_block_syntax(
                require(context.elseBlock, "if let else block is missing"));
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

        // Two grammatical forms share this builder (grammar AHFL.g4 assertStmt):
        //   (a) 'assert' '(' exprList? ')' ';'   -- new arity-2 form
        //   (b) 'assert' expr ';'                -- legacy arity-1 form (backward-compat)
        // Both normalize to AssertStmtSyntax; arity-validation is deferred to the
        // typechecker so a single, stable `typecheck.WRONG_ARITY` code is produced.
        if (const auto list = borrow(context.exprList())) {
            const auto exprs = list->get().expr();
            statement->raw_arg_count = exprs.size();
            if (!exprs.empty()) {
                statement->condition = build_expr_syntax(require(exprs[0], "assert condition is missing"));
            }
            if (exprs.size() >= 2) {
                statement->message = build_expr_syntax(require(exprs[1], "assert message is missing"));
            }
            // 3+ arguments are intentionally preserved as-is (via raw_arg_count)
            // so the typechecker emits a WrongArity diagnostic referencing the
            // real user-visible count.
        } else if (const auto bare = borrow(context.expr())) {
            // Legacy `assert cond;` form (no parens).
            statement->condition = build_expr_syntax(bare->get());
            statement->raw_arg_count = 1;
        }
        return statement;
    }

    [[nodiscard]] Owned<ast::UnwrapStmtSyntax>
    build_unwrap_stmt(AHFLParser::UnwrapStmtContext &context) const {
        auto statement = make_owned<ast::UnwrapStmtSyntax>();
        statement->range = context_range(context, source_);
        if (const auto list = borrow(context.exprList())) {
            const auto exprs = list->get().expr();
            statement->raw_arg_count = exprs.size();
            if (!exprs.empty()) {
                statement->operand = build_expr_syntax(require(exprs[0], "unwrap operand is missing"));
            }
        }
        return statement;
    }

    // P4-02: expression-level `unwrap(operand)` — mirrors the statement form's
    // operand-builder but stores into an UnwrapExprSyntax so `let x = unwrap(o)`
    // produces a T-typed right-hand side instead of discarding the payload.
    [[nodiscard]] Owned<ast::ExprSyntax>
    build_unwrap_expr(AHFLParser::UnwrapExprContext &context) const {
        auto expr = make_expr_syntax(ast::ExprSyntaxKind::UnwrapExpr,
                                     context_range(context, source_));
        auto &unwrap = std::get<ast::UnwrapExprSyntax>(expr->node);
        unwrap.operand =
            build_expr_syntax(require(context.expr(), "unwrap operand is missing"));
        return expr;
    }

    [[nodiscard]] Owned<ast::RequiresStmtSyntax>
    build_requires_stmt(AHFLParser::RequiresStmtContext &context) const {
        auto statement = make_owned<ast::RequiresStmtSyntax>();
        statement->range = context_range(context, source_);
        if (const auto list = borrow(context.exprList())) {
            const auto exprs = list->get().expr();
            statement->raw_arg_count = exprs.size();
            if (!exprs.empty()) {
                statement->condition = build_expr_syntax(require(exprs[0], "requires condition is missing"));
            }
            if (exprs.size() >= 2) {
                statement->message = build_expr_syntax(require(exprs[1], "requires message is missing"));
            }
        }
        return statement;
    }

    [[nodiscard]] Owned<ast::UnreachableStmtSyntax>
    build_unreachable_stmt(AHFLParser::UnreachableStmtContext &context) const {
        auto statement = make_owned<ast::UnreachableStmtSyntax>();
        statement->range = context_range(context, source_);
        if (const auto list = borrow(context.exprList())) {
            const auto exprs = list->get().expr();
            statement->raw_arg_count = exprs.size();
            if (!exprs.empty()) {
                statement->message = build_expr_syntax(require(exprs[0], "unreachable message is missing"));
            }
        }
        // `unreachable;` (no parentheses) reaches here with exprList()==nullptr
        // and raw_arg_count == 0, which is the correct legal arity-0 form.
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
            auto &unary_node = std::get<ast::UnaryTemporalExpr>(expr->node);
            unary_node.op = temporal_unary_op_from(
                require(context.getStart(), "temporal unary operator is missing").getText());
            unary_node.operand = build_temporal_unary_expr(inner->get());
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
            std::get<ast::EmbeddedTemporalExpr>(temporal->node).expr =
                build_expr_syntax(expr->get());
            return temporal;
        }

        const auto keyword =
            require(context.getStart(), "temporal atom start token is missing").getText();

        if (keyword == "called") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Called,
                                                      context_range(context, source_));
            std::get<ast::CalledTemporalExpr>(temporal->node).name =
                text_of(require(context.IDENT(0), "called() target is missing"));
            return temporal;
        }

        if (keyword == "in_state") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::InState,
                                                      context_range(context, source_));
            std::get<ast::InStateTemporalExpr>(temporal->node).name =
                text_of(require(context.IDENT(0), "in_state() value is missing"));
            return temporal;
        }

        if (keyword == "running") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Running,
                                                      context_range(context, source_));
            std::get<ast::RunningTemporalExpr>(temporal->node).name =
                text_of(require(context.IDENT(0), "running() value is missing"));
            return temporal;
        }

        if (keyword == "completed") {
            auto temporal = make_temporal_expr_syntax(ast::TemporalExprSyntaxKind::Completed,
                                                      context_range(context, source_));
            auto &completed = std::get<ast::CompletedTemporalExpr>(temporal->node);
            completed.name = text_of(require(context.IDENT(0), "completed() target is missing"));

            if (context.IDENT().size() > 1) {
                completed.state_name =
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
        auto guard = RecursionDepthGuard{*this, "type parameter", context_range(context, source_)};
        if (const auto primitive_type = borrow(context.primitiveType())) {
            return build_primitive_type_syntax(primitive_type->get());
        }

        if (const auto fn_type = borrow(context.fnType())) {
            return build_fn_type_syntax(fn_type->get());
        }

        auto type = make_owned<ast::TypeSyntax>();
        type->range = context_range(context, source_);

        const auto qualified_name =
            borrow(context.qualifiedIdent());
        if (!qualified_name) {
            throw std::logic_error("type did not match any supported AHFL type syntax kind");
        }

        ast::NamedType named{.name = build_qualified_name(qualified_name->get())};
        const auto &child_types = context.type_();
        named.type_args.reserve(child_types.size());
        for (auto *child : child_types) {
            named.type_args.push_back(build_type_syntax(
                require(child, "generic type argument is missing")));
        }
        type->node = std::move(named);
        return type;
    }

    [[nodiscard]] Owned<ast::TypeSyntax>
    build_primitive_type_syntax(AHFLParser::PrimitiveTypeContext &context) const {
        auto type = make_owned<ast::TypeSyntax>();
        type->range = context_range(context, source_);

        const auto primitive_keyword =
            require(context.getStart(), "primitive type start token is missing").getText();

        if (primitive_keyword == "Unit") {
            type->node = ast::UnitType{};
            return type;
        }

        if (primitive_keyword == "Bool") {
            type->node = ast::BoolType{};
            return type;
        }

        if (primitive_keyword == "Int") {
            type->node = ast::IntType{};
            return type;
        }

        if (primitive_keyword == "Float") {
            type->node = ast::FloatType{};
            return type;
        }

        if (primitive_keyword == "String") {
            if (context.INT_LITERAL().empty()) {
                type->node = ast::StringType{};
                return type;
            }

            auto min_val = parse_integer_literal(
                text_of(require(context.INT_LITERAL(0), "string minimum length is missing")));
            auto max_val = parse_integer_literal(
                text_of(require(context.INT_LITERAL(1), "string maximum length is missing")));
            type->node = ast::BoundedStringType{
                .min_length = static_cast<std::uint64_t>(min_val),
                .max_length = static_cast<std::uint64_t>(max_val),
            };
            return type;
        }

        if (primitive_keyword == "UUID") {
            type->node = ast::UuidType{};
            return type;
        }

        if (primitive_keyword == "Timestamp") {
            type->node = ast::TimestampType{};
            return type;
        }

        if (primitive_keyword == "Duration") {
            type->node = ast::DurationType{};
            return type;
        }

        if (primitive_keyword == "Decimal") {
            auto scale = parse_integer_literal(
                text_of(require(context.INT_LITERAL(0), "decimal scale is missing")));
            type->node = ast::DecimalType{.scale = static_cast<std::uint8_t>(scale)};
            return type;
        }

        throw std::logic_error("primitive type did not match any supported AHFL primitive");
    }

    [[nodiscard]] Owned<ast::TypeSyntax>
    build_fn_type_syntax(AHFLParser::FnTypeContext &context) const {
        auto type = make_owned<ast::TypeSyntax>();
        type->range = context_range(context, source_);

        ast::FnType fn_type;

        // Build parameter types from typeList.
        if (const auto type_list = borrow(context.typeList())) {
            for (auto *param_type_ctx : type_list->get().type_()) {
                fn_type.params.push_back(build_type_syntax(*param_type_ctx));
            }
        }

        // Optional return type (defaults to Unit if omitted).
        if (const auto return_ctx = borrow(context.type_())) {
            fn_type.return_type = build_type_syntax(return_ctx->get());
        }

        // Optional effect clause.
        if (const auto effect_spec_ctx = borrow(context.effectSpec())) {
            auto &spec = effect_spec_ctx->get();
            fn_type.has_effect_clause = true;

            const auto spec_kind = effect_clause_kind_from(
                require(spec.getStart(), "effect spec start token is missing").getText());
            fn_type.effect_kind = spec_kind;
            if (spec_kind == ast::EffectClauseKind::Capability) {
                for (auto *capability_context : spec.capabilityRef()) {
                    fn_type.effect_capabilities.push_back(build_qualified_name(
                        require(require(capability_context, "capability reference is missing")
                                    .qualifiedIdent(),
                                "capability reference name is missing")));
                }
            }
        }

        type->node = std::move(fn_type);
        return type;
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(AHFLParser::QualifiedIdentContext &context) const {
        return build_qualified_name(context.identifier(), context_range(context, source_));
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(AHFLParser::CallableNameContext &context) const {
        auto name = make_owned<ast::QualifiedName>();
        name->range = context_range(context, source_);
        for (auto *piece : context.callableNamePiece()) {
            // Intentionally discard the `require` result: the null-check side effect
            // (throws on null) is all we need here; dereferencing the returned
            // reference is equivalent to using `piece` directly for getText().
            (void)require(piece, "callable name segment is missing");
            name->segments.push_back(piece->getText());
        }
        return name;
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(AHFLParser::QualifiedValueExprContext &context) const {
        return build_qualified_name(context.identifier(), context_range(context, source_));
    }

    [[nodiscard]] Owned<ast::QualifiedName>
    build_qualified_name(const std::vector<AHFLParser::IdentifierContext *> &segments,
                         SourceRange range) const {
        auto name = make_owned<ast::QualifiedName>();
        name->range = range;

        for (auto *segment_context : segments) {
            name->segments.push_back(
                identifier_text(require(segment_context, "qualified name segment is missing")));
        }

        return name;
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

    [[nodiscard]] ast::CapabilityEffectKind
    capability_effect_kind_from(std::string_view spelling) const {
        if (spelling == "read") {
            return ast::CapabilityEffectKind::Read;
        }
        if (spelling == "external_side_effect") {
            return ast::CapabilityEffectKind::ExternalSideEffect;
        }
        if (spelling == "durable_write") {
            return ast::CapabilityEffectKind::DurableWrite;
        }
        if (spelling == "financial_write") {
            return ast::CapabilityEffectKind::FinancialWrite;
        }
        return ast::CapabilityEffectKind::Unknown;
    }

    [[nodiscard]] ast::CapabilityReceiptMode
    capability_receipt_mode_from(std::string_view spelling) const {
        if (spelling == "required") {
            return ast::CapabilityReceiptMode::Required;
        }
        if (spelling == "optional") {
            return ast::CapabilityReceiptMode::Optional;
        }
        return ast::CapabilityReceiptMode::None;
    }

    [[nodiscard]] ast::CapabilityRetryMode
    capability_retry_mode_from(std::string_view spelling) const {
        if (spelling == "safe") {
            return ast::CapabilityRetryMode::Safe;
        }
        if (spelling == "safe_if_idempotent") {
            return ast::CapabilityRetryMode::SafeIfIdempotent;
        }
        return ast::CapabilityRetryMode::Unsafe;
    }

    [[nodiscard]] Owned<ast::CapabilityEffectSyntax>
    build_capability_effect(AHFLParser::CapabilityEffectBlockContext &context) const {
        auto effect = make_owned<ast::CapabilityEffectSyntax>();
        effect->range = context_range(context, source_);

        for (auto *item_context : context.capabilityEffectItem()) {
            auto &item = require(item_context, "capability effect item is missing");
            const auto keyword =
                require(item.getStart(), "capability effect item keyword is missing").getText();

            if (keyword == "effect") {
                effect->effect_kind = capability_effect_kind_from(
                    require(item.capabilityEffectKind(), "capability effect kind is missing")
                        .getText());
                continue;
            }
            if (keyword == "domain") {
                effect->domain = build_qualified_name(
                    require(item.qualifiedIdent(), "capability effect domain is missing"));
                continue;
            }
            if (keyword == "idempotency") {
                effect->idempotency_key = build_path_syntax(
                    require(item.pathExpr(), "capability idempotency path is missing"));
                continue;
            }
            if (keyword == "receipt") {
                effect->receipt_mode = capability_receipt_mode_from(
                    require(item.capabilityReceiptMode(), "capability receipt mode is missing")
                        .getText());
                continue;
            }
            if (keyword == "retry") {
                effect->retry_mode = capability_retry_mode_from(
                    require(item.capabilityRetryMode(), "capability retry mode is missing")
                        .getText());
                continue;
            }
            if (keyword == "timeout") {
                effect->timeout = build_duration_syntax(
                    require(item.durationLiteral(), "capability timeout is missing"));
                continue;
            }
            if (keyword == "compensation") {
                effect->compensation = build_qualified_name(
                    require(item.qualifiedIdent(), "capability compensation is missing"));
                continue;
            }
            if (keyword == "policy") {
                effect->policies =
                    build_qualified_name_list_opt(borrow(item.qualifiedIdentListOpt()));
                continue;
            }

            throw std::logic_error("capability effect item did not match any supported kind");
        }

        return effect;
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

        // The grammar accepts five shapes:
        //   1. IDENT ':' type_                 — normal named parameter
        //   2. 'self'                          — bare self receiver
        //   3. 'mut' 'self'                    — bare mutable self receiver
        //   4. 'self' ':' type_                — typed self receiver
        //   5. 'mut' 'self' ':' type_          — typed mutable self receiver
        // Alts 2–5 are used inside trait/impl methods; semantic validation
        // (self must not appear outside trait/impl) is deferred to P3b.
        if (const auto ident = context.IDENT()) {
            // Shape 1: named parameter IDENT : type_
            param->name = text_of(require(ident, "parameter name is missing"));
            param->type = build_type_syntax(require(context.type_(), "parameter type is missing"));
            return param;
        }

        // Shapes 2–5: self receiver. Inspect the raw token sequence of the
        // rule context: the first literal is either 'self' or 'mut'; 'self'
        // is always the receiver keyword so we look for its presence via
        // a case-insensitive text scan of the rule's token span.
        const std::string raw = text_of(context);
        const bool has_mut = (raw.size() >= 3 && raw.substr(0, 3) == "mut");
        param->is_self = true;
        param->is_self_mut = has_mut;
        param->name = "self";
        if (const auto type_ctx = context.type_()) {
            // Shapes 4 & 5: explicit type annotation present
            param->type = build_type_syntax(require(type_ctx, "self param type is missing"));
        }
        // Shapes 2 & 3: bare self / mut self (type remains null — inferred in P3b)
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

        // --------------------------------------------------------------------
        // The grammar defines three labelled alternatives for enumVariant:
        //   * unitEnumVariant   — bare IDENT (no payload)
        //   * tupleEnumVariant  — IDENT ( typeList )
        //   * structEnumVariant — IDENT ( variantFieldList )  [RFC d-1 POC]
        //
        // We downcast to the concrete context type so each arm reads the
        // appropriate children without ambiguity.
        // --------------------------------------------------------------------
        if (auto *unit = dynamic_cast<AHFLParser::UnitEnumVariantContext *>(&context)) {
            variant->name = text_of(require(unit->IDENT(), "enum variant name is missing"));
            return variant;
        }

        if (auto *tuple = dynamic_cast<AHFLParser::TupleEnumVariantContext *>(&context)) {
            variant->name = text_of(require(tuple->IDENT(), "enum variant name is missing"));
            // P1 (ADT, RFC §1.5): optional positional tuple payload, e.g.
            // `Some(T)`, `Err(E)`.
            if (const auto type_list = borrow(tuple->typeList())) {
                for (auto *type_context : type_list->get().type_()) {
                    variant->payload.push_back(build_type_syntax(
                        require(type_context, "enum variant payload type is missing")));
                }
            }
            return variant;
        }

        if (auto *strct = dynamic_cast<AHFLParser::StructEnumVariantContext *>(&context)) {
            variant->name = text_of(require(strct->IDENT(), "enum variant name is missing"));
            // RFC d-1 minimal POC: struct-form enum variant with named fields,
            // e.g. `Point(x: Int, y: Int)`.
            if (const auto field_list = borrow(strct->variantFieldList())) {
                for (auto *field_ctx : field_list->get().variantFieldDecl()) {
                    auto field = make_owned<ast::EnumVariantFieldSyntax>();
                    field->range = context_range(require(field_ctx, "struct variant field missing"), source_);
                    field->name = text_of(require(field_ctx->IDENT(), "struct variant field name missing"));
                    field->type = build_type_syntax(
                        require(field_ctx->type_(), "struct variant field type missing"));
                    if (const auto def = borrow(field_ctx->constExpr())) {
                        field->default_value = build_expr_syntax(def->get());
                    }
                    variant->named_fields.push_back(std::move(field));
                }
            }
            return variant;
        }

        // Defensive fallback (should not be reachable with the grammar above).
        throw std::logic_error("enumVariant context did not match any labelled alternative");
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

        if (const auto decreases_decl = borrow(context.decreasesDecl())) {
            clause->kind = ast::ContractClauseKind::Decreases;
            // `decreases: *;` uses the wildcard arm — expr() returns nullopt.
            if (const auto expr_context = borrow(decreases_decl->get().expr())) {
                clause->expr = build_expr_syntax(expr_context->get());
            } else {
                clause->is_wildcard = true;
            }
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
    result.source.invalidate_line_starts_cache();

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
        const auto invariant_violations = ast::validate_program_invariants(*result.program);
        for (const auto &violation : invariant_violations) {
            result.diagnostics.error()
                .message("internal AST invariant violation: " + violation.message)
                .range(violation.range)
                .emit();
        }
        if (!invariant_violations.empty()) {
            result.program.reset();
        }
    } catch (const ParserStackOverflowException &overflow) {
        // Structured, code-carrying diagnostic (with ErrorCode for tooling).
        // Code consumers (CLI / LSP / mutation) can branch on PARSER_STACK_OVERFLOW
        // to propose "split into smaller declarations" refactors.
        result.diagnostics.error()
            .code(error_codes::parse::ParserStackOverflow)
            .message(messages::parse::ParserStackOverflow,
                     std::string_view(overflow.nesting_kind()),
                     std::to_string(overflow.depth()),
                     std::to_string(overflow.limit()))
            .range(overflow.where())
            .emit();
    } catch (const std::exception &exception) {
        result.diagnostics.error()
            .message("parser failed: " + std::string(exception.what()))
            .emit();
    }

    if (result.program && options_.emit_parse_note && !result.has_errors()) {
        result.diagnostics.note()
            .message("parsed with the generated ANTLR4 C++ parser from grammar/AHFL.g4")
            .emit();
    }

    // M4: Desugar built-in container syntax into nominal stdlib equivalents.
    // Runs after parsing and invariant validation, before name resolution.
    // Controlled by FrontendOptions::enable_desugaring — off by default
    // during the migration period while downstream passes are adapted.
    if (result.program && !result.has_errors() && options_.enable_desugaring) {
        DesugarPass desugar;
        desugar.run(*result.program);
    }

    return result;
}

} // namespace ahfl
