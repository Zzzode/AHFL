// RFC 0027 P2 (KR6.11): the hir(file) query-vs-direct equivalence guard.
//
// The RFC's frontend graph names `hir(file)` as the node LSP hover/completion
// read; P3 landed parse/resolve/typecheck/type_of but left hir off the graph.
// This guard proves the newly added node: hir is a derived VIEW over the
// typecheck memo (AHFL has no standalone HIR stage — TypeChecker::check emits the
// TypedProgram wholesale), so its identity is exactly the TypedProgram the
// direct pipeline produces, projected through the one canonical
// serialize_typed_program_json, plus the short-circuit `ran` flag.
//
// Discipline mirrors the S3/S4 guards: the query body routes through
// ahfl::query::snapshot_hir (the single production definition); this guard
// independently re-derives the same projection with TypeChecker +
// serialize_typed_program_json and never calls snapshot_hir, so a regression
// inside that definition is caught rather than reproduced. Do not
// "de-duplicate" either side by routing one through the other.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/query/frontend_queries.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

using ahfl::query::FileId;
using ahfl::query::FrontendQueries;
using ahfl::query::HirSnapshot;
using ahfl::query::ModuleId;

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::filesystem::path repo_root() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR};
#else
    return std::filesystem::path{"."};
#endif
}

// Independently re-derived direct identity of hir(file): run the plain pipeline
// (parse -> resolve -> typecheck, with the CLI's short-circuit structure) and
// project the typed program through the same canonical serializer the query
// memo compares on. This deliberately does NOT call snapshot_hir. The
// TypeCheckResult is owned here (not a self-referential pointer, which would
// dangle when the struct moves); callers read direct.checked->typed_program.
struct DirectHir {
    HirSnapshot snapshot;
    std::optional<ahfl::TypeCheckResult> checked;
};

[[nodiscard]] DirectHir project_hir_directly(const ahfl::ast::Program &program) {
    DirectHir direct;

    const ahfl::Resolver resolver;
    const ahfl::ResolveResult resolved = resolver.resolve(program);
    if (resolved.has_errors()) {
        return direct; // ran stays false: the CLI short-circuits before typecheck
    }

    const ahfl::TypeChecker checker;
    direct.checked.emplace(checker.check(program, resolved));
    direct.snapshot.ran = true;
    direct.snapshot.typed_program_json =
        ahfl::serialize_typed_program_json(direct.checked->typed_program);
    return direct;
}

[[nodiscard]] std::vector<std::filesystem::path>
collect_corpus(std::vector<std::pair<std::string, std::size_t>> &roots_found) {
    std::vector<std::filesystem::path> files;
    const auto root = repo_root();
    for (const auto &relative : {"tests/golden", "examples"}) {
        const auto base = root / relative;
        std::error_code ec;
        std::size_t found = 0;
        if (!std::filesystem::is_directory(base, ec)) {
            roots_found.emplace_back(relative, 0);
            continue;
        }
        std::filesystem::recursive_directory_iterator it(base, ec);
        for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            if (it->path().extension() == ".ahfl") {
                files.push_back(it->path());
                ++found;
            }
        }
        roots_found.emplace_back(relative, found);
    }
    std::ranges::sort(files);
    return files;
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

} // namespace

TEST_CASE("hir query equals the directly produced typed program") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "hand_written.ahfl", std::string{kHandWrittenSource});

    const auto queried_hir = queries.hir(file);
    REQUIRE(queried_hir.has_value());
    CHECK(queried_hir->ran);
    CHECK_FALSE(queried_hir->typed_program_json.empty());

    // The independently derived direct side.
    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("hand_written.ahfl", std::string{kHandWrittenSource});
    REQUIRE(parsed.program != nullptr);
    const DirectHir direct = project_hir_directly(*parsed.program);

    CHECK(direct.snapshot.ran);
    CHECK(*queried_hir == direct.snapshot);

    // The borrowed TypedProgram is the memoized one and serializes to exactly the
    // memo identity the query returned.
    const ahfl::TypedProgram *borrowed = queries.typed_program(file);
    REQUIRE(borrowed != nullptr);
    CHECK_FALSE(borrowed->expressions.empty());
    CHECK(ahfl::serialize_typed_program_json(*borrowed) == queried_hir->typed_program_json);
}

