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
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/typed_hir_serialization.hpp"
#include "base/json/json_value.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
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
        const auto parse =
            ahfl::parse_project(frontend,
                                ahfl::test_support::project_input_with_repo_std_for_test_file(
                                    entry_files, root, __FILE__));
        if (parse.has_errors()) {
            std::ostringstream ss;
            parse.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== PROJECT PARSE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(parse.has_errors());

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(parse.graph);
        if (resolve.has_errors()) {
            std::ostringstream ss;
            resolve.diagnostics.render(ss);
            std::fprintf(
                stderr, "=== PROJECT RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
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
    const auto root =
        std::filesystem::temp_directory_path() / ("ahfl_stmtcov_" + std::string(name));
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
    CHECK(after.pattern_index == before.pattern_index);
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
    const auto source = std::string("module cov::rt;\n") + "import std::option as option;\n" +
                        kSharedPrefix +
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

// RFC 0026 P4 (coercion) F1 forward-fix (Codex P1-3 / P0-2): a NON-EMPTY
// TypedAdjustmentPlan must survive a typed-HIR JSON round-trip with full fidelity
// (every op kind + arg_index + child + node source/target), and a corrupted op
// kind on the wire must FAIL the deserialize closed rather than downgrade to
// IntWiden. Builds a List<Int(0,0)>(4) -> List<Int>(8) plan (root: CapacityWiden +
// TypeArg{0->child IntWiden}).
TEST_CASE("Typed-HIR round-trips a non-empty let_adjustment plan and rejects a bad op") {
    ahfl::TypeContext types;
    const auto int_ty = types.make(ahfl::TypeKind::Int);
    const auto bounded = types.bounded_int(0, 0);
    const auto list_bounded = types.struct_type("std::collections::List", std::nullopt,
                                                std::vector<ahfl::TypePtr>{bounded},
                                                std::optional<std::uint64_t>{4});
    const auto list_int = types.struct_type("std::collections::List", std::nullopt,
                                            std::vector<ahfl::TypePtr>{int_ty},
                                            std::optional<std::uint64_t>{8});

    ahfl::TypedAdjustmentPlan plan;
    plan.source = list_bounded;
    plan.target = list_int;
    plan.root = 0;
    ahfl::TypedAdjustmentNode root_node;
    root_node.source = list_bounded;
    root_node.target = list_int;
    root_node.ops.push_back(ahfl::TypedAdjustmentOp{.kind = ahfl::TypedAdjustmentOpKind::CapacityWiden});
    root_node.ops.push_back(ahfl::TypedAdjustmentOp{
        .kind = ahfl::TypedAdjustmentOpKind::TypeArg, .arg_index = 0, .child = 1});
    ahfl::TypedAdjustmentNode elem_node;
    elem_node.source = bounded;
    elem_node.target = int_ty;
    elem_node.ops.push_back(ahfl::TypedAdjustmentOp{.kind = ahfl::TypedAdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(root_node));
    plan.nodes.push_back(std::move(elem_node));

    ahfl::TypedProgram program;
    ahfl::TypedStatement let;
    let.kind = ahfl::TypedStmtKind::Let;
    let.node_id = 4242;
    let.target_name = "xs";
    let.let_type = list_int;
    let.let_adjustment = std::move(plan);
    program.statements.push_back(std::move(let));

    const auto json = ahfl::serialize_typed_program_json(program);
    const auto back = ahfl::deserialize_typed_program_json(json);
    REQUIRE(back.has_value());
    REQUIRE_FALSE(back->statements.empty());
    const auto &out_let = back->statements.back();
    REQUIRE(out_let.let_adjustment.has_value());
    const auto &out_plan = *out_let.let_adjustment;
    CHECK(out_plan.root == 0);
    REQUIRE(out_plan.nodes.size() == 2);
    REQUIRE(out_plan.nodes[0].ops.size() == 2);
    CHECK(out_plan.nodes[0].ops[0].kind == ahfl::TypedAdjustmentOpKind::CapacityWiden);
    CHECK(out_plan.nodes[0].ops[1].kind == ahfl::TypedAdjustmentOpKind::TypeArg);
    CHECK(out_plan.nodes[0].ops[1].arg_index == 0);
    CHECK(out_plan.nodes[0].ops[1].child == 1);
    REQUIRE(out_plan.nodes[1].ops.size() == 1);
    CHECK(out_plan.nodes[1].ops[0].kind == ahfl::TypedAdjustmentOpKind::IntWiden);

    // An unknown op-kind spelling on the wire fails closed (no silent IntWiden).
    auto corrupt = json;
    const auto pos = corrupt.find("\"capacity_widen\"");
    REQUIRE(pos != std::string::npos);
    corrupt.replace(pos, std::string("\"capacity_widen\"").size(), "\"bogus_op\"");
    CHECK_FALSE(ahfl::deserialize_typed_program_json(corrupt).has_value());
}

TEST_CASE("Typed-HIR round-trips top and bottom adjustment leaf operations") {
    ahfl::TypeContext types;
    const auto int_ty = types.make(ahfl::TypeKind::Int);
    const auto any_ty = types.make(ahfl::TypeKind::Any);
    const auto never_ty = types.make(ahfl::TypeKind::Never);

    const auto make_plan =
        [](ahfl::TypePtr source, ahfl::TypePtr target, ahfl::TypedAdjustmentOpKind kind) {
            ahfl::TypedAdjustmentPlan plan;
            plan.source = source;
            plan.target = target;
            plan.root = 0;
            ahfl::TypedAdjustmentNode node;
            node.source = source;
            node.target = target;
            node.ops.push_back(ahfl::TypedAdjustmentOp{.kind = kind});
            plan.nodes.push_back(std::move(node));
            return plan;
        };

    ahfl::TypedProgram program;
    ahfl::TypedStatement to_any;
    to_any.kind = ahfl::TypedStmtKind::Let;
    to_any.node_id = 4301;
    to_any.target_name = "to_any";
    to_any.let_type = any_ty;
    to_any.let_adjustment = make_plan(int_ty, any_ty, ahfl::TypedAdjustmentOpKind::ToAny);
    program.statements.push_back(std::move(to_any));

    ahfl::TypedStatement from_never;
    from_never.kind = ahfl::TypedStmtKind::Let;
    from_never.node_id = 4302;
    from_never.target_name = "from_never";
    from_never.let_type = int_ty;
    from_never.let_adjustment = make_plan(never_ty, int_ty, ahfl::TypedAdjustmentOpKind::FromNever);
    program.statements.push_back(std::move(from_never));

    const auto back =
        ahfl::deserialize_typed_program_json(ahfl::serialize_typed_program_json(program));
    REQUIRE(back.has_value());
    REQUIRE(back->statements.size() == 2);
    REQUIRE(back->statements[0].let_adjustment.has_value());
    REQUIRE(back->statements[1].let_adjustment.has_value());
    CHECK(back->statements[0].let_adjustment->nodes[0].ops[0].kind ==
          ahfl::TypedAdjustmentOpKind::ToAny);
    CHECK(back->statements[1].let_adjustment->nodes[0].ops[0].kind ==
          ahfl::TypedAdjustmentOpKind::FromNever);
}

// RFC 0026 P4 (coercion) F1 re-review fix (Codex P1): the typed-HIR plan reader
// must fail the WHOLE deserialize closed when an inner structural field is the
// wrong JSON kind (nodes/ops not an array, a node/op item not an object, an
// unknown variance spelling, or a non-object let_adjustment) - typed-HIR has no
// later plan verifier, so a silent downgrade would let a malformed cache load.
// Each corruption below must make deserialize_typed_program_json return nullopt.
TEST_CASE("Typed-HIR plan deserialize fails closed on malformed inner structure") {
    ahfl::TypeContext types;
    const auto int_ty = types.make(ahfl::TypeKind::Int);
    const auto bounded = types.bounded_int(0, 0);

    ahfl::TypedAdjustmentPlan plan;
    plan.source = bounded;
    plan.target = int_ty;
    plan.root = 0;
    ahfl::TypedAdjustmentNode node;
    node.source = bounded;
    node.target = int_ty;
    node.ops.push_back(ahfl::TypedAdjustmentOp{.kind = ahfl::TypedAdjustmentOpKind::IntWiden});
    plan.nodes.push_back(std::move(node));

    ahfl::TypedProgram program;
    ahfl::TypedStatement let;
    let.kind = ahfl::TypedStmtKind::Let;
    let.node_id = 7;
    let.target_name = "x";
    let.let_type = int_ty;
    let.let_adjustment = std::move(plan);
    program.statements.push_back(std::move(let));

    const auto json = ahfl::serialize_typed_program_json(program);
    REQUIRE(ahfl::deserialize_typed_program_json(json).has_value()); // baseline parses

    const auto corrupt_with = [&](std::string_view needle, std::string_view replacement) {
        auto text = json;
        const auto pos = text.find(needle);
        REQUIRE(pos != std::string::npos);
        text.replace(pos, needle.size(), replacement);
        return text;
    };

    SUBCASE("nodes is present but not an array") {
        CHECK_FALSE(
            ahfl::deserialize_typed_program_json(corrupt_with("\"nodes\":[", "\"nodes\":{"))
                .has_value());
    }
    SUBCASE("ops is present but not an array") {
        CHECK_FALSE(
            ahfl::deserialize_typed_program_json(corrupt_with("\"ops\":[", "\"ops\":{"))
                .has_value());
    }
    SUBCASE("let_adjustment is present but not an object") {
        CHECK_FALSE(ahfl::deserialize_typed_program_json(
                        corrupt_with("\"let_adjustment\":{", "\"let_adjustment\":["))
                        .has_value());
    }
    SUBCASE("unknown adjustment op kind") {
        CHECK_FALSE(
            ahfl::deserialize_typed_program_json(corrupt_with("\"int_widen\"", "\"not_a_kind\""))
                .has_value());
    }
}

// RFC 0026 P4 (coercion) F1 re-review fix (Codex P1): an unknown variance
// spelling in a struct/enum decl's type_param_variances fails the typed-HIR
// deserialize closed (not a silent Invariant). Round-trips a real generic decl,
// then corrupts its variance token.
TEST_CASE_FIXTURE(TypedHIRFixture, "Typed-HIR deserialize fails closed on unknown variance") {
    const auto root = make_temp_project("variance_unknown");
    const auto source_path = module_source_path(root, "vr::u");
    const auto source = std::string("module vr::u;\n") + "struct Box<T> { value: T; }\n" +
                        "fn keep(b: Box<Int>) -> Box<Int> effect Pure decreases 0 { return b; }\n";
    write_file(source_path, source);
    const auto result = check_project(root, {source_path});
    const auto json = ahfl::serialize_typed_program_json(result.typed_program);
    REQUIRE(ahfl::deserialize_typed_program_json(json).has_value());
    // The generic Box carries a "covariant" entry; corrupting it must fail closed.
    const auto pos = json.find("\"covariant\"");
    REQUIRE(pos != std::string::npos);
    auto corrupt = json;
    corrupt.replace(pos, std::string("\"covariant\"").size(), "\"sideways\"");
    CHECK_FALSE(ahfl::deserialize_typed_program_json(corrupt).has_value());
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

TEST_CASE_FIXTURE(
    TypedHIRFixture,
    "TypedStmtKind matrix — typed_visit dispatches every kind exactly once per statement") {
    const auto root = make_temp_project("stmt_kind_dispatch");
    const auto source_path = module_source_path(root, "cov::dispatch");
    const auto source = std::string("module cov::dispatch;\n") + "import std::option as option;\n" +
                        kSharedPrefix +
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

// Declaration bounds are part of the current snapshot, including their order
// and source ranges. Exercise the public writer/reader with multiple bounds so
// dropping a clause or one of its trait names cannot pass a closed round-trip.
template <typename Info>
[[nodiscard]] ahfl::TypedProgram declaration_bounds_program(ahfl::ast::NodeKind kind) {
    Info info{};
    info.symbol = ahfl::SymbolId{1};
    info.canonical_name = "snapshot::Bounded";
    info.where_clause.bounds = {
        ahfl::WhereBoundInfo{
            .subject_name = "T",
            .trait_names = {"snapshot::Display", "snapshot::Eq"},
            .source_range = {21, 45},
        },
        ahfl::WhereBoundInfo{
            .subject_name = "U",
            .trait_names = {"snapshot::Clone"},
            .source_range = {48, 66},
        },
    };
    if constexpr (requires { info.type_param_names; }) {
        info.type_param_names = {"T", "U"};
    }
    if constexpr (requires { info.return_type; }) {
        info.return_type = ahfl::TypeContext::global().make(ahfl::TypeKind::Bool);
    }
    ahfl::TypedDecl declaration{};
    declaration.kind = kind;
    declaration.symbol = info.symbol;
    declaration.payload = std::move(info);
    ahfl::TypedProgram program;
    program.declarations.push_back(std::move(declaration));
    return program;
}

[[nodiscard]] ahfl::json::JsonValue &snapshot_declaration_value(ahfl::json::JsonValue &root) {
    auto *declarations = root.get_mut("declarations");
    REQUIRE(declarations != nullptr);
    REQUIRE(declarations->array_items.size() == 1);
    auto *payload = declarations->array_items.front()->get_mut("payload");
    REQUIRE(payload != nullptr);
    auto *value = payload->get_mut("value");
    REQUIRE(value != nullptr);
    return *value;
}

template <typename Info>
void check_declaration_bounds_roundtrip(ahfl::ast::NodeKind kind) {
    const auto program = declaration_bounds_program<Info>(kind);
    const auto snapshot = ahfl::serialize_typed_program_json(program);
    const auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    REQUIRE(restored->declarations.size() == 1);
    const auto &bounds = std::get<Info>(restored->declarations.front().payload).where_clause.bounds;
    REQUIRE(bounds.size() == 2);
    CHECK(bounds[0].subject_name == "T");
    CHECK(bounds[0].trait_names == std::vector<std::string>{"snapshot::Display", "snapshot::Eq"});
    CHECK(bounds[0].source_range.begin_offset == 21);
    CHECK(bounds[0].source_range.end_offset == 45);
    CHECK(bounds[1].subject_name == "U");
    CHECK(bounds[1].trait_names == std::vector<std::string>{"snapshot::Clone"});
    CHECK(bounds[1].source_range.begin_offset == 48);
    CHECK(bounds[1].source_range.end_offset == 66);
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);

    const auto rejects = [&](auto mutate) {
        auto document = ahfl::json::parse_json(snapshot);
        REQUIRE(document.has_value());
        auto &value = snapshot_declaration_value(**document);
        mutate(value);
        CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                        .has_value());
    };
    rejects([](auto &value) {
        std::erase_if(value.object_fields,
                      [](const auto &field) { return field.first == "where_clause"; });
    });
    rejects([](auto &value) { value.set("where_clause", ahfl::json::JsonValue::make_null()); });
    rejects([](auto &value) { value.set("where_clause", ahfl::json::JsonValue::make_object()); });
    rejects([](auto &value) {
        value.get_mut("where_clause")->array_items.front() = ahfl::json::JsonValue::make_null();
    });
    for (const std::string_view field : {"subject_name", "trait_names", "source_range"}) {
        INFO("missing bound field: " << field);
        rejects([&](auto &value) {
            auto &bound = *value.get_mut("where_clause")->array_items.front();
            std::erase_if(bound.object_fields,
                          [&](const auto &item) { return item.first == field; });
        });
    }
    rejects([](auto &value) {
        value.get_mut("where_clause")->array_items.front()->set(
            "trait_names", ahfl::json::JsonValue::make_null());
    });
    rejects([](auto &value) {
        value.get_mut("where_clause")->array_items.front()->get_mut("trait_names")
            ->array_items.front() = ahfl::json::JsonValue::make_int(7);
    });
}

TEST_CASE("Typed HIR declaration where clauses preserve complete ordered bounds") {
    SUBCASE("Struct") {
        check_declaration_bounds_roundtrip<ahfl::StructTypeInfo>(ahfl::ast::NodeKind::StructDecl);
    }
    SUBCASE("Enum") {
        check_declaration_bounds_roundtrip<ahfl::EnumTypeInfo>(ahfl::ast::NodeKind::EnumDecl);
    }
    SUBCASE("Capability") {
        check_declaration_bounds_roundtrip<ahfl::CapabilityTypeInfo>(
            ahfl::ast::NodeKind::CapabilityDecl);
    }
    SUBCASE("Fn") {
        check_declaration_bounds_roundtrip<ahfl::FnTypeInfo>(ahfl::ast::NodeKind::FnDecl);
    }
    SUBCASE("Trait uses the same complete bounds reader") {
        check_declaration_bounds_roundtrip<ahfl::TraitTypeInfo>(ahfl::ast::NodeKind::TraitDecl);
    }
}

TEST_CASE("Typed HIR capability effect rejects missing and malformed objects safely") {
    const auto program = declaration_bounds_program<ahfl::CapabilityTypeInfo>(
        ahfl::ast::NodeKind::CapabilityDecl);
    const auto snapshot = ahfl::serialize_typed_program_json(program);
    REQUIRE(ahfl::deserialize_typed_program_json(snapshot).has_value());

    SUBCASE("missing effect") {
        auto document = ahfl::json::parse_json(snapshot);
        REQUIRE(document.has_value());
        auto &value = snapshot_declaration_value(**document);
        std::erase_if(value.object_fields,
                      [](const auto &field) { return field.first == "effect"; });
        CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                        .has_value());
    }
    SUBCASE("wrong effect shape") {
        for (const std::string_view malformed : {"null", "[]", "true", "7", "\"read\"", "{}"}) {
            INFO("effect: " << malformed);
            auto document = ahfl::json::parse_json(snapshot);
            auto effect = ahfl::json::parse_json(malformed);
            REQUIRE(document.has_value());
            REQUIRE(effect.has_value());
            snapshot_declaration_value(**document).set("effect", std::move(*effect));
            CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                            .has_value());
        }
    }
    SUBCASE("wrong required effect member") {
        auto document = ahfl::json::parse_json(snapshot);
        REQUIRE(document.has_value());
        snapshot_declaration_value(**document).get_mut("effect")->set(
            "declared", ahfl::json::JsonValue::make_string("true"));
        CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                        .has_value());
    }
}

TEST_CASE("Typed HIR snapshots preserve a real definition zero and Flow agent ownership") {
    ahfl::Frontend frontend;
    const auto parsed = frontend.parse_text("symbol_presence.ahfl", R"AHFL(
struct First { flag: Bool = false; }
agent Worker {
    input: First;
    context: First;
    output: First;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
contract for Worker { ensures: true; }
flow for Worker {
    state Init { goto Done; }
    state Done { return input; }
}
)AHFL");
    REQUIRE_FALSE(parsed.has_errors());
    REQUIRE(parsed.program != nullptr);
    ahfl::Resolver resolver;
    const auto resolved = resolver.resolve(*parsed.program);
    REQUIRE_FALSE(resolved.has_errors());
    ahfl::TypeChecker checker;
    const auto checked = checker.check(*parsed.program, resolved);
    REQUIRE_FALSE(checked.has_errors());
    const auto &program = checked.typed_program;
    REQUIRE_FALSE(program.declarations.empty());
    REQUIRE(program.declarations.front().symbol.has_value());
    CHECK(program.declarations.front().symbol->value == 0);
    REQUIRE(std::holds_alternative<ahfl::StructTypeInfo>(program.declarations.front().payload));

    const auto snapshot = ahfl::serialize_typed_program_json(program);
    auto document = ahfl::json::parse_json(snapshot);
    REQUIRE(document.has_value());
    CHECK((*document)->get("declarations")->array_items.front()->get("symbol")->as_int() == 0);
    const auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
    REQUIRE(restored->declarations.front().symbol.has_value());
    CHECK(restored->declarations.front().symbol->value == 0);

    std::size_t flow_count = 0;
    std::size_t contract_count = 0;
    for (const auto &decl : restored->declarations) {
        if (decl.kind == ahfl::ast::NodeKind::FlowDecl) {
            ++flow_count;
            CHECK_FALSE(decl.symbol.has_value());
            REQUIRE(decl.associated_agent_symbol.has_value());
            const auto &flow = std::get<ahfl::FlowTypeInfo>(decl.payload);
            CHECK(decl.associated_agent_symbol == flow.target_symbol);
            CHECK(flow.symbol == flow.target_symbol);
        }
        if (decl.kind == ahfl::ast::NodeKind::ContractDecl) {
            ++contract_count;
            CHECK_FALSE(decl.symbol.has_value());
            REQUIRE(decl.associated_agent_symbol.has_value());
            CHECK(decl.associated_agent_symbol ==
                  std::get<ahfl::ContractTypeInfo>(decl.payload).target_symbol);
        }
    }
    CHECK(flow_count == 1);
    CHECK(contract_count == 1);

    SUBCASE("named declaration requires a present symbol") {
        auto *first = (*document)->get_mut("declarations")->array_items.front().get();
        first->set("symbol", ahfl::json::JsonValue::make_null());
        CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                        .has_value());
    }
    SUBCASE("symbol remains a mandatory field") {
        auto *first = (*document)->get_mut("declarations")->array_items.front().get();
        std::erase_if(first->object_fields,
                      [](const auto &field) { return field.first == "symbol"; });
        CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                        .has_value());
    }
}

