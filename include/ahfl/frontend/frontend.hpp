#pragma once

#include <filesystem>
#include <ostream>
#include <string>

#include "ahfl/frontend/ast.hpp"
#include "ahfl/support/diagnostics.hpp"
#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl {

struct FrontendOptions {
    bool emit_parse_note{false};
};

struct ParseResult {
    // Public result of the parse + AST lowering boundary. Generated parser
    // objects remain an implementation detail of src/frontend/frontend.cpp.
    SourceFile source;
    Owned<ast::Program> program;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

class Frontend {
  public:
    explicit Frontend(FrontendOptions options = {});

    // These entry points stop at the hand-written AST boundary. Later
    // resolve/typecheck/validate/emission stages must consume ast::Program
    // rather than ANTLR parse-tree nodes.
    [[nodiscard]] ParseResult parse_file(const std::filesystem::path &path) const;
    [[nodiscard]] ParseResult parse_text(std::string display_name, std::string text) const;

  private:
    FrontendOptions options_;
};

void dump_program_outline(const ast::Program &program, std::ostream &out);

} // namespace ahfl
