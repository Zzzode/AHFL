#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/core_json.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "base/json/json_value.hpp" // ahfl::json::kMaxJsonNestingDepth
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// KR6.9-B3 acceptance: the Core-IR JSON reader (RFC 0026 P9).
//
//   * R1 — byte-exact re-emit: print(parse(print(p))) == print(p) over a corpus
//     of verifier-clean programs.
//   * R2 — structural identity: core_program_equal(p, parse(print(p))).
//   * The malformed battery: every rejection asserts a TYPED diagnostic code
//     (not just !ok()), mirroring ir_json_round_trip.cpp's negatives.

namespace {

using namespace ahfl;

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

/// Lower one self-contained source through the REAL frontend (parse -> resolve ->
/// typecheck -> AhflIr -> Core). Returns nullopt if the source does not reach a
/// clean Core program.
[[nodiscard]] std::optional<ir::core::CoreProgram>
lower_source(const std::string &label, const std::string &source) {
    const Frontend frontend;
    auto parse = frontend.parse_text(label + ".ahfl", source);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const auto ahfl_ir = lower_program_ir(*parse.program, resolve, typecheck);
    auto result = ir::core::lower_ahfl_to_core(ahfl_ir);
    if (!result.ok()) {
        return std::nullopt;
    }
    return std::move(result.program);
}

/// The same, over the repo std sysroot (so a std Option/List body lowers).
[[nodiscard]] std::optional<ir::core::CoreProgram>
lower_sysroot_source(const std::string &unique, const std::string &source) {
    const auto root =
        std::filesystem::temp_directory_path() / ("ahfl_core_json_" + unique);
    std::filesystem::remove_all(root);
    const auto main_path = root / "app" / "main.ahfl";
    write_file(main_path, source);

    const Frontend frontend;
    const auto parse = parse_project(
        frontend,
        test_support::project_input_with_repo_std_for_test_file(main_path, root, __FILE__));
    if (parse.has_errors()) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(parse.graph);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(parse.graph, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    const auto ahfl_ir = lower_program_ir(parse.graph, resolve, typecheck);
    auto result = ir::core::lower_ahfl_to_core(ahfl_ir);
    if (!result.ok()) {
        return std::nullopt;
    }
    return std::move(result.program);
}

/// The exact source text the committed `ok_std_option.core.json` golden was
/// generated from. Kept as one function so the golden and the R2 fresh lower
/// cannot drift: both read this text.
[[nodiscard]] std::string source_text() {
    return R"AHFL(
module app::main;

import std::option;

struct Req { amount: Int; }
struct Ctx { slot: std::option::Option<Bool> = std::option::Option::None; }
struct Reply { ok: Bool = false; }

capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Req;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Charge];

    transition Init -> Done;
}

flow for Payer {
    state Init {
        if input.amount > 0 {
            ctx.slot = std::option::Option::Some(Charge(input.amount));
        } else {
            ctx.slot = std::option::Option::None;
        }
        goto Done;
    }
    state Done {
        return Reply { ok: false };
    }
}
)AHFL";
}

[[nodiscard]] std::string print(const ir::core::CoreProgram &program) {
    std::ostringstream out;
    ahfl::ir::core::print_core_ir_json(program, out);
    return out.str();
}

/// Every rejection must carry a typed diagnostic (not just !ok()), so a silent
/// default can never masquerade as a rejection.
void require_rejected_with(std::string_view json, std::string_view expected_code) {
    const auto result = ir::core::parse_core_ir_json(json);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    bool found = false;
    for (const auto &diagnostic : result.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " — " << diagnostic.message);
        if (diagnostic.code == expected_code) {
            found = true;
        }
    }
    CHECK_MESSAGE(found, "expected diagnostic code " << expected_code);
}

// ---------------------------------------------------------------------------
// The corpus: self-contained sources that lower to a verifier-clean Core
// program. R1/R2 run over EVERY entry, so a new shape cannot silently escape
// round-trip.
// ---------------------------------------------------------------------------

[[nodiscard]] std::map<std::string, std::string> corpus() {
    std::map<std::string, std::string> sources;

    // A flow with a capability call nested in an if-branch and a constructor.
    sources["flow_branch"] = R"AHFL(
module payflow;

struct Order { total: Int; }
struct Ctx { charged: Bool = false; }
struct Outcome { ok: Bool = false; }

capability Fetch(id: Int) -> Order;
capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Order;
    context: Ctx;
    output: Outcome;
    states: [Init, Work, Done];
    initial: Init;
    final: [Done];
    capabilities: [Fetch, Charge];

    transition Init -> Work;
    transition Work -> Done;
}

flow for Payer {
    state Init {
        goto Work;
    }
    state Work with {
        retry: 2;
        timeout: 30s;
    } {
        let order = Fetch(input.total);
        if order.total > 0 {
            ctx.charged = Charge(order.total);
            goto Done;
        } else {
            goto Done;
        }
    }
    state Done {
        return Outcome { ok: ctx.charged };
    }
}
)AHFL";

    // A multi-node DAG workflow (kWorkflowSource shape).
    sources["workflow_pipe"] = R"AHFL(
module wf;

struct Req { amount: Int; }
struct Mid { total: Int; }
struct Reply { ok: Bool; }

agent First {
    input: Req;
    output: Mid;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

agent Second {
    input: Mid;
    output: Reply;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

workflow Pipe {
    input: Req;
    output: Reply;

    node first: First(input);
    node second: Second(Mid { total: first.total }) after [first];

    return: second;
}
)AHFL";

    // An enum with a unit variant + a struct-payload variant, and a stateless
    // (Unit context) agent — exercising the omitted-context_type branch.
    sources["enum_and_unit_context"] = R"AHFL(
module app::main;

enum Verdict { Approve, Reject }

struct Req { amount: Int; }
struct Reply { ok: Bool = false; }

agent Decider {
    input: Req;
    output: Reply;
    states: [Run, Done];
    initial: Run;
    final: [Done];
    capabilities: [];

    transition Run -> Done;
}

flow for Decider {
    state Run {
        let v = Verdict::Approve;
        goto Done;
    }
    state Done {
        return Reply { ok: false };
    }
}
)AHFL";

    return sources;
}

// The sysroot corpus: programs whose bodies use std nominals, so the
// program-global value_types arena carries a CoreVtNominal with args.
[[nodiscard]] std::map<std::string, std::string> sysroot_corpus() {
    std::map<std::string, std::string> sources;
    sources["std_option"] = source_text();
    // FB-1: outlined fn bodies + a statically-resolved CoreCallExpr (both the
    // canonical empty-type-args non-generic instance and a monomorphized
    // generic instance) must survive the fns wire table round-trip.
    sources["pure_direct_call"] = R"AHFL(
module app::main;

struct Req { amount: Int; }
struct Ctx {}
struct Reply { ok: Bool = false; doubled: Int; }

fn double_it(x: Int) -> Int {
    return x * 2;
}

fn identity<T>(x: T) -> T {
    return x;
}

agent CalcAgent {
    input: Req;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];

    transition Init -> Done;
}

flow for CalcAgent {
    state Init {
        let a: Int = double_it(21);
        let b: Int = identity<Int>(a);
        if (b == 42) {
            goto Done;
        } else {
            goto Done;
        }
    }
    state Done {
        return Reply { ok: false, doubled: 0 };
    }
}
)AHFL";
    return sources;
}

} // namespace

TEST_CASE("Core-IR JSON round-trips byte-identically (R1) and is structurally equal (R2)") {
    for (const auto &[label, source] : corpus()) {
        CAPTURE(label);
        const auto program = lower_source(label, source);
        REQUIRE(program.has_value());

        const std::string first = print(*program);
        REQUIRE_FALSE(first.empty());

        // R1 — print ∘ parse ∘ print == print, as bytes. The input to the second
        // print is a freshly interned arena (§6.2), so byte equality proves the
        // rebuild preserved canonical arena order.
        const auto parsed = ir::core::parse_core_ir_json(first);
        for (const auto &diagnostic : parsed.diagnostics) {
            INFO("diagnostic: " << diagnostic.code << " — " << diagnostic.message);
        }
        REQUIRE(parsed.ok());
        CHECK(parsed.diagnostics.empty());
        const std::string reemitted = print(*parsed.program);
        CHECK(reemitted == first);

        // R2 — structural identity. Not implied by R1 for a jointly-wrong pair.
        CHECK(ir::core::core_program_equal(*program, *parsed.program));
    }
}

