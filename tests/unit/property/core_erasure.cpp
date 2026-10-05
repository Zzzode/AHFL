// KR6.4 tail: DIFFERENTIAL erasure property at the AHFL-IR -> Core-IR boundary.
//
// RFC 0026's key tower invariant is "a construct only appears in the layer it
// belongs to": contracts, workflow safety/liveness, fn decreases measures,
// agent quotas, and the non-kind fields of a capability effect spec are
// VERIFICATION / orchestration concerns consumed above Core-IR and must not
// influence ANY execution-layer artifact.
//
// This test pins that invariant differentially rather than by asserting the
// absence of fields:
//
//   1. lower a curated, populated source P through the REAL front end
//      (parse -> resolve -> typecheck -> AhflIr -> Core);
//   2. independently lower the SAME source a second time (the HIR->IR lowering
//      is deterministic, established by the lowering-equivalence property P1)
//      and strip every verification construct at the AHFL-IR layer;
//   3. lower the stripped P' to Core and assert per-component structural
//      equality via the existing operator==. There is deliberately no
//      whole-program operator== (CoreInstanceDecl equality is same-owner-arena
//      by contract), so agents / flows / workflows / types / value_types /
//      capabilities / instances are compared component-wise.
//
// The corpus is curated (not randomly generated): every program is guaranteed
// to typecheck and to be populated with the constructs the property strips, so
// a regression that leaks a construct into Core changes an artifact and is
// caught here rather than passing vacuously.

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace {

using namespace ahfl;

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

// Run the real single-file front end on a source string. Returns nullopt (with
// a printed note) if any stage errors — a curated corpus regression.
[[nodiscard]] std::optional<ir::AhflIr> lower_to_ahfl_ir(const std::string &label,
                                                         const std::string &source) {
    const Frontend frontend;
    auto parse = frontend.parse_text(label + ".ahfl", source);
    if (parse.has_errors() || parse.program == nullptr) {
        std::printf("    (%s: parse failed)\n", label.c_str());
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        std::printf("    (%s: resolve failed)\n", label.c_str());
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        std::printf("    (%s: typecheck failed)\n", label.c_str());
        return std::nullopt;
    }
    return lower_program_ir(*parse.program, resolve, typecheck);
}

// Strip EVERY verification / orchestration construct at the AHFL-IR layer:
//   * ContractDecls disappear as whole declarations;
//   * workflow safety/liveness temporal formulas are cleared;
//   * fn effect-clause decreases measures are cleared;
//   * agent quota items are cleared;
//   * a capability effect spec is reduced to its effect CATEGORY (the single
//     field the execution layer is allowed to keep) — domain / idempotency
//     key / receipt / retry / timeout / compensation / policies are dropped.
void strip_verification_constructs(ir::AhflIr &program) {
    std::erase_if(program.declarations, [](const ir::Decl &decl) {
        return std::holds_alternative<ir::ContractDecl>(decl);
    });
    for (ir::Decl &decl : program.declarations) {
        std::visit(Overloaded{
                       [](ir::AgentDecl &agent) { agent.quota.clear(); },
                       [](ir::WorkflowDecl &workflow) {
                           workflow.safety.clear();
                           workflow.liveness.clear();
                       },
                       [](ir::FnDecl &fn) {
                           fn.effect.has_decreases = false;
                           fn.effect.decreases_terms.clear();
                       },
                       [](ir::CapabilityDecl &capability) {
                           const auto kind = capability.effect.kind;
                           capability.effect = ir::CapabilityEffectSpec{};
                           capability.effect.kind = kind;
                       },
                       [](const auto &) {},
                   },
                   decl);
    }
}

// Non-vacuity counters over the UNSTRIPPED AHFL-IR. The property is
// meaningless unless each construct it claims to strip was actually present.
struct VerificationPopulation {
    std::size_t contract_decls = 0;
    std::size_t contract_clauses = 0;
    std::size_t quota_items = 0;
    std::size_t workflow_safety = 0;
    std::size_t workflow_liveness = 0;
    std::size_t fn_decreases = 0;
    std::size_t capability_spec_annotations = 0;
};

[[nodiscard]] VerificationPopulation population_of(const ir::AhflIr &program) {
    VerificationPopulation pop;
    for (const ir::Decl &decl : program.declarations) {
        std::visit(Overloaded{
                       [&](const ir::ContractDecl &contract) {
                           ++pop.contract_decls;
                           pop.contract_clauses += contract.clauses.size();
                       },
                       [&](const ir::AgentDecl &agent) { pop.quota_items += agent.quota.size(); },
                       [&](const ir::WorkflowDecl &workflow) {
                           pop.workflow_safety += workflow.safety.size();
                           pop.workflow_liveness += workflow.liveness.size();
                       },
                       [&](const ir::FnDecl &fn) {
                           if (fn.effect.has_decreases) {
                               ++pop.fn_decreases;
                           }
                       },
                       [&](const ir::CapabilityDecl &capability) {
                           const auto &spec = capability.effect;
                           if (spec.domain.has_value() || spec.idempotency_key.has_value() ||
                               spec.receipt_mode != ir::CapabilityReceiptMode::None ||
                               spec.retry_mode != ir::CapabilityRetryMode::Unsafe ||
                               spec.timeout.has_value() || spec.compensation.has_value() ||
                               !spec.policies.empty()) {
                               ++pop.capability_spec_annotations;
                           }
                       },
                       [](const auto &) {},
                   },
                   decl);
    }
    return pop;
}

// Compare two lowered Core programs component-wise, recording one check per
// flat store. Every store is structurally equal (same-owner-arena equality
// within each independently-interned but deterministic arena).
void programs_component_equal(const ir::core::CoreProgram &full,
                              const ir::core::CoreProgram &stripped,
                              const std::string &label) {
    auto component = [&](bool component_equal, const std::string &name) {
        check(component_equal, label + ": " + name + " unchanged by verification erasure");
    };
    component(full.format_version == stripped.format_version, "format_version");
    component(full.types == stripped.types, "types");
    component(full.value_types == stripped.value_types, "value_types");
    component(full.capabilities == stripped.capabilities, "capabilities");
    component(full.agents == stripped.agents, "agents");
    component(full.flows == stripped.flows, "flows");
    component(full.workflows == stripped.workflows, "workflows");
    component(full.instances == stripped.instances, "instances");
}

// ---------------------------------------------------------------------------
// Curated corpus.
// ---------------------------------------------------------------------------

// A fully-populated orchestration program: two agents whose flows use
// capability calls, if/else mutual exclusion and a match, a contract carrying
// every clause kind (requires/ensures/invariant temporal atom/forbid/
// decreases self.length + wildcard), agent quotas, capabilities carrying full
// effect specs, and a multi-node DAG workflow with safety/liveness. Two
// uncalled Pure fns carry decreases measures (verification-only metadata).
const std::string kFullOrchestration = R"AHFL(
module diff;

struct Req { amount: Int; }
struct Mid { total: Int; }
struct Reply { ok: Bool; }
struct Ctx { length: Int = 0; charged: Bool = false; }

enum Verdict { Good(Int), Bad, }

fn length(x: Int) -> Int effect Pure;

fn measure(xs: Int) -> Int effect Pure decreases length(xs) {
    return xs;
}

capability Fetch(id: Int) -> Int {
    effect: read;
    domain: identity;
    receipt: none;
    retry: safe;
}

capability Charge(amount: Int) -> Bool {
    effect: financial_write;
    domain: payments;
    receipt: required;
    retry: safe_if_idempotent;
    timeout: 30s;
}

agent First {
    input: Req;
    context: Ctx;
    output: Mid;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Fetch, Charge];
    quota: {
        max_tool_calls: 5;
    }
    transition Init -> Done;
}

