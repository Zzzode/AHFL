#include "ahfl/frontend.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
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

template <typename NodeT> [[nodiscard]] std::string text_or_error(MaybeRef<NodeT> node) {
    return node.has_value() ? node->get().getText() : "<error>";
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

[[nodiscard]] std::string source_text(const SourceFile &source, SourceRange range) {
    return std::string(source.slice(range));
}

template <typename DeclT, typename ContextT, typename... Args>
[[nodiscard]] Owned<ast::Decl>
make_decl(const SourceFile &source, ContextT &context, Args &&...args) {
    const auto range = context_range(context, source);
    return make_owned<DeclT>(std::forward<Args>(args)..., source_text(source, range), range);
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

    [[nodiscard]] Owned<ast::Decl> build_module_decl(AHFLParser::ModuleDeclContext &context) const {
        return make_decl<ast::ModuleDecl>(
            source_, context, text_of(require(context.qualifiedIdent(), "module name is missing")));
    }

    [[nodiscard]] Owned<ast::Decl> build_import_decl(AHFLParser::ImportDeclContext &context) const {
        return make_decl<ast::ImportDecl>(
            source_,
            context,
            text_of(require(context.qualifiedIdent(), "import path is missing")),
            text_or_empty(borrow(context.IDENT())));
    }

    [[nodiscard]] Owned<ast::Decl>
    build_top_level_decl(AHFLParser::TopLevelDeclContext &context) const {
        if (const auto const_decl = borrow(context.constDecl())) {
            return make_decl<ast::ConstDecl>(
                source_,
                const_decl->get(),
                text_of(require(const_decl->get().IDENT(), "const name is missing")));
        }

        if (const auto type_alias_decl = borrow(context.typeAliasDecl())) {
            return make_decl<ast::TypeAliasDecl>(
                source_,
                type_alias_decl->get(),
                text_of(require(type_alias_decl->get().IDENT(), "type alias name is missing")));
        }

        if (const auto struct_decl = borrow(context.structDecl())) {
            return make_decl<ast::StructDecl>(
                source_,
                struct_decl->get(),
                text_of(require(struct_decl->get().IDENT(), "struct name is missing")));
        }

        if (const auto enum_decl = borrow(context.enumDecl())) {
            return make_decl<ast::EnumDecl>(
                source_,
                enum_decl->get(),
                text_of(require(enum_decl->get().IDENT(), "enum name is missing")));
        }

        if (const auto capability_decl = borrow(context.capabilityDecl())) {
            return make_decl<ast::CapabilityDecl>(
                source_,
                capability_decl->get(),
                text_of(require(capability_decl->get().IDENT(), "capability name is missing")));
        }

        if (const auto predicate_decl = borrow(context.predicateDecl())) {
            return make_decl<ast::PredicateDecl>(
                source_,
                predicate_decl->get(),
                text_of(require(predicate_decl->get().IDENT(), "predicate name is missing")));
        }

        if (const auto agent_decl = borrow(context.agentDecl())) {
            return make_decl<ast::AgentDecl>(
                source_,
                agent_decl->get(),
                text_of(require(agent_decl->get().IDENT(), "agent name is missing")));
        }

        if (const auto contract_decl = borrow(context.contractDecl())) {
            return make_decl<ast::ContractDecl>(
                source_,
                contract_decl->get(),
                text_of(
                    require(contract_decl->get().qualifiedIdent(), "contract target is missing")));
        }

        if (const auto flow_decl = borrow(context.flowDecl())) {
            return make_decl<ast::FlowDecl>(
                source_,
                flow_decl->get(),
                text_of(require(flow_decl->get().qualifiedIdent(), "flow target is missing")));
        }

        if (const auto workflow_decl = borrow(context.workflowDecl())) {
            return make_decl<ast::WorkflowDecl>(
                source_,
                workflow_decl->get(),
                text_of(require(workflow_decl->get().IDENT(), "workflow name is missing")));
        }

        throw std::logic_error(
            "top-level declaration did not match any supported AHFL declaration kind");
    }
};

} // namespace

Frontend::Frontend(FrontendOptions options) : options_(options) {}

ParseResult Frontend::parse_file(const std::filesystem::path &path) const {
    ParseResult result;
    result.source.display_name = path.string();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.diagnostics.error("failed to open source file: " + path.string());
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    result.source.content = buffer.str();

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
    out << "program " << program.source_name << '\n';

    for (const auto &declaration : program.declarations) {
        out << "  - " << declaration->headline() << '\n';
    }
}

} // namespace ahfl
