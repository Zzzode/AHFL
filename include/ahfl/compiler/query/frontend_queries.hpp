#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <ostream>
#include <string>

#include "ahfl/base/query/query_engine.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

// The project input model and parse result live in the compiler's internal
// `src/` tree (they own an AST-bearing SourceGraph and a ProjectInput). Only
// *references* to them cross this header's boundary, so forward declarations
// keep the public include layer free of a src-relative include (which would not
// survive installation). The .cpp includes the real header.
namespace ahfl {
struct ProjectInput;
struct ProjectInputModel;
struct ProjectParseResult;
} // namespace ahfl

// RFC 0027 P2/P3 (KR6.11): the frontend as a query graph over the QueryEngine.
//
// P2 moved `parse(file)` onto the graph — the safest stage (file-scoped, const,
// trivially comparable identity) so the query-vs-direct equivalence methodology
// was established end to end before the semantic stages followed.
//
// P3 (this slice) adds the module-level semantic stages, preserving the RFC's
// graded granularity (`resolve(module)` / `typecheck(module)` / `type_of(expr)`):
//
//   source_text(FileId)  -> SourceText        (input, set by the driver / editor)
//   parse(FileId)        -> ParseSnapshot     (derived)
//   resolve(ModuleId)    -> ResolveSnapshot   (derived; reads parse, caches ResolveResult)
//   typecheck(ModuleId)  -> TypecheckSnapshot (derived; reads resolve, caches TypeCheckResult)
//   type_of(ModuleId, node_id, source_id)     (derived VIEW over the typecheck memo)
//
// Identity is index-based (CLAUDE.md Principle 2): a FileId / ModuleId is a
// numeric slot index, never a path string. In the file-scoped frontend graph one
// analysis unit is one file, so ModuleId slot i and FileId slot i name the same
// unit — they are distinct phantom-typed keys precisely so the two granularities
// in the RFC's diagram (file-level parse, module-level resolve) cannot be
// confused at a call site.
//
// The semantic stages are *whole-unit* queries, matching the resolver's and type
// checker's actual API (`resolve(const ast::Program&)` / `check(program, resolve)`):
// the RFC's `resolve(module)` names the analysis unit, not a per-module split of
// a graph-wide resolution. A graph-wide `SourceGraph` unit is a separate input
// shape (see `FrontendQueries` docs) and is deliberately not synthesized here.

namespace ahfl::query {

// Phantom tag so an input index cannot be passed where a file index is expected.
struct FileTag {};
using FileId = QueryKey<FileTag>;

// Phantom tag for the module-level (analysis-unit) granularity of resolve /
// typecheck. Distinct from FileTag so a file index cannot be passed where a
// module index is expected.
struct ModuleTag {};
using ModuleId = QueryKey<ModuleTag>;

// Phantom tag for the project (multi-file `SourceGraph`) granularity. A project
// is one analysis unit whose input is a whole ProjectInputModel, so its slots
// are indexed by ProjectId and never confused with the file-scoped ones.
struct ProjectTag {};
using ProjectId = QueryKey<ProjectTag>;

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
// The outline is deliberately *content- and span-sensitive*, not shape-only: it
// projects each node's source span as well as its structure (see AstPrinter's
// span sink). This matters because the resolve and typecheck stages consume
// source ranges and node ids, so "same shape, different offsets" (e.g. a comment
// inserted mid-body, or two same-length literals) is a different parse and must
// not share a memo. Because of that, the parse outline is the AST-sensitive edge
// the typecheck slot relies on (see dump_resolve_outline's scope note).
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

// Value identity of a resolve: the canonical symbol-table outline, the
// canonical diagnostic JSON, the error flag, and whether the stage actually ran.
// The `ran` flag is part of the identity on purpose — the CLI pipeline
// short-circuits (a parse error means resolve never runs), and "the stage was
// skipped" must be distinguishable from "the stage ran and produced no
// diagnostics", or the query and the direct pipeline could disagree on when a
// stage executed while still agreeing on the bytes.
struct ResolveSnapshot {
    std::string symbols_outline;
    std::string diagnostics_json;
    bool has_errors = false;
    bool ran = false;

    [[nodiscard]] friend bool operator==(const ResolveSnapshot &,
                                         const ResolveSnapshot &) noexcept = default;
};

// Value identity of a typecheck: the canonical TypedProgram projection (the
// existing `serialize_typed_program_json` — one SSOT, no second typed-tree
// spelling), the canonical diagnostic JSON, the error flag, and the run flag
// (false when parse or resolve short-circuited the stage).
struct TypecheckSnapshot {
    std::string typed_program_json;
    std::string diagnostics_json;
    bool has_errors = false;
    bool ran = false;

