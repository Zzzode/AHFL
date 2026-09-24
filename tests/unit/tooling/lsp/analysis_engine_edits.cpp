// RFC 0027 P4 (KR6.12): deterministic edit-sequence equivalence for the LSP
// analysis engine. This test runs at the pure AnalysisService level (no JSON-RPC,
// no display server, no VS Code): it opens documents, applies a fixed script of
// edits, closes and reopens them, and after EVERY step asserts that what the
// workspace QueryEngine serves (parse diagnostics, resolve diagnostics,
// typecheck diagnostics, and the typed-HIR JSON the hover/completion/signature
// handlers read) is byte-equivalent to an INDEPENDENT cold pipeline driven
// directly over the same final text.
//
// This is the LSP-side counterpart of ahfl.query.incremental_equiv_all: that
// guard pins FrontendQueries against a cold engine on random edit scripts;
// this one pins the whole AnalysisService (slot allocation, overlay-driven
// project inputs, snapshot invalidation, borrowed stage results) against the
// direct Frontend/Resolver/TypeChecker pipeline the LSP used before P4. The
// script is deterministic (no RNG): a fixed list of edits chosen to move a
// document through clean -> parse-broken -> type-broken -> clean, plus
// equal-text writes (memo no-ops) and cross-file edits (slot isolation).
#include "tooling/lsp/analysis_service.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace ahfl::lsp;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name.c_str());
    } else {
        std::printf("  FAIL: %s\n", name.c_str());
    }
}

// One stable projection of a semantic analysis, shared by the query-engine
// side and the independent cold-pipeline side. Order-independent for
// diagnostics (the two pipelines must agree on the SET, not bag iteration
// side-effects), exact for the typed-HIR JSON (the artifact hover reads).
struct Projection {
    std::vector<std::string> parse_diagnostics;
    std::vector<std::string> resolve_diagnostics;
    std::vector<std::string> typecheck_diagnostics;
    std::string typed_program_json; // empty when typecheck did not run

    [[nodiscard]] bool operator==(const Projection &other) const {
        return parse_diagnostics == other.parse_diagnostics &&
               resolve_diagnostics == other.resolve_diagnostics &&
               typecheck_diagnostics == other.typecheck_diagnostics &&
               typed_program_json == other.typed_program_json;
    }
};

[[nodiscard]] std::vector<std::string> codes_of(const ahfl::DiagnosticBag &bag) {
    std::vector<std::string> facts;
    facts.reserve(bag.entries().size());
    for (const auto &diagnostic : bag.entries()) {
        // Range + code + message: the whole diagnostic identity the LSP
        // publishes. Both sides parse the same text, so every field must agree.
        std::string fact;
        if (diagnostic.range.has_value()) {
            fact += std::to_string(diagnostic.range->begin_offset);
            fact += ":";
            fact += std::to_string(diagnostic.range->end_offset);
        }
        fact += ":";
        fact += diagnostic.code.value_or("");
        fact += ":";
        fact += diagnostic.message;
        fact += ":";
        fact += diagnostic.source_name.value_or("");
        facts.push_back(std::move(fact));
    }
    std::sort(facts.begin(), facts.end());
    return facts;
}

// The independent cold reference: parse / resolve / typecheck driven directly,
// exactly as the LSP did before P4. Never touches a QueryEngine.
[[nodiscard]] Projection cold_projection(const std::string &uri, const std::string &text) {
    ahfl::Frontend frontend;
    ahfl::Resolver resolver;
    ahfl::TypeChecker type_checker;

    Projection projection;
    auto parse_result = frontend.parse_text(uri, text);
    projection.parse_diagnostics = codes_of(parse_result.diagnostics);
    if (parse_result.has_errors() || !parse_result.program) {
        return projection;
    }
    auto resolve_result = resolver.resolve(*parse_result.program);
    projection.resolve_diagnostics = codes_of(resolve_result.diagnostics);
    if (resolve_result.has_errors()) {
        return projection;
    }
    auto type_result = type_checker.check(*parse_result.program, resolve_result);
    projection.typecheck_diagnostics = codes_of(type_result.diagnostics);
    if (!type_result.has_errors()) {
        projection.typed_program_json =
            ahfl::serialize_typed_program_json(type_result.typed_program);
    }
    return projection;
}

