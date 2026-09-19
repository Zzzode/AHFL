#pragma once

// RFC 0027 (KR6.11-S5): the differential edit-sequence property harness.
//
// The RFC names this property as THE core acceptance criterion of query-ification:
// under batches of edits the incremental engine must never diverge from a
// cold-cache full recomputation. This header is the single definition of that
// property (CLAUDE.md: no parallel SSOT copies): what an edit is, what the
// observable projection of a run is, and what "incremental == cold" means. Two
// drivers consume it and differ only in who supplies the edit script and how a
// violation is reported:
//
//   * tests/unit/compiler/query/incremental_equiv_property.cpp   (doctest, seeded)
//   * tests/fuzz/fuzz_query_edits.cpp                            (libFuzzer bytes)
//
// The property per edit step, with the step's final text T:
//
//   1. `engine.parse(file)` (long-lived engine that has seen every edit) equals a
//      fresh `FrontendQueries` that has only ever seen T. This is literally
//      "incremental == cold-cache recomputation" and it is compared through the
//      production snapshot definition (ahfl::query::ParseSnapshot).
//   2. The same engine snapshot equals an *independently re-derived* projection
//      of T (dump_program_outline + serialize_diagnostic_report_json), so a
//      regression inside snapshot_parse_result is caught rather than reproduced.
//      This mirrors the S3 guard's discipline; do not route it through
//      snapshot_parse_result.
//   3. The AST the engine holds in its slot, after N edits, drives the *same*
//      downstream pipeline (resolve -> typecheck -> lower) to byte-identical
//      resolve/typecheck diagnostic JSON and IR JSON as a freshly parsed AST of
//      T. resolve/typecheck are still direct-pipeline stages (RFC 0027 P3 /
//      KR6.11-S4 has not landed); because the projection already includes their
//      diagnostic JSON, this guard keeps working unchanged when they move onto
//      the graph — it only gets a new side to compare.
//   4. The parse recompute counter advances by exactly one when, and only when,
//      the text actually changed. This is the invalidation-precision half:
//      "invalidates too much" (a recompute with no cause) and "invalidates too
//      little" (a stale memo served after an edit) are both failures.
//   5. Between the edit and the re-evaluation, `engine.program(file)` reports no
//      AST — a changed text must make the borrow fail closed rather than hand
//      back the superseded revision's program. This is the invariant
//      KR6.11-S3 fixed; here it is asserted under 500 randomized histories
//      instead of a single hand-written edit, because the stale window only
//      exists while a slot is dirty, and only a sequence reliably lands in it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostic_serialization.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/query/frontend_queries.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace ahfl::test_support::query_edit {

// ---------------------------------------------------------------------------
// Deterministic RNG
// ---------------------------------------------------------------------------

// splitmix64. A property test whose failures must be reproducible from a printed
// seed cannot depend on std::mt19937's engine-to-engine/libtstdc++ mapping, so
// the harness carries its own fully specified generator.
class SplitMix64 {
  public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    [[nodiscard]] std::size_t below(std::size_t bound) noexcept {
        return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
    }

  private:
    std::uint64_t state_;
};

// ---------------------------------------------------------------------------
// Edit operators
// ---------------------------------------------------------------------------

enum class EditKind : std::uint8_t { Insert, Delete, Replace, ResetEqual };

inline constexpr std::size_t kEditKindCount = 4;

// The token alphabet the edit operators draw from. Deliberately a mix of
// structure (keywords, terminators), identifiers and a literal so an edit can
// turn a clean source into a diagnostic source and back — equivalence must cover
// the erroring projection, not just the success case. No *opening* bracket is in
// the alphabet: the property under guard is invalidation, not parser robustness
// (that is fuzz_parser's job), and an unbounded nesting generator would only add
// parse-time cost.
inline constexpr std::array<std::string_view, 16> kEditTokens = {
    "struct", "agent",  "state", "flow", "Int",  "f", "x", "y",
    ";",      ":",      "->",    " ",    "\n",   "}", "0", "true",
};

// Upper bound on the mutated text, so a long insert run cannot grow a sequence
// without bound (the cold + direct sides re-parse the text every step).
inline constexpr std::size_t kMaxTextBytes = 16 * 1024;

// One edit request. `offset` and `length` are RAW values reduced modulo the
// current text length at apply time, so a script decoded before the first edit
// stays meaningful as the text length changes. Identity is index-based (an
// EditKind tag + numeric arguments), never a textual description.
struct Edit {
    EditKind kind = EditKind::Insert;
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string token;
};

