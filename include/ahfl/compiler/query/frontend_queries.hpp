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

// Canonical snapshot of a ParseResult. This is the single *production*
// definition of parse equivalence: the query body routes through it (and only
// it). The query-vs-direct equivalence guard deliberately does NOT call it —
// it re-derives the same two projections independently so that a regression
// inside this definition is caught rather than reproduced. Do not
// "de-duplicate" the guard by routing it through here: that would make the
// guard tautological and blind to exactly the bugs it exists to find.
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

    // Borrow the AST of the file whose parse slot is currently valid — i.e.
    // whose parse(FileId) evaluation is still Clean or Verified at the current
    // engine revision. This is revision-checked on every call: after
    // set_source_text(file, ...) the file's parse slot is Dirty (a changed text
    // bumped the revision and marked the slot dirty, or the slot was never
    // computed), so this returns nullptr until parse(file) is re-evaluated.
    // It therefore never hands back the AST of a superseded text revision.
    //
    // Null when the slot is unset/dirty/visiting, when it was never computed,
    // or when the parse produced diagnostics that invalidated the program. The
    // pointer is owned by this object; a borrowed pointer stays valid across
    // further set_source_text / parse calls for OTHER files (deque storage) but
    // is only meaningful for the revision it was checked at — a caller that
    // edits this file must re-drive parse(file) before the AST is meaningful
    // again. Later resolve/typecheck query bodies read the AST from inside
    // their own compute function (which the engine has just brought up to
    // date), so they borrow only a slot that is Clean/Verified.
    [[nodiscard]] const ast::Program *program(FileId file) const;

    // How many times parse(file) actually ran its compute function. Precise
    // per-file instrumentation for the invalidation tests.
    [[nodiscard]] std::size_t parse_computes(FileId file) const;

    [[nodiscard]] Revision revision() const noexcept;
    [[nodiscard]] QueryStats stats() const;

  private:
    // Per-FileId slot record: the parse result (owning the AST) plus the
    // compute counter. One store, one index — a single field added per slot
    // threads through exactly one place, so the two can never desynchronize.
    struct SlotRecord {
        ParseResult result;
        std::size_t computes = 0;
    };

    Frontend frontend_;
    QueryEngine engine_;
    InputQueryT<SourceText> source_text_;
    DerivedQueryT<ParseSnapshot> parse_;
    // Slot records indexed by FileId slot. A deque so that adding a file never
    // moves an existing SlotRecord (and thus never invalidates a borrowed
    // ast::Program), mirroring QueryEngine's own slot storage choice.
    std::deque<SlotRecord> slots_;
};

} // namespace ahfl::query
