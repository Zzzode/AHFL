#pragma once
#include <string>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"

namespace ahfl::formatter {

struct FormatOptions {
    int indent_width = 4;
    bool use_tabs = false;
    int max_line_length = 100;
    bool trailing_newline = true;
    bool align_fields = true;
    bool sort_imports = true;
};

struct FormatResult {
    bool success = false;
    std::string formatted;
    std::string error;
    int lines_changed = 0;
};

struct FormatDiff {
    int line;
    std::string original;
    std::string formatted;
};

[[nodiscard]] FormatResult format_source(const std::string &source,
                                         const FormatOptions &options = {});

[[nodiscard]] bool check_formatting(const std::string &source, const FormatOptions &options = {});

[[nodiscard]] std::vector<FormatDiff> compute_diff(const std::string &original,
                                                   const std::string &formatted);

/// Standalone formatter for a single DecreasesClauseSyntax.
/// Guaranteed NOT to route through DeclKind / NodeKind (R-09).
/// Produces the canonical surface spelling:
///   wildcard          → "decreases *;"
///   empty non-wild    → "decreases ();"
///   [e0, e1, e2]      → "decreases (e0, e1, e2);"
[[nodiscard]] std::string format_decreases_clause(const ahfl::ast::DecreasesClauseSyntax &clause);

} // namespace ahfl::formatter