    [[nodiscard]] friend bool operator==(const TypecheckSnapshot &,
                                         const TypecheckSnapshot &) noexcept = default;
};

// Canonical snapshots, mirroring snapshot_parse_result: the query bodies route
// through these (and only these). The equivalence guard re-derives the same
// projections independently so a regression inside these definitions is caught
// rather than reproduced; do not route the guard through them.
[[nodiscard]] ResolveSnapshot snapshot_resolve_result(const ResolveResult &result, bool ran);
[[nodiscard]] TypecheckSnapshot snapshot_typecheck_result(const TypeCheckResult &result, bool ran);

// Canonical textual projection of a ResolveResult: symbols, references, imports,
// public aliases and reachability, each in its store's order.
//
// SCOPE — read this before trusting the outline as an identity. The outline is a
// *lossy* fold of the AST-indexed parts of a resolution: it covers the symbol
// table and the reference/import/alias side tables, but NOT the resolution's
// per-expression annotations (`captured_names_by_expr`) nor anything about
// declaration *bodies* (operators, literals, field order) that does not change a
// symbol's name, kind, declaration range or reachability. Two different ASTs can
// therefore produce a byte-identical resolve outline.
//
// The invariant that keeps the memo sound despite this is stated where it is
// enforced: the typecheck slot must take an explicit dependency on the *parse*
// memo (whose outline IS span- and content-sensitive) as well as on this resolve
// memo. Under that edge, "the resolve outline is unchanged" implies "the AST is
// unchanged", because a changed AST always moves a node span and therefore the
// parse outline. The resolve outline's own job is the narrower one — to make the
// resolve memo a *value* the engine can compare and to short-circuit a resolve
// recompute when only unrelated bytes changed — not to re-establish the AST.
void dump_resolve_outline(const ResolveResult &result, std::ostream &out);

// Derived view of one expression's type. `found` is false when the typed program
// carries no record for the requested AST node id (e.g. the node is not an
// expression, or the stage chain short-circuited); `type` is then null.
//
// This view is a thin read over the *memoized* typecheck result: it does not
// register a family of its own, because a per-expression query would need a
// (module, node_id) key registry whose only purpose would be to re-read a table
// the typecheck memo already holds. The brief's `type_of(module, node_id,
// source_id)` therefore resolves to: bring the typecheck memo up to date, then
// look the node up through `TypedProgram::find_expr` (the existing reverse
// index). Repeated reads are memo hits at the typecheck slot; no per-expression
// recompute is promised (RFC 0027 P3). The type pointer is interned through
// TypeContext, so its identity is stable and the view is cheap to compare.
struct TypeOfResult {
    TypePtr type = nullptr;
    bool found = false;

    [[nodiscard]] friend bool operator==(const TypeOfResult &,
                                         const TypeOfResult &) noexcept = default;
};

// Value identity of a project parse: the two byte-comparable projections of a
// `ProjectParseResult` — the *graph* outline (per-source AST structure plus the
// module/entry/import-edge facts the downstream stages read) and the canonical
// diagnostic JSON — plus the error flag. This is deliberately NOT a comparison
// of the parsed `SourceGraph` itself: a `SourceUnit` owns an `Owned<ast::Program>`
// and is neither copyable nor comparable, so the graph cannot be an engine value.
// The outline is the projection that makes "the same project parse" a value.
struct ProjectParseSnapshot {
    std::string graph_outline;
    std::string diagnostics_json;
    bool has_errors = false;

