#pragma once

#include <cstddef>
#include <deque>
#include <expected>
#include <string>

#include "ahfl/base/query/query_engine.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

// RFC 0027 P2 (KR6.11): parse(file) as a derived query over a source_text(file)
// input. This is the first frontend stage moved onto the QueryEngine graph; it
// is deliberately the safest one (file-scoped, const, trivially comparable
// result identity) so that the query-vs-direct equivalence methodology is
// established end to end before resolve/typecheck (P3) follow the same shape.
//
// Two families are registered:
//
//   source_text(FileId) -> SourceText     (input, set by the driver / editor)
//   parse(FileId)       -> ParseSnapshot  (derived, compute = Frontend::parse_text)
//
// Identity is index-based (CLAUDE.md Principle 2): a FileId is a numeric slot
// index, never a path string. Display names exist only inside SourceText so the
// frontend can label its diagnostics.

namespace ahfl::query {

// Phantom tag so an input index cannot be passed where a file index is expected.
struct FileTag {};
using FileId = QueryKey<FileTag>;

// Atomic editor/driver fact: the text of one file plus its display name.
// Equality is what makes an equal-text write a no-op (no revision bump).
struct SourceText {
    std::string display_name;
    std::string text;

    [[nodiscard]] friend bool operator==(const SourceText &, const SourceText &) noexcept = default;
};

// Value identity of a parse: the two byte-comparable projections of ParseResult
// (canonical AST outline + canonical diagnostic JSON) plus the error flag. The
// engine memoizes this snapshot; it is also exactly what the query-vs-direct
// equivalence guard compares, so the "what counts as the same parse" question
// has one answer shared by the engine and the guard.
//
// The owned AST itself (ParseResult::program) is not stored here: it is not
// copyable or equality-comparable, so it cannot live in the engine's type-erased
// memo. It lives in the FrontendQueries-owned parse-result store instead, and
// later resolve/typecheck query bodies borrow it from there while the store is
// alive (see FrontendQueries::program).
struct ParseSnapshot {
    std::string outline;
    std::string diagnostics_json;
    bool has_errors = false;

    [[nodiscard]] friend bool operator==(const ParseSnapshot &,
                                         const ParseSnapshot &) noexcept = default;
};

// Canonical snapshot of a ParseResult. This is the single definition of parse
// equivalence: the query body and the equivalence guard both route through it,
// so they cannot drift apart.
[[nodiscard]] ParseSnapshot snapshot_parse_result(const ParseResult &result);

// The frontend query graph. Owns the QueryEngine, the frontend, and the parse
// results (including each Owned<ast::Program>). Single-threaded, mirroring
// QueryEngine's contract: register/set before evaluating, and do not call
// set_source_text from inside a compute function.
class FrontendQueries {
  public:
    explicit FrontendQueries(FrontendOptions options = {});

    FrontendQueries(const FrontendQueries &) = delete;
    FrontendQueries &operator=(const FrontendQueries &) = delete;
    FrontendQueries(FrontendQueries &&) = delete;
    FrontendQueries &operator=(FrontendQueries &&) = delete;

    // Set (or replace) one file's text. Replacing with equal text is a no-op:
    // no revision bump, no invalidation.
    void set_source_text(FileId file, std::string display_name, std::string text);

    // Evaluate parse(file): recomputes only when the file's text changed (or a
    // transitive input changed), otherwise served from the memo.
    [[nodiscard]] std::expected<ParseSnapshot, CycleError> parse(FileId file);

    // Borrow the AST of a file whose parse slot has been evaluated. Null when
    // the slot has never been computed or the parse produced diagnostics that
    // invalidated the program. The pointer is owned by this object and is valid
    // until the file's text changes.
    [[nodiscard]] const ast::Program *program(FileId file) const;

    // How many times parse(file) actually ran its compute function. Precise
    // per-file instrumentation for the invalidation tests.
    [[nodiscard]] std::size_t parse_computes(FileId file) const;

    [[nodiscard]] Revision revision() const noexcept;
    [[nodiscard]] QueryStats stats() const;

  private:
    Frontend frontend_;
    QueryEngine engine_;
    InputQueryT<SourceText> source_text_;
    DerivedQueryT<ParseSnapshot> parse_;
    // ParseResult store indexed by FileId slot. A deque so that adding a file
    // never moves an existing ParseResult (and thus never invalidates a
    // borrowed ast::Program), mirroring QueryEngine's own slot storage choice.
    std::deque<ParseResult> parse_results_;
    std::deque<std::size_t> parse_computes_;
};

} // namespace ahfl::query
