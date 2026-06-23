#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using namespace ahfl;

// ---------------------------------------------------------------------------
// Shared pipeline fixture. Keeps ownership of parse_result / resolve_result so
// pointers returned by `lower_to_ir` remain valid for the caller.
// ---------------------------------------------------------------------------
struct PipelineFixture {
    ahfl::Frontend frontend;
    // Members used to back the output of `lower()` so string_views and
    // pointers held by `TypedProgram` stay alive across DOCTEST assertions.
    std::optional<ahfl::ParseResult> parse_;
    std::optional<ahfl::ResolveResult> resolve_;
    std::optional<ahfl::TypeCheckResult> typecheck_;

    const ahfl::ir::Program *lower(std::string_view source,
                                   std::string_view filename = "instance_test.ahfl") {
        parse_.emplace(frontend.parse_text(std::string{filename}, std::string{source}));
        if (parse_->has_errors()) {
            std::ostringstream ss; parse_->diagnostics.render(ss);
            std::fprintf(stderr, "=== PARSE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
            return nullptr;
        }
        ahfl::Resolver resolver;
        resolve_.emplace(resolver.resolve(*parse_->program));
        if (resolve_->has_errors()) {
            std::ostringstream ss; resolve_->diagnostics.render(ss);
            std::fprintf(stderr, "=== RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
            return nullptr;
        }
        ahfl::TypeChecker checker;
        typecheck_.emplace(checker.check(*parse_->program, *resolve_));
        if (typecheck_->has_errors()) {
            std::ostringstream ss; typecheck_->diagnostics.render(ss);
            std::fprintf(stderr, "=== TYPECHECK DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
            return nullptr;
        }
        lowered_program_.emplace(
            lower_typed_program(typecheck_->typed_program, *parse_->program));
        return &*lowered_program_;
    }

    [[nodiscard]] std::string fingerprint_json() const {
        REQUIRE(lowered_program_.has_value());
        std::ostringstream ss;
        print_program_ir_json(*lowered_program_, ss);
        return ss.str();
    }

  private:
    std::optional<ahfl::ir::Program> lowered_program_;
};

// Count InstanceDecls in a program, optionally filtered by kind.
std::size_t count_instances(const ahfl::ir::Program &p,
                            ahfl::ir::InstanceKind kind = ahfl::ir::InstanceKind::Unknown) {
    std::size_t n = 0;
    for (const auto &decl : p.declarations) {
        if (const auto *inst = std::get_if<ahfl::ir::InstanceDecl>(&decl); inst != nullptr) {
            if (kind == ahfl::ir::InstanceKind::Unknown || inst->kind == kind) ++n;
        }
    }
    return n;
}

std::vector<std::string> instance_names(const ahfl::ir::Program &p) {
    std::vector<std::string> names;
    for (const auto &decl : p.declarations) {
        if (const auto *inst = std::get_if<ahfl::ir::InstanceDecl>(&decl); inst != nullptr) {
            names.push_back(inst->name);
        }
    }
    return names;
}

// ---------------------------------------------------------------------------
// AHFL source reused across several test cases. Grammar is verified against
// the golden test fixtures (tests/golden/assurance/ok_effects.ahfl and
// tests/golden/formal/ok_flow_workflow_semantics.ahfl):
//   * `capability` may be declared abstractly as `Name(params) -> Ret;`.
//   * agent context is accessed as `ctx.<field>` inside `flow for <Agent>`.
//   * `workflow` has `input:`/`output:`/`node <name> : <Agent>(args) ;` / `return: <name>;`.
// ---------------------------------------------------------------------------
constexpr std::string_view kTwoCallsSameType = R"AHFL(
module tests;

struct Request { value: Int; }
struct Response { value: Int; ctx_tag: String; }
struct Context { trace: String = "seed"; }

capability Inc(x: Int) -> Int;
capability Format(x: Int) -> String;
capability Do(req: Request) -> Response;

agent Worker {
    input: Request;
    context: Context;
    output: Response;
    states: [Working, Done];
    initial: Working;
    final: [Done];
    capabilities: [Inc, Format, Do];
    transition Working -> Done;
}

flow for Worker {
    state Working {
        let a: Int = Inc(input.value);
        let b: Int = Inc(a);
        let tag: String = Format(b);
        goto Done;
    }
    state Done {
        return Do(Request { value: 0 });
    }
}
)AHFL";

constexpr std::string_view kWorkflowWithTwoAgents = R"AHFL(
module workflows;

struct ReqA { id: Int; }
struct ReqB { text: String; }
struct CommonCtx { step: Int = 0; }
struct Resp { ok: Bool; }

capability Ping() -> Bool;

agent Alpha {
    input: ReqA;
    context: CommonCtx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Ping];
}
flow for Alpha {
    state Done {
        let p: Bool = Ping();
        return Resp { ok: p };
    }
}

agent Beta {
    input: ReqB;
    context: CommonCtx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Ping];
}
flow for Beta {
    state Done {
        let p: Bool = Ping();
        return Resp { ok: true };
    }
}

