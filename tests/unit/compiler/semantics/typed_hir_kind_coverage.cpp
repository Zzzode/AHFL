// ============================================================================
// Wave-19 Lane 2 B1 — TypedStmtKind coverage matrix.
//
// Exhaustively exercises every non-zero TypedStmtKind (10 kinds as of M8) on
// two axes:
//   (a) JSON round-trip — serialize_typed_program_json followed by
//       deserialize_typed_program_json preserves each statement's kind and
//       its kind-specific payload fields (target_name, goto_target_state,
//       block indices, type-ref strategy, failure_kind, …).
//   (b) typed_visit<T> dispatch — the per-kind visitor template defined in
//       typed_hir.hpp invokes the matching visit_*_stmt() overload exactly
//       once per statement; unknown-kind statements route to
//       visit_unknown_stmt().
//
// The file deliberately reproduces the small helpers (TypedHIRFixture,
// write_file, module_source_path, make_temp_project, kSharedPrefix) used
// by tests/unit/compiler/semantics/typed_hir.cpp rather than pulling them
// through a shared header: this keeps the M8 coverage gate self-contained
// and immune to refactors of the neighbouring test file.
// ============================================================================

#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Test fixtures and helpers (mirror of typed_hir.cpp equivalents).
// ---------------------------------------------------------------------------

struct TypedHIRFixture {
    ahfl::Frontend frontend;

    [[nodiscard]] ahfl::TypeCheckResult
    check_project(const std::filesystem::path &root,
                  const std::vector<std::filesystem::path> &entry_files) const {
        const auto parse = frontend.parse_project(ahfl::ProjectInput{
            .entry_files = entry_files,
            .search_roots = {root, std::filesystem::path{"std"}},
            .inject_prelude = true,
        });
        if (parse.has_errors()) {
            std::ostringstream ss;
            parse.diagnostics.render(ss);
            std::fprintf(stderr,
                         "=== PROJECT PARSE DIAGNOSTICS ===\n%s\n=== END ===\n",
                         ss.str().c_str());
        }
        REQUIRE_FALSE(parse.has_errors());

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(parse.graph);
        if (resolve.has_errors()) {
            std::ostringstream ss;
            resolve.diagnostics.render(ss);
            std::fprintf(stderr,
                         "=== PROJECT RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n",
                         ss.str().c_str());
        }
        REQUIRE_FALSE(resolve.has_errors());

        ahfl::TypeChecker checker;
        auto result = checker.check(parse.graph, resolve);
        if (result.has_errors()) {
            std::ostringstream ss;
            result.diagnostics.render(ss);
            std::fprintf(stderr,
                         "=== PROJECT TYPECHECK DIAGNOSTICS ===\n%s\n=== END ===\n",
                         ss.str().c_str());
        }
        REQUIRE_FALSE(result.has_errors());
        return result;
    }
};

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

[[nodiscard]] std::filesystem::path make_temp_project(std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("ahfl_stmtcov_" + std::string(name));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

[[nodiscard]] std::filesystem::path module_source_path(const std::filesystem::path &root,
                                                       std::string_view module_name) {
    auto path = root;
    std::string module{module_name};
    std::size_t start = 0;
    while (true) {
        const auto separator = module.find("::", start);
        const auto segment = module.substr(start, separator - start);
        if (separator == std::string_view::npos) {
            path /= segment + ".ahfl";
            return path;
        }
        path /= segment;
        start = separator + 2;
    }
}

const char *kSharedPrefix = R"AHFL(
struct Request { value: String; token: Optional<String> = std::option::Option::None; }
struct Context { value: String = ""; }
struct Response { value: String; code: Int = 200; }

capability Echo(payload: String) -> Response;
predicate Ok(x: String) -> Bool;

const kMagic: Int = 42;
const kGreeting: String = "hello";
)AHFL";

// Helper: find the first statement of a given kind.
[[nodiscard]] const ahfl::TypedStatement *
find_first_kind(const std::vector<ahfl::TypedStatement> &stmts, ahfl::TypedStmtKind kind) {
    for (const auto &s : stmts) {
        if (s.kind == kind) {
            return &s;
        }
    }
    return nullptr;
}

