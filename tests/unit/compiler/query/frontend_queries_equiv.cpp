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
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

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

    // The `source_id` half of the (module, node_id, source_id) key is a real
    // discriminant, not decoration: a single-file parse gives every expression a
    // null SourceId, so a lookup that supplies Some(id) must NOT match those
    // None-keyed records. This exercises TypedProgram::find_expr's has_source_id
    // branch, which the enumerated lookups above (all pass expr.source_id ==
    // nullopt) never reach. Any id works for the negative direction; use two
    // distinct ones so a fix that merely ignores the field cannot pass.
    for (const auto &expr : expressions) {
        REQUIRE_FALSE(expr.source_id.has_value());
        for (const std::size_t candidate : {std::size_t{0}, std::size_t{7}}) {
            const auto mismatched = queries.type_of(module, expr.node_id, ahfl::SourceId{candidate});
            REQUIRE(mismatched.has_value());
            INFO("node_id=" << expr.node_id << " source_id=" << candidate);
            CHECK_FALSE(mismatched->found);
            CHECK(mismatched->type == nullptr);
        }
    }

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

TEST_CASE("an AST-only edit that leaves the resolve outline identical still re-typechecks") {
    // Regression for KR6.11-S4: the resolve snapshot is a LOSSY fold of the AST
    // (it carries the symbol table and the reference/import/alias side tables,
    // but not the resolution's per-expression annotations nor body-level facts
    // such as operators and literals). The typecheck slot consumes the AST as
    // well as the resolve result, so it must take an explicit dependency on the
    // *parse* memo. Without that edge, an edit whose parse snapshot differs but
    // whose resolve snapshot is byte-identical (the three edits below) leaves
    // typecheck green-proven and serves the PREVIOUS AST's TypeCheckResult.
    //
    // Each edit is same-length, introduces no diagnostic, and — crucially — moves
    // no symbol: at one point every one of these produced resolve_snapshot A == B
    // and typecheck_computes == 1, so the engine-fed IR reproduced the OLD source.
    struct Case {
        const char *name;
        std::string_view before;
        std::string_view after;
    };
    const Case cases[] = {
        // (a) struct field default literal, same length.
        {"default-literal",
         "module a;\nstruct S { v: Int = 1; }\n",
         "module a;\nstruct S { v: Int = 2; }\n"},
        // (b) struct field reorder, same length, no symbols lost or gained.
        {"field-order",
         "module a;\nstruct S { a: Int; b: Int; }\n",
         "module a;\nstruct S { b: Int; a: Int; }\n"},
        // (c) lambda capture list `\[a]` -> `\[b]`: only
        //     ResolveResult::captured_names_by_expr changes, which the resolve
        //     outline does not cover.
        {"capture-list",
         "module a;\nfn g() -> Int effect Pure decreases 0 {\n  let a: Int = 1;\n"
         "  let b: Int = 2;\n  let f = \\[a] (y: Int) -> a + y;\n  return f(1);\n}\n",
         "module a;\nfn g() -> Int effect Pure decreases 0 {\n  let a: Int = 1;\n"
         "  let b: Int = 2;\n  let f = \\[b] (y: Int) -> a + y;\n  return f(1);\n}\n"},
        // (d) a mid-body comment shifts every following node's offsets without
        //     changing the AST shape at all.
        {"offset-shift",
         "module a;\nfn h() -> Int effect Pure decreases 0 {\n  let p: Int = 1;\n"
         "  return p;\n}\n",
         "module a;\nfn h() -> Int effect Pure decreases 0 {\n  // c\n  let p: Int = 1;\n"
         "  return p;\n}\n"},
    };

    for (const auto &test : cases) {
        INFO("case " << test.name);
        REQUIRE(test.before != test.after);

        FrontendQueries queries;
        const FileId file{0};
        const ModuleId module{0};
        queries.set_source_text(file, "edit.ahfl", std::string{test.before});
        REQUIRE(queries.parse(file).has_value());
        REQUIRE(queries.resolve(module).has_value());
        REQUIRE(queries.typecheck(module).has_value());
        REQUIRE(queries.typecheck_computes(module) == 1);

        // Apply the edit and re-drive the chain.
        queries.set_source_text(file, "edit.ahfl", std::string{test.after});
        REQUIRE(queries.parse(file).has_value());
        REQUIRE(queries.resolve(module).has_value());
        const auto edited_typecheck = queries.typecheck(module);
        REQUIRE(edited_typecheck.has_value());

        // The typecheck must have recomputed (the AST changed), and what it
        // serves must be the edit's typed program — not the pre-edit one.
        CHECK(queries.typecheck_computes(module) == 2);
        const ahfl::TypeCheckResult *borrowed = queries.typecheck_result(module);
        REQUIRE(borrowed != nullptr);
        CHECK(ahfl::serialize_typed_program_json(borrowed->typed_program) ==
              edited_typecheck->typed_program_json);

        const ahfl::Frontend frontend;
        auto direct_before = frontend.parse_text("edit.ahfl", std::string{test.before});
        auto direct_after = frontend.parse_text("edit.ahfl", std::string{test.after});
        REQUIRE(direct_before.program != nullptr);
        REQUIRE(direct_after.program != nullptr);
        const auto before_projection = project_directly(*direct_before.program);
        const auto after_projection = project_directly(*direct_after.program);

        // The edit must be observable downstream (otherwise the case is vacuous)…
        REQUIRE(before_projection.typecheck.typed_program_json !=
                after_projection.typecheck.typed_program_json);
        // …and the engine must serve the AFTER projection, byte for byte.
        CHECK(*edited_typecheck == after_projection.typecheck);

        // The engine-held AST and the borrowed stage results lower to the same IR
        // the direct pipeline produces for the edited text — the artifact a user
        // actually sees.
        if (!after_projection.ir_json.empty()) {
            const ahfl::ast::Program *query_program = queries.program(file);
            const ahfl::ResolveResult *resolved = queries.resolve_result(module);
            const ahfl::TypeCheckResult *typed = queries.typecheck_result(module);
            REQUIRE(query_program != nullptr);
            REQUIRE(resolved != nullptr);
            REQUIRE(typed != nullptr);
            std::ostringstream ir;
            ahfl::print_program_ir_json(
                ahfl::lower_program_ir(*query_program, *resolved, *typed), ir);
            CHECK(ir.str() == after_projection.ir_json);
        }
    }
}

