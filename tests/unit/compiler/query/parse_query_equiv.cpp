// RFC 0027 P2 (KR6.11-S3): the query-vs-direct equivalence guard.
//
// `parse(file)` is the first frontend stage moved onto the QueryEngine graph.
// RFC 0027's migration strategy is that every stage promoted to the query graph
// must be proven equivalent to the direct pipeline — the whole migration hinges
// on "the query result is the same result", so the methodology is established
// here, on the cheapest stage, exactly as it must be applied to
// resolve/typecheck (P3) later.
//
// Equivalence is defined once, by ahfl::query::snapshot_parse_result: the two
// byte-comparable projections of ParseResult (canonical AST outline + canonical
// diagnostic report JSON) plus the error flag. The query body routes through
// that definition; this guard independently re-derives the direct side with
// dump_program_outline / serialize_diagnostic_report_json so a regression in the
// definition cannot hide behind itself.
//
// Beyond equivalence, the engine's incremental contract is asserted directly:
// memo hits, equal-text no-ops, per-file invalidation isolation, and the
// storage-lifetime pattern Slice 4 copies (the owned AST lives in the query
// object and is borrowed by later query bodies).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/query/frontend_queries.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostic_serialization.hpp"

using ahfl::query::FileId;
using ahfl::query::FrontendQueries;
using ahfl::query::ParseSnapshot;

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Independent direct-pipeline snapshot: what ParseResult means to a caller that
// never heard of the query engine.
[[nodiscard]] ParseSnapshot direct_snapshot(const ahfl::ParseResult &result) {
    ParseSnapshot snapshot;
    snapshot.has_errors = result.has_errors();
    if (result.program != nullptr) {
        std::ostringstream outline;
        ahfl::dump_program_outline(*result.program, outline);
        snapshot.outline = outline.str();
    }
    snapshot.diagnostics_json = ahfl::serialize_diagnostic_report_json(
        ahfl::DiagnosticReport::from_bag(result.diagnostics));
    return snapshot;
}

[[nodiscard]] ParseSnapshot parse_directly(const std::string &display_name,
                                           const std::string &text) {
    const ahfl::Frontend frontend;
    return direct_snapshot(frontend.parse_text(display_name, text));
}

constexpr std::string_view kHandWrittenSource = R"AHFL(
struct Request { value: Int; }
struct Ctx { count: Int = 0; }

agent Worker {
    input: Request;
    context: Ctx;
    output: Ctx;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for Worker {
    state Init {
        let doubled = input.value + input.value;
        goto Done;
    }
    state Done { return Ctx { count: 0 }; }
}
)AHFL";

[[nodiscard]] std::filesystem::path repo_root() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR};
#else
    return std::filesystem::path{"."};
#endif
}

// Every .ahfl file under the given roots, sorted for run-to-run determinism.
// The golden corpus deliberately includes malformed sources: equivalence is a
// property of the whole projection, so erroring parses are part of the guard
// (the diagnostics JSON must match, not just the success case).
[[nodiscard]] std::vector<std::filesystem::path> collect_corpus() {
    std::vector<std::filesystem::path> files;
    const auto root = repo_root();
    for (const auto &relative : {"tests/golden", "examples"}) {
        const auto base = root / relative;
        std::error_code ec;
        if (!std::filesystem::is_directory(base, ec)) {
            continue;
        }
        std::filesystem::recursive_directory_iterator it(base, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() == ".ahfl") {
                files.push_back(it->path());
            }
        }
    }
    std::ranges::sort(files);
    return files;
}

} // namespace

TEST_CASE("query parse equals the direct frontend for a hand-written source") {
    FrontendQueries queries;
    const FileId file{0};
    queries.set_source_text(file, "hand_written.ahfl", std::string{kHandWrittenSource});

    const auto queried = queries.parse(file);
    REQUIRE(queried.has_value());

    const auto direct = parse_directly("hand_written.ahfl", std::string{kHandWrittenSource});

    CHECK(queried->outline == direct.outline);
    CHECK(queried->diagnostics_json == direct.diagnostics_json);
    CHECK(queried->has_errors == direct.has_errors);
    CHECK_FALSE(queried->has_errors);

    // The AST is owned by the query object and borrowable while it is alive —
    // the storage-lifetime pattern later resolve/typecheck query bodies use.
    const auto *program = queries.program(file);
    REQUIRE(program != nullptr);
    std::ostringstream outline;
    ahfl::dump_program_outline(*program, outline);
    CHECK(outline.str() == direct.outline);
}