contract for First {
    requires: input.amount > 0;
    ensures: output.total >= 0;
    invariant: always called(Charge);
    forbid: always called(Fetch);
    decreases: self.length;
    decreases: *;
}

flow for First {
    state Init {
        let n: Int = Fetch(input.amount);
        let v: Verdict = Verdict::Good(n);
        let r: Int = match v {
            Good(x) => x,
            Bad => 0,
        };
        if r > 0 {
            let c: Bool = Charge(r);
            ctx.charged = c;
            goto Done;
        } else {
            goto Done;
        }
    }
    state Done {
        if ctx.charged {
            return Mid { total: 1 };
        } else {
            return Mid { total: 0 };
        }
    }
}

agent Second {
    input: Mid;
    output: Reply;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for Second {
    state Done {
        return Reply { ok: true };
    }
}

workflow Pipe {
    input: Req;
    output: Reply;

    node first: First(input);
    node second: Second(Mid { total: first.total }) after [first];

    safety: always (not running(second) or completed(second));
    liveness: eventually completed(second, Done);

    return: second;
}
)AHFL";

// A smaller agent + flow + workflow with a populated contract and a workflow
// property (shapes copied from the lowering-equivalence curated corpus).
const std::string kContractAgent = R"AHFL(
module ca;

struct Request { value: Int; }
struct Ctx { }
struct Reply { total: Int; }

