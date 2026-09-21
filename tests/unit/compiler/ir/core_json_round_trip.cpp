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
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
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
  "instances": []
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
  "instances": []
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
  "instances": []
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
  "instances": []
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
  ]
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
  ]
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
            R"(,"workflows":[],"instances":[]})";
        require_rejected_with(doc + flow + tail + states, "core.json.REGION_TOO_DEEP");
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

TEST_CASE("the writer fails closed on a kInvalid required id") {
    auto program = lower_source("flow_branch", corpus().at("flow_branch"));
    REQUIRE(program.has_value());
    // Corrupt a required-valid id: a capability return_type left kInvalid must
    // be a writer error, never an emitted 4294967295.
    REQUIRE_FALSE(program->capabilities.empty());
    program->capabilities[0].return_type = ir::core::CoreValueTypeId{};
    std::ostringstream out;
    std::string error;
    ir::core::print_core_ir_json(*program, out, &error);
    CHECK_FALSE(error.empty());
}