TEST_CASE("query parse equals the direct frontend for a malformed source") {
    // Equivalence must cover the failure projection too: a parse that produces
    // diagnostics drops the AST, so the query and the direct frontend must agree
    // on the diagnostic JSON and on has_errors, and both must expose no program.
    constexpr std::string_view kMalformed = "agent {";

    FrontendQueries queries;
    const FileId file{0};
    queries.set_source_text(file, "malformed.ahfl", std::string{kMalformed});

    const auto queried = queries.parse(file);
    REQUIRE(queried.has_value());

    const auto direct = parse_directly("malformed.ahfl", std::string{kMalformed});
    CHECK(queried->has_errors);
    CHECK(queried->has_errors == direct.has_errors);
    CHECK(queried->diagnostics_json == direct.diagnostics_json);
    CHECK(queried->outline == direct.outline);
    CHECK(queried->outline.empty());
    CHECK(queries.program(file) == nullptr);
}

TEST_CASE("repeated query eval is a memo hit") {
    FrontendQueries queries;
    const FileId file{0};
    queries.set_source_text(file, "memo.ahfl", std::string{kHandWrittenSource});

    REQUIRE(queries.parse(file).has_value());
    CHECK(queries.parse_computes(file) == 1);

    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.parse(file).has_value());
    CHECK(queries.parse_computes(file) == 1); // still one compute
    CHECK(queries.stats().memo_hits == 2);
    CHECK(queries.stats().recomputations == 1);
}

TEST_CASE("equal-text write does not recompute; changed text recomputes exactly once") {
    FrontendQueries queries;
    const FileId file{0};
    const std::string source{kHandWrittenSource};
    queries.set_source_text(file, "edit.ahfl", source);
    REQUIRE(queries.parse(file).has_value());
    CHECK(queries.parse_computes(file) == 1);

    // Equal value: no revision bump, no invalidation.
    queries.set_source_text(file, "edit.ahfl", source);
    CHECK(queries.revision() == 1);
    REQUIRE(queries.parse(file).has_value());
    CHECK(queries.parse_computes(file) == 1);
    CHECK(queries.stats().input_update_noops == 1);

    // Changed text: exactly one recompute, and the new outline reflects it.
    const std::string edited = source + "\n// a trailing comment changes the AST\n";
    queries.set_source_text(file, "edit.ahfl", edited);
    CHECK(queries.revision() == 2);

    const auto after = queries.parse(file);
    REQUIRE(after.has_value());
    CHECK(queries.parse_computes(file) == 2);

    REQUIRE(queries.parse(file).has_value());
    CHECK(queries.parse_computes(file) == 2); // memoized again

    const auto direct = parse_directly("edit.ahfl", edited);
    CHECK(after->outline == direct.outline);
    CHECK(after->diagnostics_json == direct.diagnostics_json);
}

TEST_CASE("editing one file leaves a sibling file's parse memo intact") {
    FrontendQueries queries;
    const FileId file_a{0};
    const FileId file_b{1};
    queries.set_source_text(file_a, "a.ahfl", std::string{kHandWrittenSource});
    queries.set_source_text(file_b, "b.ahfl", std::string{kHandWrittenSource});

    REQUIRE(queries.parse(file_a).has_value());
    REQUIRE(queries.parse(file_b).has_value());
    CHECK(queries.parse_computes(file_a) == 1);
    CHECK(queries.parse_computes(file_b) == 1);

    queries.set_source_text(file_a, "a.ahfl", std::string{kHandWrittenSource} + "\n");

    REQUIRE(queries.parse(file_a).has_value());
    CHECK(queries.parse_computes(file_a) == 2); // A recomputed

    REQUIRE(queries.parse(file_b).has_value());
    CHECK(queries.parse_computes(file_b) == 1); // B untouched
}

TEST_CASE("query parse is byte-identical to the direct frontend over the golden corpus") {
    const auto files = collect_corpus();
    // The corpus is the point of this guard; a silently empty walk would make
    // the test vacuously green.
    REQUIRE(files.size() >= 100);

    FrontendQueries queries;
    std::size_t checked = 0;
    std::size_t erroring = 0;

    for (const auto &path : files) {
        const std::string display_name = path.generic_string();
        const std::string text = read_file(path);
        REQUIRE_FALSE(text.empty());

        const FileId file{checked};
        queries.set_source_text(file, display_name, text);

        const auto queried = queries.parse(file);
        INFO("query parse failed for " << display_name);
        REQUIRE(queried.has_value());

        const auto direct = parse_directly(display_name, text);
        INFO("outline mismatch for " << display_name);
        CHECK(queried->outline == direct.outline);
        INFO("diagnostics mismatch for " << display_name);
        CHECK(queried->diagnostics_json == direct.diagnostics_json);
        CHECK(queried->has_errors == direct.has_errors);

        if (direct.has_errors) {
            ++erroring;
        }
        ++checked;
    }

    // Both halves of the projection are exercised: a corpus of only-valid
    // sources would not prove the diagnostic JSON path.
    CHECK(checked == files.size());
    CHECK(checked > 0);
    MESSAGE("corpus equivalence: " << checked << " files (" << erroring << " with diagnostics)");
}