TEST_CASE("the committed Core-IR JSON golden round-trips byte-identically") {
    // A golden anchored on the real-frontend sysroot path (std Option), so the
    // byte output is pinned to a committed artifact rather than only to a
    // self-consistent re-print (RFC 0026 P9 §7 R1/R2 + B2's golden anchoring).
#ifdef AHFL_SOURCE_DIR
    const auto path = std::filesystem::path{AHFL_SOURCE_DIR} / "tests" / "golden" / "core" /
                      "ok_std_option.core.json";
#else
    const auto path = std::filesystem::path{"tests"} / "golden" / "core" /
                      "ok_std_option.core.json";
#endif
    REQUIRE(std::filesystem::exists(path));
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string golden = buffer.str();
    REQUIRE_FALSE(golden.empty());

    // R1 — parse then re-print must reproduce the committed bytes exactly.
    const auto parsed = ir::core::parse_core_ir_json(golden);
    for (const auto &diagnostic : parsed.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " — " << diagnostic.message);
    }
    REQUIRE(parsed.ok());
    CHECK(print(*parsed.program) == golden);

    // R2 — the parsed program is structurally identical to a fresh lowering of
    // the SAME source text compiled into the golden (the corpus source, not a
    // reformatted copy: source_range offsets are part of the IR).
    const auto reprogram = lower_sysroot_source("golden", source_text());
    REQUIRE(reprogram.has_value());
    CHECK(ir::core::core_program_equal(*reprogram, *parsed.program));
}

TEST_CASE("Core-IR JSON round-trips the real-sysroot std-nominal corpus") {
    for (const auto &[label, source] : sysroot_corpus()) {
        CAPTURE(label);
        const auto program = lower_sysroot_source(label, source);
        REQUIRE(program.has_value());

        const std::string first = print(*program);
        const auto parsed = ir::core::parse_core_ir_json(first);
        REQUIRE(parsed.ok());
        CHECK(print(*parsed.program) == first);
        CHECK(ir::core::core_program_equal(*program, *parsed.program));
    }
}

// ---------------------------------------------------------------------------
// §7 "Corpus" — the DESIGN-MANDATED directory-discovered corpus.
//
// RFC 0026 P9 §7 requires round-trip over "every in-tree fixture that lowers to
// a verifier-clean Core program, ... so a new fixture cannot silently escape
// round-trip". The KR6.7 conformance classifier's method — directory discovery
// with an asserted pinned set — is mirrored here: the test scans tests/golden,
// lowers everything, and asserts the discovered set EQUALS the pinned list. A new
// fixture (or one that stops lowering) changes the discovered set and FAILS the
// equality check, so the list can never lag the tree silently. This closes the
// node-set holes the hand-picked `corpus()` left: p6_match_* (pattern/match
// arms), p6_collection (bounded-collection ops), p6_coerce* (coercion plans),
// the temporal fixtures, and the workflow/flow fixtures.
// ---------------------------------------------------------------------------

namespace {

#ifdef AHFL_SOURCE_DIR
constexpr std::string_view kGoldenRoot = AHFL_SOURCE_DIR "/tests/golden";
#else
constexpr std::string_view kGoldenRoot = "tests/golden";
#endif

/// Lower one golden fixture through the real frontend. Returns nullopt unless it
/// reaches a verifier-clean Core program.
[[nodiscard]] std::optional<ir::core::CoreProgram>
lower_golden_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto program = lower_source(path.stem().string(), buffer.str());
    if (!program.has_value()) {
        return std::nullopt;
    }
    if (!ir::core::verify_core_program(*program).ok()) {
        return std::nullopt;
    }
    return program;
}

/// Every `*.ahfl` under tests/golden (relative, sorted) that lowers to a
/// verifier-clean Core program.
[[nodiscard]] std::vector<std::string> discover_core_corpus() {
    const auto root = std::filesystem::path{kGoldenRoot};
    std::vector<std::string> discovered;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end;
         it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) {
            continue;
        }
        const auto &path = it->path();
        if (path.extension() != ".ahfl") {
            continue;
        }
        if (lower_golden_file(path).has_value()) {
            discovered.push_back(std::filesystem::relative(path, root, ec).generic_string());
        }
    }
    REQUIRE_FALSE(ec);
    std::sort(discovered.begin(), discovered.end());
    return discovered;
}