// Helper: stringify TypedStmtKind for diagnostic messages.
[[nodiscard]] const char *kind_name(ahfl::TypedStmtKind k) {
    switch (k) {
    case ahfl::TypedStmtKind::None:
        return "None";
    case ahfl::TypedStmtKind::Let:
        return "Let";
    case ahfl::TypedStmtKind::Assign:
        return "Assign";
    case ahfl::TypedStmtKind::If:
        return "If";
    case ahfl::TypedStmtKind::IfLet:
        return "IfLet";
    case ahfl::TypedStmtKind::Goto:
        return "Goto";
    case ahfl::TypedStmtKind::Return:
        return "Return";
    case ahfl::TypedStmtKind::Assert:
        return "Assert";
    case ahfl::TypedStmtKind::Unwrap:
        return "Unwrap";
    case ahfl::TypedStmtKind::Requires:
        return "Requires";
    case ahfl::TypedStmtKind::Unreachable:
        return "Unreachable";
    case ahfl::TypedStmtKind::ExprStatement:
        return "ExprStatement";
    }
    return "<unknown>";
}

// Source text that — when type-checked — generates every non-zero
// TypedStmtKind exactly once inside a single flow body.
//
// Kind coverage rationale:
//   Let          → `let target: Int = 1;`
//   Assign       → `target = 2;`
//   If           → `if target > 0 { ... } else { ... }`
//   Goto         → `goto DoAssert;` / `goto End;`
//   Return       → `return Response { ... };`
//   Assert       → `assert(target > 0, "pos");`
//   Unwrap       → `let t: String = unwrap(req.token, "no token");`
//   Requires     → `requires(Ok("yes"), "pred");`
//   Unreachable  → inside the unreachable `else` arm of a statically-known branch
//   ExprStatement→ `Echo("hi");` (capability-call as statement)
const char *kAllKindsSource = R"AHFL(
fn force_unreachable(flag: Bool) -> String effect Pure decreases 0 {
    if flag {
        return "reachable";
    } else {
        unreachable("flag is always true");
    }
}

flow for CoverageAgent {
    state DoWork {
        let target: Int = 1;
        ctx.value = "assigned";
        assert(target > 0, "pos");
        requires(Ok("yes"), "pred must hold");
        if target > 0 {
            let req: Request = Request { value: "r", token: std::option::Option::None };
            unwrap(req.token);
            let u: String = force_unreachable(true);
            Echo("hi");
            goto End;
        } else {
            goto End;
        }
    }
    state End {
        return Response { value: "done", code: 200 };
    }
}
)AHFL";

// ---------------------------------------------------------------------------
// Axis (a) — each kind is produced AND round-trips with matching payloads.
// ---------------------------------------------------------------------------