[[nodiscard]] inline std::string apply_edit(const std::string &text, const Edit &edit) {
    std::string result = text;
    const std::size_t size = result.size();
    switch (edit.kind) {
    case EditKind::Insert: {
        if (size >= kMaxTextBytes) {
            return result; // refuse to grow: the edit degenerates to a no-op
        }
        result.insert(edit.offset % (size + 1), edit.token);
        break;
    }
    case EditKind::Delete: {
        if (size == 0) {
            return result;
        }
        const std::size_t at = edit.offset % size;
        result.erase(at, std::min<std::size_t>(1 + (edit.length % 8), size - at));
        break;
    }
    case EditKind::Replace: {
        if (size == 0) {
            result.insert(0, edit.token);
            break;
        }
        const std::size_t at = edit.offset % size;
        result.erase(at, std::min<std::size_t>(1 + (edit.length % 8), size - at));
        result.insert(at, edit.token);
        break;
    }
    case EditKind::ResetEqual:
        // Deliberately the identity: exercises the equal-write no-op path
        // (no revision bump, no invalidation, no recompute).
        break;
    }
    return result;
}

// Seeded edit generation. Equal-value re-sets are the minority arm so a sequence
// is dominated by real text changes.
[[nodiscard]] inline Edit random_edit(SplitMix64 &rng) {
    Edit edit;
    const std::size_t roll = rng.below(8);
    if (roll == 7) {
        edit.kind = EditKind::ResetEqual;
    } else {
        edit.kind = static_cast<EditKind>(roll % 3);
    }
    edit.offset = static_cast<std::size_t>(rng.next());
    edit.length = static_cast<std::size_t>(rng.next());
    edit.token = std::string{kEditTokens[rng.below(kEditTokens.size())]};
    return edit;
}

// Byte-script decoding for the libFuzzer driver: 5 bytes per edit (kind, 16-bit
// offset, length, token index). An empty or truncated script yields no edits,
// which is a valid (vacuous) input for the fuzzer.
[[nodiscard]] inline std::vector<Edit> decode_edits(const std::uint8_t *data, std::size_t size,
                                                    std::size_t max_edits) {
    std::vector<Edit> edits;
    if (data == nullptr) {
        return edits;
    }
    std::size_t cursor = 0;
    while (cursor + 4 < size && edits.size() < max_edits) {
        Edit edit;
        edit.kind = static_cast<EditKind>(data[cursor] % kEditKindCount);
        edit.offset = static_cast<std::size_t>(data[cursor + 1]) * 256U + data[cursor + 2];
        edit.length = static_cast<std::size_t>(data[cursor + 3]);
        edit.token = std::string{kEditTokens[data[cursor + 4] % kEditTokens.size()]};
        edits.push_back(std::move(edit));
        cursor += 5;
    }
    return edits;
}

// ---------------------------------------------------------------------------
// The observable projection of a run
// ---------------------------------------------------------------------------

// The two byte-comparable projections of a parse, INDEPENDENTLY re-derived (not
// via ahfl::query::snapshot_parse_result) so the guard can catch a regression
// inside that production definition instead of mirroring it.
struct ParseProjection {
    std::string outline;
    std::string diagnostics_json;
    bool has_errors = false;

    [[nodiscard]] friend bool operator==(const ParseProjection &,
                                         const ParseProjection &) noexcept = default;
};

// The downstream artifacts of one AST: the resolve / typecheck diagnostic JSON
// and the full IR JSON. A pipeline short-circuit (resolve errors mean no
// typecheck) is part of the projection, so a divergence in *when* a stage is
// skipped is caught too. A stage that throws is also observable behavior: it is
// encoded in `thrown` rather than aborting the harness, so both sides must agree
// on the throw.
struct DownstreamProjection {
    std::string resolve_diagnostics_json;
    std::string typecheck_diagnostics_json;
    std::string ir_json;
    std::string thrown;

    [[nodiscard]] friend bool operator==(const DownstreamProjection &,
                                         const DownstreamProjection &) noexcept = default;
};

[[nodiscard]] inline ParseProjection project_parse(const ParseResult &parsed) {
    ParseProjection projection;
    projection.has_errors = parsed.has_errors();
    if (parsed.program != nullptr) {
        std::ostringstream outline;
        dump_program_outline(*parsed.program, outline);
        projection.outline = outline.str();
    }
    // A null program (parse failure) leaves the outline empty rather than
    // fabricating one; the diagnostic JSON carries the full reason.
    projection.diagnostics_json =
        serialize_diagnostic_report_json(DiagnosticReport::from_bag(parsed.diagnostics));
    return projection;
}

[[nodiscard]] inline ParseProjection project_snapshot(const query::ParseSnapshot &snapshot) {
    return ParseProjection{snapshot.outline, snapshot.diagnostics_json, snapshot.has_errors};
}