// The query-engine side: whatever the AnalysisService serves for the URI right
// now, projected into the same shape.
[[nodiscard]] Projection engine_projection(const LspAnalysisSnapshot &snapshot) {
    Projection projection;
    if (snapshot.parse_result != nullptr) {
        projection.parse_diagnostics = codes_of(snapshot.parse_result->diagnostics);
    }
    if (snapshot.resolve_result != nullptr) {
        projection.resolve_diagnostics = codes_of(snapshot.resolve_result->diagnostics);
    }
    if (snapshot.type_check_result != nullptr) {
        projection.typecheck_diagnostics =
            codes_of(snapshot.type_check_result->diagnostics);
        if (!snapshot.type_check_result->has_errors()) {
            projection.typed_program_json =
                ahfl::serialize_typed_program_json(snapshot.type_check_result->typed_program);
        }
    }
    return projection;
}

constexpr std::string_view kCleanSource =
    "fn inspect(x: Int) -> Int effect Pure decreases 0 {\n"
    "    let item = x;\n"
    "    return item;\n"
    "}\n";

constexpr std::string_view kTypeErrorSource =
    "fn inspect(x: Int) -> Int effect Pure decreases 0 {\n"
    "    let item = \"hello\";\n"
    "    return item;\n"
    "}\n";

constexpr std::string_view kFixedSource =
    "fn inspect(x: String) -> String effect Pure decreases 0 {\n"
    "    let item = \"hello\";\n"
    "    return item;\n"
    "}\n";

constexpr std::string_view kParseErrorSource =
    "fn inspect(x: Int) -> Int effect Pure decreases 0 {\n"
    "    let item = ;\n"
    "}\n";

// An unrelated document used to assert cross-slot isolation: editing it must
// never invalidate the observed URI's query results.
constexpr std::string_view kOtherCleanSource =
    "fn other(y: Int) -> Int effect Pure decreases 0 {\n"
    "    return y;\n"
    "}\n";
constexpr std::string_view kOtherEditedSource =
    "fn other(y: Int) -> Int effect Pure decreases 0 {\n"
    "    let z = y + 1;\n"
    "    return z;\n"
    "}\n";

struct EditStep {
    std::string target_uri;
    int version;
    std::string text;
};

void test_edit_sequence_equiv_cold() {
    const std::string uri = "file:///edit-sequence-equiv.ahfl";
    const std::string other_uri = "file:///edit-sequence-other.ahfl";

    // The fixed deterministic edit script. Equal-text steps exercise the
    // input-equality no-op; the malformed/type-error steps exercise
    // short-circuit; steps on other_uri exercise cross-slot isolation.
    const std::vector<EditStep> steps = {
        {uri, 1, std::string{kCleanSource}},
        {uri, 2, std::string{kCleanSource}}, // equal text: memo no-op
        {uri, 3, std::string{kTypeErrorSource}}, // clean -> type error
        {other_uri, 1, std::string{kOtherCleanSource}},
        {uri, 4, std::string{kParseErrorSource}}, // type error -> parse error
        {other_uri, 2, std::string{kOtherEditedSource}}, // unrelated edit
        {uri, 5, std::string{kTypeErrorSource}}, // parse error -> type error
        {uri, 6, std::string{kCleanSource}}, // back to clean
        {uri, 7, std::string{kFixedSource}}, // signature edit, still clean
        {uri, 8, std::string{kFixedSource}}, // equal text again
    };

    DocumentStore store;
    AnalysisService analysis(store);
    std::size_t mismatches = 0;

    auto reconcile = [&](const std::string &target_uri, int step) {
        const auto *document = store.get(target_uri);
        if (document == nullptr) {
            return;
        }
        const auto *snapshot = analysis.snapshot_for_uri(target_uri);
        if (snapshot == nullptr) {
            std::printf("  FAIL: step %d produced no snapshot for %s\n",
                        step,
                        target_uri.c_str());
            ++mismatches;
            return;
        }
        const auto expected = cold_projection(target_uri, document->text);
        const auto actual = engine_projection(*snapshot);
        if (!(actual == expected)) {
            std::printf("  FAIL: step %d query-engine projection != cold pipeline for %s\n",
                        step,
                        target_uri.c_str());
            ++mismatches;
        }
    };

    // Open both documents and reconcile the initial state.
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 0,
        .text = std::string{kCleanSource},
    });
    store.open(TextDocumentItem{
        .uri = other_uri,
        .language_id = "ahfl",
        .version = 0,
        .text = std::string{kOtherCleanSource},
    });
    reconcile(uri, 0);
    reconcile(other_uri, 0);

    for (std::size_t index = 0; index < steps.size(); ++index) {
        const auto &step = steps[index];
        store.change(step.target_uri, step.version, step.text);
        // The DocumentStore change does not itself invalidate analysis; the
        // server invalidates affected paths on didChange. Mimic that exactly:
        const auto path = AnalysisService::path_from_uri(step.target_uri);
        if (path.has_value()) {
            analysis.invalidate_paths({*path});
        }
        reconcile(step.target_uri, static_cast<int>(index + 1));
        // After every step the other document must still be served correctly,
        // proving an edit to one slot never corrupts another.
        reconcile(uri == step.target_uri ? other_uri : uri, static_cast<int>(index + 1));
    }

    check(mismatches == 0,
          "editSequence.query_engine_projection_equals_cold_pipeline_every_step");
}