predicate ready(value: Int) -> Bool;

agent Node {
    input: Request;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for Node {
    requires: input.value > 0;
    ensures: output.total == input.value;
    invariant: always ready(input.value);
    decreases: *;
}

flow for Node {
    state Init { goto Done; }
    state Done { return Reply { total: input.value }; }
}

workflow Pipe {
    input: Request;
    output: Reply;

    node run: Node(input);

    safety: always (not running(run) or (1 + 1 == 2));
    liveness: eventually completed(run, Done);

    return: run;
}
)AHFL";

// ---------------------------------------------------------------------------
// P2: edge-case corpus. Each program carries a verification construct in its
// MINIMAL populated form (zero-or-one clauses, partial spec, quota without
// contract). The differential property must hold for these just as for the
// fully-populated programs: stripping the construct at the AHFL-IR layer must
// not perturb a single Core-IR component.
// ---------------------------------------------------------------------------

// An agent with a quota but NO contract: the quota is verification-only
// metadata that must be erased without a ContractDecl present.
const std::string kQuotaOnlyAgent = R"AHFL(
module qa;

struct Request { value: Int; }
struct Ctx { }
struct Reply { ok: Bool; }

agent Worker {
    input: Request;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    quota: {
        max_tool_calls: 3;
    }
    transition Init -> Done;
}

flow for Worker {
    state Init { goto Done; }
    state Done { return Reply { ok: true }; }
}
)AHFL";

// A capability carrying only a domain annotation (no receipt/retry/timeout/
// compensation): the partial spec must leave only effect_kind on Core.
const std::string kDomainOnlyCapability = R"AHFL(
module dc;

struct Req { id: String; }
struct Receipt { id: String; }

capability ChargeCard(request: Req) -> Receipt {
    effect: financial_write;
    domain: payments;
}

agent Billing {
    input: Req;
    output: Receipt;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [ChargeCard];
}

flow for Billing {
    state Done {
        return ChargeCard(input);
    }
}
)AHFL";

// A workflow with only a safety property (no liveness): the single temporal
// formula must be erased without a liveness sibling present.
const std::string kSafetyOnlyWorkflow = R"AHFL(
module sw;

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

    safety: always (not running(second) or completed(second));

    return: second;
}
)AHFL";

// An empty contract (zero clauses): the ContractDecl itself must be erased
// even though it carries no clauses to strip.
const std::string kEmptyContract = R"AHFL(
module ec;

struct Req { id: Int; }
struct Ctx { }
struct Resp { ok: Bool; }

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for Worker {
}

