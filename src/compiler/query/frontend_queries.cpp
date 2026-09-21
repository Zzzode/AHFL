#include "ahfl/compiler/query/frontend_queries.hpp"

#include <sstream>
#include <utility>

#include "ahfl/base/support/diagnostic_serialization.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

namespace ahfl::query {

namespace {

// Append the numeric value of a scoped enum to a canonical outline. Enums are
// projected by their underlying value rather than a hand-written spelling table:
// the outline is a *memo identity*, not a user-facing artifact, and a numeric
// value is index-based canonical identity (CLAUDE.md Principle 2). A spelling
// table would be a second SSOT that could silently drift from the enum.
template <typename Enum> void append_enum(std::ostream &out, Enum value) {
    out << static_cast<int>(value);
}

void append_source_id(std::ostream &out, const std::optional<SourceId> &id) {
    out << (id.has_value() ? id->value : 0);
}

void append_range(std::ostream &out, const SourceRange &range) {
    out << range.begin_offset << '-' << range.end_offset;
}

} // namespace

ParseSnapshot snapshot_parse_result(const ParseResult &result) {
    ParseSnapshot snapshot;
    snapshot.has_errors = result.has_errors();

    if (result.program != nullptr) {
        std::ostringstream outline;
        dump_program_outline(*result.program, outline);
        snapshot.outline = outline.str();
    }
    // A null program (parse failure) leaves the outline empty rather than
    // fabricating one; the diagnostic JSON below carries the full reason.

    snapshot.diagnostics_json =
        serialize_diagnostic_report_json(DiagnosticReport::from_bag(result.diagnostics));
    return snapshot;
}

void dump_resolve_outline(const ResolveResult &result, std::ostream &out) {
    const auto &symbols = result.symbol_table.symbols();
    out << "symbols " << symbols.size() << '\n';
    for (const auto &symbol : symbols) {
        out << "  symbol " << symbol.id.value << ' ' << symbol.canonical_name << ' '
            << symbol.local_name << ' ' << symbol.module_name << ' ';
        append_enum(out, symbol.name_space);
        out << ' ';
        append_enum(out, symbol.kind);
        out << " vis ";
        append_enum(out, symbol.visibility);
        out << " src ";
        append_source_id(out, symbol.source_id);
        out << " at ";
        append_range(out, symbol.declaration_range);
        out << " api " << (result.is_api_reachable(symbol.id) ? 1 : 0) << " art "
            << (result.is_artifact_reachable(symbol.id) ? 1 : 0) << '\n';
    }

    const auto &references = result.references();
    out << "references " << references.size() << '\n';
    for (const auto &reference : references) {
        out << "  reference ";
        append_enum(out, reference.kind);
        out << ' ' << reference.text << " src ";
        append_source_id(out, reference.source_id);
        out << " at ";
        append_range(out, reference.range);
        out << " -> " << reference.target.value << '\n';
    }

    const auto &imports = result.imports();
    out << "imports " << imports.size() << '\n';
    for (const auto &binding : imports) {
        out << "  import " << binding.alias << ' ' << binding.target_module << " src ";
        append_source_id(out, binding.source_id);
        out << " at ";
        append_range(out, binding.declaration_range);
        out << '\n';
    }

    const auto &aliases = result.public_aliases();
    out << "aliases " << aliases.size() << '\n';
    for (const auto &alias : aliases) {
        out << "  alias " << alias.id.value << ' ' << alias.canonical_name << ' '
            << alias.local_name << ' ' << alias.module_name << ' ';
        append_enum(out, alias.name_space);
        out << " vis ";
        append_enum(out, alias.visibility);
        out << " src ";
        append_source_id(out, alias.source_id);
        out << " at ";
        append_range(out, alias.declaration_range);
        out << " -> " << alias.target.value << " api " << (result.is_api_reachable(alias.id) ? 1 : 0)
            << " art " << (result.is_artifact_reachable(alias.id) ? 1 : 0) << '\n';
    }
}

ResolveSnapshot snapshot_resolve_result(const ResolveResult &result, bool ran) {
    ResolveSnapshot snapshot;
    snapshot.has_errors = result.has_errors();
    snapshot.ran = ran;

    if (ran) {
        std::ostringstream outline;
        dump_resolve_outline(result, outline);
        snapshot.symbols_outline = outline.str();
        snapshot.diagnostics_json =
            serialize_diagnostic_report_json(DiagnosticReport::from_bag(result.diagnostics));
    }
    // A short-circuited resolve (a parse error means the stage never runs) leaves
    // both projections empty; `ran` distinguishes it from a clean run.
    return snapshot;
}

TypecheckSnapshot snapshot_typecheck_result(const TypeCheckResult &result, bool ran) {
    TypecheckSnapshot snapshot;
    snapshot.has_errors = result.has_errors();
    snapshot.ran = ran;

    if (ran) {
        snapshot.typed_program_json = serialize_typed_program_json(result.typed_program);
        snapshot.diagnostics_json =
            serialize_diagnostic_report_json(DiagnosticReport::from_bag(result.diagnostics));
    }
    return snapshot;
}

FrontendQueries::FrontendQueries(FrontendOptions options)
    : frontend_(options), engine_(CyclePolicy::Error),
      source_text_(engine_.register_input<SourceText>()),
      parse_(engine_.register_derived<ParseSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> ParseSnapshot {
              const SourceText &source = ctx.get(source_text_, InputId{key.index()});
              // The parse query is a thin wrapper: the frontend owns all
              // parsing semantics; the query only owns the lifetime + memo.
              ParseResult result = frontend_.parse_text(source.display_name, source.text);
              ParseSnapshot snapshot = snapshot_parse_result(result);

              const std::size_t slot = key.index();
              if (slots_.size() <= slot) {
                  slots_.resize(slot + 1);
              }
              // Replacing the whole ParseResult drops the previous Owned<program>
              // at exactly the point the memo is replaced: AST and snapshot
              // never disagree about which text they came from.
              slots_[slot].result = std::move(result);
              ++slots_[slot].computes;
              return snapshot;
          })),
      resolve_(engine_.register_derived<ResolveSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> ResolveSnapshot {
              const FileId file{key.index()};
              // Bring the parse slot up to date; the resolver reads its AST.
              static_cast<void>(ctx.read(parse_, key));

              const std::size_t slot = key.index();
              if (resolve_slots_.size() <= slot) {
                  resolve_slots_.resize(slot + 1);
              }
              auto &record = resolve_slots_[slot];

              const ast::Program *program = this->program(file);
              if (program == nullptr) {
                  // Parse failed or produced no AST: the stage never runs, exactly
                  // as the CLI pipeline short-circuits.
                  record.result = ResolveResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_resolve_result(record.result, /*ran=*/false);
              }

              const Resolver resolver;
              record.result = resolver.resolve(*program);
              record.ran = true;
              ++record.computes;
              return snapshot_resolve_result(record.result, /*ran=*/true);
          })),
      typecheck_(engine_.register_derived<TypecheckSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> TypecheckSnapshot {
              const FileId file{key.index()};
              // The typechecker reads BOTH the resolve result and the AST (it
              // consumes `captured_names_by_expr`, node ids and every source
              // range), so this slot must depend on BOTH memos. `ctx.read` is
              // what records a dependency edge; the body otherwise borrows the
              // AST through `this->program(file)`, a raw `this` read the engine
              // cannot observe. Without the read below, an edit whose parse
              // snapshot differs but whose *resolve* snapshot is byte-identical —
              // a default literal, an operator, a struct field order, a capture
              // list, or any edit that only shifts source offsets — leaves the
              // resolve memo green, so typecheck is green-proven and the previous
              // AST's TypeCheckResult is served for the new AST. The parse
              // snapshot's outline is span-sensitive precisely so that this read
              // (and the resolve one) invalidates on any such edit.
              //
              // The snapshot's value is not needed here — `this->program` below
              // reads the same slot's AST — but the read itself is: it is the
              // dependency edge that keeps this slot honest.
              static_cast<void>(ctx.read(parse_, key));

              // The resolve *snapshot* carries both facts the short-circuit
              // needs: whether the stage ran and whether it errored. (The
              // borrow accessor deliberately refuses to hand out an errored
              // result, so it cannot answer "resolve ran but errored" — the
              // snapshot is the authority.)
              const ResolveSnapshot resolve_snapshot = ctx.read(resolve_, key);

              const std::size_t slot = key.index();
              if (typecheck_slots_.size() <= slot) {
                  typecheck_slots_.resize(slot + 1);
              }
              auto &record = typecheck_slots_[slot];

              // Mirror the CLI pipeline exactly: a parse failure skips resolve,
              // and a resolve error skips typecheck.
              if (!resolve_snapshot.ran || resolve_snapshot.has_errors) {
                  record.result = TypeCheckResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_typecheck_result(record.result, /*ran=*/false);
              }

              const ResolveResult *resolve = this->resolve_result(ModuleId{key.index()});
              const ast::Program *program = this->program(file);
              if (resolve == nullptr || program == nullptr) {
                  // Unreachable when the snapshots above say parse and resolve ran
                  // clean at the current revision; guard anyway rather than
                  // dereference.
                  record.result = TypeCheckResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_typecheck_result(record.result, /*ran=*/false);
              }

              const TypeChecker type_checker;
              record.result = type_checker.check(*program, *resolve);
              record.ran = true;
              ++record.computes;
              return snapshot_typecheck_result(record.result, /*ran=*/true);
          })) {}

