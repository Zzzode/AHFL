#include "ahfl/compiler/query/frontend_queries.hpp"

#include "compiler/syntax/frontend/project.hpp"

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

HirSnapshot snapshot_hir(const TypeCheckResult &result, bool ran) {
    HirSnapshot snapshot;
    snapshot.ran = ran;
    // hir's identity is the typed HIR alone — the same one-SSOT projection
    // typecheck uses for its typed_program_json half, deliberately without the
    // diagnostics (those stay the identity of the typecheck node).
    if (ran) {
        snapshot.typed_program_json = serialize_typed_program_json(result.typed_program);
    }
    // A short-circuited chain (parse/resolve failed) leaves the projection empty;
    // `ran` distinguishes it from a clean run, exactly as the stage snapshots do.
    return snapshot;
}

void dump_project_graph_outline(const SourceGraph &graph, std::ostream &out) {
    // Deterministic by construction: entries and sources are projected in their
    // own store order (the parser's deterministic load order), so the outline is
    // a stable function of the parse result.
    out << "entries " << graph.entry_sources.size() << '\n';
    for (const auto entry : graph.entry_sources) {
        out << "  entry " << entry.value << '\n';
    }

    out << "sources " << graph.sources.size() << '\n';
    for (const auto &source : graph.sources) {
        out << "  source " << source.id.value << ' ' << source.module_name << " prefix "
            << source.package_prefix << " exported " << (source.module_exported ? 1 : 0) << '\n';
        out << "    artifacts " << source.artifact_exports.size();
        for (const auto &artifact : source.artifact_exports) {
            out << ' ' << artifact;
        }
        out << '\n';
        out << "    dependencies " << source.dependency_prefixes.size();
        for (const auto &dependency : source.dependency_prefixes) {
            out << ' ' << dependency;
        }
        out << '\n';
        out << "    intrinsics "
            << (source.compiler_intrinsics_allow.has_value()
                    ? std::to_string(source.compiler_intrinsics_allow->size())
                    : std::string{"-"});
        if (source.compiler_intrinsics_allow.has_value()) {
            for (const auto &entry : *source.compiler_intrinsics_allow) {
                out << ' ' << entry;
            }
        }
        out << '\n';
        out << "    module_range ";
        append_range(out, source.module_range);
        out << '\n';
        out << "    imports " << source.imports.size() << '\n';
        for (const auto &import : source.imports) {
            out << "      import " << import.module_name << ' ' << import.alias << ' ';
            append_range(out, import.range);
            out << '\n';
        }
        // The AST outline is the span- and content-sensitive half: it is what
        // makes "same modules, different bodies" a different project parse, and
        // it is exactly the projection the semantic stages consume.
        out << "    ast\n";
        if (source.program != nullptr) {
            dump_program_outline(*source.program, out);
        }
    }

    // module_to_source is a hash map; project it in a canonical (sorted) order so
    // the outline is stable regardless of bucket layout.
    std::vector<std::pair<std::string, std::size_t>> module_owners;
    module_owners.reserve(graph.module_to_source.size());
    for (const auto &[module_name, source_id] : graph.module_to_source) {
        module_owners.emplace_back(module_name, source_id.value);
    }
    std::sort(module_owners.begin(), module_owners.end());
    out << "module_owners " << module_owners.size() << '\n';
    for (const auto &[module_name, source_id] : module_owners) {
        out << "  owner " << module_name << ' ' << source_id << '\n';
    }

    out << "import_edges " << graph.import_edges.size() << '\n';
    for (const auto &edge : graph.import_edges) {
        out << "  edge " << edge.importer.value << " -> " << edge.imported.value << ' '
            << edge.request.module_name << ' ' << edge.request.alias << ' ';
        append_range(out, edge.request.range);
        out << '\n';
    }
}

ProjectParseSnapshot snapshot_project_parse_result(const ProjectParseResult &result) {
    ProjectParseSnapshot snapshot;
    snapshot.has_errors = result.has_errors();

    // A failed project parse still yields a graph (partial sources may exist);
    // projecting it keeps "which units made it in" part of the identity, exactly
    // as the parse snapshot keeps a null program distinct from a real one.
    std::ostringstream outline;
    dump_project_graph_outline(result.graph, outline);
    snapshot.graph_outline = outline.str();

    snapshot.diagnostics_json =
        serialize_diagnostic_report_json(DiagnosticReport::from_bag(result.diagnostics));
    return snapshot;
}