    [[nodiscard]] friend bool operator==(const ProjectParseSnapshot &,
                                         const ProjectParseSnapshot &) noexcept = default;
};

// Canonical snapshot of a ProjectParseResult: the single *production* definition
// of project-parse equivalence. The query body routes through it (and only it);
// the equivalence guard re-derives the same projections independently so a
// regression inside this definition is caught rather than reproduced — do not
// "de-duplicate" the guard by routing it through here.
[[nodiscard]] ProjectParseSnapshot snapshot_project_parse_result(const ProjectParseResult &result);

// Canonical outline of a parsed `SourceGraph`: the graph-level facts the downstream
// stages consume — entry sources, each source's id / module / package prefix /
// exported + artifact-export + dependency lists / compiler-intrinsics allow and
// its AST (via the existing `dump_program_ast_outline` projection, which is
// span- and content-sensitive), plus every import edge. Two project parses are
// the same parse exactly when this outline and the diagnostic JSON match.
//
// The graph's `path` members are deliberately NOT included: a path is a display
// artifact, and the parse of a source is a function of its text and module
// identity, not of where it sits on disk — omitting it keeps two logically
// identical parses (e.g. a fixture copied to a temp dir) equal. `SourceUnit::id`
// IS included, because it is the index every reference in the graph resolves
// through and the downstream stages key symbols by it.
void dump_project_graph_outline(const SourceGraph &graph, std::ostream &out);

// The frontend query graph. Owns the QueryEngine, the frontend, and the stage
// results (parse results including each Owned<ast::Program>, plus the resolve
// and typecheck results). Single-threaded, mirroring QueryEngine's contract:
// register/set before evaluating, and do not call set_source_text from inside a
// compute function.
//
// Two analysis-unit shapes live on this graph:
//
//  * File-scoped: `parse(FileId)` -> `resolve(ModuleId)` -> `typecheck(ModuleId)`,
//    the CLI's file path (`run_analysis<ast::Program>`). One unit, one file, so
//    ModuleId slot i names the same unit as FileId slot i — the two tags are
//    distinct precisely so the RFC's graded granularities (file-level parse,
//    module-level resolve) cannot be confused at a call site.
//
//  * Project-scoped: `parse_project(ProjectId)`, the CLI's package/workspace path
//    (`run_analysis<SourceGraph>`). Its input is a whole `ProjectInputModel` — the
//    resolved project configuration plus the frozen text of every source the
//    parser can reach — so `parse_project` is a *pure function of that input*:
//    prelude injection, module-root discovery and import-edge resolution happen
//    inside the compute body over the model, not by probing the filesystem. The
//    input is `std::equality_comparable` (see ProjectInputModel), which is what
//    lets the project parse be memoized. The owned `SourceGraph` (and its ASTs)
//    cannot live in the engine's type-erased memo, so it lives in a deque-backed
//    store here and is borrowed by the resolve/typecheck project stages.
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

    // Set (or replace) the project model for a project slot. Equal models are a
    // no-op, so re-resolving an unchanged project does not invalidate the parse.
    void set_project_input(ProjectId project, ProjectInputModel model);

    // Freeze a live `ProjectInput` (reading the filesystem) and set it as the
    // project model. The one impure boundary of the project route: call it once
    // per project, then evaluate; a re-set with an equal resolved model is a
    // no-op, so a caller that re-resolves an unchanged tree pays no reparse.
    void set_project_input(ProjectId project, const ProjectInput &input);

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

    // Evaluate resolve(module): runs the resolver over the unit's AST and caches
    // the ResolveResult. Short-circuits (and records `ran == false`) when the
    // unit's parse produced errors or no AST, mirroring the CLI pipeline. The
    // AST is borrowed from the parse store inside the compute body, so the
    // engine has just brought it up to date.
    [[nodiscard]] std::expected<ResolveSnapshot, CycleError> resolve(ModuleId module);

    // Evaluate typecheck(module): runs the type checker over the unit's AST and
    // resolve result, caches the TypeCheckResult. Short-circuits when resolve
    // short-circuited or errored.
    [[nodiscard]] std::expected<TypecheckSnapshot, CycleError> typecheck(ModuleId module);

    // Derived view: the type recorded for the AST expression `node_id` (with its
    // `source_id` qualifier) in the unit's cached TypedProgram. Brings the
    // typecheck memo up to date (a memo hit when nothing changed) and reads
    // through the typed program's existing reverse index — no family of its own;
    // see TypeOfResult. `found` is false when the stage chain short-circuited or
    // the node is not a recorded expression.
    [[nodiscard]] std::expected<TypeOfResult, CycleError>
    type_of(ModuleId module, std::uint64_t node_id, std::optional<SourceId> source_id);

    // Borrow the cached resolve result of a unit whose resolve slot is currently
    // valid (Clean/Verified). Null when the slot is unset/dirty/visiting, never
    // computed, or the stage short-circuited. The pointer is owned by this
    // object (deque-backed store); it is meaningful only for the revision it was
    // checked at.
    [[nodiscard]] const ResolveResult *resolve_result(ModuleId module) const;

    // Borrow the cached typecheck result, with the same validity contract as
    // resolve_result.
    [[nodiscard]] const TypeCheckResult *typecheck_result(ModuleId module) const;

    // How many times resolve(module) / typecheck(module) actually ran their
    // compute function. Per-stage instrumentation for the invalidation tests.
    [[nodiscard]] std::size_t resolve_computes(ModuleId module) const;
    [[nodiscard]] std::size_t typecheck_computes(ModuleId module) const;

