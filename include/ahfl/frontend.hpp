#pragma once

#include <filesystem>
#include <ostream>
#include <string>

#include "ahfl/ast.hpp"
#include "ahfl/diagnostics.hpp"
#include "ahfl/ownership.hpp"
#include "ahfl/source.hpp"

namespace ahfl {

struct FrontendOptions {
    bool emit_parse_note{false};
};

struct ParseResult {
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

    [[nodiscard]] ParseResult parse_file(const std::filesystem::path &path) const;
    [[nodiscard]] ParseResult parse_text(std::string display_name, std::string text) const;

  private:
    FrontendOptions options_;
};

void dump_program_outline(const ast::Program &program, std::ostream &out);

} // namespace ahfl