FrontendQueries::FrontendQueries(FrontendOptions options)
    : frontend_(options), engine_(CyclePolicy::Error),
      source_text_(engine_.register_input<SourceText>()),
      project_input_(engine_.register_input<ProjectInputModel>()),
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
          })),
      hir_(engine_.register_derived<HirSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> HirSnapshot {
              // hir is a pure VIEW over the typecheck memo: this read is the
              // whole dependency edge (it transitively covers resolve and parse,
              // and the AST's span-sensitive outline through them), so hir
              // invalidates on exactly the chain that produces the typed program.
              // hir never runs the type checker itself; the count below measures
              // only the (cheap) projection, and typecheck_computes the checker.
              const TypecheckSnapshot typecheck_snapshot = ctx.read(typecheck_, key);

              const std::size_t slot = key.index();
              if (hir_compute_slots_.size() <= slot) {
                  hir_compute_slots_.resize(slot + 1);
              }
              ++hir_compute_slots_[slot];

              // The snapshot carries whether the chain actually produced a typed
              // program (the CLI's short-circuit structure); mirror the
              // typecheck body by deciding on the snapshot and then borrowing the
              // object the projection is taken of.
              if (!typecheck_snapshot.ran) {
                  return HirSnapshot{};
              }
              const TypeCheckResult *result = this->typecheck_result(ModuleId{key.index()});
              if (result == nullptr) {
                  // Unreachable when the snapshot says typecheck ran clean at the
                  // current revision; guard anyway rather than dereference.
                  return HirSnapshot{};
              }
              return snapshot_hir(*result, /*ran=*/true);
          })),
      project_parse_(engine_.register_derived<ProjectParseSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> ProjectParseSnapshot {
              // The project parse is a pure function of the model for every
              // source the freeze walk could enumerate: the resolved config
              // plus the frozen text of every reachable source. Prelude
              // injection, module-root discovery and import-edge resolution are
              // deterministic and live behind this one input. The only disk
              // access left is a snapshot miss — a candidate the walk could not
              // enumerate (e.g. beneath a non-listable directory) — where both
              // the existence gate and the source reader consult the candidate
              // path directly, exactly as the pre-model direct pipeline did.
              const ProjectInputModel &model = ctx.get(project_input_, InputId{key.index()});
              // Qualified: the member `FrontendQueries::parse_project(ProjectId)` would
              // otherwise hide the free function of the same name.
              ProjectParseResult result = ahfl::parse_project(frontend_, model);
              ProjectParseSnapshot snapshot = snapshot_project_parse_result(result);

              const std::size_t slot = key.index();
              if (project_slots_.size() <= slot) {
                  project_slots_.resize(slot + 1);
              }
              // Replacing the whole graph drops the previous units' ASTs at exactly
              // the point the memo is replaced — graph and snapshot never disagree
              // about which model they came from. The project-level parse
              // diagnostic bag moves into the same slot record for the same
              // reason: the LSP publishes it alongside the graph.
              project_slots_[slot].graph = std::move(result.graph);
              project_slots_[slot].diagnostics = std::move(result.diagnostics);
              ++project_slots_[slot].computes;
              return snapshot;
          })),
      project_resolve_(engine_.register_derived<ResolveSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> ResolveSnapshot {
              // Bring the project parse slot up to date; the resolver reads the
              // graph's units and import edges. The snapshot carries whether the
              // parse errored, which is the CLI's short-circuit condition: the
              // pipeline stops before resolving a failed project parse.
              const ProjectParseSnapshot parse_snapshot = ctx.read(project_parse_, key);

              const std::size_t slot = key.index();
              if (project_resolve_slots_.size() <= slot) {
                  project_resolve_slots_.resize(slot + 1);
              }
              auto &record = project_resolve_slots_[slot];

              const SourceGraph *graph = this->project_graph(ProjectId{key.index()});
              if (parse_snapshot.has_errors || graph == nullptr) {
                  // Parse errored (stage skipped, as in the CLI) or no graph at
                  // the current revision (the slot is dirty): never resolve.
                  record.result = ResolveResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_resolve_result(record.result, /*ran=*/false);
              }

              const Resolver resolver;
              record.result = resolver.resolve(*graph);
              record.ran = true;
              ++record.computes;
              return snapshot_resolve_result(record.result, /*ran=*/true);
          })),
      project_typecheck_(engine_.register_derived<TypecheckSnapshot>(
          [this](QueryContext &ctx, DerivedId key) -> TypecheckSnapshot {
              // Both the resolve result AND the graph are consumed by the type
              // checker, and the graph outline is the span- and content-sensitive
              // edge; take an explicit dependency on the project parse memo so an
              // AST-only edit cannot leave this slot green-proven.
              static_cast<void>(ctx.read(project_parse_, key));
              const ResolveSnapshot resolve_snapshot = ctx.read(project_resolve_, key);

              const std::size_t slot = key.index();
              if (project_typecheck_slots_.size() <= slot) {
                  project_typecheck_slots_.resize(slot + 1);
              }
              auto &record = project_typecheck_slots_[slot];

              // Mirror the CLI pipeline: a project parse error skips resolve, and a
              // resolve error skips typecheck.
              if (!resolve_snapshot.ran || resolve_snapshot.has_errors) {
                  record.result = TypeCheckResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_typecheck_result(record.result, /*ran=*/false);
              }

              const ResolveResult *resolve = this->project_resolve_result(ProjectId{key.index()});
              const SourceGraph *graph = this->project_graph(ProjectId{key.index()});
              if (resolve == nullptr || graph == nullptr) {
                  record.result = TypeCheckResult{};
                  record.ran = false;
                  ++record.computes;
                  return snapshot_typecheck_result(record.result, /*ran=*/false);
              }

              const TypeChecker type_checker;
              record.result = type_checker.check(*graph, *resolve);
              record.ran = true;
              ++record.computes;
              return snapshot_typecheck_result(record.result, /*ran=*/true);
          })) {}