/// The pinned set, asserted to equal what directory discovery finds right now.
[[nodiscard]] std::vector<std::string> pinned_core_corpus() {
    return {
        "formal/fail_bounded_data_semantics.ahfl",
        "formal/fail_real_smv_control.ahfl",
        "formal/fail_smt_bmc_refuted.ahfl",
        "formal/ok_bounded_data_semantics.ahfl",
        "formal/ok_flow_workflow_semantics.ahfl",
        "formal/ok_real_smv_control.ahfl",
        "formal/ok_smt_encoding.ahfl",
        "formal/warn_not_in_verified_subset.ahfl",
        "formatter/formatted_struct_2spaces.ahfl",
        "formatter/formatted_struct_4spaces.ahfl",
        "formatter/unformatted_struct.ahfl",
        "infra/multi_agent.ahfl",
        "infra/palette.ahfl",
        "infra/service_mesh.ahfl",
        "infra/stateless.ahfl",
        "ir/ok_dead_state_elimination.ahfl",
        "ir/ok_pass_productization.ahfl",
        "ir/ok_runtime_plan_effect.ahfl",
        "ir/ok_workflow_simplification.ahfl",
        "ir/ok_workflow_value_flow.ahfl",
        "runtime/e2e_multi_agent.ahfl",
        "runtime/enum_variant_e2e.ahfl",
        "runtime/float_output_e2e.ahfl",
        "runtime/if_let_e2e.ahfl",
        "typecheck/agent_duplicate_final_state.ahfl",
        "typecheck/agent_duplicate_state.ahfl",
        "typecheck/agent_final_state_not_declared.ahfl",
        "typecheck/agent_final_state_outgoing_transition.ahfl",
        "typecheck/agent_initial_state_not_declared.ahfl",
        "typecheck/agent_transition_source_not_declared.ahfl",
        "typecheck/agent_transition_target_not_declared.ahfl",
        "typecheck/agent_unreachable_state.ahfl",
        "typecheck/contract_invariant_running_not_allowed.ahfl",
        "typecheck/contract_invariant_unknown_state.ahfl",
        "typecheck/flow_duplicate_handler.ahfl",
        "typecheck/flow_final_handler_must_return.ahfl",
        "typecheck/flow_illegal_goto.ahfl",
        "typecheck/flow_missing_final_state_handler.ahfl",
        "typecheck/flow_missing_handler.ahfl",
        "typecheck/flow_non_final_handler_must_goto.ahfl",
        "typecheck/flow_return_in_non_final.ahfl",
        "typecheck/ok_agent_schema_alias.ahfl",
        "typecheck/temporal_called_in_workflow.ahfl",
        "typecheck/temporal_completed_in_agent_contract.ahfl",
        "typecheck/temporal_in_state_in_workflow.ahfl",
        "typecheck/temporal_unknown_workflow_node.ahfl",
        "typecheck/workflow_completed_invalid_state.ahfl",
        "wasm/e1_identity_agent.ahfl",
        "wasm/e2_capability_agent.ahfl",
        "wasm/e3_capability_workflow.ahfl",
        "wasm/e3_capability_workflow_resume.ahfl",
        "wasm/e3_identity_workflow.ahfl",
        "wasm/fb1_aggregate_direct_call.ahfl",
        "wasm/fb1_direct_call.ahfl",
        "wasm/fb2_bounded_recursion.ahfl",
        "wasm/fb2_too_deep_recursion.ahfl",
        "wasm/fb3_bounded_collection.ahfl",
        "wasm/fb3_byvalue_capture.ahfl",
        "wasm/fb3_higher_order.ahfl",
        "wasm/fb3_nested_activation.ahfl",
        "wasm/fb3_nested_lambda_flow.ahfl",
        "wasm/fb4_construct_only_closure.ahfl",
        "wasm/fb4_cross_agent_capability_leak.ahfl",
        "wasm/fb4_effect_clause_pure_body.ahfl",
        "wasm/fb4_effectful_fn.ahfl",
        "wasm/fb4_int64_capability_abi.ahfl",
        "wasm/p6_aggregate.ahfl",
        "wasm/p6_cascade.ahfl",
        "wasm/p6_cascade_high.ahfl",
        "wasm/p6_coerce.ahfl",
        "wasm/p6_coerce_bounds.ahfl",
        "wasm/p6_collection.ahfl",
        "wasm/p6_elseless_fallthrough.ahfl",
        "wasm/p6_elseless_taken.ahfl",
        "wasm/p6_frame_oversized_backing.ahfl",
        "wasm/p6_frame_two_containers.ahfl",
        "wasm/p6_implies.ahfl",
        "wasm/p6_match_aggregate_payload_reject.ahfl",
        "wasm/p6_match_arm_trap.ahfl",
        "wasm/p6_match_binding_payload.ahfl",
        "wasm/p6_match_enum.ahfl",
        "wasm/p6_match_expr.ahfl",
        "wasm/p6_match_fallthrough.ahfl",
        "wasm/p6_match_guard.ahfl",
        "wasm/p6_match_or.ahfl",
        "wasm/p6_match_result_i64.ahfl",
        "wasm/p6_member_base.ahfl",
        "wasm/p6_neg_compare.ahfl",
        "wasm/p6_nested_depth3.ahfl",
        "wasm/p6_nested_depth3_taken.ahfl",
        "wasm/p6_nested_elseless.ahfl",
        "wasm/p6_nested_elseless_taken.ahfl",
        "wasm/p6_nested_fallthrough.ahfl",
        "wasm/p6_nested_projection.ahfl",
        "wasm/p6_nested_taken_high.ahfl",
        "wasm/p6_scalar_cond.ahfl",
        "wasm/p6_scalar_trap.ahfl",
        "wasm/p6_unwrap_none_trap.ahfl",
        "wasm/p6_unwrap_some.ahfl",
        "wasm/v2a_computed_aggregate.ahfl",
        "wasm/v2a_computed_enum.ahfl",
        "wasm/v2a_computed_if_let_return.ahfl",
        "wasm/v2a_computed_payload_enum.ahfl",
        "wasm/v2a_computed_scalar.ahfl",
        "wasm/v2a_input_nested_aggregate_reject.ahfl",
        "wasm/v2a_input_payload_enum_reject.ahfl",
        "wasm/v2b_bounded_string.ahfl",
        "wasm/v2b_builtin_i64_final.ahfl",
        "wasm/v2b_computed_string.ahfl",
        "wasm/v2b_enum_string.ahfl",
        "wasm/v2b_list_nested_string_arena.ahfl",
        "wasm/v2b_list_string_arena.ahfl",
        "wasm/v2b_rodata_overflow.ahfl",
        "wasm/v2b_string_passthrough.ahfl",
        // V2-C fix-forward regressions: a two-handler bridge chain, a
        // producing match followed by an ordered bridge statement, and a
        // single tag-only-enum bridge argument.
        "wasm/v2c_bridge_chain.ahfl",
        "wasm/v2c_multi_arg_bridge.ahfl",
        "wasm/v2c_route_then_bridge.ahfl",
        "wasm/v2c_single_arg_bridge.ahfl",
        "wasm/v2c_single_enum_bridge.ahfl",
        // V2-D: the opaque-terminal preamble shape is verifier-clean Core; the
        // fail-closed gate now lives in wasm codegen (BFS reachability), not in
        // Core verification, so the fixture belongs in the lowering corpus.
        "wasm/v2d_computed_goto_preamble_reject.ahfl",
        // V2-D fix-forward: a non-entry computed-final node whose I_k would be
        // dereferenced as an inline nested frame is verifier-clean Core; the
        // fail-closed gate lives in wasm workflow codegen, not the verifier.
        "wasm/v2d_nonentry_nested_frame_reject.ahfl",
        // WH-4: the trace-ring KAT fixture lowers to a verifier-clean Core
        // program, so it belongs in the lowering corpus.
        "wasm/wh4_trace_workflow.ahfl",
    };
}

} // namespace

TEST_CASE("the discovered Core-IR corpus equals the pinned set (a fixture cannot escape)") {
    const auto discovered = discover_core_corpus();
    const auto pinned = pinned_core_corpus();

    // Both directions: a newly-committed fixture that lowers (missing from the
    // pin) AND a pinned fixture that stopped lowering (a stale entry) fail.
    std::vector<std::string> missing; // discovered but not pinned
    std::set_difference(discovered.begin(), discovered.end(), pinned.begin(), pinned.end(),
                        std::back_inserter(missing));
    std::vector<std::string> stale; // pinned but not discovered
    std::set_difference(pinned.begin(), pinned.end(), discovered.begin(), discovered.end(),
                        std::back_inserter(stale));
    for (const auto &path : missing) {
        INFO("newly discovered fixture not in the pinned set: " << path);
    }
    for (const auto &path : stale) {
        INFO("pinned fixture no longer lowers to a verifier-clean Core program: " << path);
    }
    CHECK(missing.empty());
    CHECK(stale.empty());
    CHECK(discovered == pinned);
}

TEST_CASE("Core-IR JSON round-trips every discovered fixture (R1 byte-exact, R2 structural)") {
    const auto root = std::filesystem::path{kGoldenRoot};
    const auto discovered = discover_core_corpus();
    REQUIRE_FALSE(discovered.empty());
    for (const auto &relative : discovered) {
        CAPTURE(relative);
        const auto program = lower_golden_file(root / relative);
        REQUIRE(program.has_value());

        const std::string first = print(*program);
        REQUIRE_FALSE(first.empty());

        // R1 — print ∘ parse ∘ print == print, as bytes.
        const auto parsed = ir::core::parse_core_ir_json(first);
        for (const auto &diagnostic : parsed.diagnostics) {
            INFO("diagnostic: " << diagnostic.code << " — " << diagnostic.message);
        }
        REQUIRE(parsed.ok());
        CHECK(parsed.diagnostics.empty());
        CHECK(print(*parsed.program) == first);

        // R2 — structural identity over EVERY table and body component-wise.
        CHECK(ir::core::core_program_equal(*program, *parsed.program));
    }
}

TEST_CASE("a parsed Core program is verifier-clean") {
    const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
    REQUIRE(program.has_value());
    const auto parsed = ir::core::parse_core_ir_json(print(*program));
    REQUIRE(parsed.ok());
    const auto verified = ir::core::verify_core_program(*parsed.program);
    if (!verified.ok()) {
        for (const auto &diagnostic : verified.diagnostics) {
            INFO("verifier diagnostic: " << diagnostic.code << " — " << diagnostic.message);
        }
    }
    CHECK(verified.ok());
}

// ---------------------------------------------------------------------------
// Malformed battery (fail-closed admission, RFC 0026 P9 §7/§9). Each case
// asserts a specific typed code.
// ---------------------------------------------------------------------------

namespace {

/// The printed form of a clean program, for surgical negative edits.
[[nodiscard]] std::string sample_json() {
    const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
    REQUIRE(program.has_value());
    return print(*program);
}

} // namespace

