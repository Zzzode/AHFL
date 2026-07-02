#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"

namespace ahfl {

struct FrontendOptions {
    bool emit_parse_note{false};
    /// M4: Enable syntax desugaring pass. When true, built-in container
    /// syntax (Optional<T>, some/none, list/set/map literals) is rewritten
    /// into canonical nominal-generic stdlib equivalents after parsing.
    /// Defaults to false during the migration period; will become the
    /// default once all downstream passes support the desugared form.
    bool enable_desugaring{false};
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

struct SourceUnit {
    SourceId id;
    std::filesystem::path path;
    std::string module_name;
    SourceRange module_range;
    SourceFile source;
    Owned<ast::Program> program;
    std::vector<ImportRequest> imports;
};

struct ImportEdge {
    SourceId importer;
    SourceId imported;
    ImportRequest request;
};

struct SourceGraph {
    std::vector<SourceId> entry_sources;
    std::vector<SourceUnit> sources;
    std::unordered_map<std::string, SourceId> module_to_source;
    std::vector<ImportEdge> import_edges;
};

struct ProjectInput {
    struct ModuleRoot {
        std::string prefix;
        std::filesystem::path root;
        std::vector<std::string> exported_modules;
    };

    std::vector<std::filesystem::path> entry_files{};
    std::vector<std::filesystem::path> search_roots{};
    std::vector<ModuleRoot> module_roots{};
    // P6: project-aware parses include the repository stdlib by default and
    // inject std::prelude for non-std source units. Tests and specialized
    // tools may disable these while exercising raw source-graph behavior.
    //
    // NOTE (2026-07): inject_prelude defaults to false — users must
    // explicitly `import std::...` modules. Stdlib is still discoverable
    // via include_stdlib=true, but no symbols are auto-injected into scope.
    bool include_stdlib{true};
    bool inject_prelude{false};
    std::vector<std::filesystem::path> stdlib_search_roots{};
    // Normalized absolute path string -> unsaved document text. LSP and other
    // project-aware tooling use this to overlay open editor buffers while
    // reusing the canonical SourceGraph loader.
    std::unordered_map<std::string, std::string> source_overlays{};
};

struct ProjectParseResult {
    SourceGraph graph;
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
    [[nodiscard]] ProjectParseResult parse_project(const ProjectInput &input) const;

  private:
    FrontendOptions options_;
};

void dump_program_outline(const ast::Program &program, std::ostream &out);
void dump_project_outline(const SourceGraph &graph, std::ostream &out);
void dump_project_ast_outline(const SourceGraph &graph, std::ostream &out);

// ----------------------------------------------------------------------------
// DecreasesClauseSyntax – standalone, three-phase handlers (R-09: NOT dispatched
// through DeclKind / NodeKind).  Every consumer is a dedicated free function.
// ----------------------------------------------------------------------------

/// Print a human-readable (but stable) debug outline for a single decreases
/// clause.  Format mirrors `dump_program_outline` so printer → formatter can
/// be compared bit-for-bit on canonical programs.
void print_decreases_clause(const ast::DecreasesClauseSyntax &clause,
                            std::ostream &out,
                            int base_indent = 0);

/// Canonicalise a decreases clause in place.
///
///   * Wildcard form:     leave untouched (terms is allowed to be empty).
///   * Explicit-term form:
///       - drop nullptr entries;
///       - merge any top-level "comma-like" tuples (currently a no-op because
///         ExprSyntax has no Tuple node, but the hook is reserved so a later
///         grammar change does not require a signature bump);
///       - deduplicate consecutive identical spellings to avoid obviously
///         redundant measures.
///
/// Returns true if any structural change was made.
bool desugar_decreases_clause(ast::DecreasesClauseSyntax &clause);

/// Format a decreases clause back to surface source text (no trailing newline
/// by default; the caller wraps in the surrounding contract body).
///
///   wildcard          →  "decreases *;"
///   empty non-wild    →  "decreases ();"
///   [e0, e1, e2]      →  "decreases (e0, e1, e2);"
[[nodiscard]] std::string format_decreases_clause(const ast::DecreasesClauseSyntax &clause);

} // namespace ahfl