void FrontendQueries::set_source_text(FileId file, std::string display_name, std::string text) {
    engine_.set_input(
        source_text_, InputId{file.index()}, SourceText{std::move(display_name), std::move(text)});
}

std::expected<ParseSnapshot, CycleError> FrontendQueries::parse(FileId file) {
    return engine_.eval(parse_, DerivedId{file.index()});
}

std::expected<ResolveSnapshot, CycleError> FrontendQueries::resolve(ModuleId module) {
    return engine_.eval(resolve_, DerivedId{module.index()});
}

std::expected<TypecheckSnapshot, CycleError> FrontendQueries::typecheck(ModuleId module) {
    return engine_.eval(typecheck_, DerivedId{module.index()});
}

std::expected<TypeOfResult, CycleError>
FrontendQueries::type_of(ModuleId module, std::uint64_t node_id, std::optional<SourceId> source_id) {
    // Bring the typecheck memo up to date (a memo hit when nothing changed), then
    // resolve the node id through the typed program's existing reverse index.
    // The stage chain is the only compute; the lookup itself is O(1).
    const auto checked = engine_.eval(typecheck_, DerivedId{module.index()});
    if (!checked.has_value()) {
        return std::unexpected(std::move(checked.error()));
    }

    TypeOfResult result;
    const TypeCheckResult *current = typecheck_result(module);
    if (current == nullptr) {
        return result; // the stage chain short-circuited: no node has a type
    }
    if (const TypedExpr *expr = current->typed_program.find_expr(node_id, source_id);
        expr != nullptr) {
        result.type = expr->type;
        result.found = true;
    }
    return result;
}