TEST_CASE("Core-IR JSON reader rejects malformed input with typed diagnostics") {
    const std::string valid = sample_json();
    REQUIRE_FALSE(valid.empty());

    SUBCASE("not JSON at all") {
        require_rejected_with("this is not json", "core.json.NOT_JSON");
    }

    SUBCASE("truncated document") {
        require_rejected_with(valid.substr(0, valid.size() / 2), "core.json.NOT_JSON");
    }

    SUBCASE("duplicate JSON object key") {
        // The shared parser rejects a duplicate key (json_value.cpp parse_object).
        require_rejected_with(
            R"({"format_version": "ahfl.core.v1", "format_version": "ahfl.core.v1"})",
            "core.json.NOT_JSON");
    }

    SUBCASE("wrong format_version") {
        std::string edited = valid;
        const std::string from = "\"ahfl.core.v1\"";
        edited.replace(edited.find(from), from.size(), "\"ahfl.ir.v2\"");
        require_rejected_with(edited, "core.json.BAD_FORMAT_VERSION");
    }

    SUBCASE("wrong layer") {
        std::string edited = valid;
        const std::string from = "\"layer\": \"core\"";
        edited.replace(edited.find(from), from.size(), "\"layer\": \"typed-hir\"");
        require_rejected_with(edited, "core.json.BAD_LAYER");
    }

    SUBCASE("unknown table field in the envelope") {
        std::string edited = valid;
        const std::string from = "\"layer\": \"core\",";
        edited.replace(edited.find(from), from.size(), "\"bogus\": 1,\n  \"layer\": \"core\",");
        require_rejected_with(edited, "core.json.UNKNOWN_FIELD");
    }

    SUBCASE("missing a required table") {
        // Removing "types" leaves the envelope incomplete.
        std::string edited = valid;
        const std::string from = "\"types\": [";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        // Find the balancing close of the array (the tables are newline-indented).
        const auto end = edited.find("\n  ],\n", begin);
        REQUIRE(end != std::string::npos);
        edited.erase(begin, (end + 6) - begin);
        require_rejected_with(edited, "core.json.MISSING_FIELD");
    }

    SUBCASE("out-of-range value-type reference") {
        // A huge dispatch/param value-type id must be rejected as out of range.
        std::string edited = valid;
        const std::string from = "\"param_types\": [";
        const auto begin = edited.find(from);
        if (begin == std::string::npos) {
            // No capability params in this program; use the flow value_types table.
            const std::string from2 = "\"value_count\": ";
            const auto vc = edited.find(from2);
            REQUIRE(vc != std::string::npos);
            const auto line_end = edited.find('\n', vc);
            edited.replace(vc, line_end - vc, "\"value_count\": 99999");
        } else {
            const auto line_end = edited.find(']', begin);
            edited.replace(begin, (line_end + 1) - begin, "\"param_types\": [4294967290]");
        }
        const auto result = ir::core::parse_core_ir_json(edited);
        REQUIRE_FALSE(result.ok());
        REQUIRE_FALSE(result.diagnostics.empty());
    }

    SUBCASE("unknown value-type kind") {
        std::string edited = valid;
        const std::string from = "\"kind\": \"nominal\"";
        const auto begin = edited.find(from);
        if (begin == std::string::npos) {
            return; // no nominal in this program; covered by other cases
        }
        edited.replace(begin, from.size(), "\"kind\": \"wat\"");
        require_rejected_with(edited, "core.json.UNKNOWN_KIND");
    }

    SUBCASE("forward value-type reference") {
        // Re-point a value-type child at a LATER slot: a forward reference must
        // fail closed (§6.2), never silently resolve.
        const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
        REQUIRE(program.has_value());
        // A hand-built doc with two entries where the first is a Tuple whose
        // element is slot 1 (a forward reference).
        const std::string doc = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [
    {
      "kind": "tuple",
      "elements": [1]
    },
    {
      "kind": "bool"
    }
  ],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [],
  "fns": []
})";
        require_rejected_with(doc, "core.json.FORWARD_VALUE_TYPE");
    }

    SUBCASE("non-canonical (reordered) value-type arena") {
        // Two DIFFERENT value types in swapped order: interning mints the
        // first-seen node at 0, so neither slot's minted id matches its position.
        const std::string doc = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [
    {
      "kind": "bool"
    },
    {
      "kind": "unit"
    }
  ],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [],
  "fns": []
})";
        // This document is IN canonical order (bool at 0, unit at 1) and legal,
        // so the swap must be constructed by repeating a shape that collides.
        const std::string swapped = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [
    {
      "kind": "tuple",
      "elements": [0]
    },
    {
      "kind": "bool"
    }
  ],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [],
  "fns": []
})";
        // `tuple` referencing a bool element is structurally fine, but the arena
        // is not a canonical hash-cons (a child id must precede its parent, and
        // slot 0 references a nonexistent earlier entry) — rejected either as a
        // forward reference or by the final verifier.
        const auto result = ir::core::parse_core_ir_json(swapped);
        REQUIRE_FALSE(result.ok());

        // A genuinely non-canonical doc: the same node twice. The second slot
        // interning to the FIRST slot's id trips the identity-remap assert.
        const std::string duplicate = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [
    {
      "kind": "bool"
    },
    {
      "kind": "bool"
    }
  ],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [],
  "fns": []
})";
        require_rejected_with(duplicate, "core.json.NONCANONICAL_ARENA");
        (void)doc;
    }

    SUBCASE("unknown statement kind") {
        std::string edited = valid;
        const std::string from = "\"kind\": \"capability_call\"";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        edited.replace(begin, from.size(), "\"kind\": \"teleport\"");
        require_rejected_with(edited, "core.json.UNKNOWN_KIND");
    }

    SUBCASE("extra field on a node") {
        std::string edited = valid;
        const std::string from = "\"kind\": \"store\",";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        edited.insert(begin + from.size(), "\n              \"bogus\": 1,");
        require_rejected_with(edited, "core.json.UNKNOWN_FIELD");
    }

    SUBCASE("duplicate instance_key") {
        // Only meaningful if the program has >= 2 instances; otherwise build a
        // minimal doc with two same-key instances.
        const std::string doc = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [
    {
      "id": 0,
      "instance_key": "dup",
      "origin": {"kind": "predicate", "canonical_name": "p::P"},
      "dispatch_types": [],
      "payload": {"kind": "predicate"}
    },
    {
      "id": 1,
      "instance_key": "dup",
      "origin": {"kind": "predicate", "canonical_name": "p::P"},
      "dispatch_types": [],
      "payload": {"kind": "predicate"}
    }
  ],
  "fns": []
})";
        require_rejected_with(doc, "core.json.DUPLICATE_INSTANCE_KEY");
    }

    SUBCASE("wrong variant arity in a bad payload") {
        // A capability payload with a bogus field is a field-mask violation.
        const std::string doc = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [
    {
      "id": 0,
      "instance_key": "k",
      "origin": {"kind": "predicate", "canonical_name": "p::P"},
      "dispatch_types": [],
      "payload": {"kind": "predicate", "base": 0}
    }
  ],
  "fns": []
})";
        require_rejected_with(doc, "core.json.UNKNOWN_FIELD");
    }

    SUBCASE("extra field inside a NESTED object (symbol_ref / source_range / bounds)") {
        // §7 applies the unknown-field gate at EVERY object, not only one level
        // up. A hand-editable symbol_ref with a stray member (a `canonical` typo
        // for `canonical_name`) must be rejected, not silently admitted — the
        // writer-by-construction argument does not cover symbol_ref / source_range
        // / bounds, which are hand-constructible.
        const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
        REQUIRE(program.has_value());
        const std::string valid = print(*program);

        // Inject a stray member right after an opening brace so the result stays
        // syntactically valid JSON: the rejection must come from the reader's
        // per-object unknown-field gate, not from a parse error.
        const auto inject_after_brace = [&](std::string_view anchor) {
            std::string edited = valid;
            const auto begin = edited.find(anchor);
            REQUIRE(begin != std::string::npos);
            const auto brace = edited.find('{', begin);
            REQUIRE(brace != std::string::npos);
            edited.insert(brace + 1, "\n\"BOGUS\": 1,");
            return edited;
        };
        require_rejected_with(inject_after_brace("\"symbol_ref\": {"),
                              "core.json.UNKNOWN_FIELD");
        require_rejected_with(inject_after_brace("\"source_range\": {"),
                              "core.json.UNKNOWN_FIELD");
    }

    SUBCASE("extra field inside a bounds object") {
        // A scalar refinement `{"minimum","maximum"}` is likewise hand-editable.
        const std::string doc = R"({
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [],
  "value_types": [
    {"kind": "int", "bounds": {"minimum": 0, "maximum": 10, "BOGUS": 1}}
  ],
  "capabilities": [],
  "agents": [],
  "flows": [],
  "workflows": [],
  "instances": [],
  "fns": []
})";
        require_rejected_with(doc, "core.json.UNKNOWN_FIELD");
    }

    SUBCASE("self-referential expression (arena cycle)") {
        // A unary expr whose operand is ITSELF: the expr arena must be acyclic.
        // The reader admits the shape, and the final verify pass rejects it.
        const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
        REQUIRE(program.has_value());
        std::string edited = print(*program);
        // Replace the first unary/binary expr operand with its own index is hard
        // textually; instead build the minimal cycle: a flow with one `value_ref`
        // (a leaf) is fine, but a `unary` whose operand points at itself is not.
        // Find the flow's exprs array and inject a self-referential unary node.
        const std::string from = "\"exprs\": [";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        const std::string inject =
            "\n        {\n          \"kind\": \"unary\",\n          \"op\": \"not\",\n"
            "          \"operand\": 0,\n          \"result_type\": 0\n        },";
        // Insert as slot 0, shifting existing exprs; this makes slot 0's operand
        // reference slot 0 (itself) only if the injected node lands first.
        edited.insert(begin + from.size(), inject);
        const auto result = ir::core::parse_core_ir_json(edited);
        // Either the reader's own shape checks or the final verifier rejects it;
        // a self-referential arena is never silently accepted.
        REQUIRE_FALSE(result.ok());
        REQUIRE_FALSE(result.diagnostics.empty());
    }

    SUBCASE("over-deep region nesting is rejected by the depth bound") {
        // The reader is an untrusted-input admission boundary: an arbitrarily
        // deep region tree must fail closed rather than recurse the native
        // stack (RFC 0026 P9 §5). Build a program whose body nests `if` regions
        // past the bound.
        const auto nest = [](auto &&self, std::size_t depth,
                             std::size_t limit) -> std::string {
            if (depth >= limit) {
                return R"({"statements": []})";
            }
            return std::string(R"({"statements": [{"kind": "if", "condition": 0, )") +
                   R"("then_region": )" + self(self, depth + 1, limit) + "}]}";
        };
        const std::string body = nest(nest, 0, 1100);
        const std::string doc =
            std::string(R"({"format_version":"ahfl.core.v1","layer":"core","types":[],)") +
            R"("value_types":[{"kind":"bool"}],"capabilities":[],"agents":[],"flows":[)";
        const std::string flow =
            R"({"agent":0,"agent_name":"A","target_ref":{"kind":"agent","canonical_name":"A"},)";
        const std::string tail =
            R"("value_count":1,"exprs":[],"value_types":[0],"coercion_plans":[],"patterns":[],)";
        const std::string states =
            R"("states":[{"state":0,"state_name":"S","policy":{},"body":)" + body + "}]}]" +
            R"(,"workflows":[],"instances":[],"fns":[]})";
        require_rejected_with(doc + flow + tail + states, "core.json.REGION_TOO_DEEP");
    }

    SUBCASE("bracket-only nesting is rejected by the shared parser's depth bound") {
        // REGION-shaped input (above) only reaches the reader's region bound
        // because the reader walks one `statements` level per region. The SHARED
        // JSON parser recurses once per `[`/`{`, so a bracket-only document — no
        // `layer`, no tables — reaches native stack overflow before `read_region`
        // ever runs. The parser itself must fail closed on nesting past
        // `ahfl::json::kMaxJsonNestingDepth`, so this case (which the old
        // region-shaped pin could NOT catch) also stays green. A ~20 KB document
        // of pure `[` used to segfault the process.
        const std::string brackets(50000, '[');
        require_rejected_with(brackets, "core.json.NOT_JSON");

        // One level UNDER the bound still parses (as a plain JSON value), proving
        // the bound is a nesting limit and not a blanket bracket reject.
        const std::string just_under(ahfl::json::kMaxJsonNestingDepth, '[');
        const std::string closed = just_under + std::string(ahfl::json::kMaxJsonNestingDepth, ']');
        // Not a Core document (`layer` missing), but it must be rejected as a
        // BAD_ENVELOPE rather than NOT_JSON: the parse itself succeeded.
        require_rejected_with(closed, "core.json.BAD_ENVELOPE");

        // One level OVER the bound fails at the parse.
        const std::string over(ahfl::json::kMaxJsonNestingDepth + 1, '[');
        require_rejected_with(over, "core.json.NOT_JSON");
    }

    SUBCASE("value_count disagrees with the value_types table length") {
        // §5 — the per-body value_types table is dense (size == value_count).
        const auto program = lower_source("flow_branch", corpus().at("flow_branch"));
        REQUIRE(program.has_value());
        std::string edited = print(*program);
        const std::string from = "\"value_count\": ";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        const auto value_begin = begin + from.size();
        const auto value_end = edited.find(',', value_begin);
        REQUIRE(value_end != std::string::npos);
        edited.replace(value_begin, value_end - value_begin, "4242");
        require_rejected_with(edited, "core.json.VALUE_COUNT_MISMATCH");
    }

    SUBCASE("a shape-valid but verifier-invalid program is rejected by the final verify pass") {
        // An out-of-range agent initial-state id passes the reader's shape checks
        // (it is just a u32) but the final `verify_core_program` rejects it on a
        // program with at least two states, proving the reader's admission
        // boundary includes verification (§9).
        const auto program = lower_source("enum_and_unit_context",
                                          corpus().at("enum_and_unit_context"));
        REQUIRE(program.has_value());
        std::string edited = print(*program);
        const std::string from = "\"initial\": 0,";
        const auto begin = edited.find(from);
        REQUIRE(begin != std::string::npos);
        edited.replace(begin, from.size(), "\"initial\": 77,");
        const auto result = ir::core::parse_core_ir_json(edited);
        REQUIRE_FALSE(result.ok());
        REQUIRE_FALSE(result.diagnostics.empty());
        bool verify_code = false;
        for (const auto &diagnostic : result.diagnostics) {
            // The reader wraps a final verification failure in its own namespace
            // (`core.json.VERIFY_FAILED`) and names the underlying `core.verify.*`
            // code in the message, so a caller can distinguish an admission
            // failure from a structural violation.
            CHECK(diagnostic.code == "core.json.VERIFY_FAILED");
            if (diagnostic.message.find("core.verify.STATE_ID_OUT_OF_RANGE") != std::string::npos) {
                verify_code = true;
            }
        }
        CHECK(verify_code);
    }
}