TEST_CASE("hir_expr is an O(1) view agreeing with the direct typed program") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "hand_written.ahfl", std::string{kHandWrittenSource});
    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    REQUIRE(queries.hir(file).has_value());

    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("hand_written.ahfl", std::string{kHandWrittenSource});
    REQUIRE(parsed.program != nullptr);
    const DirectHir direct = project_hir_directly(*parsed.program);
    REQUIRE(direct.checked.has_value());

    // Every directly produced typed expression resolves through hir_expr with the
    // same interned type, and hir_expr is literally the TypedProgram reverse index
    // (find_expr) the hover/completion consumers read today.
    std::size_t checked_nodes = 0;
    for (const auto &expr : direct.checked->typed_program.expressions) {
        const auto looked_up = queries.hir_expr(file, expr.node_id, expr.source_id);
        REQUIRE(looked_up.has_value());
        INFO("node_id=" << expr.node_id);
        REQUIRE(*looked_up != nullptr);
        CHECK((*looked_up)->type == expr.type);
        ++checked_nodes;
    }
    CHECK(checked_nodes == direct.checked->typed_program.expressions.size());
    CHECK(checked_nodes > 0);

    // type_of reads through hir: same per-expression facts via the view.
    for (const auto &expr : direct.checked->typed_program.expressions) {
        const auto typed = queries.type_of(module, expr.node_id, expr.source_id);
        REQUIRE(typed.has_value());
        CHECK(typed->found);
        CHECK(typed->type == expr.type);
    }

    // The source_id discriminant is honored (find_expr's has_source_id branch):
    // single-file records carry nullopt, so a Some(id) lookup must miss.
    const auto &first = direct.checked->typed_program.expressions.front();
    const auto mismatched = queries.hir_expr(file, first.node_id, ahfl::SourceId{7});
    REQUIRE(mismatched.has_value());
    CHECK(*mismatched == nullptr);

    const auto missing = queries.hir_expr(file, /*node_id=*/0xFFFFFFFFULL, std::nullopt);
    REQUIRE(missing.has_value());
    CHECK(*missing == nullptr);
}

TEST_CASE("malformed sources short-circuit hir identically") {
    constexpr std::string_view kMalformed = "agent {";

    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "malformed.ahfl", std::string{kMalformed});

    REQUIRE(queries.parse(file).has_value());
    const auto queried_hir = queries.hir(file);
    REQUIRE(queried_hir.has_value());
    CHECK_FALSE(queried_hir->ran);
    CHECK(queried_hir->typed_program_json.empty());

    // Nothing is borrowable, and no type is fabricated for any node.
    CHECK(queries.typed_program(file) == nullptr);
    const auto expr = queries.hir_expr(file, 1, std::nullopt);
    REQUIRE(expr.has_value());
    CHECK(*expr == nullptr);
    const auto typed = queries.type_of(module, 1, std::nullopt);
    REQUIRE(typed.has_value());
    CHECK_FALSE(typed->found);

    // The direct side agrees: a null program means the chain never reaches hir.
    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("malformed.ahfl", std::string{kMalformed});
    CHECK(parsed.program == nullptr);
}

TEST_CASE("a resolve error leaves hir unborrowable but the memo distinguished") {
    constexpr std::string_view kUnresolved = R"AHFL(
flow for Missing {
    state Init { goto Init; }
}
)AHFL";

    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "unresolved.ahfl", std::string{kUnresolved});
    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());

    const auto queried_hir = queries.hir(file);
    REQUIRE(queried_hir.has_value());
    CHECK_FALSE(queried_hir->ran);
    CHECK(queries.typed_program(file) == nullptr);

    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("unresolved.ahfl", std::string{kUnresolved});
    REQUIRE(parsed.program != nullptr);
    const ahfl::Resolver resolver;
    CHECK(resolver.resolve(*parsed.program).has_errors());
}

TEST_CASE("hir memoizes and invalidates on exactly the typecheck chain") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    const std::string source{kHandWrittenSource};
    queries.set_source_text(file, "edit.ahfl", source);

    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    REQUIRE(queries.hir(file).has_value());
    CHECK(queries.typecheck_computes(module) == 1);
    CHECK(queries.hir_computes(file) == 1);

    // Repeated reads (including the per-expression view and type_of) are memo
    // hits: hir is a view, it never re-runs the projection on a hit.
    REQUIRE(queries.hir(file).has_value());
    REQUIRE(queries.hir_expr(file, 1, std::nullopt).has_value());
    REQUIRE(queries.type_of(module, 1, std::nullopt).has_value());
    CHECK(queries.hir_computes(file) == 1);
    CHECK(queries.typecheck_computes(module) == 1);

    // Equal-value write: nothing invalidates.
    queries.set_source_text(file, "edit.ahfl", source);
    REQUIRE(queries.hir(file).has_value());
    CHECK(queries.hir_computes(file) == 1);

    // A comment-only edit moves no AST node: parse reruns (its input changed) but
    // green-proves equal, so resolve/typecheck/hir all stay green.
    queries.set_source_text(file, "edit.ahfl", source + "\n// a trailing comment only\n");
    REQUIRE(queries.hir(file).has_value());
    CHECK(queries.typecheck_computes(module) == 1);
    CHECK(queries.hir_computes(file) == 1);

    // An AST-changing edit invalidates the whole chain: typecheck and hir each
    // recompute exactly once.
    const std::string edited = source + "\nstruct Added { z: Int; }\n";
    queries.set_source_text(file, "edit.ahfl", edited);
    const auto edited_hir = queries.hir(file);
    REQUIRE(edited_hir.has_value());
    CHECK(queries.typecheck_computes(module) == 2);
    CHECK(queries.hir_computes(file) == 2);
    CHECK(edited_hir->ran);

    // And hir serves the edited text's typed program, byte for byte.
    const ahfl::Frontend frontend;
    auto direct_after = frontend.parse_text("edit.ahfl", edited);
    REQUIRE(direct_after.program != nullptr);
    const DirectHir after = project_hir_directly(*direct_after.program);
    CHECK(*edited_hir == after.snapshot);

    REQUIRE(queries.hir(file).has_value());
    CHECK(queries.hir_computes(file) == 2);
}