const ast::Program *FrontendQueries::program(FileId file) const {
    const std::size_t slot = file.index();
    if (slots_.size() <= slot) {
        return nullptr; // never computed
    }
    // Revision check: borrow only a slot the engine currently considers valid,
    // i.e. Clean (memo produced at the current revision) or Verified (memo
    // green-proven against the current dependency values). A Dirty/Visiting
    // slot holds a superseded text revision's ParseResult; a stale borrow here
    // would silently hand a later query body a different text's AST. State is
    // the right predicate (not verified_at == revision) because the parse
    // slot's only dependency is its own file's text: an edit marks it Dirty
    // eagerly, while an unrelated file's edit leaves it valid.
    const SlotInfo info = engine_.inspect_slot(parse_.family(), slot);
    if (!info.has_value ||
        (info.state != SlotState::Clean && info.state != SlotState::Verified)) {
        return nullptr;
    }
    return slots_[slot].result.program.get();
}

const ResolveResult *FrontendQueries::resolve_result(ModuleId module) const {
    const std::size_t slot = module.index();
    if (resolve_slots_.size() <= slot) {
        return nullptr; // never computed
    }
    // Same Clean/Verified borrow contract as program(): the slot must be valid at
    // the current revision, and the stage must actually have run (a
    // short-circuited resolve stores a default-constructed result that must not
    // be handed out as if it were a resolution).
    const SlotInfo info = engine_.inspect_slot(resolve_.family(), slot);
    if (!info.has_value || (info.state != SlotState::Clean && info.state != SlotState::Verified) ||
        !resolve_slots_[slot].ran) {
        return nullptr;
    }
    return &resolve_slots_[slot].result;
}

const TypeCheckResult *FrontendQueries::typecheck_result(ModuleId module) const {
    const std::size_t slot = module.index();
    if (typecheck_slots_.size() <= slot) {
        return nullptr; // never computed
    }
    const SlotInfo info = engine_.inspect_slot(typecheck_.family(), slot);
    if (!info.has_value || (info.state != SlotState::Clean && info.state != SlotState::Verified) ||
        !typecheck_slots_[slot].ran) {
        return nullptr;
    }
    return &typecheck_slots_[slot].result;
}

std::size_t FrontendQueries::parse_computes(FileId file) const {
    const std::size_t slot = file.index();
    if (slots_.size() <= slot) {
        return 0;
    }
    return slots_[slot].computes;
}

std::size_t FrontendQueries::resolve_computes(ModuleId module) const {
    const std::size_t slot = module.index();
    if (resolve_slots_.size() <= slot) {
        return 0;
    }
    return resolve_slots_[slot].computes;
}

std::size_t FrontendQueries::typecheck_computes(ModuleId module) const {
    const std::size_t slot = module.index();
    if (typecheck_slots_.size() <= slot) {
        return 0;
    }
    return typecheck_slots_[slot].computes;
}

Revision FrontendQueries::revision() const noexcept {
    return engine_.revision();
}

QueryStats FrontendQueries::stats() const {
    return engine_.stats();
}

} // namespace ahfl::query