TEST_CASE("the writer fails closed on a kInvalid required id: NO artifact is published") {
    // Every REQUIRED-valid id position (§2): a `kInvalid` there is a writer error,
    // and the contract is a SUPPRESSED document — not a truncated one and not one
    // carrying the `4294967295` sentinel. Each case corrupts one position and
    // asserts (a) `error` is set and (b) the caller's stream received ZERO bytes,
    // so a consumer that ignores `error` cannot read a malformed/wrong artifact.
    const auto base = lower_source("flow_branch", corpus().at("flow_branch"));
    REQUIRE(base.has_value());

    // `CoreProgram` owns `unique_ptr` regions and is not copyable, so the seed is
    // produced fresh per case by a factory and then corrupted in place.
    const auto require_program_suppressed = [&](const std::string &what, const auto &make_seed,
                                                auto &&corrupt) {
        CAPTURE(what);
        auto seed = make_seed();
        REQUIRE(seed.has_value());
        corrupt(*seed);
        std::ostringstream out;
        std::string error;
        ir::core::print_core_ir_json(*seed, out, &error);
        CHECK_FALSE(error.empty());
        INFO("error: " << error);
        CHECK(out.str().empty());
    };
    const auto require_suppressed = [&](const std::string &what, auto &&corrupt) {
        require_program_suppressed(
            what, [&]() { return lower_source("flow_branch", corpus().at("flow_branch")); },
            corrupt);
    };

    // Audited positions.
    require_suppressed("flow.agent (un-audited before this fix; emitted `\"agent\": ,`)",
                       [](auto &p) { p.flows[0].target = ir::core::CoreAgentId{}; });
    require_suppressed("agent.input",
                       [](auto &p) { p.agents[0].input_type = ir::core::CoreTypeId{}; });
    require_suppressed("capability.param_types (un-audited before this fix)", [](auto &p) {
        p.capabilities[0].param_types[0] = ir::core::CoreValueTypeId{};
    });
    require_suppressed("capability.return_type",
                       [](auto &p) { p.capabilities[0].return_type = ir::core::CoreValueTypeId{}; });

    // Audited positions covering the workflow / type-template tables. The
    // workflow-bearing source is the DAG fixture so workflow.id / node.target /
    // input / output are all reachable.
    const auto require_wf_suppressed = [&](const std::string &what, auto &&corrupt) {
        require_program_suppressed(
            what, [&]() { return lower_source("workflow_pipe", corpus().at("workflow_pipe")); },
            corrupt);
    };
    require_wf_suppressed("workflow.id",
                          [](auto &p) { p.workflows[0].id = ir::core::CoreWorkflowId{}; });
    require_wf_suppressed("workflow.input",
                          [](auto &p) { p.workflows[0].input_type = ir::core::CoreTypeId{}; });
    require_wf_suppressed("workflow.output",
                          [](auto &p) { p.workflows[0].output_type = ir::core::CoreTypeId{}; });
    require_wf_suppressed("workflow_node.target_instance", [](auto &p) {
        p.workflows[0].nodes[0].target_instance = ir::core::CoreInstanceId{};
    });

    // A member-type-template root left kInvalid must also suppress the document.
    // The real-frontend sysroot fixture carries a generic struct member template
    // (`Ctx::slot : Option<Bool>`), so its `field_type_template_roots` is
    // non-empty — the un-audited-before-this-fix position the brief names.
    const auto opt = lower_sysroot_source("writer_template_root", source_text());
    REQUIRE(opt.has_value());
    std::size_t template_root_type = opt->types.size();
    for (std::size_t i = 0; i < opt->types.size(); ++i) {
        if (!opt->types[i].field_type_template_roots.empty()) {
            template_root_type = i;
            break;
        }
    }
    REQUIRE(template_root_type != opt->types.size());
    require_program_suppressed(
        "type.field_type_template_roots (un-audited before this fix)",
        [&]() { return lower_sysroot_source("writer_template_root", source_text()); },
        [&](auto &p) {
            p.types[template_root_type].field_type_template_roots[0] =
                ir::core::CoreMemberTypeTemplateNodeId{};
        });

    // A clean program still emits a parseable artifact (the suppression is not a
    // blanket "never publish").
    std::ostringstream clean_out;
    ir::core::print_core_ir_json(*base, clean_out);
    CHECK_FALSE(clean_out.str().empty());
    CHECK(ir::core::parse_core_ir_json(clean_out.str()).ok());
}