TEST_CASE("a comment-only edit that moves no node keeps the semantic stages green") {
    // The complement of the case above: a trailing comment changes the text but
    // moves no node, so the parse outline is unchanged and the semantic stages
    // must stay green. This pins the invalidation-precision half — the parse
    // outline must be span-sensitive for the nodes but must NOT include the
    // Program's file-extent range (which covers trailing trivia) or every
    // comment would needlessly re-run resolve and typecheck.
    FrontendQueries queries;
    const FileId file{0};
    const ModuleId module{0};
    const std::string source{kHandWrittenSource};
    queries.set_source_text(file, "green.ahfl", source);
    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());
    REQUIRE(queries.resolve_computes(module) == 1);
    REQUIRE(queries.typecheck_computes(module) == 1);

    queries.set_source_text(file, "green.ahfl", source + "\n// trailing comment only\n");
    REQUIRE(queries.parse(file).has_value());
    REQUIRE(queries.resolve(module).has_value());
    REQUIRE(queries.typecheck(module).has_value());

    CHECK(queries.parse_computes(file) == 2);              // parse reran: its input changed
    CHECK(queries.resolve_computes(module) == 1);          // green: the AST is unchanged
    CHECK(queries.typecheck_computes(module) == 1);        // green: nothing it reads changed
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

// ---------------------------------------------------------------------------
// Project (multi-file + std) equivalence. The file-scoped cases above prove the
// single-analysis-unit shape; these prove the same migration criterion for the
// package/workspace shape whose input is a whole ProjectInputModel. They are the
// unit-level counterpart of the CLI gate's project half.
// ---------------------------------------------------------------------------