void FrontendQueries::set_source_text(FileId file, std::string display_name, std::string text) {
    engine_.set_input(
        source_text_, InputId{file.index()}, SourceText{std::move(display_name), std::move(text)});
}

void FrontendQueries::set_project_input(ProjectId project, ProjectInputModel model) {
    engine_.set_input(project_input_, InputId{project.index()}, std::move(model));
}

void FrontendQueries::set_project_input(ProjectId project, const ProjectInput &input) {
    set_project_input(project, resolve_project_input(input));
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

std::expected<HirSnapshot, CycleError> FrontendQueries::hir(FileId file) {
    return engine_.eval(hir_, DerivedId{file.index()});
}

std::expected<ProjectParseSnapshot, CycleError>
FrontendQueries::parse_project(ProjectId project) {
    return engine_.eval(project_parse_, DerivedId{project.index()});
}

std::expected<ResolveSnapshot, CycleError>
FrontendQueries::project_resolve(ProjectId project) {
    return engine_.eval(project_resolve_, DerivedId{project.index()});
}

std::expected<TypecheckSnapshot, CycleError>
FrontendQueries::project_typecheck(ProjectId project) {
    return engine_.eval(project_typecheck_, DerivedId{project.index()});
}

std::expected<TypeOfResult, CycleError>
FrontendQueries::type_of(ModuleId module, std::uint64_t node_id, std::optional<SourceId> source_id) {
    // Drive the memo through hir (the node the RFC's graph points LSP at); in
    // the file-scoped graph ModuleId slot i and FileId slot i name the same
    // unit. The stage chain is the only compute; the lookup itself is O(1).
    const FileId file{module.index()};
    const auto evaluated = hir(file);
    if (!evaluated.has_value()) {
        return std::unexpected(std::move(evaluated.error()));
    }

    const auto expr = hir_expr(file, node_id, std::move(source_id));
    if (!expr.has_value()) {
        return std::unexpected(std::move(expr.error()));
    }

    TypeOfResult result;
    if (*expr != nullptr) {
        result.type = (*expr)->type;
        result.found = true;
    }
    return result;
}

std::expected<const TypedExpr *, CycleError>
FrontendQueries::hir_expr(FileId file, std::uint64_t node_id, std::optional<SourceId> source_id) {
    // Bring the hir memo up to date (a memo hit when nothing changed). Driving
    // here — rather than assuming a prior hir() — is what keeps the O(1) lookup
    // sound on a stale slot: the engine never lets us index a superseded text's
    // typed program.
    const auto evaluated = engine_.eval(hir_, DerivedId{file.index()});
    if (!evaluated.has_value()) {
        return std::unexpected(std::move(evaluated.error()));
    }

    const TypedProgram *program = typed_program(file);
    if (program == nullptr) {
        return nullptr; // the stage chain short-circuited: no node has a type
    }
    return program->find_expr(node_id, std::move(source_id));
}

const TypedProgram *FrontendQueries::typed_program(FileId file) const {
    // The same Clean/Verified borrow contract as typecheck_result(): hand out
    // only the typed program of a hir/typecheck slot valid at the current
    // revision whose stage actually ran. The TypedProgram is owned by the
    // typecheck slot store; inspecting that family is the revision edge that
    // keeps this fail-closed (hir_ is a pure projection of it).
    const std::size_t slot = file.index();
    if (typecheck_slots_.size() <= slot || !typecheck_slots_[slot].ran) {
        return nullptr;
    }
    const SlotInfo info = engine_.inspect_slot(hir_.family(), slot);
    if (!info.has_value ||
        (info.state != SlotState::Clean && info.state != SlotState::Verified)) {
        return nullptr;
    }
    return &typecheck_slots_[slot].result.typed_program;
}

std::size_t FrontendQueries::hir_computes(FileId file) const {
    const std::size_t slot = file.index();
    if (hir_compute_slots_.size() <= slot) {
        return 0;
    }
    return hir_compute_slots_[slot];
}

const SourceGraph *FrontendQueries::project_graph(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_slots_.size() <= slot) {
        return nullptr; // never computed
    }
    // The same Clean/Verified revision contract as program(): a model edit marks
    // the slot Dirty eagerly, so a superseded model's graph is never handed out.
    const SlotInfo info = engine_.inspect_slot(project_parse_.family(), slot);
    if (!info.has_value ||
        (info.state != SlotState::Clean && info.state != SlotState::Verified)) {
        return nullptr;
    }
    return &project_slots_[slot].graph;
}