// ---------------------------------------------------------------------------
// R1/R2 over the NUMERIC domain, which the fixture corpus never reaches: every
// `id` / `capacity` / `bounds` / `scale` in the corpus is tiny, so the shared
// parser's SignedInteger / UnsignedInteger classification of a large magnitude is
// never exercised. The projection emits indices as bare 64-bit-capable integers,
// so a change to that classification (or to `write_u64` / `req_u64`) could break
// R1 for a large capacity/bound with no test noticing. This locks INT64_MIN /
// INT64_MAX bounds, a UINT64_MAX capacity, and an INT64_MIN decimal scale.
// ---------------------------------------------------------------------------
TEST_CASE("Core-IR JSON round-trips the full 64-bit numeric domain (R1/R2)") {
    // A minimal program carrying one value type per wide-numeric position, built
    // by hand since no fixture lowers to these magnitudes. `value_types` must be
    // a canonical postordered hash-cons, so the wide leaves come first. The base
    // of the wide-capacity nominal is a synthesized `std::collections::List`
    // declared type, which the verifier checks against the builtin descriptor.
    ir::core::CoreProgram program;
    program.value_types.push_back(ir::core::CoreValueType{
        ir::core::CoreVtInt{std::pair<std::int64_t, std::int64_t>{INT64_MIN, INT64_MAX}}});
    program.value_types.push_back(ir::core::CoreValueType{ir::core::CoreVtDecimal{INT64_MIN}});

    ir::core::CoreTypeDecl list;
    list.kind = ir::core::CoreTypeDecl::Kind::Struct;
    list.name = "std::collections::List";
    list.type_param_count = 1;
    list.variances = {ir::core::CoreVariance::Covariant};
    list.role = ir::core::CoreNominalRole::List;
    list.symbol_ref.kind = ahfl::ir::SymbolRefKind::Type;
    list.symbol_ref.canonical_name = "std::collections::List";
    program.types.push_back(std::move(list));
    program.value_types.push_back(ir::core::CoreValueType{
        ir::core::CoreVtNominal{ir::core::CoreTypeId{0}, {ir::core::CoreValueTypeId{0}}, UINT64_MAX}});

    const std::string first = print(program);
    REQUIRE_FALSE(first.empty());
    // The wide values really did serialize as bare 64-bit-capable integers.
    CHECK(first.find(std::to_string(UINT64_MAX)) != std::string::npos);
    CHECK(first.find(std::to_string(INT64_MIN)) != std::string::npos);
    CHECK(first.find(std::to_string(INT64_MAX)) != std::string::npos);

    const auto parsed = ir::core::parse_core_ir_json(first);
    for (const auto &diagnostic : parsed.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " — " << diagnostic.message);
    }
    REQUIRE(parsed.ok());

    // R1 — byte-exact re-emit.
    CHECK(print(*parsed.program) == first);
    // R2 — structural identity, including the exact magnitude of every wide field.
    CHECK(ir::core::core_program_equal(program, *parsed.program));
    REQUIRE(parsed.program->value_types.size() == program.value_types.size());
    const auto &nominal =
        std::get<ir::core::CoreVtNominal>(parsed.program->value_types.back().node);
    REQUIRE(nominal.capacity.has_value());
    CHECK(*nominal.capacity == UINT64_MAX);
    const auto &decimal = std::get<ir::core::CoreVtDecimal>(parsed.program->value_types[1].node);
    CHECK(decimal.scale == INT64_MIN);
    const auto &int_leaf = std::get<ir::core::CoreVtInt>(parsed.program->value_types[0].node);
    REQUIRE(int_leaf.bounds.has_value());
    CHECK(int_leaf.bounds->first == INT64_MIN);
    CHECK(int_leaf.bounds->second == INT64_MAX);
}

