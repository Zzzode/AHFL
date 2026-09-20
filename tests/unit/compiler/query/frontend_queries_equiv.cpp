// RFC 0027 P3 (KR6.11-S4): the resolve / typecheck / type_of query-vs-direct
// equivalence guard.
//
// P2 (S3) proved the methodology on `parse`, the cheapest stage. P3 promotes the
// semantic stages — `resolve(module)`, `typecheck(module)`, and the derived
// `type_of(module, node_id, source_id)` view — onto the same query graph, and
// the RFC's migration strategy requires each promotion to be proven equivalent
// to the direct pipeline. This guard is that proof, over the whole golden
// corpus, exactly as the S3 guard does it for parse.
//
// Equivalence is defined once per stage by ahfl::query::snapshot_resolve_result /
// snapshot_typecheck_result (and by the typecheck memo's own typed-program
// projection). The query bodies route through those definitions; this guard
// independently re-derives the direct side — resolver.resolve / type_checker.check
// / serialize_* / lower + print — so a regression inside a definition cannot hide
// behind itself. Do not "de-duplicate" the guard by routing it through the query
// accessors: that would make it tautological.
//
// The downstream IR JSON is compared too: resolve and typecheck are the stages
// the CLI's whole golden fleet (ahflc emit ir-json / check) feeds from, so
// equality of the diagnostics alone would not prove bit-equivalence of what a
// user actually sees. This is the S4 acceptance criterion in miniature.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/query/frontend_queries.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/base/support/diagnostic_serialization.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

using ahfl::query::FileId;
using ahfl::query::FrontendQueries;
using ahfl::query::ModuleId;
using ahfl::query::ResolveSnapshot;
using ahfl::query::TypecheckSnapshot;

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

// The independently re-derived direct projection of one parsed AST: the resolve /
// typecheck diagnostic JSON, the typed-program JSON, and the full IR JSON. The
// short-circuit structure mirrors the CLI pipeline (a resolve error means
// typecheck never runs), so a divergence in *when* a stage is skipped is caught
// too.
struct DirectProjection {
    ResolveSnapshot resolve;
    TypecheckSnapshot typecheck;
    std::string ir_json;

    [[nodiscard]] friend bool operator==(const DirectProjection &,
                                         const DirectProjection &) noexcept = default;
};

[[nodiscard]] DirectProjection project_directly(const ahfl::ast::Program &program) {
    DirectProjection projection;

    const ahfl::Resolver resolver;
    const ahfl::ResolveResult resolved = resolver.resolve(program);
    projection.resolve.has_errors = resolved.has_errors();
    projection.resolve.ran = true;
    {
        std::ostringstream outline;
        ahfl::query::dump_resolve_outline(resolved, outline);
        projection.resolve.symbols_outline = outline.str();
    }
    projection.resolve.diagnostics_json =
        ahfl::serialize_diagnostic_report_json(ahfl::DiagnosticReport::from_bag(resolved.diagnostics));

    if (resolved.has_errors()) {
        projection.typecheck = TypecheckSnapshot{};
        return projection;
    }

    const ahfl::TypeChecker checker;
    const ahfl::TypeCheckResult checked = checker.check(program, resolved);
    projection.typecheck.has_errors = checked.has_errors();
    projection.typecheck.ran = true;
    projection.typecheck.typed_program_json = ahfl::serialize_typed_program_json(checked.typed_program);
    projection.typecheck.diagnostics_json = ahfl::serialize_diagnostic_report_json(
        ahfl::DiagnosticReport::from_bag(checked.diagnostics));

    if (checked.has_errors()) {
        return projection;
    }

    std::ostringstream ir;
    ahfl::print_program_ir_json(ahfl::lower_program_ir(program, resolved, checked), ir);
    projection.ir_json = ir.str();
    return projection;
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

TEST_CASE("resolve/typecheck queries equal the direct pipeline for a hand-written source") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "hand_written.ahfl", std::string{kHandWrittenSource});

    REQUIRE(queries.parse(file).has_value());
    const auto query_resolve = queries.resolve(module);
    REQUIRE(query_resolve.has_value());
    CHECK_FALSE(query_resolve->has_errors);
    CHECK(query_resolve->ran);

    const auto query_typecheck = queries.typecheck(module);
    REQUIRE(query_typecheck.has_value());
    CHECK_FALSE(query_typecheck->has_errors);
    CHECK(query_typecheck->ran);

    // Direct side: an independently parsed AST through the plain pipeline.
    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("hand_written.ahfl", std::string{kHandWrittenSource});
    REQUIRE(parsed.program != nullptr);
    const auto direct = project_directly(*parsed.program);

    CHECK(*query_resolve == direct.resolve);
    CHECK(*query_typecheck == direct.typecheck);
    CHECK_FALSE(direct.ir_json.empty());

    // The borrowed stage results are the memoized ones, and they carry the
    // resolution the pipeline would have seen.
    const ahfl::ResolveResult *borrowed_resolve = queries.resolve_result(module);
    const ahfl::TypeCheckResult *borrowed_typecheck = queries.typecheck_result(module);
    REQUIRE(borrowed_resolve != nullptr);
    REQUIRE(borrowed_typecheck != nullptr);
    CHECK_FALSE(borrowed_resolve->symbol_table.symbols().empty());
    CHECK_FALSE(borrowed_typecheck->typed_program.expressions.empty());
}