void test_close_reopen_equiv_cold() {
    const std::string uri = "file:///close-reopen-equiv.ahfl";
    DocumentStore store;
    AnalysisService analysis(store);

    std::size_t mismatches = 0;
    auto reconcile = [&](int step) {
        if (!store.contains(uri)) {
            return;
        }
        const auto *document = store.get(uri);
        const auto *snapshot = analysis.snapshot_for_uri(uri);
        if (snapshot == nullptr) {
            ++mismatches;
            return;
        }
        const auto expected = cold_projection(uri, document->text);
        const auto actual = engine_projection(*snapshot);
        if (!(actual == expected)) {
            std::printf("  FAIL: close/reopen step %d diverges from cold pipeline\n", step);
            ++mismatches;
        }
    };

    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = std::string{kCleanSource},
    });
    reconcile(1);

    store.close(uri);
    // A reopen is a fresh slot allocation: the engine must still agree with
    // the cold pipeline for every text version that follows.
    const std::vector<std::string> versions = {
        std::string{kTypeErrorSource},
        std::string{kParseErrorSource},
        std::string{kFixedSource},
    };
    int version = 2;
    for (const auto &text : versions) {
        store.open(TextDocumentItem{
            .uri = uri,
            .language_id = "ahfl",
            .version = version,
            .text = text,
        });
        reconcile(version);
        store.close(uri);
        ++version;
    }

    check(mismatches == 0, "editSequence.close_reopen_query_engine_equals_cold_pipeline");
}

void test_invalidation_drops_superseded_typed_hir() {
    // After an edit the previously served snapshot is invalidated; the next
    // request rebuilds through the engine and the typed HIR must be the new
    // text's, never the superseded one (the revision-checked borrow contract).
    const std::string uri = "file:///invalidation-typed-hir.ahfl";
    DocumentStore store;
    AnalysisService analysis(store);

    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = std::string{kCleanSource},
    });
    const auto *first = analysis.snapshot_for_uri(uri);
    check(first != nullptr && first->typed_program() != nullptr,
          "editSequence.invalidation.first_snapshot_has_typed_hir");

    store.change(uri, 2, std::string{kFixedSource});
    const auto path = AnalysisService::path_from_uri(uri);
    check(path.has_value(), "editSequence.invalidation.path_resolves");
    if (path.has_value()) {
        analysis.invalidate_paths({*path});
    }
    // Note: `first` is dangling after invalidate_paths — the snapshot pointer
    // contract is "valid until the next invalidation", and the erasure is
    // exactly what prevents a handler from reading a superseded typed program.
    // Do not dereference it here.
    const auto *second = analysis.snapshot_for_uri(uri);
    check(second != nullptr && second->typed_program() != nullptr,
          "editSequence.invalidation.second_snapshot_has_typed_hir");
    if (second != nullptr && second->typed_program() != nullptr) {
        const std::string second_json =
            ahfl::serialize_typed_program_json(*second->typed_program());
        const std::string cold_json =
            cold_projection(uri, std::string{kFixedSource}).typed_program_json;
        check(second_json == cold_json && !cold_json.empty(),
              "editSequence.invalidation.rebuilt_typed_hir_matches_cold_pipeline");
    }
}

} // namespace

int main() {
    std::printf("LSP Analysis Engine Edit-Sequence Equivalence Tests\n");
    std::printf("===================================================\n\n");

    test_edit_sequence_equiv_cold();
    test_close_reopen_equiv_cold();
    test_invalidation_drops_superseded_typed_hir();

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