TEST_CASE("warm hir equals cold hir across an AST-changing edit") {
    // The RFC's core acceptance property at the hir node: a long-lived engine's
    // hir after an edit equals a cold-cache engine that only ever saw the edited
    // text — and both equal the direct pipeline.
    const std::string before = std::string{kHandWrittenSource};
    const std::string after = before + "\nstruct Extra { flag: Bool; }\n";

    FrontendQueries warm;
    const FileId file{0};
    const ModuleId module{0};
    warm.set_source_text(file, "incr.ahfl", before);
    REQUIRE(warm.hir(file).has_value());
    warm.set_source_text(file, "incr.ahfl", after);
    const auto warm_hir = warm.hir(file);
    REQUIRE(warm_hir.has_value());

    FrontendQueries cold;
    cold.set_source_text(file, "incr.ahfl", after);
    const auto cold_hir = cold.hir(file);
    REQUIRE(cold_hir.has_value());

    CHECK(*warm_hir == *cold_hir);

    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("incr.ahfl", after);
    REQUIRE(parsed.program != nullptr);
    const DirectHir direct = project_hir_directly(*parsed.program);
    CHECK(*warm_hir == direct.snapshot);

    // The O(1) accessor over the warm memo agrees with the direct typed program.
    REQUIRE(direct.checked.has_value());
    for (const auto &expr : direct.checked->typed_program.expressions) {
        const auto looked_up = warm.hir_expr(file, expr.node_id, expr.source_id);
        REQUIRE(looked_up.has_value());
        REQUIRE(*looked_up != nullptr);
        CHECK((*looked_up)->type == expr.type);
    }
    static_cast<void>(module);
}

TEST_CASE("hir is byte-identical to the pipeline over the golden corpus") {
    std::vector<std::pair<std::string, std::size_t>> roots_found;
    const auto files = collect_corpus(roots_found);
    REQUIRE_FALSE(files.empty());
    for (const auto &[root_name, count] : roots_found) {
        INFO("expected corpus root absent: " << root_name);
        CHECK(count > 0);
    }

    FrontendQueries queries;
    const ahfl::Frontend frontend;
    std::size_t checked = 0;
    std::size_t reached_hir = 0;
    std::size_t exprs_compared = 0;

    for (const auto &path : files) {
        const std::string display_name = path.generic_string();
        const std::string text = read_file(path);
        REQUIRE_FALSE(text.empty());

        const FileId file{checked};
        const ModuleId module{checked};
        queries.set_source_text(file, display_name, text);
        const auto queried_hir = queries.hir(file);
        INFO("query hir failed for " << display_name);
        REQUIRE(queried_hir.has_value());

        auto parsed = frontend.parse_text(display_name, text);
        if (parsed.program == nullptr) {
            // Parse failure: hir must report the same short-circuit and hand out
            // no typed program.
            INFO("hir short-circuit mismatch for " << display_name);
            CHECK_FALSE(queried_hir->ran);
            CHECK(queries.typed_program(file) == nullptr);
            ++checked;
            continue;
        }
        const DirectHir direct = project_hir_directly(*parsed.program);

        INFO("hir mismatch for " << display_name);
        CHECK(*queried_hir == direct.snapshot);
        CHECK((queries.typed_program(file) != nullptr) == direct.snapshot.ran);

        if (!direct.snapshot.ran) {
            ++checked;
            continue;
        }
        ++reached_hir;

        // The O(1) accessor surface must reproduce every direct typed expression
        // (node id + source_id + interned type), so a future LSP migration onto
        // hir_expr is proven against the corpus it will actually serve.
        REQUIRE(direct.checked.has_value());
        for (const auto &expr : direct.checked->typed_program.expressions) {
            const auto looked_up = queries.hir_expr(file, expr.node_id, expr.source_id);
            REQUIRE(looked_up.has_value());
            INFO("hir_expr miss for node " << expr.node_id << " in " << display_name);
            REQUIRE(*looked_up != nullptr);
            CHECK((*looked_up)->type == expr.type);
            ++exprs_compared;
        }
        ++checked;
    }

    CHECK(checked == files.size());
    CHECK(checked > 0);
    CHECK(reached_hir > 0);
    CHECK(exprs_compared > 0);
    MESSAGE("hir equivalence: " << checked << " files, " << reached_hir
                               << " reached hir, " << exprs_compared
                               << " O(1) expression lookups compared");
}