flow for Worker {
    state Init { goto Done; }
    state Done { return Resp { ok: true }; }
}
)AHFL";

// A contract with only a wildcard decreases clause (no requires/ensures/
// invariant/forbid): the single clause must be erased.
const std::string kDecreasesOnlyContract = R"AHFL(
module dco;

struct Req { id: Int; }
struct Ctx { }
struct Resp { ok: Bool; }

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for Worker {
    decreases: *;
}

flow for Worker {
    state Init { goto Done; }
    state Done { return Resp { ok: true }; }
}
)AHFL";

[[nodiscard]] std::vector<std::pair<std::string, std::string>> corpus() {
    std::vector<std::pair<std::string, std::string>> programs;
    programs.emplace_back("full_orchestration", kFullOrchestration);
    programs.emplace_back("contract_agent", kContractAgent);
    return programs;
}

// P2: edge-case corpus entries. Each carries a verification construct in its
// minimal form. The differential property is the same; the non-vacuity is
// lighter (the construct's PRESENCE is checked by the N3 unit tests, not here).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> edge_case_corpus() {
    std::vector<std::pair<std::string, std::string>> programs;
    programs.emplace_back("quota_only_agent", kQuotaOnlyAgent);
    programs.emplace_back("domain_only_capability", kDomainOnlyCapability);
    programs.emplace_back("safety_only_workflow", kSafetyOnlyWorkflow);
    programs.emplace_back("empty_contract", kEmptyContract);
    programs.emplace_back("decreases_only_contract", kDecreasesOnlyContract);
    return programs;
}

void run_differential(const std::string &label, const std::string &source) {
    // Two independent lowerings of the same source (HIR->IR is deterministic).
    auto full = lower_to_ahfl_ir(label + "_full", source);
    auto stripped = lower_to_ahfl_ir(label + "_stripped", source);
    if (!full.has_value() || !stripped.has_value()) {
        check(false, label + ": corpus reaches the AHFL-IR layer");
        return;
    }
    check(true, label + ": corpus reaches the AHFL-IR layer");

    // Non-vacuity: the unmodified program must actually be populated with the
    // constructs the property is about.
    const auto pop = population_of(*full);
    check(pop.contract_clauses > 0, label + ": corpus carries contract clauses");
    check(pop.workflow_safety > 0, label + ": corpus carries workflow safety");
    check(pop.workflow_liveness > 0, label + ": corpus carries workflow liveness");
    if (label == "full_orchestration") {
        check(pop.quota_items > 0, label + ": corpus carries agent quota items");
        check(pop.fn_decreases > 0, label + ": corpus carries fn decreases measures");
        check(pop.capability_spec_annotations > 0,
              label + ": corpus carries capability effect-spec annotations");
    } else {
        check(pop.quota_items == 0, label + ": corpus intentionally has no quota items");
    }

    const auto full_core = ir::core::lower_ahfl_to_core(*full);
    if (!full_core.ok()) {
        for (const auto &d : full_core.diagnostics) {
            std::printf("    (%s: full lower diagnostic %s - %s)\n", label.c_str(),
                        d.code.c_str(), d.message.c_str());
        }
    }
    check(full_core.ok(), label + ": populated program lowers clean");
    check(full_core.is_executable, label + ": populated program is executable");

    strip_verification_constructs(*stripped);
    const auto stripped_core = ir::core::lower_ahfl_to_core(*stripped);
    check(stripped_core.ok(), label + ": stripped program lowers clean");

    // The differential: stripping verification constructs at the AHFL-IR layer
    // must not perturb a single Core-IR component.
    programs_component_equal(full_core.program, stripped_core.program, label);
}