namespace {

using ahfl::query::ProjectId;

// The direct pipeline's project projection, re-derived independently (mirrors
// project_directly): resolve / typecheck diagnostics, the typed-program JSON, and
// the IR JSON, over the *directly parsed* graph. The short-circuit structure is
// reproduced so a divergence in *when* a stage is skipped is caught too.
[[nodiscard]] DirectProjection project_graph_directly(const ahfl::SourceGraph &graph) {
    DirectProjection projection;

    const ahfl::Resolver resolver;
    const ahfl::ResolveResult resolved = resolver.resolve(graph);
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
    const ahfl::TypeCheckResult checked = checker.check(graph, resolved);
    projection.typecheck.has_errors = checked.has_errors();
    projection.typecheck.ran = true;
    projection.typecheck.typed_program_json = ahfl::serialize_typed_program_json(checked.typed_program);
    projection.typecheck.diagnostics_json = ahfl::serialize_diagnostic_report_json(
        ahfl::DiagnosticReport::from_bag(checked.diagnostics));

    if (checked.has_errors()) {
        return projection;
    }

    std::ostringstream ir;
    ahfl::print_program_ir_json(ahfl::lower_program_ir(graph, resolved, checked), ir);
    projection.ir_json = ir.str();
    return projection;
}

} // namespace

TEST_CASE("project parse/resolve/typecheck equal the direct pipeline over a multi-module package") {
    const auto root = repo_root();
    const auto workspace_manifest = root / "tests/integration/check_ok/ahfl.workspace.toml";
    const auto entry = root / "tests/integration/check_ok/app/main.ahfl";
    REQUIRE(std::filesystem::exists(workspace_manifest));
    REQUIRE(std::filesystem::exists(entry));

    // The CLI's workspace path builds a package graph from the workspace and
    // takes the app package's target entry file.
    auto graph_result = ahfl::package_graph::build_package_graph_from_workspace(
        ahfl::package_graph::WorkspaceBuildInput{
            .workspace_manifest_path = workspace_manifest,
            .package_name = "check-ok-app",
            .sysroot_manifest_path = root / "std/ahfl.toml",
        });
    REQUIRE_FALSE(graph_result.has_errors());
    REQUIRE(graph_result.graph.has_value());
    auto input = ahfl::test_support::project_input_from_package_graph(*graph_result.graph, entry);
    REQUIRE_FALSE(input.entry_files.empty());

    // Direct route: freeze + parse with the free function.
    const ahfl::Frontend frontend;
    const auto model = ahfl::resolve_project_input(input);
    auto direct_result = ahfl::parse_project(frontend, model);

    // Query route: the same model through the engine.
    FrontendQueries queries;
    const ProjectId project{0};
    queries.set_project_input(project, model);

    const auto queried_parse = queries.parse_project(project);
    REQUIRE(queried_parse.has_value());
    const auto queried_resolve = queries.project_resolve(project);
    REQUIRE(queried_resolve.has_value());
    const auto queried_typecheck = queries.project_typecheck(project);
    REQUIRE(queried_typecheck.has_value());

    // The parse snapshot is the production identity; the guard re-derives the
    // same projections independently so a regression inside the definition is
    // caught rather than reproduced.
    CHECK(*queried_parse == ahfl::query::snapshot_project_parse_result(direct_result));

    const auto direct = project_graph_directly(direct_result.graph);
    CHECK(*queried_resolve == direct.resolve);
    CHECK(*queried_typecheck == direct.typecheck);

    // The borrowed graph is the memoized one, and it carries the multi-file shape:
    // more than one source unit, with the app module among them.
    const ahfl::SourceGraph *borrowed = queries.project_graph(project);
    REQUIRE(borrowed != nullptr);
    CHECK(borrowed->sources.size() > 1);

    // The ASTs and stage results the engine holds lower to the same IR the direct
    // pipeline produces — the artifact a user actually sees.
    const ahfl::ResolveResult *resolved = queries.project_resolve_result(project);
    const ahfl::TypeCheckResult *typed = queries.project_typecheck_result(project);
    REQUIRE(resolved != nullptr);
    REQUIRE(typed != nullptr);
    if (!direct.ir_json.empty()) {
        std::ostringstream ir;
        ahfl::print_program_ir_json(ahfl::lower_program_ir(*borrowed, *resolved, *typed), ir);
        CHECK(ir.str() == direct.ir_json);
    }
}