// Mirror of the CLI pipeline's short-circuits so the projection is the artifact
// a user would actually see (ahflc: parse -> resolve -> typecheck -> lower).
[[nodiscard]] inline DownstreamProjection project_downstream(const ast::Program &program) {
    DownstreamProjection projection;
    try {
        const Resolver resolver;
        const ResolveResult resolve = resolver.resolve(program);
        projection.resolve_diagnostics_json =
            serialize_diagnostic_report_json(DiagnosticReport::from_bag(resolve.diagnostics));
        if (resolve.has_errors()) {
            return projection;
        }
        const TypeChecker checker;
        const TypeCheckResult checked = checker.check(program, resolve);
        projection.typecheck_diagnostics_json =
            serialize_diagnostic_report_json(DiagnosticReport::from_bag(checked.diagnostics));
        if (checked.has_errors()) {
            return projection;
        }
        std::ostringstream ir;
        print_program_ir_json(lower_program_ir(program, resolve, checked), ir);
        projection.ir_json = ir.str();
    } catch (const std::exception &exception) {
        projection.thrown = exception.what();
    } catch (...) {
        projection.thrown = "<non-std exception>";
    }
    return projection;
}

// ---------------------------------------------------------------------------
// The property itself
// ---------------------------------------------------------------------------

struct SequenceOutcome {
    std::size_t steps = 0;
    // Steps whose edit actually altered the text. Not necessarily `steps`: an
    // Insert at the size cap, a Delete on empty text, a Replace that reinstates
    // the same bytes, and ResetEqual are all legitimate no-ops, and the property
    // is stated against this count rather than against `steps`.
    std::size_t changed_steps = 0;
    std::size_t mismatches = 0;
    std::size_t erroring_parses = 0;
    std::size_t clean_parses = 0;
    std::size_t nonempty_ir = 0;
    std::size_t recomputes = 0;
    std::string first_mismatch;

    [[nodiscard]] bool ok() const noexcept {
        return mismatches == 0 && steps > 0;
    }
};

[[nodiscard]] inline std::string describe(const ParseProjection &projection) {
    std::ostringstream out;
    out << "has_errors=" << (projection.has_errors ? "true" : "false")
        << " outline_bytes=" << projection.outline.size()
        << " diagnostics_bytes=" << projection.diagnostics_json.size();
    return out.str();
}

[[nodiscard]] inline std::string describe(const DownstreamProjection &projection) {
    std::ostringstream out;
    out << "resolve_bytes=" << projection.resolve_diagnostics_json.size()
        << " typecheck_bytes=" << projection.typecheck_diagnostics_json.size()
        << " ir_bytes=" << projection.ir_json.size();
    if (!projection.thrown.empty()) {
        out << " thrown=\"" << projection.thrown << "\"";
    }
    return out.str();
}

inline void record_mismatch(SequenceOutcome &outcome, std::size_t step, std::string message) {
    ++outcome.mismatches;
    if (outcome.first_mismatch.empty()) {
        outcome.first_mismatch = "step " + std::to_string(step) + ": " + std::move(message);
    }
}