TEST_CASE("type_of returns the direct typed program's per-expression type") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "hand_written.ahfl", std::string{kHandWrittenSource});
    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());

    const ahfl::TypeCheckResult *checked = queries.typecheck_result(module);
    REQUIRE(checked != nullptr);
    const auto &expressions = checked->typed_program.expressions;
    REQUIRE_FALSE(expressions.empty());

    // Every typed expression's type must be recoverable through type_of, and each
    // lookup must agree with the typed store's own record.
    std::size_t checked_nodes = 0;
    for (const auto &expr : expressions) {
        const auto looked_up = queries.type_of(module, expr.node_id, expr.source_id);
        REQUIRE(looked_up.has_value());
        INFO("node_id=" << expr.node_id);
        CHECK(looked_up->found);
        CHECK(looked_up->type == expr.type);
        ++checked_nodes;
    }
    CHECK(checked_nodes == expressions.size());

    // An unknown node id is reported as "not found", never as a fabricated type.
    const auto missing = queries.type_of(module, /*node_id=*/0xFFFFFFFFULL, std::nullopt);
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->found);
    CHECK(missing->type == nullptr);
}

TEST_CASE("malformed sources short-circuit identically through the queries") {
    // A parse failure must mean resolve and typecheck never run — the same
    // short-circuit the CLI pipeline performs. The run flags carry that fact.
    constexpr std::string_view kMalformed = "agent {";

    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    queries.set_source_text(file, "malformed.ahfl", std::string{kMalformed});

    REQUIRE(queries.parse(file).has_value());
    const auto query_resolve = queries.resolve(module);
    REQUIRE(query_resolve.has_value());
    CHECK_FALSE(query_resolve->ran);
    CHECK(query_resolve->symbols_outline.empty());
    CHECK(query_resolve->diagnostics_json.empty());

    const auto query_typecheck = queries.typecheck(module);
    REQUIRE(query_typecheck.has_value());
    CHECK_FALSE(query_typecheck->ran);

    // No stage result is borrowable, and no type is fabricated for any node.
    CHECK(queries.resolve_result(module) == nullptr);
    CHECK(queries.typecheck_result(module) == nullptr);
    const auto type = queries.type_of(module, 1, std::nullopt);
    REQUIRE(type.has_value());
    CHECK_FALSE(type->found);

    // The direct side agrees: a null program means neither stage runs.
    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("malformed.ahfl", std::string{kMalformed});
    CHECK(parsed.program == nullptr);
}