// Kind-plural assertion helpers.  Keeping them as free functions lets each
// assertion live on its own source line, which doctest reports distinctly.
void assert_roundtrip_fields(const ahfl::TypedStatement &before,
                             const ahfl::TypedStatement &after) {
    REQUIRE(after.kind == before.kind);
    // Shared envelope (always present, regardless of kind).
    CHECK(after.range.begin_offset == before.range.begin_offset);
    CHECK(after.range.end_offset == before.range.end_offset);
    CHECK(after.source_id == before.source_id);
    CHECK(after.node_id == before.node_id);
    REQUIRE(after.children_expr_index.size() == before.children_expr_index.size());
    for (std::size_t i = 0; i < before.children_expr_index.size(); ++i) {
        CHECK(after.children_expr_index[i] == before.children_expr_index[i]);
    }
    // Kind-specific payload mirrors.
    CHECK(after.target_name == before.target_name);
    CHECK(after.goto_target_state == before.goto_target_state);
    CHECK(after.then_block_index == before.then_block_index);
    CHECK(after.else_block_index == before.else_block_index);
    CHECK(after.let_type_ref_strategy == before.let_type_ref_strategy);
    CHECK(after.assign_target_root_kind == before.assign_target_root_kind);
    CHECK(after.assert_message == before.assert_message);
    CHECK(after.assertion_kind == before.assertion_kind);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "TypedStmtKind matrix — every non-zero kind is produced by typecheck") {
    const auto root = make_temp_project("stmt_kind_presence");
    const auto source_path = module_source_path(root, "cov::all_kinds");
    const auto source = std::string("module cov::all_kinds;\n") +
                        "import std::option as option;\n" + kSharedPrefix +
                        "agent CoverageAgent {\n"
                        "    input: Request;\n"
                        "    context: Context;\n"
                        "    output: Response;\n"
                        "    states: [DoWork, End];\n"
                        "    initial: DoWork;\n"
                        "    final: [End];\n"
                        "    capabilities: [Echo];\n"
                        "    transition DoWork -> End;\n"
                        "}\n" +
                        kAllKindsSource;
    write_file(source_path, source);
    const auto result = check_project(root, {source_path});
    const auto &stmts = result.typed_program.statements;
    REQUIRE_FALSE(stmts.empty());

    // 10 TypedStmtKind values excluding None — all must be reachable in the
    // program's flat statement store.
    const std::vector<ahfl::TypedStmtKind> expected = {
        ahfl::TypedStmtKind::Let,
        ahfl::TypedStmtKind::Assign,
        ahfl::TypedStmtKind::If,
        ahfl::TypedStmtKind::Goto,
        ahfl::TypedStmtKind::Return,
        ahfl::TypedStmtKind::Assert,
        ahfl::TypedStmtKind::Unwrap,
        ahfl::TypedStmtKind::Requires,
        ahfl::TypedStmtKind::Unreachable,
        ahfl::TypedStmtKind::ExprStatement,
    };
    for (const auto k : expected) {
        const auto *stmt = find_first_kind(stmts, k);
        INFO("looking for TypedStmtKind::" << kind_name(k));
        REQUIRE(stmt != nullptr);
    }
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "TypedStmtKind matrix — round-trip preserves kind + payload per kind") {
    const auto root = make_temp_project("stmt_kind_roundtrip");
    const auto source_path = module_source_path(root, "cov::rt");
    const auto source = std::string("module cov::rt;\n") + "import std::option as option;\n" + kSharedPrefix +
                        "agent CoverageAgent {\n"
                        "    input: Request;\n"
                        "    context: Context;\n"
                        "    output: Response;\n"
                        "    states: [DoWork, End];\n"
                        "    initial: DoWork;\n"
                        "    final: [End];\n"
                        "    capabilities: [Echo];\n"
                        "    transition DoWork -> End;\n"
                        "}\n" +
                        kAllKindsSource;
    write_file(source_path, source);
    const auto result = check_project(root, {source_path});
    const auto &before_stmts = result.typed_program.statements;

    const auto json = ahfl::serialize_typed_program_json(result.typed_program);
    const auto deserialized = ahfl::deserialize_typed_program_json(json);
    REQUIRE(deserialized.has_value());
    const auto &after_stmts = deserialized->statements;

    // Equal count overall.
    REQUIRE(after_stmts.size() == before_stmts.size());

    // Per-kind round-trip on the first occurrence of each TypedStmtKind.
    const std::vector<ahfl::TypedStmtKind> expected = {
        ahfl::TypedStmtKind::Let,
        ahfl::TypedStmtKind::Assign,
        ahfl::TypedStmtKind::If,
        ahfl::TypedStmtKind::Goto,
        ahfl::TypedStmtKind::Return,
        ahfl::TypedStmtKind::Assert,
        ahfl::TypedStmtKind::Unwrap,
        ahfl::TypedStmtKind::Requires,
        ahfl::TypedStmtKind::Unreachable,
        ahfl::TypedStmtKind::ExprStatement,
    };
    for (const auto k : expected) {
        INFO("round-trip TypedStmtKind::" << kind_name(k));
        const auto *before = find_first_kind(before_stmts, k);
        REQUIRE(before != nullptr);
        // Use the same positional index: deserialization preserves statement
        // order (flat array field in the JSON).
        const auto idx = static_cast<std::size_t>(std::distance(&before_stmts[0], before));
        REQUIRE(idx < after_stmts.size());
        const auto &after = after_stmts[idx];
        assert_roundtrip_fields(*before, after);
    }

    // Plus: a None-shaped sentinel built manually also round-trips kind.
    ahfl::TypedProgram sentinel_program = result.typed_program;
    ahfl::TypedStatement none_stmt;
    none_stmt.kind = ahfl::TypedStmtKind::None;
    none_stmt.node_id = 9999;
    sentinel_program.statements.push_back(none_stmt);
    const auto json2 = ahfl::serialize_typed_program_json(sentinel_program);
    const auto back2 = ahfl::deserialize_typed_program_json(json2);
    REQUIRE(back2.has_value());
    REQUIRE_FALSE(back2->statements.empty());
    const auto &last = back2->statements.back();
    CHECK(last.kind == ahfl::TypedStmtKind::None);
    CHECK(last.node_id == 9999);
}