// RFC 0026 FB-3a1: the two new closure expr nodes (§3.1 CoreClosureExpr, §5.2
// CoreCallClosureExpr) and the CoreFnDecl declared `captures` signature must be
// wire-symmetric (R1 byte-exact, R2 structural identity) through the same
// kind-name SSOT as every other node. The fixture is a verifier-clean
// lambda-lifted shape built by hand (lifting itself arrives in FB-3a2).
TEST_CASE("Core-IR JSON round-trips CoreClosureExpr / CoreCallClosureExpr and fn captures") {
    using namespace ir::core;
    CoreProgram program;
    program.value_types.push_back(CoreValueType{CoreVtInt{}});                 // 0 Int
    program.value_types.push_back(CoreValueType{CoreVtFn{{CoreValueTypeId{0}}, // 1 Fn(Int)->Int
                                                        CoreValueTypeId{0}}});
    program.value_types.push_back(CoreValueType{CoreVtClosure{ // 2 Closure{sig1,[Int]}
        CoreValueTypeId{1},
        {CoreClosureCapture{CoreValueTypeId{0}, CoreCaptureMode::ByValue}}}});
    const auto vt_int = CoreValueTypeId{0};
    const auto vt_closure = CoreValueTypeId{2};

    // Instance / fn 0: the lifted body g0(Int) -> Int with one declared Int env
    // capture slot.
    CoreInstanceDecl inst0;
    inst0.id = CoreInstanceId{0};
    inst0.instance_key = "_inst_g0";
    inst0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    inst0.payload = CoreFnInstance{CoreFnId{0}};
    program.instances.push_back(std::move(inst0));

    CoreFnDecl g0;
    g0.id = CoreFnId{0};
    g0.instance = CoreInstanceId{0};
    g0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    g0.params = {CoreValueId{0}};
    g0.captures = {vt_int};
    g0.env_bindings = {CoreValueId{1}};
    g0.name = "_inst_g0";
    g0.storage.value_count = 2;
    g0.storage.value_types = {vt_int, vt_int};
    g0.storage.exprs.push_back(CoreExpr{CoreValueRefExpr{CoreValueId{0}}, std::nullopt, vt_int});
    g0.body.statements.push_back(CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(g0));

    // Instance / fn 1: the constructing body mk. v0 = Int param, v1 = closure.
    CoreInstanceDecl inst1;
    inst1.id = CoreInstanceId{1};
    inst1.instance_key = "_inst_mk";
    inst1.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "mk", "mk", "", 701};
    inst1.payload = CoreFnInstance{CoreFnId{1}};
    program.instances.push_back(std::move(inst1));

    CoreFnDecl mk;
    mk.id = CoreFnId{1};
    mk.instance = CoreInstanceId{1};
    mk.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "mk", "mk", "", 701};
    mk.params = {CoreValueId{0}};
    mk.name = "_inst_mk";
    mk.storage.value_count = 2;
    mk.storage.value_types = {vt_int, vt_closure};
    mk.storage.exprs.push_back(
        CoreExpr{CoreClosureExpr{CoreFnId{0}, {CoreValueId{0}}}, std::nullopt, vt_closure});
    mk.storage.exprs.push_back(CoreExpr{
        CoreCallClosureExpr{CoreValueId{1}, {CoreValueId{0}}}, std::nullopt, vt_int});
    mk.body.statements.push_back(CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(mk));

    // The seed is verifier-clean (the reader's final gate would reject otherwise).
    REQUIRE(verify_core_program(program).ok());

    const std::string first = print(program);
    REQUIRE_FALSE(first.empty());
    // The wire kind names resolve from core_expr_nodes.def (single SSOT).
    CHECK(first.find("\"kind\": \"closure\"") != std::string::npos);
    CHECK(first.find("\"kind\": \"call_closure\"") != std::string::npos);
    // The declared fn capture signature is serialized (and remapped on read).
    CHECK(first.find("\"captures\": [") != std::string::npos);

    const auto parsed = ir::core::parse_core_ir_json(first);
    for (const auto &diagnostic : parsed.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " - " << diagnostic.message);
    }
    REQUIRE(parsed.ok());
    CHECK(parsed.diagnostics.empty());

    // R1 byte-exact and R2 structural identity (now including the fns table).
    CHECK(print(*parsed.program) == first);
    CHECK(core_program_equal(program, *parsed.program));

    // The two nodes survive with their exact fn id / operand lists.
    const auto &mk_ex = parsed.program->fns[1].storage.exprs;
    REQUIRE(mk_ex.size() == 2);
    const auto *closure = std::get_if<CoreClosureExpr>(&mk_ex[0].node);
    REQUIRE(closure != nullptr);
    CHECK(closure->fn == CoreFnId{0});
    REQUIRE(closure->env.size() == 1);
    CHECK(closure->env[0] == CoreValueId{0});
    const auto *call = std::get_if<CoreCallClosureExpr>(&mk_ex[1].node);
    REQUIRE(call != nullptr);
    CHECK(call->callee == CoreValueId{1});
    REQUIRE(call->args.size() == 1);
    CHECK(call->args[0] == CoreValueId{0});
    // The lifted fn's declared capture signature round-trips.
    REQUIRE(parsed.program->fns[0].captures.size() == 1);
    CHECK(parsed.program->fns[0].captures[0] == vt_int);
}

// The closure-node reader enforces its field mask: an unknown key on a closure
// expr is rejected rather than silently dropped.
TEST_CASE("Core-IR JSON reader rejects an unknown field on a closure expression") {
    using namespace ir::core;
    CoreProgram program;
    program.value_types.push_back(CoreValueType{CoreVtInt{}});
    program.value_types.push_back(CoreValueType{CoreVtFn{{CoreValueTypeId{0}},
                                                        CoreValueTypeId{0}}});
    program.value_types.push_back(CoreValueType{CoreVtClosure{
        CoreValueTypeId{1},
        {CoreClosureCapture{CoreValueTypeId{0}, CoreCaptureMode::ByValue}}}});

    CoreInstanceDecl inst;
    inst.id = CoreInstanceId{0};
    inst.instance_key = "_inst_g0";
    inst.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    inst.payload = CoreFnInstance{CoreFnId{0}};
    program.instances.push_back(std::move(inst));

    CoreFnDecl g0;
    g0.id = CoreFnId{0};
    g0.instance = CoreInstanceId{0};
    g0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    g0.params = {CoreValueId{0}};
    g0.captures = {CoreValueTypeId{0}};
    g0.env_bindings = {CoreValueId{1}};
    g0.name = "_inst_g0";
    g0.storage.value_count = 2;
    g0.storage.value_types = {CoreValueTypeId{0}, CoreValueTypeId{0}};
    g0.storage.exprs.push_back(
        CoreExpr{CoreClosureExpr{CoreFnId{0}, {CoreValueId{0}}}, std::nullopt,
                CoreValueTypeId{2}});
    g0.body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(g0));

    std::string doc = print(program);
    REQUIRE_FALSE(doc.empty());
    // Insert a stray field at the start of the closure EXPR object. Its unique
    // `"fn"` member distinguishes it from the value-type closure entry (which
    // also spells its kind "closure" but carries "signature"); the reader's
    // per-object field mask must reject the stray key rather than drop it.
    const std::string anchor = "\"fn\": 0,";
    const auto fn_pos = doc.find(anchor);
    REQUIRE(fn_pos != std::string::npos);
    const auto line_start = doc.rfind('\n', fn_pos) + 1;
    const std::string indent = doc.substr(line_start, fn_pos - line_start);
    doc.insert(line_start, indent + "\"bogus\": 1,\n");
    require_rejected_with(doc, "core.json.UNKNOWN_FIELD");
}

namespace {

// Build the single-fn closure fixture the unknown-field test uses, print it,
// and replace the closure CONSTRUCTION expr's own "result_type" number with
// `tampered`. The construction expr is the only expr carrying a "fn" member,
// and the printer emits "result_type" immediately before that member inside the
// same object, so a backwards rfind locates exactly the right field.
[[nodiscard]] std::string tampered_closure_result_type_doc(std::uint32_t tampered) {
    using namespace ir::core;
    CoreProgram program;
    program.value_types.push_back(CoreValueType{CoreVtInt{}});
    program.value_types.push_back(
        CoreValueType{CoreVtFn{{CoreValueTypeId{0}}, CoreValueTypeId{0}}});
    program.value_types.push_back(CoreValueType{CoreVtClosure{
        CoreValueTypeId{1}, {CoreClosureCapture{CoreValueTypeId{0}, CoreCaptureMode::ByValue}}}});

    CoreInstanceDecl inst;
    inst.id = CoreInstanceId{0};
    inst.instance_key = "_inst_g0";
    inst.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    inst.payload = CoreFnInstance{CoreFnId{0}};
    program.instances.push_back(std::move(inst));

    CoreFnDecl g0;
    g0.id = CoreFnId{0};
    g0.instance = CoreInstanceId{0};
    g0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    g0.params = {CoreValueId{0}};
    g0.captures = {CoreValueTypeId{0}};
    g0.env_bindings = {CoreValueId{1}};
    g0.name = "_inst_g0";
    g0.storage.value_count = 2;
    g0.storage.value_types = {CoreValueTypeId{0}, CoreValueTypeId{0}};
    g0.storage.exprs.push_back(
        CoreExpr{CoreClosureExpr{CoreFnId{0}, {CoreValueId{0}}}, std::nullopt, CoreValueTypeId{2}});
    g0.body.statements.push_back(CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(g0));

    std::string doc = print(program);
    REQUIRE_FALSE(doc.empty());

    const std::string fn_anchor = "\"fn\": 0,";
    const auto fn_pos = doc.find(fn_anchor);
    REQUIRE(fn_pos != std::string::npos);
    const std::string rt_key = "\"result_type\": ";
    const auto rt_pos = doc.rfind(rt_key, fn_pos);
    REQUIRE(rt_pos != std::string::npos);
    const auto num_begin = rt_pos + rt_key.size();
    const auto num_end = doc.find(',', num_begin);
    REQUIRE(num_end != std::string::npos);
    doc.replace(num_begin, num_end - num_begin, std::to_string(tampered));
    return doc;
}

} // namespace