TEST_CASE("project stages recompute exactly once per model change and memoize otherwise") {
    // A project built entirely in memory (a temp root plus `source_cache`), so the
    // test exercises the model's value semantics — including that an equal-model
    // re-set is a no-op — without depending on a package graph.
    const auto root = std::filesystem::temp_directory_path() / "ahfl_project_query_memo";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "app", ec);
    REQUIRE_FALSE(ec);

    const auto write_source = [&](const std::filesystem::path &path, std::string_view text) {
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    // A real two-module project: `app::main` imports `app::lib::types`, and both
    // live under the single search root exactly as their module paths demand, so
    // the import edge is actually resolved (and the graph really has two units).
    const std::filesystem::path main_path = root / "app" / "main.ahfl";
    const std::filesystem::path lib_path = root / "app" / "lib" / "types.ahfl";
    constexpr std::string_view kMain = "module app::main;\nimport app::lib::types;\n";
    constexpr std::string_view kLib = "module app::lib::types;\nstruct Ctx { count: Int = 0; }\n";
    write_source(main_path, kMain);
    write_source(lib_path, kLib);

    const ahfl::ProjectInput input{
        .entry_files = {main_path},
        .search_roots = {root},
        .inject_prelude = false,
    };

    FrontendQueries queries;
    const ProjectId project{0};
    const auto model = ahfl::resolve_project_input(input);
    queries.set_project_input(project, model);

    REQUIRE(queries.parse_project(project).has_value());
    REQUIRE(queries.project_resolve(project).has_value());
    REQUIRE(queries.project_typecheck(project).has_value());
    CHECK(queries.project_parse_computes(project) == 1);
    CHECK(queries.project_resolve_computes(project) == 1);
    CHECK(queries.project_typecheck_computes(project) == 1);

    // The import really resolved: the graph carries both units (otherwise the
    // "edit a non-entry module" case below would be vacuous).
    const ahfl::SourceGraph *graph = queries.project_graph(project);
    REQUIRE(graph != nullptr);
    REQUIRE(graph->sources.size() == 2);
    REQUIRE(graph->import_edges.size() == 1);
    REQUIRE(graph->import_edges.front().imported.value == 1); // the imported unit

    // Repeated evaluation is served from the memo at every stage.
    REQUIRE(queries.parse_project(project).has_value());
    REQUIRE(queries.project_typecheck(project).has_value());
    CHECK(queries.project_parse_computes(project) == 1);
    CHECK(queries.project_typecheck_computes(project) == 1);

    // An equal model re-set (the same tree re-resolved) is a no-op: the model's
    // equality is what makes that true, and it is the whole reason a project can
    // be an engine input.
    queries.set_project_input(project, ahfl::resolve_project_input(input));
    REQUIRE(queries.parse_project(project).has_value());
    REQUIRE(queries.project_typecheck(project).has_value());
    CHECK(queries.project_parse_computes(project) == 1);
    CHECK(queries.project_typecheck_computes(project) == 1);

    // A real text change in a NON-entry module invalidates the whole chain: the
    // model differs, so the project parse recomputes, its graph outline changes,
    // and every downstream stage follows. This pins that "changing a dependency's
    // text is observable" — the failure mode a content-blind input model would
    // hide, and the reason the model carries every reachable source's text.
    const std::string edited_lib =
        "module app::lib::types;\nstruct Ctx { count: Int = 0; }\nstruct Extra { z: Int; }\n";
    write_source(lib_path, edited_lib);
    queries.set_project_input(project, ahfl::resolve_project_input(input));
    const auto edited_parse = queries.parse_project(project);
    REQUIRE(edited_parse.has_value());
    REQUIRE(queries.project_resolve(project).has_value());
    REQUIRE(queries.project_typecheck(project).has_value());
    CHECK(queries.project_parse_computes(project) == 2);
    CHECK(queries.project_typecheck_computes(project) == 2);

    // The edited model's graph and the direct pipeline agree on the new ARRIVAL.
    const ahfl::Frontend frontend;
    auto direct_result = ahfl::parse_project(frontend, ahfl::resolve_project_input(input));
    CHECK(*edited_parse == ahfl::query::snapshot_project_parse_result(direct_result));

    std::filesystem::remove_all(root, ec);
}