const DiagnosticBag *FrontendQueries::project_parse_diagnostics(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_slots_.size() <= slot) {
        return nullptr;
    }
    const SlotInfo info = engine_.inspect_slot(project_parse_.family(), slot);
    if (!info.has_value ||
        (info.state != SlotState::Clean && info.state != SlotState::Verified)) {
        return nullptr;
    }
    return &project_slots_[slot].diagnostics;
}

const ResolveResult *FrontendQueries::project_resolve_result(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_resolve_slots_.size() <= slot) {
        return nullptr;
    }
    const SlotInfo info = engine_.inspect_slot(project_resolve_.family(), slot);
    if (!info.has_value || (info.state != SlotState::Clean && info.state != SlotState::Verified) ||
        !project_resolve_slots_[slot].ran) {
        return nullptr;
    }
    return &project_resolve_slots_[slot].result;
}

const TypeCheckResult *FrontendQueries::project_typecheck_result(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_typecheck_slots_.size() <= slot) {
        return nullptr;
    }
    const SlotInfo info = engine_.inspect_slot(project_typecheck_.family(), slot);
    if (!info.has_value || (info.state != SlotState::Clean && info.state != SlotState::Verified) ||
        !project_typecheck_slots_[slot].ran) {
        return nullptr;
    }
    return &project_typecheck_slots_[slot].result;
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

const ParseResult *FrontendQueries::parse_result(FileId file) const {
    const std::size_t slot = file.index();
    if (slots_.size() <= slot) {
        return nullptr;
    }
    // Same Clean/Verified revision contract as program(): the result handed
    // out must belong to the slot's current text. Unlike program() the result
    // is available even for a parse that errored — an errored parse stores a
    // null program, but its source and diagnostics are exactly what an LSP
    // consumer publishes.
    const SlotInfo info = engine_.inspect_slot(parse_.family(), slot);
    if (!info.has_value ||
        (info.state != SlotState::Clean && info.state != SlotState::Verified)) {
        return nullptr;
    }
    return &slots_[slot].result;
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

std::size_t FrontendQueries::project_parse_computes(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_slots_.size() <= slot) {
        return 0;
    }
    return project_slots_[slot].computes;
}

std::size_t FrontendQueries::project_resolve_computes(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_resolve_slots_.size() <= slot) {
        return 0;
    }
    return project_resolve_slots_[slot].computes;
}

std::size_t FrontendQueries::project_typecheck_computes(ProjectId project) const {
    const std::size_t slot = project.index();
    if (project_typecheck_slots_.size() <= slot) {
        return 0;
    }
    return project_typecheck_slots_[slot].computes;
}

Revision FrontendQueries::revision() const noexcept {
    return engine_.revision();
}

QueryStats FrontendQueries::stats() const {
    return engine_.stats();
}

} // namespace ahfl::query
