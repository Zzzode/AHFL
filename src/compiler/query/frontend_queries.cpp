#include "ahfl/compiler/query/frontend_queries.hpp"

#include <sstream>
#include <utility>

#include "ahfl/base/support/diagnostic_serialization.hpp"

namespace ahfl::query {

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
          })) {}

void FrontendQueries::set_source_text(FileId file, std::string display_name, std::string text) {
    engine_.set_input(
        source_text_, InputId{file.index()}, SourceText{std::move(display_name), std::move(text)});
}

std::expected<ParseSnapshot, CycleError> FrontendQueries::parse(FileId file) {
    return engine_.eval(parse_, DerivedId{file.index()});
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

std::size_t FrontendQueries::parse_computes(FileId file) const {
    const std::size_t slot = file.index();
    if (slots_.size() <= slot) {
        return 0;
    }
    return slots_[slot].computes;
}

Revision FrontendQueries::revision() const noexcept {
    return engine_.revision();
}

QueryStats FrontendQueries::stats() const {
    return engine_.stats();
}

} // namespace ahfl::query