// P2: edge-case differential. Same stripping + comparison as run_differential
// with per-label non-vacuity: each edge-case program must carry the construct
// it claims to test, so a future edit that removes the construct cannot make
// the differential pass vacuously.
void run_edge_case(const std::string &label, const std::string &source) {
    auto full = lower_to_ahfl_ir(label + "_full", source);
    auto stripped = lower_to_ahfl_ir(label + "_stripped", source);
    if (!full.has_value() || !stripped.has_value()) {
        check(false, label + ": corpus reaches the AHFL-IR layer");
        return;
    }
    check(true, label + ": corpus reaches the AHFL-IR layer");

    // Per-label non-vacuity: the construct must be PRESENT before stripping.
    const auto pop = population_of(*full);
    if (label == "quota_only_agent") {
        check(pop.quota_items > 0, label + ": corpus carries agent quota items");
    } else if (label == "domain_only_capability") {
        check(pop.capability_spec_annotations > 0,
              label + ": corpus carries capability effect-spec annotations");
    } else if (label == "safety_only_workflow") {
        check(pop.workflow_safety > 0, label + ": corpus carries workflow safety");
    } else if (label == "empty_contract") {
        check(pop.contract_decls > 0, label + ": corpus carries a contract declaration");
    } else if (label == "decreases_only_contract") {
        check(pop.contract_clauses > 0, label + ": corpus carries contract clauses");
    }

    const auto full_core = ir::core::lower_ahfl_to_core(*full);
    if (!full_core.ok()) {
        for (const auto &d : full_core.diagnostics) {
            std::printf("    (%s: full lower diagnostic %s - %s)\n", label.c_str(),
                        d.code.c_str(), d.message.c_str());
        }
    }
    check(full_core.ok(), label + ": populated program lowers clean");
    check(full_core.is_executable, label + ": populated program is executable");

    strip_verification_constructs(*stripped);
    const auto stripped_core = ir::core::lower_ahfl_to_core(*stripped);
    check(stripped_core.ok(), label + ": stripped program lowers clean");

    programs_component_equal(full_core.program, stripped_core.program, label);
}

// P1: idempotency. Lowering the SAME AHFL-IR twice must produce byte-identical
// Core-IR. `lower_ahfl_to_core` takes a const reference, so the input is not
// mutated; two independent lowerings of the same AhflIr must agree on every
// component.
void run_idempotency(const std::string &label, const std::string &source) {
    auto ahfl_ir = lower_to_ahfl_ir(label + "_idem", source);
    if (!ahfl_ir.has_value()) {
        check(false, label + ": corpus reaches the AHFL-IR layer");
        return;
    }
    check(true, label + ": corpus reaches the AHFL-IR layer");

    const auto first = ir::core::lower_ahfl_to_core(*ahfl_ir);
    const auto second = ir::core::lower_ahfl_to_core(*ahfl_ir);
    check(first.ok(), label + ": first lowering is clean");
    check(second.ok(), label + ": second lowering is clean");

    auto component = [&](bool component_equal, const std::string &name) {
        check(component_equal, label + ": idempotent " + name);
    };
    component(first.program.format_version == second.program.format_version, "format_version");
    component(first.program.types == second.program.types, "types");
    component(first.program.value_types == second.program.value_types, "value_types");
    component(first.program.capabilities == second.program.capabilities, "capabilities");
    component(first.program.agents == second.program.agents, "agents");
    component(first.program.flows == second.program.flows, "flows");
    component(first.program.workflows == second.program.workflows, "workflows");
    component(first.program.instances == second.program.instances, "instances");
}

} // namespace

int main() {
    std::printf("=== Property Tests: Core Erasure Barrier (KR6.4) ===\n");
    for (const auto &[label, source] : corpus()) {
        run_differential(label, source);
    }
    std::printf("\n--- P2: edge-case corpus ---\n");
    for (const auto &[label, source] : edge_case_corpus()) {
        run_edge_case(label, source);
    }
    std::printf("\n--- P1: idempotency ---\n");
    for (const auto &[label, source] : corpus()) {
        run_idempotency(label, source);
    }
    for (const auto &[label, source] : edge_case_corpus()) {
        run_idempotency(label, source);
    }
    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