// FB-3a1 fix-forward P0 (wire path): the JSON reader maps the 4294967295
// sentinel to a kInvalid CoreValueTypeId (map_value_type_id), so a tampered
// closure expr "result_type" reaches the final verify_core_program gate. Before
// the fix that dereferenced program_.value_types[kInvalid] and SEGVed inside
// parse_core_ir_json; it must now be a typed core.json.VERIFY_FAILED rejection
// naming CLOSURE_RESULT_TYPE_INVALID instead of a crash.
TEST_CASE("Core-IR JSON reader rejects a kInvalid closure result_type without crashing") {
    const std::string doc = tampered_closure_result_type_doc(ir::core::CoreValueTypeId::kInvalid);
    const auto result = ir::core::parse_core_ir_json(doc);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    bool verify_code = false;
    for (const auto &diagnostic : result.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " - " << diagnostic.message);
        CHECK(diagnostic.code == "core.json.VERIFY_FAILED");
        if (diagnostic.message.find("core.verify.CLOSURE_RESULT_TYPE_INVALID") !=
            std::string::npos) {
            verify_code = true;
        }
    }
    CHECK(verify_code);
}

// A numerically out-of-range (non-sentinel) closure result_type is caught one
// layer earlier, by the reader's value-type remap, as core.json.OUT_OF_RANGE —
// it must likewise be a clean rejection, never a crash.
TEST_CASE("Core-IR JSON reader rejects an out-of-range numeric closure result_type") {
    const std::string doc = tampered_closure_result_type_doc(9999);
    require_rejected_with(doc, "core.json.OUT_OF_RANGE");
}

// FB-3a2 fix-forward P2: 'captures' and 'env_bindings' were added to the fn
// object WITHOUT bumping the pre-stabilization 'ahfl.core.v1' format. An older
// v1 producer therefore emits fn objects with neither field; the reader must
// default both to empty (an ordinary fn declares no env slots) and accept the
// doc rather than failing the required-field check.
TEST_CASE("Core-IR JSON reader accepts a v1 fn object without captures or env_bindings") {
    using namespace ir::core;
    CoreProgram program;
    program.value_types.push_back(CoreValueType{CoreVtInt{}});

    CoreInstanceDecl inst;
    inst.id = CoreInstanceId{0};
    inst.instance_key = "_inst_g0";
    inst.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    inst.payload = CoreFnInstance{CoreFnId{0}};
    program.instances.push_back(std::move(inst));

    // An ordinary fn with no env capture signature at all.
    CoreFnDecl g0;
    g0.id = CoreFnId{0};
    g0.instance = CoreInstanceId{0};
    g0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    g0.params = {CoreValueId{0}};
    g0.name = "_inst_g0";
    g0.storage.value_count = 1;
    g0.storage.value_types = {CoreValueTypeId{0}};
    g0.storage.exprs.push_back(
        CoreExpr{CoreValueRefExpr{CoreValueId{0}}, std::nullopt, CoreValueTypeId{0}});
    g0.body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(g0));

    std::string doc = print(program);
    REQUIRE_FALSE(doc.empty());

    // Erase the two field lines exactly as an older v1 writer would omit them
    // (each printed line carries its own trailing comma).
    const auto erase_field_lines = [&doc](std::string_view key) {
        const std::string needle = "\"" + std::string(key) + "\"";
        for (auto pos = doc.find(needle); pos != std::string::npos;
             pos = doc.find(needle, pos)) {
            const auto line_begin = doc.rfind('\n', pos) + 1;
            const auto line_end = doc.find('\n', pos);
            REQUIRE(line_end != std::string::npos);
            doc.erase(line_begin, line_end - line_begin + 1);
            pos = line_begin;
        }
    };
    erase_field_lines("captures");
    erase_field_lines("env_bindings");
    CHECK(doc.find("\"captures\"") == std::string::npos);
    CHECK(doc.find("\"env_bindings\"") == std::string::npos);

    const auto parsed = parse_core_ir_json(doc);
    for (const auto &diagnostic : parsed.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " - " << diagnostic.message);
        CHECK(false);
    }
    REQUIRE(parsed.ok());
    REQUIRE(parsed.program->fns.size() == 1);
    CHECK(parsed.program->fns[0].captures.empty());
    CHECK(parsed.program->fns[0].env_bindings.empty());
}

// The pairing invariant stays fail-closed: a fn object that carries
// env_bindings WITHOUT the parallel captures (a partial / tampered doc) cannot
// satisfy env_bindings.size()==captures.size() and is rejected by the verifier
// gate, instead of silently binding phantom env slots.
TEST_CASE("Core-IR JSON reader rejects a fn object pairing env_bindings without captures") {
    using namespace ir::core;
    CoreProgram program;
    program.value_types.push_back(CoreValueType{CoreVtInt{}});

    CoreInstanceDecl inst;
    inst.id = CoreInstanceId{0};
    inst.instance_key = "_inst_g0";
    inst.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    inst.payload = CoreFnInstance{CoreFnId{0}};
    program.instances.push_back(std::move(inst));

    CoreFnDecl g0;
    g0.id = CoreFnId{0};
    g0.instance = CoreInstanceId{0};
    g0.origin = ir::SymbolRef{ir::SymbolRefKind::Function, "g0", "g0", "", 700};
    g0.params = {CoreValueId{0}};
    g0.captures = {CoreValueTypeId{0}};
    g0.env_bindings = {CoreValueId{1}};
    g0.name = "_inst_g0";
    g0.storage.value_count = 2;
    g0.storage.value_types = {CoreValueTypeId{0}, CoreValueTypeId{0}};
    g0.storage.exprs.push_back(
        CoreExpr{CoreValueRefExpr{CoreValueId{0}}, std::nullopt, CoreValueTypeId{0}});
    g0.body.statements.push_back(
        CoreStmt{CoreReturnStmt{true, CoreValueId{0}}, std::nullopt});
    program.fns.push_back(std::move(g0));

    std::string doc = print(program);
    REQUIRE_FALSE(doc.empty());

    // Print the doc to find the span: the fn-declared captures array prints as
    // `"captures": [\n  0\n],` (one id per line). Remove from the key's line
    // start through the line of the array's closing "]," so the remaining JSON
    // stays well-formed, leaving env_bindings behind unpaired.
    {
        const std::string needle = "\"captures\": [";
        const auto key_pos = doc.find(needle);
        REQUIRE(key_pos != std::string::npos);
        const auto line_begin = doc.rfind('\n', key_pos) + 1;
        auto close_pos = doc.find(']', key_pos);
        REQUIRE(close_pos != std::string::npos);
        const auto line_end = doc.find('\n', close_pos);
        REQUIRE(line_end != std::string::npos);
        doc.erase(line_begin, line_end - line_begin + 1);
    }
    REQUIRE(doc.find("\"captures\"") == std::string::npos);

    // Parsing succeeds structurally (both fields default independently), but the
    // reader's final verifier gate rejects the 0-vs-1 pairing.
    const auto parsed = parse_core_ir_json(doc);
    for (const auto &diagnostic : parsed.diagnostics) {
        INFO("diagnostic: " << diagnostic.code << " - " << diagnostic.message);
    }
    CHECK_FALSE(parsed.ok());
    bool saw_pairing_gate = false;
    for (const auto &diagnostic : parsed.diagnostics) {
        // The reader wraps post-parse verification failures as
        // core.json.VERIFY_FAILED with the inner verifier code in the message.
        if (diagnostic.code == "core.json.VERIFY_FAILED" &&
            diagnostic.message.find("core.verify.CLOSURE_CAPTURE_ARITY") !=
                std::string::npos &&
            diagnostic.message.find("pre-bound env slot") != std::string::npos) {
            saw_pairing_gate = true;
        }
    }
    CHECK(saw_pairing_gate);
}