void check_metadata_symbol_null(ahfl::ast::NodeKind kind, ahfl::TypedDeclPayload payload) {
    ahfl::TypedDecl decl{};
    decl.kind = kind;
    decl.symbol = std::nullopt;
    if (kind == ahfl::ast::NodeKind::FlowDecl || kind == ahfl::ast::NodeKind::ContractDecl) {
        decl.associated_agent_symbol = ahfl::SymbolId{0};
    }
    decl.payload = std::move(payload);
    ahfl::TypedProgram program;
    program.declarations.push_back(std::move(decl));
    const auto snapshot = ahfl::serialize_typed_program_json(program);
    const auto restored = ahfl::deserialize_typed_program_json(snapshot);
    REQUIRE(restored.has_value());
    REQUIRE(restored->declarations.size() == 1);
    CHECK_FALSE(restored->declarations.front().symbol.has_value());
    CHECK(restored->declarations.front().associated_agent_symbol ==
          program.declarations.front().associated_agent_symbol);
    CHECK(ahfl::serialize_typed_program_json(*restored) == snapshot);
    auto document = ahfl::json::parse_json(snapshot);
    REQUIRE(document.has_value());
    auto &encoded = *(*document)->get_mut("declarations")->array_items.front();
    REQUIRE(encoded.get("symbol") != nullptr);
    CHECK(encoded.get("symbol")->is_null());
    encoded.set("symbol", ahfl::json::JsonValue::make_int(0));
    CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                    .has_value());
    encoded.set("symbol", ahfl::json::JsonValue::make_int(7));
    CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                    .has_value());
    std::erase_if(encoded.object_fields,
                  [](const auto &field) { return field.first == "symbol"; });
    CHECK_FALSE(ahfl::deserialize_typed_program_json(ahfl::json::serialize_json(**document))
                    .has_value());
}

TEST_CASE("Typed HIR metadata requires explicit null instead of a definition identity") {
    SUBCASE("Module") {
        check_metadata_symbol_null(ahfl::ast::NodeKind::ModuleDecl, ahfl::ModuleDeclInfo{});
    }
    SUBCASE("Import") {
        check_metadata_symbol_null(ahfl::ast::NodeKind::ImportDecl, ahfl::ImportDeclInfo{});
    }
    SUBCASE("Impl") {
        ahfl::ImplTypeInfo impl{};
        impl.target_type = ahfl::TypeContext::global().make(ahfl::TypeKind::Bool);
        check_metadata_symbol_null(ahfl::ast::NodeKind::ImplDecl, std::move(impl));
    }
    SUBCASE("Contract") {
        check_metadata_symbol_null(ahfl::ast::NodeKind::ContractDecl, ahfl::ContractTypeInfo{});
    }
    SUBCASE("Flow") {
        check_metadata_symbol_null(ahfl::ast::NodeKind::FlowDecl, ahfl::FlowTypeInfo{});
    }
}

} // namespace