// Drive one edit sequence through a long-lived engine and, at every step, assert
// the whole projection against cold recomputation. The engine is created inside,
// so each sequence starts from a genuinely cold cache; the display name is held
// constant across the sequence so diagnostics that embed it stay comparable.
[[nodiscard]] inline SequenceOutcome run_edit_sequence(std::string_view display_name,
                                                       std::string_view seed_text,
                                                       const std::vector<Edit> &edits) {
    SequenceOutcome outcome;
    const std::string name{display_name};

    query::FrontendQueries engine;
    const query::FileId file{0};
    engine.set_source_text(file, name, std::string{seed_text});
    // Prime the cold cache: the first evaluation is unconditional (nothing was
    // computed yet), so the per-step recompute counter below must start from a
    // slot that is already valid at the seed revision.
    if (!engine.parse(file).has_value()) {
        record_mismatch(outcome, 0, "cold engine parse(file) on the seed reported a cycle error");
        return outcome;
    }
    const std::size_t baseline_computes = engine.parse_computes(file);
    if (baseline_computes != 1) {
        record_mismatch(outcome, 0,
                        "seed evaluation computed " + std::to_string(baseline_computes) +
                            " time(s), expected exactly 1");
    }

    std::string text{seed_text};
    for (std::size_t index = 0; index < edits.size(); ++index) {
        const std::string next_text = apply_edit(text, edits[index]);
        const bool changed = next_text != text;
        // Valid at the seed revision thanks to the priming evaluation above.
        const ast::Program *const program_before = engine.program(file);
        engine.set_source_text(file, name, next_text);
        text = next_text;
        ++outcome.steps;
        if (changed) {
            ++outcome.changed_steps;
        }

        // (5) Stale-borrow window. A text change dirties the slot, so until the
        // parse is re-driven there is no AST for the current revision; handing
        // back the superseded program here is exactly the class of bug this
        // property exists to catch (and the one S3's revision check fixes).
        const ast::Program *stale = engine.program(file);
        if (changed && stale != nullptr) {
            record_mismatch(outcome, index,
                            "program(file) returned non-null inside the dirty window after a "
                            "text-changing edit (stale AST borrow)");
        }

        // (4) Invalidation precision: exactly one recompute per real edit, none
        // for an equal re-set.
        const std::size_t before = engine.parse_computes(file);
        const auto engine_snapshot = engine.parse(file);
        const std::size_t after = engine.parse_computes(file);
        if (!engine_snapshot) {
            record_mismatch(outcome, index, "engine parse(file) reported a cycle error");
            continue;
        }
        const std::size_t delta = after - before;
        outcome.recomputes += delta;
        const std::size_t expected_delta = changed ? 1U : 0U;
        if (delta != expected_delta) {
            record_mismatch(outcome, index,
                            "parse recompute delta " + std::to_string(delta) + " != " +
                                std::to_string(expected_delta) + " (text_changed=" +
                                (changed ? std::string{"true"} : std::string{"false"}) + ")");
        }

        // (1) Incremental == cold-cache: a fresh graph that has only ever seen
        // the step's final text.
        query::FrontendQueries cold;
        cold.set_source_text(file, name, text);
        const auto cold_snapshot = cold.parse(file);
        if (!cold_snapshot) {
            record_mismatch(outcome, index, "cold engine parse(file) reported a cycle error");
            continue;
        }
        const ParseProjection incremental = project_snapshot(*engine_snapshot);
        const ParseProjection cold_projection = project_snapshot(*cold_snapshot);
        if (incremental != cold_projection) {
            record_mismatch(outcome, index,
                            "incremental != cold-cache parse (" + describe(incremental) + " vs " +
                                describe(cold_projection) + ")");
        }

        // (2) Independent re-derivation through the direct frontend.
        const Frontend frontend;
        const ParseResult direct = frontend.parse_text(name, text);
        const ParseProjection direct_projection = project_parse(direct);
        if (incremental != direct_projection) {
            record_mismatch(outcome, index,
                            "engine snapshot != independent direct projection (" +
                                describe(incremental) + " vs " + describe(direct_projection) + ")");
        }
        if (direct_projection.has_errors) {
            ++outcome.erroring_parses;
        } else {
            ++outcome.clean_parses;
        }

        // (3) The engine-held AST must drive the downstream pipeline to the same
        // bytes as a freshly parsed AST of the same text.
        const ast::Program *borrowed = engine.program(file);
        const bool direct_has_program = direct.program != nullptr;
        if ((borrowed != nullptr) != direct_has_program) {
            record_mismatch(outcome, index,
                            "engine program(file) presence " +
                                (borrowed != nullptr ? std::string{"non-null"}
                                                     : std::string{"null"}) +
                                " != direct frontend program presence " +
                                (direct_has_program ? std::string{"non-null"}
                                                    : std::string{"null"}));
        }
        if (borrowed != nullptr && direct_has_program && !incremental.has_errors) {
            const DownstreamProjection engine_downstream = project_downstream(*borrowed);
            const DownstreamProjection direct_downstream = project_downstream(*direct.program);
            if (engine_downstream != direct_downstream) {
                record_mismatch(outcome, index,
                                "downstream divergence (engine " + describe(engine_downstream) +
                                    " vs direct " + describe(direct_downstream) + ")");
            }
            if (!engine_downstream.ir_json.empty()) {
                ++outcome.nonempty_ir;
            }
        }

        // (5, cont.) After the edit has been re-driven the borrow is live again
        // and must describe the CURRENT text. Compared by dumping the borrowed
        // AST here rather than by pointer identity: pointer bytes are an allocator
        // detail, whereas the outline is the observable claim ("the AST you can
        // borrow right now is this text's") that gets falsified by a stale memo.
        // An equal-valued edit is the other half: it must leave the memo — and
        // therefore the borrowed pointer — untouched.
        if (borrowed != nullptr && !incremental.has_errors) {
            std::ostringstream borrowed_outline;
            dump_program_outline(*borrowed, borrowed_outline);
            if (borrowed_outline.str() != direct_projection.outline) {
                record_mismatch(outcome, index,
                                "borrowed AST outline != direct outline of the same text "
                                "(stale or superseded AST served)");
            }
        }
        if (!changed && borrowed != program_before) {
            record_mismatch(outcome, index,
                            "an equal-valued edit moved the borrowable AST (memo was needlessly "
                            "rebuilt)");
        }
    }
    return outcome;
}

// Build a seeded edit script.
[[nodiscard]] inline std::vector<Edit> seeded_edits(std::uint64_t seed, std::size_t count) {
    SplitMix64 rng{seed};
    std::vector<Edit> edits;
    edits.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        edits.push_back(random_edit(rng));
    }
    return edits;
}

} // namespace ahfl::test_support::query_edit