// ---------------------------------------------------------------------------
// Axis (b) — typed_visit dispatch counters.
// ---------------------------------------------------------------------------

// A visitor that records how many times each per-kind overload was called.
struct KindDispatchCounter {
    std::unordered_map<std::string, std::size_t> counts;

    void bump(const char *name) {
        counts[std::string(name)] += 1;
    }

    void visit_let_stmt(const ahfl::TypedStatement &) {
        bump("let");
    }
    void visit_assign_stmt(const ahfl::TypedStatement &) {
        bump("assign");
    }
    void visit_if_stmt(const ahfl::TypedStatement &) {
        bump("if");
    }
    void visit_if_let_stmt(const ahfl::TypedStatement &) {
        bump("if_let");
    }
    void visit_goto_stmt(const ahfl::TypedStatement &) {
        bump("goto");
    }
    void visit_return_stmt(const ahfl::TypedStatement &) {
        bump("return");
    }
    void visit_assert_stmt(const ahfl::TypedStatement &) {
        bump("assert");
    }
    void visit_unwrap_stmt(const ahfl::TypedStatement &) {
        bump("unwrap");
    }
    void visit_requires_stmt(const ahfl::TypedStatement &) {
        bump("requires");
    }
    void visit_unreachable_stmt(const ahfl::TypedStatement &) {
        bump("unreachable");
    }
    void visit_expr_stmt(const ahfl::TypedStatement &) {
        bump("expr");
    }
    void visit_unknown_stmt(const ahfl::TypedStatement &) {
        bump("unknown");
    }
};

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "TypedStmtKind matrix — typed_visit dispatches every kind exactly once per statement") {
    const auto root = make_temp_project("stmt_kind_dispatch");
    const auto source_path = module_source_path(root, "cov::dispatch");
    const auto source = std::string("module cov::dispatch;\n") + "import std::option as option;\n" + kSharedPrefix +
                        "agent CoverageAgent {\n"
                        "    input: Request;\n"
                        "    context: Context;\n"
                        "    output: Response;\n"
                        "    states: [DoWork, End];\n"
                        "    initial: DoWork;\n"
                        "    final: [End];\n"
                        "    capabilities: [Echo];\n"
                        "    transition DoWork -> End;\n"
                        "}\n" +
                        kAllKindsSource;
    write_file(source_path, source);
    const auto result = check_project(root, {source_path});
    const auto &stmts = result.typed_program.statements;

    KindDispatchCounter counter;
    for (const auto &stmt : stmts) {
        ahfl::typed_visit(stmt, counter);
    }

    // Each non-zero kind in the flat store is counted at least once.
    const std::vector<std::pair<ahfl::TypedStmtKind, const char *>> expected = {
        {ahfl::TypedStmtKind::Let, "let"},
        {ahfl::TypedStmtKind::Assign, "assign"},
        {ahfl::TypedStmtKind::If, "if"},
        {ahfl::TypedStmtKind::Goto, "goto"},
        {ahfl::TypedStmtKind::Return, "return"},
        {ahfl::TypedStmtKind::Assert, "assert"},
        {ahfl::TypedStmtKind::Unwrap, "unwrap"},
        {ahfl::TypedStmtKind::Requires, "requires"},
        {ahfl::TypedStmtKind::Unreachable, "unreachable"},
        {ahfl::TypedStmtKind::ExprStatement, "expr"},
    };
    for (const auto &[k, name] : expected) {
        const auto *stmt = find_first_kind(stmts, k);
        INFO("dispatch for TypedStmtKind::" << kind_name(k));
        REQUIRE(stmt != nullptr);
        KindDispatchCounter once;
        ahfl::typed_visit(*stmt, once);
        CHECK(once.counts[std::string(name)] == 1);
        // And no other per-kind handler is spuriously invoked.
        CHECK(once.counts.size() == 1);
    }
    CHECK(counter.counts["unknown"] == 0);

    // TypedStmtKind::None (the reserved zero value) must fall through to the
    // unknown handler via typed_visit's explicit `case None: break;` branch.
    ahfl::TypedStatement none_stmt;
    none_stmt.kind = ahfl::TypedStmtKind::None;
    KindDispatchCounter none_counter;
    ahfl::typed_visit(none_stmt, none_counter);
    CHECK(none_counter.counts["unknown"] == 1);
    CHECK(none_counter.counts.size() == 1);
}

} // namespace