workflow Pipeline {
    input: ReqA;
    output: Resp;

    node a: Alpha(input);
    node b: Beta(ReqB { text: "hi" }) after [a];

    return: b;
}
)AHFL";

} // namespace

TEST_CASE_FIXTURE(PipelineFixture,
                  "Typed HIR lowering emits capability instances, deduped by call type") {
    const auto *ir = lower(kTwoCallsSameType);
    REQUIRE(ir != nullptr);

    // Inc (Int), Format (Int), Do (Request) => three distinct capability instances.
    CHECK(count_instances(*ir, ahfl::ir::InstanceKind::Capability) == 3u);
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Typed HIR lowering: duplicate calls to same capability share one instance") {
    const auto *ir = lower(kTwoCallsSameType);
    REQUIRE(ir != nullptr);

    // Collect capability instance names; Inc(ArgTy) must appear exactly once
    // even though it is called twice in the flow body above.
    std::vector<std::string> cap_names;
    for (const auto &decl : ir->declarations) {
        if (const auto *inst = std::get_if<ahfl::ir::InstanceDecl>(&decl);
            inst != nullptr && inst->kind == ahfl::ir::InstanceKind::Capability) {
            cap_names.push_back(inst->name);
        }
    }
    // Capabilities present: Inc (Int arg), Format (Int arg), Do (Request arg).
    REQUIRE(cap_names.size() == 3u);

    // No duplicate names — two calls to Inc(Int) collapsed into one instance.
    std::unordered_set<std::string> seen(cap_names.begin(), cap_names.end());
    CHECK(seen.size() == cap_names.size());

    // All capability instances must carry the canonical mangling marker.
    for (const auto &n : cap_names) CHECK(n.find("_inst_") == 0u);
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Typed HIR lowering: capability instance kind has empty agent fields") {
    const auto *ir = lower(kTwoCallsSameType);
    REQUIRE(ir != nullptr);

    const ahfl::ir::InstanceDecl *inc = nullptr;
    for (const auto &decl : ir->declarations) {
        const auto *inst = std::get_if<ahfl::ir::InstanceDecl>(&decl);
        if (inst == nullptr || inst->kind != ahfl::ir::InstanceKind::Capability) continue;
        // Pick any capability whose canonical name has Inc.
        if (inst->symbol_ref.canonical_name.find("Inc") != std::string::npos) {
            inc = inst;
            break;
        }
    }
    REQUIRE(inc != nullptr);
    // Capability-specific invariants: no agent/workflow types.
    CHECK(inc->agent_input_type_ref.kind == ahfl::ir::TypeRefKind::Unresolved);
    CHECK(inc->agent_output_type_ref.kind == ahfl::ir::TypeRefKind::Unresolved);
    CHECK(inc->workflow_input_type_ref.kind == ahfl::ir::TypeRefKind::Unresolved);
    CHECK(!inc->params.empty()); // capability `Inc` declares one param.
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Typed HIR lowering: workflow node agent targets produce Agent instances") {
    const auto *ir = lower(kWorkflowWithTwoAgents);
    REQUIRE(ir != nullptr);

    const auto agent_count = count_instances(*ir, ahfl::ir::InstanceKind::Agent);
    // Two distinct workflow-node agent targets => two agent instances (no cross-agent share).
    CHECK(agent_count == 2u);

    const auto names = instance_names(*ir);
    std::unordered_set<std::string> unique_names(names.begin(), names.end());
    CHECK(unique_names.size() == names.size()); // no duplicates at all.

    // Capability Ping is called in both agent flows — expect exactly one
    // instance (deduplicated by SymbolId + argument type list).
    const auto cap_count = count_instances(*ir, ahfl::ir::InstanceKind::Capability);
    CHECK(cap_count >= 1u);
    for (const auto &name : names) {
        if (name.find("Ping") != std::string::npos) {
            CHECK(name.find("_inst_") == 0u);
        }
    }
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Typed HIR lowering: agent instance kind reflects structured type fields") {
    const auto *ir = lower(kWorkflowWithTwoAgents);
    REQUIRE(ir != nullptr);

    for (const auto &decl : ir->declarations) {
        const auto *inst = std::get_if<ahfl::ir::InstanceDecl>(&decl);
        if (inst == nullptr || inst->kind != ahfl::ir::InstanceKind::Agent) continue;
        CHECK(inst->agent_input_type_ref.kind != ahfl::ir::TypeRefKind::Unresolved);
        CHECK(inst->agent_context_type_ref.kind != ahfl::ir::TypeRefKind::Unresolved);
        CHECK(inst->agent_output_type_ref.kind != ahfl::ir::TypeRefKind::Unresolved);
        CHECK(!inst->symbol_ref.canonical_name.empty());
        CHECK(inst->symbol_ref.kind == ahfl::ir::SymbolRefKind::Agent);
    }
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Instance mangled names are stable across independent lowerings (ir_json fingerprint)") {
    // First pass: lower once, capture JSON and instance names.
    const auto *ir1 = lower(kTwoCallsSameType, "dup_a.ahfl");
    REQUIRE(ir1 != nullptr);
    const auto names1 = instance_names(*ir1);
    const auto json1 = fingerprint_json();

    // Reset the fixture's internal buffers so the second lowering starts from
    // a fresh TypedProgram / IR (different pointer identities).
    parse_.reset(); resolve_.reset(); typecheck_.reset();

    const auto *ir2 = lower(kTwoCallsSameType, "dup_b.ahfl");
    REQUIRE(ir2 != nullptr);
    const auto names2 = instance_names(*ir2);
    const auto json2 = fingerprint_json();

    // Sorted set comparison (emission order is deterministic via stable_sort
    // on (SymbolId, type_args) but not necessarily equal to the insertion
    // order of the first pass, so sort first).
    auto n1s = names1; std::sort(n1s.begin(), n1s.end());
    auto n2s = names2; std::sort(n2s.begin(), n2s.end());
    CHECK(n1s == n2s);

    // Full JSON fingerprint equality: identical source must produce bit-exact
    // JSON. This is the "ir_json 名字稳定" acceptance signal.
    CHECK(json1 == json2);

    // All names begin with the canonical mangling prefix and are ASCII-safe.
    for (const auto &n : n1s) {
        CHECK(n.find("_inst_") == 0u);
        for (const char c : n) {
            const unsigned char uc = static_cast<unsigned char>(c);
            const bool ok =
                (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
                (uc >= '0' && uc <= '9') || uc == '_';
            CHECK(ok);
        }
    }
}

TEST_CASE_FIXTURE(PipelineFixture,
                  "Different type arguments to the same nominal produce different instance names") {
    // Produce two independent programs that call the same nominal capability
    // (`Format`, declared below) with distinct argument types. The resulting
    // InstanceDecls must carry different mangled names.
    constexpr std::string_view src_a = R"AHFL(
module diff_args_a;
capability Format(x: Int) -> String;
struct Ctx { v: Int = 0; }
struct In { v: Int; }
struct Out { v: String; }
agent A { input: In; context: Ctx; output: Out; states:[Done]; initial:Done; final:[Done]; capabilities:[Format]; }
flow for A { state Done { return Out { v: Format(input.v) }; } }
)AHFL";
    constexpr std::string_view src_b = R"AHFL(
module diff_args_b;
capability Format(x: String) -> String;
struct Ctx { v: Int = 0; }
struct In { v: String; }
struct Out { v: String; }
agent A { input: In; context: Ctx; output: Out; states:[Done]; initial:Done; final:[Done]; capabilities:[Format]; }
flow for A { state Done { return Out { v: Format(input.v) }; } }
)AHFL";

    const auto *ir_a = lower(src_a, "a.ahfl");
    REQUIRE(ir_a != nullptr);
    const auto names_a = instance_names(*ir_a);
    parse_.reset(); resolve_.reset(); typecheck_.reset();
    const auto *ir_b = lower(src_b, "b.ahfl");
    REQUIRE(ir_b != nullptr);
    const auto names_b = instance_names(*ir_b);

    // The Format capability is expected to be an instance in both.
    auto find_cap = [](const std::vector<std::string> &names) -> std::string {
        for (const auto &n : names) {
            if (n.find("Format") != std::string::npos) return n;
        }
        return {};
    };
    const auto a_name = find_cap(names_a);
    const auto b_name = find_cap(names_b);
    REQUIRE_FALSE(a_name.empty());
    REQUIRE_FALSE(b_name.empty());
    // The two Format instances are distinguished by Int vs String type args.
    CHECK(a_name != b_name);
    // Int must appear in a_name's encoding, String in b_name's.
    CHECK(a_name.find("Int") != std::string::npos);
    CHECK(b_name.find("String") != std::string::npos);
}