    // Evaluate parse_project(project): parses the frozen model (prelude
    // injection, module-root discovery and import-edge resolution included) and
    // caches the owned `SourceGraph`. Recomputes only when the model changed;
    // otherwise served from the memo.
    [[nodiscard]] std::expected<ProjectParseSnapshot, CycleError> parse_project(ProjectId project);

    // Borrow the parsed `SourceGraph` of a project whose parse slot is currently
    // valid (Clean/Verified) at the current engine revision. Null when the slot
    // is unset/dirty/visiting or was never computed — a model edit marks the slot
    // Dirty eagerly, so a stale graph is never handed out. The pointer is owned
    // by this object (deque-backed store) and is meaningful only for the revision
    // it was checked at.
    [[nodiscard]] const SourceGraph *project_graph(ProjectId project) const;

    // Evaluate resolve_project(project) / typecheck_project(project): run the
    // graph-wide resolver / type checker over the memoized project graph, caching
    // the results. Short-circuit exactly as the CLI pipeline does — a project
    // parse error means resolve never runs, a resolve error means typecheck never
    // runs — recorded in the snapshot's `ran` flag.
    [[nodiscard]] std::expected<ResolveSnapshot, CycleError> project_resolve(ProjectId project);
    [[nodiscard]] std::expected<TypecheckSnapshot, CycleError> project_typecheck(ProjectId project);

    // Borrow the cached project resolve / typecheck result of a slot that is
    // currently valid (Clean/Verified) and actually ran. Same contract as
    // resolve_result / typecheck_result.
    [[nodiscard]] const ResolveResult *project_resolve_result(ProjectId project) const;
    [[nodiscard]] const TypeCheckResult *project_typecheck_result(ProjectId project) const;

    // How many times parse_project(project) actually ran its compute function.
    [[nodiscard]] std::size_t project_parse_computes(ProjectId project) const;
    [[nodiscard]] std::size_t project_resolve_computes(ProjectId project) const;
    [[nodiscard]] std::size_t project_typecheck_computes(ProjectId project) const;

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

    // Per-ModuleId semantic-stage record. `ran` records whether the stage
    // actually executed: a short-circuited stage stores a default-constructed
    // result, and the flag keeps "skipped" distinguishable from "ran clean".
    struct ResolveSlotRecord {
        ResolveResult result;
        bool ran = false;
        std::size_t computes = 0;
    };
    struct TypecheckSlotRecord {
        TypeCheckResult result;
        bool ran = false;
        std::size_t computes = 0;
    };

    // Per-ProjectId slot record: the parsed graph (owning every unit's AST) plus
    // the compute counter, the same one-store-one-index shape as SlotRecord.
    struct ProjectSlotRecord {
        SourceGraph graph;
        std::size_t computes = 0;
    };

    // Per-ProjectId semantic-stage records, mirroring ResolveSlotRecord /
    // TypecheckSlotRecord (a short-circuited stage stores a default-constructed
    // result and `ran` distinguishes "skipped" from "ran clean").
    struct ProjectResolveSlotRecord {
        ResolveResult result;
        bool ran = false;
        std::size_t computes = 0;
    };
    struct ProjectTypecheckSlotRecord {
        TypeCheckResult result;
        bool ran = false;
        std::size_t computes = 0;
    };

    Frontend frontend_;
    QueryEngine engine_;
    InputQueryT<SourceText> source_text_;
    InputQueryT<ProjectInputModel> project_input_;
    DerivedQueryT<ParseSnapshot> parse_;
    DerivedQueryT<ResolveSnapshot> resolve_;
    DerivedQueryT<TypecheckSnapshot> typecheck_;
    DerivedQueryT<ProjectParseSnapshot> project_parse_;
    DerivedQueryT<ResolveSnapshot> project_resolve_;
    DerivedQueryT<TypecheckSnapshot> project_typecheck_;
    // Slot records indexed by FileId / ModuleId / ProjectId slot. Deques so that
    // adding a unit never moves an existing record (and thus never invalidates a
    // borrowed ast::Program / ResolveResult / TypeCheckResult / SourceGraph),
    // mirroring QueryEngine's own slot storage choice.
    std::deque<SlotRecord> slots_;
    std::deque<ResolveSlotRecord> resolve_slots_;
    std::deque<TypecheckSlotRecord> typecheck_slots_;
    std::deque<ProjectSlotRecord> project_slots_;
    std::deque<ProjectResolveSlotRecord> project_resolve_slots_;
    std::deque<ProjectTypecheckSlotRecord> project_typecheck_slots_;
};

} // namespace ahfl::query