TEST_CASE("stages recompute exactly once per text change and memoize otherwise") {
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    const std::string source{kHandWrittenSource};
    queries.set_source_text(file, "edit.ahfl", source);

    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    CHECK(queries.parse_computes(file) == 1);
    CHECK(queries.resolve_computes(module) == 1);
    CHECK(queries.typecheck_computes(module) == 1);

    // Repeated evaluation is served from the memo at every stage.
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    REQUIRE(queries.type_of(module, 1, std::nullopt).has_value());
    CHECK(queries.parse_computes(file) == 1);
    CHECK(queries.resolve_computes(module) == 1);
    CHECK(queries.typecheck_computes(module) == 1);

    // Equal-value write: nothing invalidates.
    queries.set_source_text(file, "edit.ahfl", source);
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    CHECK(queries.resolve_computes(module) == 1);
    CHECK(queries.typecheck_computes(module) == 1);

    // Text changed but the PARSE RESULT did not (a trailing comment is not in
    // the AST): the parse slot is re-proven unchanged (green), so every
    // downstream stage is a memo hit too. This is the whole point of caching
    // the stages — resolve must NOT re-run for a comment.
    const std::string commented = source + "\n// a trailing comment only\n";
    queries.set_source_text(file, "edit.ahfl", commented);
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    CHECK(queries.parse_computes(file) == 2); // parse reran: its input text changed
    CHECK(queries.resolve_computes(module) == 1); // green: parse output unchanged
    CHECK(queries.typecheck_computes(module) == 1);

    // A change that DOES alter the AST invalidates the whole chain: every stage
    // recomputes exactly once, then memos settle again.
    const std::string edited = source + "\nstruct Added { z: Int; }\n";
    queries.set_source_text(file, "edit.ahfl", edited);
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    CHECK(queries.resolve_computes(module) == 2);
    CHECK(queries.typecheck_computes(module) == 2);

    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    CHECK(queries.resolve_computes(module) == 2);
    CHECK(queries.typecheck_computes(module) == 2);
}

TEST_CASE("increment equals cold recomputation across an AST-changing edit") {
    // The RFC's core acceptance property, at the semantic stages: a long-lived
    // engine that has already evaluated a source must produce the same stage
    // results as a cold-cache engine on the edited text. The parse half of this
    // is guarded by S5; this asserts the resolve/typecheck/type_of half directly.
    const std::string before = std::string{kHandWrittenSource};
    const std::string after = before + "\nstruct Extra { flag: Bool; }\n";

    // Warm engine: parse, resolve, typecheck, and a type_of lookup on `before`.
    FrontendQueries warm;
    const FileId file{0};
    const ModuleId module{0};
    warm.set_source_text(file, "incr.ahfl", before);
    REQUIRE(warm.parse(file).has_value());
    REQUIRE(warm.resolve(module).has_value());
    REQUIRE(warm.typecheck(module).has_value());
    REQUIRE(warm.type_of(module, 1, std::nullopt).has_value());

    // Edit: text changes and the AST changes with it.
    warm.set_source_text(file, "incr.ahfl", after);
    const auto warm_parse = warm.parse(file);
    const auto warm_resolve = warm.resolve(module);
    const auto warm_typecheck = warm.typecheck(module);
    REQUIRE(warm_parse.has_value());
    REQUIRE(warm_resolve.has_value());
    REQUIRE(warm_typecheck.has_value());

    // Cold engine on the edited text.
    FrontendQueries cold;
    cold.set_source_text(file, "incr.ahfl", after);
    REQUIRE(cold.parse(file).has_value());
    const auto cold_resolve = cold.resolve(module);
    const auto cold_typecheck = cold.typecheck(module);
    REQUIRE(cold_resolve.has_value());
    REQUIRE(cold_typecheck.has_value());

    CHECK(*warm_resolve == *cold_resolve);
    CHECK(*warm_typecheck == *cold_typecheck);
    CHECK(warm_parse->outline == cold.parse(file)->outline);

    // The direct pipeline agrees with both.
    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("incr.ahfl", after);
    REQUIRE(parsed.program != nullptr);
    const auto direct = project_directly(*parsed.program);
    CHECK(*warm_resolve == direct.resolve);
    CHECK(*warm_typecheck == direct.typecheck);
}

TEST_CASE("a semantic erroring source yields borrowable results but no IR") {
    // A source that parses cleanly but fails resolve must: expose the resolve
    // diagnostic, short-circuit typecheck, and never hand out a stage result as
    // if the stage had succeeded.
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

    const auto query_resolve = queries.resolve(module);
    REQUIRE(query_resolve.has_value());
    CHECK(query_resolve->ran);
    CHECK(query_resolve->has_errors);
    CHECK_FALSE(query_resolve->diagnostics_json.empty());

    const auto query_typecheck = queries.typecheck(module);
    REQUIRE(query_typecheck.has_value());
    CHECK_FALSE(query_typecheck->ran); // resolve errored, so typecheck never ran

    const ahfl::Frontend frontend;
    auto parsed = frontend.parse_text("unresolved.ahfl", std::string{kUnresolved});
    REQUIRE(parsed.program != nullptr);
    const auto direct = project_directly(*parsed.program);
    CHECK(direct.resolve.has_errors);
    CHECK_FALSE(direct.typecheck.ran);
    CHECK(*query_resolve == direct.resolve);
    CHECK(*query_typecheck == direct.typecheck);
}

TEST_CASE("resolve/typecheck queries are byte-identical to the pipeline over the golden corpus") {
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
    std::size_t typechecked = 0;
    std::size_t ir_compared = 0;

    for (const auto &path : files) {
        const std::string display_name = path.generic_string();
        const std::string text = read_file(path);
        REQUIRE_FALSE(text.empty());

        const FileId file{checked};
        const ModuleId module{checked};
        queries.set_source_text(file, display_name, text);

        const auto queried_parse = queries.parse(file);
        INFO("query parse failed for " << display_name);
        REQUIRE(queried_parse.has_value());
        const auto queried_resolve = queries.resolve(module);
        REQUIRE(queried_resolve.has_value());
        const auto queried_typecheck = queries.typecheck(module);
        REQUIRE(queried_typecheck.has_value());

        auto parsed = frontend.parse_text(display_name, text);
        DirectProjection direct;
        if (parsed.program != nullptr) {
            direct = project_directly(*parsed.program);
        }

        INFO("resolve mismatch for " << display_name);
        CHECK(*queried_resolve == direct.resolve);
        INFO("typecheck mismatch for " << display_name);
        CHECK(*queried_typecheck == direct.typecheck);

        if (direct.typecheck.ran) {
            ++typechecked;
        }
        if (!direct.ir_json.empty()) {
            std::ostringstream ir;
            // Re-derive the query side's IR from the query-held AST and the
            // borrowed stage results — exactly the objects the CLI would pass to
            // lower_program_ir — and compare bytes against the direct pipeline.
            const ahfl::ast::Program *query_program = queries.program(file);
            const ahfl::ResolveResult *resolved = queries.resolve_result(module);
            const ahfl::TypeCheckResult *typed = queries.typecheck_result(module);
            INFO("borrow failed for " << display_name);
            REQUIRE(query_program != nullptr);
            REQUIRE(resolved != nullptr);
            REQUIRE(typed != nullptr);
            ahfl::print_program_ir_json(
                ahfl::lower_program_ir(*query_program, *resolved, *typed), ir);
            INFO("IR JSON mismatch for " << display_name);
            CHECK(ir.str() == direct.ir_json);
            ++ir_compared;
        }
        ++checked;
    }

    CHECK(checked == files.size());
    CHECK(checked > 0);
    CHECK(typechecked > 0);
    CHECK(ir_compared > 0);
    MESSAGE("stage equivalence: "
            << checked << " files, " << typechecked << " reached typecheck, " << ir_compared
            << " compared IR JSON");
}
