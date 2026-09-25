#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_recursion.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// RFC 0026 P3 (KR6.4): A-normal-form Core-IR lowering of agent state machines,
// capability imports, and flow handler bodies. These tests cover:
//   * agent + capability projection (hand-built AhflIr, isolated);
//   * the ANF invariants Codex's review required, driven through the REAL front
//     end (parse -> resolve -> typecheck -> lower to AhflIr -> lower to Core):
//       - a nested capability call inside a pure constructor is NOT dropped and
//         is hoisted to its own ordered CapabilityCall statement (P0-1);
//       - if/else branches keep mutual exclusion as distinct regions, never a
//         flattened statement list (P0-2);
//       - goto targets resolve to typed CoreStateId;
//       - state retry/timeout policy is preserved;
//       - an unresolved capability call fails closed with a diagnostic + range.

namespace {

using namespace ahfl;

// --------------------------------------------------------------------------
// Hand-built AhflIr helpers (isolate the decl-level projection).
// --------------------------------------------------------------------------

// Append two minimal input/output structs to `program` and point `agent` at
// them. Sema (and now the Core-IR verifier) require an agent's input/output to
// be Struct types; hand-built fixtures that only exercise state-machine / body
// lowering still need a valid typed shell to pass the auto-wired verifier.
void give_shell_structs(ir::AhflIr &program, ir::AgentDecl &agent, const std::string &prefix) {
    ir::StructDecl in;
    in.name = prefix + "In";
    in.symbol_ref.kind = ir::SymbolRefKind::Type;
    in.symbol_ref.canonical_name = "app::" + prefix + "In";
    program.declarations.emplace_back(std::move(in));

    ir::StructDecl out;
    out.name = prefix + "Out";
    out.symbol_ref.kind = ir::SymbolRefKind::Type;
    out.symbol_ref.canonical_name = "app::" + prefix + "Out";
    program.declarations.emplace_back(std::move(out));

    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::" + prefix + "In";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::" + prefix + "Out";
    // A hand-built agent leaves context_type_ref defaulted (Unresolved), which
    // the lowerer would treat as a broken (non-Unit) context. Unless a caller
    // has already given an explicit context struct, mark it the Unit default so
    // the (now Sema-aligned) verifier accepts a stateless agent.
    if (agent.context_type_ref.kind == ir::TypeRefKind::Unresolved) {
        agent.context_type_ref.kind = ir::TypeRefKind::Unit;
    }
}

// RFC 0026 P4-B: a real post-Sema `ir::Expr` always carries a resolved value
// type; the Core-IR lowerer interns it into the per-body `value_types` table.
// Hand-built fixtures that used to leave `resolved_type` Unresolved must now
// spell the leaf type Sema would have inferred, or lowering fails closed with
// core.lower UNRESOLVED_TYPE. These builders keep those fixtures realistic.
ir::TypeRef int_type() {
    ir::TypeRef t;
    t.kind = ir::TypeRefKind::Int;
    return t;
}
// An enum-typed value (e.g. the result of an `Option::Some(_)` constructor).
// Optionally carries ONE resolved generic argument (the P4-B value-type interner
// checks arity against the declaration), e.g. `Option<Int>`. `TypeRef` is
// move-only, so the arg is threaded by rvalue rather than an initializer_list.
ir::TypeRef enum_type(const std::string &canonical,
                      std::optional<ir::TypeRef> type_arg = std::nullopt) {
    ir::TypeRef t;
    t.kind = ir::TypeRefKind::Enum;
    t.canonical_name = canonical;
    t.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = canonical};
    if (type_arg) {
        t.params.push_back(std::make_unique<ir::TypeRef>(std::move(*type_arg)));
    }
    return t;
}

ir::AhflIr make_single_agent_program() {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "Classifier";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Classifier";
    agent.symbol_ref.local_name = "Classifier";
    agent.symbol_ref.id = 7;
    agent.states = {"Init", "Working", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.quota = {ir::QuotaItem{"max_tool_calls", "10"}};
    agent.transitions = {ir::TransitionDecl{"Init", "Working"},
                         ir::TransitionDecl{"Working", "Done"}};
    give_shell_structs(program, agent, "Cls");
    program.declarations.emplace_back(std::move(agent));

    ir::ContractDecl contract; // verification-only: must be erased
    contract.target_ref.kind = ir::SymbolRefKind::Agent;
    contract.target_ref.canonical_name = "app::Classifier";
    program.declarations.emplace_back(std::move(contract));
    return program;
}

ir::AhflIr make_capability_program() {
    ir::AhflIr program;
    ir::CapabilityDecl cap;
    cap.name = "ChargeCard";
    cap.symbol_ref.kind = ir::SymbolRefKind::Capability;
    cap.symbol_ref.canonical_name = "app::ChargeCard";
    cap.symbol_ref.local_name = "ChargeCard";
    cap.symbol_ref.id = 11;
    cap.provenance.source_range = ahfl::SourceRange{3, 17};
    ir::ParamDecl amount;
    amount.name = "amount";
    amount.type_ref.kind = ir::TypeRefKind::Int;
    amount.type_ref.display_name = "Int";
    cap.params.push_back(std::move(amount));
    cap.return_type_ref.kind = ir::TypeRefKind::Bool;
    cap.return_type_ref.display_name = "Bool";
    cap.effect.declared = true;
    cap.effect.kind = ir::CapabilityEffectKind::FinancialWrite;
    cap.effect.domain = "payments";
    cap.effect.receipt_mode = ir::CapabilityReceiptMode::Required;
    const ir::SymbolRef capability_ref = cap.symbol_ref;
    program.declarations.emplace_back(std::move(cap));

    ir::AgentDecl agent;
    agent.name = "Classifier";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Classifier";
    agent.symbol_ref.local_name = "Classifier";
    agent.symbol_ref.id = 7;
    agent.provenance.source_range = ahfl::SourceRange{19, 41};
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {ir::TransitionDecl{"Init", "Done"}};
    agent.capability_refs.push_back(capability_ref);
    give_shell_structs(program, agent, "Cap");
    program.declarations.emplace_back(std::move(agent));
    return program;
}

// --------------------------------------------------------------------------
// Real front-end driver: source -> AhflIr (parse/resolve/typecheck/lower).
// Returns nullopt if any stage errors (so a corpus regression is visible).
// --------------------------------------------------------------------------
std::optional<ir::AhflIr> lower_source_to_ahfl_ir(const std::string &label,
                                                  const std::string &source) {
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
    return lower_program_ir(*parse.program, resolve, typecheck);
}

// Real front-end driver over the repo std sysroot (parse_project + repo std),
// mirroring core_lower_sysroot.cpp. Required by RFC 0024 bounded-quantifier
// fixtures: a `forall x in input.values` body only type-checks when the operand
// resolves to a real `std::collections::List` nominal.
void write_test_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::optional<ir::AhflIr> lower_sysroot_source_to_ahfl_ir(const std::string &unique,
                                                          const std::string &source) {
    const auto root = std::filesystem::temp_directory_path() / ("ahfl_core_erasure_" + unique);
    std::filesystem::remove_all(root);
    const auto main_path = root / "app" / "main.ahfl";
    write_test_file(main_path, source);

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
    return lower_program_ir(parse.graph, resolve, typecheck);
}

// Count capability-call statements reachable in a region (recursing branches).
void collect_calls(const ir::core::CoreRegion &region,
                   std::vector<const ir::core::CoreCapabilityCallStmt *> &out) {
    for (const auto &stmt : region.statements) {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ir::core::CoreCapabilityCallStmt>) {
                    out.push_back(&node);
                } else if constexpr (std::is_same_v<T, ir::core::CoreIfStmt>) {
                    if (node.then_region) {
                        collect_calls(*node.then_region, out);
                    }
                    if (node.else_region) {
                        collect_calls(*node.else_region, out);
                    }
                }
            },
            stmt.node);
    }
}

// True if a (possibly module-qualified) callee names the given capability, e.g.
// "nestflow::Charge" or "Charge" both name "Charge".
bool callee_is(const std::string &callee, const std::string &name) {
    return callee == name || (callee.size() > name.size() &&
                              callee.compare(callee.size() - name.size() - 2, 2, "::") == 0 &&
                              callee.compare(callee.size() - name.size(), name.size(), name) == 0);
}

// Does a verify result carry an ERROR diagnostic with this stable code?
bool has_verify_code(const ir::core::CoreVerifyResult &r, std::string_view code) {
    for (const auto &d : r.diagnostics) {
        if (d.severity == ir::core::CoreDiagnosticSeverity::Error && d.code == code) {
            return true;
        }
    }
    return false;
}

// Does a LOWER result carry an ERROR diagnostic with this stable code? (P4-B:
// value-type interning can emit a core.UNRESOLVED_TYPE ahead of a node's own
// diagnostic, so tests assert the intended code is PRESENT, not necessarily
// first.)
bool has_lower_code(const ir::core::CoreLowerResult &r, std::string_view code) {
    for (const auto &d : r.diagnostics) {
        if (d.severity == ir::core::CoreDiagnosticSeverity::Error && d.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("lower_ahfl_to_core lowers an agent state machine into Core-IR") {
    const auto result = ir::core::lower_ahfl_to_core(make_single_agent_program());
    CHECK(result.ok());
    CHECK(result.is_executable);
    // The ContractDecl is a verification construct: it produces no Core decl.
    REQUIRE(result.program.agents.size() == 1);
    CHECK(result.program.flows.empty());
    const auto &agent = result.program.agents[0];
    CHECK(agent.name == "Classifier");
    REQUIRE(agent.states.size() == 3);
    CHECK(agent.states[agent.initial.value] == "Init");
    REQUIRE(agent.finals.size() == 1);
    CHECK(agent.states[agent.finals[0].value] == "Done");
    REQUIRE(agent.transitions.size() == 2);
    CHECK(result.program.format_version == std::string(ir::core::kCoreFormatVersion));
}

TEST_CASE("lower_ahfl_to_core lowers a capability into an explicit import decl") {
    const auto result = ir::core::lower_ahfl_to_core(make_capability_program());
    CHECK(result.ok());
    REQUIRE(result.program.capabilities.size() == 1);
    const auto &cap = result.program.capabilities[0];
    CHECK(cap.name == "ChargeCard");
    CHECK(cap.symbol_ref.canonical_name == "app::ChargeCard");
    CHECK(cap.effect_kind == ir::CapabilityEffectKind::FinancialWrite);
    REQUIRE(cap.source_range.has_value());
    CHECK(cap.source_range->begin_offset == 3);
    CHECK(cap.source_range->end_offset == 17);
    REQUIRE(cap.param_types.size() == 1);
    REQUIRE(cap.param_types[0].value < result.program.value_types.size());
    REQUIRE(cap.return_type.value < result.program.value_types.size());
    CHECK(std::holds_alternative<ir::core::CoreVtInt>(
        result.program.value_types[cap.param_types[0].value].node));
    CHECK(std::holds_alternative<ir::core::CoreVtBool>(
        result.program.value_types[cap.return_type.value].node));
    REQUIRE(result.program.agents.size() == 1);
    REQUIRE(result.program.agents[0].source_range.has_value());
    CHECK(result.program.agents[0].source_range->begin_offset == 19);
    CHECK(result.program.agents[0].source_range->end_offset == 41);
    REQUIRE(result.program.agents[0].capabilities.size() == 1);
    CHECK(result.program.agents[0].capabilities[0] == ir::core::CoreCapabilityId{0});
}

TEST_CASE("lower_ahfl_to_core preserves declaration-order agent capability ids") {
    auto program = make_capability_program();

    ir::CapabilityDecl audit;
    audit.name = "Audit";
    audit.symbol_ref.kind = ir::SymbolRefKind::Capability;
    audit.symbol_ref.canonical_name = "app::Audit";
    audit.symbol_ref.local_name = "Audit";
    audit.symbol_ref.id = 12;
    ir::ParamDecl input;
    input.name = "input";
    input.type_ref.kind = ir::TypeRefKind::Int;
    audit.params.push_back(std::move(input));
    audit.return_type_ref.kind = ir::TypeRefKind::Bool;
    const ir::SymbolRef audit_ref = audit.symbol_ref;
    program.declarations.insert(program.declarations.begin() + 1, std::move(audit));

    ir::AgentDecl *agent = nullptr;
    for (auto &decl : program.declarations) {
        if (auto *candidate = std::get_if<ir::AgentDecl>(&decl)) {
            agent = candidate;
            break;
        }
    }
    REQUIRE(agent != nullptr);
    const ir::SymbolRef charge_ref = agent->capability_refs.front();
    agent->capability_refs = {audit_ref, charge_ref};

    const auto result = ir::core::lower_ahfl_to_core(program);
    REQUIRE(result.ok());
    REQUIRE(result.program.capabilities.size() == 2);
    REQUIRE(result.program.agents.size() == 1);
    REQUIRE(result.program.agents[0].capabilities.size() == 2);
    CHECK(result.program.agents[0].capabilities[0] == ir::core::CoreCapabilityId{1});
    CHECK(result.program.agents[0].capabilities[1] == ir::core::CoreCapabilityId{0});
}

TEST_CASE("lower_ahfl_to_core fails closed on an unresolved capability signature") {
    auto program = make_capability_program();
    auto &cap = std::get<ir::CapabilityDecl>(program.declarations[0]);
    cap.params[0].type_ref = ir::TypeRef{.kind = ir::TypeRefKind::Any};
    cap.provenance.source_range = ahfl::SourceRange{31, 47};

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kUnresolvedCapabilitySignature));
    const auto diagnostic = std::ranges::find_if(result.diagnostics, [](const auto &d) {
        return d.code == ir::core::diag::kUnresolvedCapabilitySignature;
    });
    REQUIRE(diagnostic != result.diagnostics.end());
    REQUIRE(diagnostic->source_range.has_value());
    CHECK(diagnostic->source_range->begin_offset == 31);
    CHECK(diagnostic->source_range->end_offset == 47);
}

TEST_CASE("lower_ahfl_to_core fails closed on unresolved and duplicate agent capabilities") {
    SUBCASE("unresolved capability identity") {
        auto program = make_capability_program();
        auto agent = std::ranges::find_if(program.declarations, [](const auto &decl) {
            return std::holds_alternative<ir::AgentDecl>(decl);
        });
        REQUIRE(agent != program.declarations.end());
        auto &agent_decl = std::get<ir::AgentDecl>(*agent);
        agent_decl.provenance.source_range = ahfl::SourceRange{51, 63};
        agent_decl.capability_refs[0].id = 999;

        const auto result = ir::core::lower_ahfl_to_core(program);
        CHECK_FALSE(result.ok());
        CHECK(has_lower_code(result, ir::core::diag::kUnresolvedAgentCapability));
    }

    SUBCASE("duplicate capability identity") {
        auto program = make_capability_program();
        auto agent = std::ranges::find_if(program.declarations, [](const auto &decl) {
            return std::holds_alternative<ir::AgentDecl>(decl);
        });
        REQUIRE(agent != program.declarations.end());
        auto &agent_decl = std::get<ir::AgentDecl>(*agent);
        agent_decl.capability_refs.push_back(agent_decl.capability_refs.front());

        const auto result = ir::core::lower_ahfl_to_core(program);
        CHECK_FALSE(result.ok());
        CHECK(has_lower_code(result, ir::core::diag::kDuplicateAgentCapability));
    }
}

TEST_CASE("lower_ahfl_to_core is deterministic (decl level)") {
    const auto program = make_capability_program();
    const auto a = ir::core::lower_ahfl_to_core(program);
    const auto b = ir::core::lower_ahfl_to_core(program);
    REQUIRE(a.program.capabilities.size() == b.program.capabilities.size());
    REQUIRE(a.program.agents.size() == b.program.agents.size());
    CHECK(a.program.capabilities == b.program.capabilities);
    CHECK(a.program.agents == b.program.agents);
    CHECK(a.program.value_types == b.program.value_types);
    // Agent identity is its symbol (instance_key was removed from CoreAgentDecl;
    // execution/dispatch identity now lives only on CoreInstanceDecl).
    CHECK(a.program.agents[0].symbol_ref.canonical_name ==
          b.program.agents[0].symbol_ref.canonical_name);
    CHECK(a.program.instances.size() == b.program.instances.size());
}

// ==========================================================================
// ANF flow lowering (real front end).
// ==========================================================================

// A self-contained program exercising: a nested capability call inside an enum
// constructor, an if/else with a capability call in one branch, a goto, and a
// state policy. Deliberately small but structurally identical to refund/audit.
const char *const kFlowSource = R"AHFL(
module payflow;

struct Order {
    total: Int;
}

struct Ctx {
    charged: Bool = false;
}

struct Outcome {
    ok: Bool = false;
}

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

TEST_CASE("flow lowering: capability call inside a branch is preserved (P0-2 mutual exclusion)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("payflow", kFlowSource);
    REQUIRE(ahfl_ir.has_value());
    auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    REQUIRE(flow.states.size() == 3);

    // Find the Work state.
    const ir::core::CoreFlowState *work = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Work") {
            work = &s;
        }
    }
    REQUIRE(work != nullptr);

    // The Charge call must live INSIDE the then-branch region — mutual
    // exclusion preserved, NOT flattened into the top-level statement list.
    bool found_if = false;
    bool charge_at_top_level = false;
    for (const auto &stmt : work->body.statements) {
        if (std::holds_alternative<ir::core::CoreIfStmt>(stmt.node)) {
            found_if = true;
            const auto &if_stmt = std::get<ir::core::CoreIfStmt>(stmt.node);
            REQUIRE(if_stmt.then_region);
            std::vector<const ir::core::CoreCapabilityCallStmt *> then_calls;
            collect_calls(*if_stmt.then_region, then_calls);
            CHECK(then_calls.size() == 1); // Charge is in the then-branch
            if (!then_calls.empty()) {
                const auto &call = *then_calls[0];
                CHECK(callee_is(call.callee_name, "Charge"));
                REQUIRE(call.capability.value < result.program.capabilities.size());
                const auto &signature = result.program.capabilities[call.capability.value];
                REQUIRE(call.args.size() == signature.param_types.size());
                REQUIRE(call.args.size() == 1);
                REQUIRE(call.args[0].value < flow.storage.value_types.size());
                REQUIRE(call.result.value < flow.storage.value_types.size());
                CHECK(flow.storage.value_types[call.args[0].value] == signature.param_types[0]);
                CHECK(flow.storage.value_types[call.result.value] == signature.return_type);
                REQUIRE(flow.target.value < result.program.agents.size());
                const auto &whitelist = result.program.agents[flow.target.value].capabilities;
                CHECK(std::ranges::find(whitelist, call.capability) != whitelist.end());
            }
        }
        if (std::holds_alternative<ir::core::CoreCapabilityCallStmt>(stmt.node)) {
            const auto &call = std::get<ir::core::CoreCapabilityCallStmt>(stmt.node);
            if (callee_is(call.callee_name, "Charge")) {
                charge_at_top_level = true;
            }
        }
    }
    CHECK(found_if);
    CHECK_FALSE(charge_at_top_level); // Charge must NOT be hoisted out of its branch
}

TEST_CASE("flow lowering: goto resolves to a typed CoreStateId and policy is preserved") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("payflow", kFlowSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    const ir::core::CoreFlowState *work = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Work") {
            work = &s;
        }
    }
    REQUIRE(work != nullptr);
    // retry/timeout policy carried onto the state.
    CHECK(work->policy.retry_limit.has_value());
    CHECK(work->policy.timeout.has_value());

    // The Init state's goto must resolve to a typed state id (not a bare string).
    const ir::core::CoreFlowState *init = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Init") {
            init = &s;
        }
    }
    REQUIRE(init != nullptr);
    bool found_goto = false;
    for (const auto &stmt : init->body.statements) {
        if (std::holds_alternative<ir::core::CoreGotoStmt>(stmt.node)) {
            found_goto = true;
            const auto &g = std::get<ir::core::CoreGotoStmt>(stmt.node);
            CHECK(g.target_name == "Work");
            // Work is state index 1 in [Init, Work, Done].
            CHECK(g.target.value == 1);
        }
    }
    CHECK(found_goto);
}

// Nested capability call inside an enum constructor (the refund/audit shape):
//   ctx.ticket = Option::Some(Charge(order.total));
// The inner Charge MUST become its own ordered CapabilityCall statement whose
// result feeds the outer pure Some(...) construct — never dropped (P0-1).
const char *const kNestedSource = R"AHFL(
module nestflow;

enum Box {
    Wrap(Bool),
    Empty,
}

struct Order {
    total: Int;
}

struct Ctx {
    ticket: Box = Box::Empty;
}

struct Outcome {
    ok: Bool = false;
}

capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Order;
    context: Ctx;
    output: Outcome;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Charge];

    transition Init -> Done;
}

flow for Payer {
    state Init {
        ctx.ticket = Box::Wrap(Charge(input.total));
        goto Done;
    }

    state Done {
        return Outcome { ok: false };
    }
}
)AHFL";

TEST_CASE("flow lowering: nested capability call inside a constructor is not dropped (P0-1)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("nestflow", kNestedSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    const ir::core::CoreFlowState *init = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Init") {
            init = &s;
        }
    }
    REQUIRE(init != nullptr);

    std::vector<const ir::core::CoreCapabilityCallStmt *> calls;
    collect_calls(init->body, calls);
    // The inner Charge call survives as an explicit ordered capability-call stmt.
    REQUIRE(calls.size() == 1);
    CHECK(callee_is(calls[0]->callee_name, "Charge"));
    // Its result value id must be consumed by a later pure construct (Some).
    const ir::core::CoreValueId charge_result = calls[0]->result;

    bool result_consumed_by_construct = false;
    for (const auto &expr : flow.storage.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &ctor = std::get<ir::core::CoreConstructExpr>(expr.node);
            for (const auto &arg : ctor.args) {
                if (arg.value == charge_result) {
                    result_consumed_by_construct = true;
                    // The Wrap variant resolves to a typed index (Wrap=0).
                    CHECK(ctor.is_enum_variant);
                    CHECK(ctor.resolved);
                    CHECK(ctor.variant.value == 0u);
                }
            }
        }
    }
    CHECK(result_consumed_by_construct);
}

// ==========================================================================
// Fail-closed: unresolved capability call (hand-built AhflIr).
// ==========================================================================

TEST_CASE("flow lowering fails closed on an unresolved capability call") {
    // A flow that calls a capability whose declaration is absent from the
    // program: lowering must emit an error diagnostic with a range and mark the
    // program non-executable — never silently drop the call.
    ir::AhflIr program;

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";

    // A capability CallExpr whose callee_ref is NOT backed by any CapabilityDecl.
    ir::CallExpr call;
    call.callee = "Ghost";
    call.callee_ref.kind = ir::SymbolRefKind::Capability;
    call.callee_ref.canonical_name = "app::Ghost";
    call.callee_ref.id = 999;
    ir::ExprRef call_ref = program.expr_arena.make(std::move(call), SourceRange{10, 20});

    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{call_ref};
    stmt->source_range = SourceRange{10, 20};

    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(result.has_errors());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnresolvedCapabilityCall);
    CHECK(result.diagnostics[0].source_range.has_value());
}

// ==========================================================================
// Builtin Option/Result variant order must match the sysroot declaration.
// ==========================================================================

namespace {

// A variant's declaration shape parsed from the sysroot: name + payload kind +
// payload arity. Enough to prove the builtin SSOT mirrors the real stdlib
// declaration (not just the name order).
struct SysrootVariant {
    std::string name;
    ir::core::CoreTypeDecl::VariantPayload::Kind kind{
        ir::core::CoreTypeDecl::VariantPayload::Kind::Unit};
    std::uint32_t arity{0};
};

// Count top-level (depth-1) comma-separated items in a payload body, i.e. the
// payload arity. `Some(T)` -> 1, `Pair(A, B)` -> 2, `Rec { a: T, b: U }` -> 2.
[[nodiscard]] std::uint32_t count_payload_items(const std::string &body) {
    // An empty/whitespace body means zero items.
    bool any = false;
    for (char c : body) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            any = true;
            break;
        }
    }
    if (!any) {
        return 0;
    }
    std::uint32_t items = 1;
    int depth = 0;
    for (char c : body) {
        if (c == '(' || c == '<' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '>' || c == '}' || c == ']') {
            --depth;
        } else if (c == ',' && depth == 0) {
            ++items;
        }
    }
    return items;
}

// Extract the variants (name + payload kind + arity) of the first `enum <Name>`
// block in an AHFL source file, in declaration order. Minimal textual scan (no
// full parse) — enough to pin the sysroot declaration the builtin table mirrors.
std::vector<SysrootVariant> read_enum_variants(const std::string &path,
                                               const std::string &enum_name) {
    std::vector<SysrootVariant> variants;
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return variants;
    }
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    std::fclose(f);

    // The enum header may be `enum Name` or `enum Name<...>`; match the bare name
    // then the following `{`.
    const auto enum_pos = text.find("enum " + enum_name);
    if (enum_pos == std::string::npos) {
        return variants;
    }
    const auto open = text.find('{', enum_pos);
    if (open == std::string::npos) {
        return variants;
    }
    // Find the matching close brace for the enum body (variants may contain
    // nested `{...}` struct payloads, so track depth).
    std::size_t close = std::string::npos;
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            if (--depth == 0) {
                close = i;
                break;
            }
        }
    }
    if (close == std::string::npos) {
        return variants;
    }
    const std::string body = text.substr(open + 1, close - open - 1);
    // Walk top-level (depth-0) comma-separated variant entries. Each entry is an
    // identifier optionally followed by `(...)` (tuple) or `{...}` (struct).
    using PK = ir::core::CoreTypeDecl::VariantPayload::Kind;
    std::size_t i = 0;
    while (i < body.size()) {
        // Skip separators / whitespace.
        while (i < body.size() && !(std::isalnum(static_cast<unsigned char>(body[i])) ||
                                    body[i] == '_')) {
            ++i;
        }
        if (i >= body.size()) {
            break;
        }
        std::string name;
        while (i < body.size() &&
               (std::isalnum(static_cast<unsigned char>(body[i])) || body[i] == '_')) {
            name.push_back(body[i++]);
        }
        // Skip whitespace to peek at an optional payload delimiter.
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) {
            ++i;
        }
        SysrootVariant v;
        v.name = name;
        if (i < body.size() && (body[i] == '(' || body[i] == '{')) {
            const char open_ch = body[i];
            const char close_ch = open_ch == '(' ? ')' : '}';
            v.kind = open_ch == '(' ? PK::Tuple : PK::Struct;
            int d = 0;
            const std::size_t payload_start = i + 1;
            std::size_t payload_end = i;
            for (; i < body.size(); ++i) {
                if (body[i] == open_ch) {
                    ++d;
                } else if (body[i] == close_ch) {
                    if (--d == 0) {
                        payload_end = i;
                        ++i;
                        break;
                    }
                }
            }
            v.arity = count_payload_items(body.substr(payload_start, payload_end - payload_start));
        } else {
            v.kind = PK::Unit;
            v.arity = 0;
        }
        variants.push_back(std::move(v));
        // Advance past a trailing top-level comma.
        while (i < body.size() && body[i] != ',') {
            // Skip anything up to the next separator only if it's whitespace; a
            // stray token would be malformed source, which the parser would have
            // rejected — here we simply resync on the next comma.
            if (!std::isspace(static_cast<unsigned char>(body[i]))) {
                break;
            }
            ++i;
        }
        if (i < body.size() && body[i] == ',') {
            ++i;
        }
    }
    return variants;
}

} // namespace

TEST_CASE("builtin variant table matches stdlib declaration (name + payload kind + arity)") {
    // Reads the ACTUAL production builtin table (builtin_enum_table()) and the
    // sysroot std/*.ahfl, and asserts each descriptor's variants match the
    // sysroot declaration by NAME, PAYLOAD KIND, and ARITY. Reordering or
    // reshaping EITHER side (e.g. changing std Option's `Some(T)` to a struct
    // payload) now fails — closing the "sync test doesn't compare std" gap.
    const auto &table = ir::core::builtin_enum_table();
    REQUIRE_FALSE(table.empty());
    for (const auto &desc : table) {
        const std::string name(desc.name);
        const std::string path = "std/" + [&] {
            std::string lower;
            for (char c : name) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return lower;
        }() + ".ahfl";
        const auto sysroot = read_enum_variants(path, name);
        INFO("enum " << name << " from " << path);
        REQUIRE(sysroot.size() == desc.variants.size());
        for (std::size_t i = 0; i < desc.variants.size(); ++i) {
            INFO("variant #" << i);
            CHECK(std::string(desc.variants[i].name) == sysroot[i].name);
            CHECK(desc.variants[i].payload_kind == sysroot[i].kind);
            CHECK(desc.variants[i].payload_type_params.size() == sysroot[i].arity);
        }
    }
}

TEST_CASE("builtin enum variant resolves via the builtin path by symbol identity") {
    // A hand-built AhflIr that constructs std::option::Option::Some(<int>) with
    // a callee_ref to the Option ENUM symbol (as the front end emits). No user
    // Option is declared, so resolution must go through the builtin table — and
    // by SYMBOL IDENTITY / canonical name, not by unqualified-name hijack.
    ir::AhflIr program;

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    // Option::Some(0) as a variant-constructor CallExpr (callee_ref -> Enum).
    ir::ExprRef arg = program.expr_arena.make(ir::IntegerLiteralExpr{"0"}, std::nullopt, int_type());
    ir::CallExpr call;
    call.callee = "std::option::Option::Some";
    call.callee_ref.kind = ir::SymbolRefKind::Type; // the front end resolves to the Enum symbol
    call.callee_ref.canonical_name = "std::option::Option";
    call.arguments.push_back(arg);
    ir::ExprRef call_ref = program.expr_arena.make(std::move(call), std::nullopt,
                                                   enum_type("std::option::Option", int_type()));

    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{call_ref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    CHECK(result.ok());
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow_out = result.program.flows[0];

    bool found_some = false;
    for (const auto &expr : flow_out.storage.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &ctor = std::get<ir::core::CoreConstructExpr>(expr.node);
            if (ctor.is_enum_variant && ctor.variant_name == "Some") {
                found_some = true;
                CHECK(ctor.resolved);
                CHECK(ctor.variant.value == 0u); // Some=0 in the builtin table
                // The owning type id points at the builtin Option in the type table.
                REQUIRE(ctor.type_id.value < result.program.types.size());
                CHECK(result.program.types[ctor.type_id.value].name == "std::option::Option");
            }
        }
    }
    CHECK(found_some);
}

// ==========================================================================
// Fail-closed executable completeness + scope + target (Codex re-review).
// ==========================================================================

// Build a minimal flow whose single handler statement is provided by `make_stmt`.
ir::AhflIr make_single_handler_flow(
    const std::function<ir::StatementPtr(ir::AhflIr &)> &make_stmt) {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(make_stmt(program));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));
    return program;
}

TEST_CASE("unsupported statement makes the program non-executable (P0-1)") {
    // An `assert(...)` is not yet lowered; dropping it would turn a failing
    // program into a no-op, so lowering must FAIL closed (Error), not Warning.
    const auto program = make_single_handler_flow([](ir::AhflIr &p) {
        ir::ExprRef cond = p.expr_arena.make(ir::BoolLiteralExpr{false});
        auto stmt = std::make_unique<ir::Statement>();
        stmt->node = ir::AssertStatement{cond, ir::ExprRef{}};
        stmt->source_range = ahfl::SourceRange{1, 2};
        return stmt;
    });
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredStatement);
    CHECK(result.diagnostics[0].severity == ir::core::CoreDiagnosticSeverity::Error);
}

TEST_CASE("branch-local let bindings do not leak across branches or past the if (P0-2)") {
    // let x = 1; if true { let x = 2; } else { let x = 3; } return x;
    // The returned value must be the OUTER x (value id from `let x = 1`), not
    // the else-branch's shadow. We assert the return's value id equals the
    // outer binding's, proving branch scopes are snapshot/restored.
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";

    const auto make_let = [&](const std::string &name, const std::string &val) {
        ir::ExprRef init = program.expr_arena.make(ir::IntegerLiteralExpr{val}, std::nullopt,
                                                   int_type());
        auto s = std::make_unique<ir::Statement>();
        ir::LetStatement let;
        let.name = name;
        // An inferred `let x = <Int>` carries the initializer's type in type_ref
        // (Sema's FromInitializerType strategy).
        let.type_ref = int_type();
        let.initializer = init;
        s->node = std::move(let);
        return s;
    };
    // let x = 1;
    handler.body.statements.push_back(make_let("x", "1"));
    // if true { let x = 2; } else { let x = 3; }
    {
        ir::TypeRef bool_ty;
        bool_ty.kind = ir::TypeRefKind::Bool;
        ir::ExprRef cond =
            program.expr_arena.make(ir::BoolLiteralExpr{true}, std::nullopt, std::move(bool_ty));
        ir::IfStatement if_stmt;
        if_stmt.condition = cond;
        auto then_block = std::make_unique<ir::Block>();
        then_block->statements.push_back(make_let("x", "2"));
        auto else_block = std::make_unique<ir::Block>();
        else_block->statements.push_back(make_let("x", "3"));
        if_stmt.then_block = std::move(then_block);
        if_stmt.else_block = std::move(else_block);
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(if_stmt);
        handler.body.statements.push_back(std::move(s));
    }
    // return x;
    {
        ir::PathExpr px;
        px.path.root_name = "x";
        ir::ExprRef xref = program.expr_arena.make(std::move(px));
        auto s = std::make_unique<ir::Statement>();
        s->node = ir::ReturnStatement{xref};
        handler.body.statements.push_back(std::move(s));
    }
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    REQUIRE(result.ok());
    const auto &body = result.program.flows[0].states[0].body;

    // Outer `let x = 1` is the FIRST let statement; capture its result value id.
    std::optional<ir::core::CoreValueId> outer_x;
    for (const auto &stmt : body.statements) {
        if (std::holds_alternative<ir::core::CoreLetStmt>(stmt.node)) {
            outer_x = std::get<ir::core::CoreLetStmt>(stmt.node).result;
            break;
        }
    }
    REQUIRE(outer_x.has_value());

    // The return reads `x`; after the if, the local `x` must still be the outer
    // binding, so its lowered value-ref chain must originate from outer_x —
    // never the else-branch's `3`. We check the return statement's value id
    // traces to a pure expr that references outer_x (a bare local ref returns
    // the binding's value id directly).
    std::optional<ir::core::CoreValueId> returned;
    for (const auto &stmt : body.statements) {
        if (std::holds_alternative<ir::core::CoreReturnStmt>(stmt.node)) {
            const auto &r = std::get<ir::core::CoreReturnStmt>(stmt.node);
            REQUIRE(r.has_value);
            returned = r.value;
        }
    }
    REQUIRE(returned.has_value());
    CHECK(*returned == *outer_x); // returns OUTER x, branch shadows discarded
}

TEST_CASE("a branch shadow with a DIFFERENT nominal type does not corrupt the outer local's "
          "type (P0-1)") {
    // let x: AType;  (AType has field `a`)
    // if true { let x: BType = …; }  (BType has field `b`, NOT `a`)
    // ctx.out = x.a;  -- after the if, x must STILL be AType, so `x.a` resolves.
    //
    // If lower_if snapshot/restored only the value id (not the type), the
    // branch-local `x: BType` would leave the outer `x`'s tracked type as BType,
    // and `x.a` would fail-closed (BType has no `a`). This asserts the merged
    // scope snapshot restores value AND type together.
    ir::AhflIr program;

    const auto make_struct = [&](const std::string &name, const std::string &canonical,
                                 std::size_t id, const std::string &field) {
        ir::StructDecl s;
        s.name = name;
        s.symbol_ref.kind = ir::SymbolRefKind::Type;
        s.symbol_ref.canonical_name = canonical;
        s.symbol_ref.id = id;
        ir::FieldDecl f;
        f.name = field;
        f.type_ref.kind = ir::TypeRefKind::Int;
        s.fields.push_back(std::move(f));
        ir::MemberTypeTemplateNode node;
        node.kind = ir::MemberTypeTemplateKind::Concrete;
        node.type_ref.kind = ir::TypeRefKind::Int;
        s.member_type_templates.push_back(std::move(node));
        s.field_type_template_roots.push_back(0);
        program.declarations.emplace_back(std::move(s));
    };
    make_struct("AType", "app::AType", 60, "a");
    make_struct("BType", "app::BType", 61, "b");

    // Ctx has a single Int field `out` (the store target).
    {
        ir::StructDecl ctx;
        ctx.name = "Ctx";
        ctx.symbol_ref.kind = ir::SymbolRefKind::Type;
        ctx.symbol_ref.canonical_name = "app::Ctx";
        ctx.symbol_ref.id = 62;
        ir::FieldDecl f;
        f.name = "out";
        f.type_ref.kind = ir::TypeRefKind::Int;
        ctx.fields.push_back(std::move(f));
        ir::MemberTypeTemplateNode node;
        node.kind = ir::MemberTypeTemplateKind::Concrete;
        node.type_ref.kind = ir::TypeRefKind::Int;
        ctx.member_type_templates.push_back(std::move(node));
        ctx.field_type_template_roots.push_back(0);
        program.declarations.emplace_back(std::move(ctx));
    }

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.context_type_ref.kind = ir::TypeRefKind::Struct;
    agent.context_type_ref.canonical_name = "app::Ctx";
    give_shell_structs(program, agent, "Shd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";

    const auto make_typed_let = [&](const std::string &name, const std::string &type_canonical) {
        // A realistic `let x: T = <init>` has BOTH the declared annotation and the
        // initializer resolved to the SAME nominal T (Sema coerces/checks), so
        // P4-B records the local's value type as the nominal T and the declared
        // type matches the initializer type (P0-3 lower_let check).
        const auto struct_ty = [&] {
            ir::TypeRef t;
            t.kind = ir::TypeRefKind::Struct;
            t.canonical_name = type_canonical;
            t.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                          .canonical_name = type_canonical};
            return t;
        };
        ir::ExprRef init = program.expr_arena.make(ir::IntegerLiteralExpr{"0"}, std::nullopt,
                                                   struct_ty());
        auto s = std::make_unique<ir::Statement>();
        ir::LetStatement let;
        let.name = name;
        let.type_ref = struct_ty();
        let.initializer = init;
        s->node = std::move(let);
        return s;
    };
    // let x: AType = 0;
    handler.body.statements.push_back(make_typed_let("x", "app::AType"));
    // if true { let x: BType = 0; }
    {
        ir::TypeRef bool_ty;
        bool_ty.kind = ir::TypeRefKind::Bool;
        ir::ExprRef cond =
            program.expr_arena.make(ir::BoolLiteralExpr{true}, std::nullopt, std::move(bool_ty));
        ir::IfStatement if_stmt;
        if_stmt.condition = cond;
        auto then_block = std::make_unique<ir::Block>();
        then_block->statements.push_back(make_typed_let("x", "app::BType"));
        if_stmt.then_block = std::move(then_block);
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(if_stmt);
        handler.body.statements.push_back(std::move(s));
    }
    // ctx.out = x.a;
    {
        ir::PathExpr xa;
        xa.path.root_name = "x";
        xa.path.members = {"a"};
        // `x.a` reads AType's Int field `a`; a real projection carries the leaf
        // type Sema inferred, which P4-B interns as the projection's result type.
        ir::ExprRef xaref = program.expr_arena.make(std::move(xa), std::nullopt, int_type());
        ir::AssignStatement assign;
        assign.target.root_kind = ir::PathRootKind::Context;
        assign.target.root_name = "ctx";
        assign.target.members = {"out"};
        assign.value = xaref;
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(assign);
        handler.body.statements.push_back(std::move(s));
    }
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // `x.a` must resolve against AType (field `a`), NOT the branch-local BType.
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // The AType CoreTypeId, to compare the resolved read's owner against.
    std::optional<ir::core::CoreTypeId> atype_id;
    for (std::uint32_t i = 0; i < result.program.types.size(); ++i) {
        if (result.program.types[i].name == "app::AType") {
            atype_id = ir::core::CoreTypeId{i};
        }
    }
    REQUIRE(atype_id.has_value());

    bool saw_x_a = false;
    for (const auto &expr : result.program.flows[0].storage.exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root == ir::core::CorePathRoot::Local && p->members.size() == 1 &&
                p->members[0] == "a") {
                saw_x_a = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type == *atype_id);  // outer AType, not branch BType
                REQUIRE(p->projection.size() == 1);
                CHECK(p->projection[0].owner_type == *atype_id);
                CHECK(p->projection[0].field.value == 0u); // AType.a is field 0
            }
        }
    }
    CHECK(saw_x_a);
}

TEST_CASE("flow with an unresolved target agent fails closed (P1-1)") {
    ir::AhflIr program;
    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::Ghost";
    flow.target_ref.local_name = "Ghost";
    flow.target_ref.id = 404;
    ir::StateHandler handler;
    handler.state_name = "S";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnresolvedFlowTarget);
}

TEST_CASE("flow handler naming an unknown state fails closed, never defaults to state 0 (P1-1)") {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"Init"};
    agent.initial_state = "Init";
    agent.final_states = {"Init"};
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "Nonexistent"; // not a declared state
    handler.source_range = ahfl::SourceRange{5, 9};
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnknownHandlerState);
    CHECK(result.diagnostics[0].source_range.has_value());
    // The bogus handler was NOT lowered onto state 0.
    REQUIRE(result.program.flows.size() == 1);
    CHECK(result.program.flows[0].states.empty());
}

TEST_CASE("pure unsupported expression makes the program non-executable (P0-1 round 3)") {
    // A MethodCallExpr (no capability) is not yet lowered to Core-IR. Even
    // without an effect, it must FAIL closed — a CoreUnsupportedExpr the backend
    // cannot execute may never be reported as executable.
    const auto program = make_single_handler_flow([](ir::AhflIr &p) {
        // return input.method() — a pure method call, still unsupported.
        ir::ExprRef base = p.expr_arena.make(
            ir::PathExpr{.path = ir::Path{.root_name = "input"}});
        ir::MethodCallExpr call;
        call.receiver = base;
        call.method = "compute";
        ir::ExprRef cref = p.expr_arena.make(std::move(call));
        auto stmt = std::make_unique<ir::Statement>();
        stmt->node = ir::ReturnStatement{cref};
        return stmt;
    });
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(has_lower_code(result, ir::core::diag::kUnloweredExpression));
    CHECK(result.diagnostics[0].severity == ir::core::CoreDiagnosticSeverity::Error);
}

TEST_CASE("struct constructor binds values by field identity, not source write order (P0-2 round 3)") {
    // struct Pair { a: Int; b: Int; }  return Pair { b: 2, a: 1 };
    // Fields are WRITTEN b-then-a but must bind by identity: the arg tagged
    // CoreFieldId a(=0) is the value `1`, and b(=1) is `2` — never positional.
    const char *const source = R"AHFL(
module pairflow;

struct Pair {
    a: Int;
    b: Int;
}

struct Req {
    n: Int;
}

struct Ctx {
    seen: Bool = false;
}

agent A {
    input: Req;
    context: Ctx;
    output: Pair;
    states: [S];
    initial: S;
    final: [S];
    capabilities: [];
}

flow for A {
    state S {
        return Pair { b: 2, a: 1 };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("pairflow", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // This program has NO member projections and all fields resolve, so it must
    // be fully executable — a hard assertion, not the weak field-projection
    // tolerance used by the ANF-structure tests above.
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    // Find the Pair construct and the literal each arg's value came from.
    const ir::core::CoreConstructExpr *pair = nullptr;
    for (const auto &expr : flow.storage.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &c = std::get<ir::core::CoreConstructExpr>(expr.node);
            if (c.type_name.find("Pair") != std::string::npos && !c.is_enum_variant) {
                pair = &c;
            }
        }
    }
    REQUIRE(pair != nullptr);
    REQUIRE(pair->resolved);
    REQUIRE(pair->args.size() == 2);

    // Map each field id -> the integer literal spelling that produced its value.
    const auto literal_for_value = [&](ir::core::CoreValueId v) -> std::string {
        // The value is bound by a CoreLetStmt whose expr is an integer literal.
        for (const auto &state : flow.states) {
            for (const auto &stmt : state.body.statements) {
                if (const auto *let = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                    if (let->result == v && let->expr.value < flow.storage.exprs.size()) {
                        const auto &e = flow.storage.exprs[let->expr.value].node;
                        if (const auto *lit = std::get_if<ir::core::CoreLiteralExpr>(&e)) {
                            return lit->spelling;
                        }
                    }
                }
            }
        }
        return {};
    };

    std::string field_a_val;
    std::string field_b_val;
    for (const auto &arg : pair->args) {
        if (arg.field.value == 0u) { // field `a` (declared first)
            field_a_val = literal_for_value(arg.value);
        } else if (arg.field.value == 1u) { // field `b` (declared second)
            field_b_val = literal_for_value(arg.value);
        }
    }
    // Despite `Pair { b: 2, a: 1 }`, identity binds a<-1 and b<-2.
    CHECK(field_a_val == "1");
    CHECK(field_b_val == "2");
}

// ==========================================================================
// Struct-field member projection lowering (resolves to typed CoreFieldId).
// ==========================================================================

const char *const kProjectionSource = R"AHFL(
module projflow;

struct Inner {
    n: Int;
}

struct Req {
    top: Int;
    inner: Inner;
}

struct Ctx {
    saved: Int = 0;
    nested: Inner = Inner { n: 0 };
}

struct Reply {
    ok: Bool = false;
}

agent A {
    input: Req;
    context: Ctx;
    output: Reply;
    states: [S];
    initial: S;
    final: [S];
    capabilities: [];
}

flow for A {
    state S {
        ctx.saved = input.top;
        ctx.nested.n = input.inner.n;
    }
}
)AHFL";

TEST_CASE("member projection resolves reads and stores to typed CoreFieldId (fail-closed lifted)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("projflow", kProjectionSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // With member projections now resolved, the program is fully executable —
    // NO more field-projection fail-closed diagnostics.
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    REQUIRE(flow.states.size() == 1);
    const auto &body = flow.states[0].body;

    // Gather the resolved read (CorePathExpr) and store (CoreStoreStmt) projections.
    // Reads live in the expr arena; stores are statements.
    bool saw_input_top_read = false;   // input.top -> field 0 of Req
    bool saw_input_inner_n_read = false; // input.inner.n -> [1, 0]
    for (const auto &expr : flow.storage.exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 1 &&
                p->members[0] == "top") {
                saw_input_top_read = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(p->projection.size() == 1);
                CHECK(p->projection[0].owner_type == p->root_type); // step owner == root
                CHECK(p->projection[0].field.value == 0u); // Req.top is field 0
            }
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 2 &&
                p->members[0] == "inner" && p->members[1] == "n") {
                saw_input_inner_n_read = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(p->projection.size() == 2);
                CHECK(p->projection[0].owner_type == p->root_type); // Req
                CHECK(p->projection[0].field.value == 1u);          // Req.inner is field 1
                // Step continuity: step 1's owner is step 0's result (Inner).
                CHECK(p->projection[1].owner_type == p->projection[0].result_type);
                CHECK(p->projection[1].owner_type.value != ir::core::CoreTypeId::kInvalid);
                CHECK(p->projection[1].field.value == 0u); // Inner.n is field 0
                // Terminal step reaches a primitive (Int): no result type.
                CHECK(p->projection[1].result_type.value == ir::core::CoreTypeId::kInvalid);
            }
        }
    }
    CHECK(saw_input_top_read);
    CHECK(saw_input_inner_n_read);

    bool saw_ctx_saved_store = false;    // ctx.saved -> field 0 of Ctx
    bool saw_ctx_nested_n_store = false; // ctx.nested.n -> [1, 0]
    for (const auto &stmt : body.statements) {
        if (const auto *store = std::get_if<ir::core::CoreStoreStmt>(&stmt.node)) {
            const auto &pl = store->place;
            if (pl.root == ir::core::CorePathRoot::Context && pl.members.size() == 1 &&
                pl.members[0] == "saved") {
                saw_ctx_saved_store = true;
                CHECK(pl.projection_resolved);
                CHECK(pl.root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(pl.projection.size() == 1);
                CHECK(pl.projection[0].owner_type == pl.root_type);
                CHECK(pl.projection[0].field.value == 0u); // Ctx.saved is field 0
            }
            if (pl.root == ir::core::CorePathRoot::Context && pl.members.size() == 2 &&
                pl.members[0] == "nested" && pl.members[1] == "n") {
                saw_ctx_nested_n_store = true;
                CHECK(pl.projection_resolved);
                CHECK(pl.root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(pl.projection.size() == 2);
                CHECK(pl.projection[0].owner_type == pl.root_type);  // Ctx
                CHECK(pl.projection[0].field.value == 1u);           // Ctx.nested is field 1
                CHECK(pl.projection[1].owner_type == pl.projection[0].result_type); // Inner
                CHECK(pl.projection[1].field.value == 0u);           // Inner.n is field 0
                CHECK(pl.projection[1].result_type.value == ir::core::CoreTypeId::kInvalid);
            }
        }
    }
    CHECK(saw_ctx_saved_store);
    CHECK(saw_ctx_nested_n_store);
}

TEST_CASE("member projection on an unknown field fails closed") {
    // Hand-built AhflIr: agent input is a struct with field `a`, but the flow
    // reads `input.ghost` (no such field) -> fail-closed diagnostic, not a
    // silently-executable projection.
    ir::AhflIr program;

    ir::StructDecl req;
    req.name = "Req";
    req.symbol_ref.kind = ir::SymbolRefKind::Type;
    req.symbol_ref.canonical_name = "app::Req";
    req.symbol_ref.id = 50;
    ir::FieldDecl fa;
    fa.name = "a";
    fa.type_ref.kind = ir::TypeRefKind::Int;
    req.fields.push_back(std::move(fa));
    program.declarations.emplace_back(std::move(req));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::Req";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // return input.ghost;
    ir::PathExpr px;
    px.path.root_kind = ir::PathRootKind::Input;
    px.path.root_name = "input";
    px.path.members = {"ghost"};
    ir::ExprRef pref = program.expr_arena.make(std::move(px), SourceRange{3, 9});
    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ReturnStatement{pref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(has_lower_code(result, ir::core::diag::kUnloweredFieldProjection));
}

// ==========================================================================
// Auto-verify wiring: lower_ahfl_to_core runs the Core-IR verifier on a
// lowering-clean candidate and merges verifier Errors; a lowering that already
// errored is NOT re-verified (no duplicate core.verify.* noise).
// ==========================================================================

namespace {
[[nodiscard]] bool has_verify_code(const ir::core::CoreLowerResult &r) {
    for (const auto &d : r.diagnostics) {
        if (d.code.rfind("core.verify.", 0) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("lowering-clean but verifier-rejected program merges verify errors and is not executable") {
    // An agent whose INPUT type is an enum (not a struct). The lowerer does not
    // enforce Sema's schema boundary, so lowering itself is clean; the auto-wired
    // verifier must reject it (input shell must be a struct) and the merged
    // result must be non-executable.
    ir::AhflIr program;

    ir::EnumDecl mode;
    mode.name = "Mode";
    mode.symbol_ref.kind = ir::SymbolRefKind::Type;
    mode.symbol_ref.canonical_name = "app::Mode";
    mode.symbol_ref.id = 70;
    mode.variants.push_back(ir::EnumVariantDecl{"A", ir::EnumVariantPayloadKind::Unit, {}, {}, {}});
    mode.variants.push_back(ir::EnumVariantDecl{"B", ir::EnumVariantPayloadKind::Unit, {}, {}, {}});
    program.declarations.emplace_back(std::move(mode));

    ir::StructDecl reply;
    reply.name = "Reply";
    reply.symbol_ref.kind = ir::SymbolRefKind::Type;
    reply.symbol_ref.canonical_name = "app::Reply";
    reply.symbol_ref.id = 71;
    program.declarations.emplace_back(std::move(reply));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Enum; // <-- not a struct
    agent.input_type_ref.canonical_name = "app::Mode";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::Reply";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "S";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(has_verify_code(result)); // the verifier's TYPED_SHELL_INVALID merged in
}

TEST_CASE("a lowering-error program is NOT re-verified (no duplicate core.verify.* noise)") {
    // A flow that calls an unresolved capability -> lowering Error. Because the
    // program is already non-executable, the verifier must NOT run, so no
    // core.verify.* diagnostics appear.
    ir::AhflIr program;

    ir::StructDecl req;
    req.name = "Req";
    req.symbol_ref.kind = ir::SymbolRefKind::Type;
    req.symbol_ref.canonical_name = "app::Req";
    req.symbol_ref.id = 80;
    program.declarations.emplace_back(std::move(req));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::Req";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::Req";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // return Ghost(); -- unresolved capability callee
    ir::CallExpr call;
    call.callee = "Ghost";
    call.callee_ref.kind = ir::SymbolRefKind::Capability;
    call.callee_ref.canonical_name = "app::Ghost";
    ir::ExprRef cref = program.expr_arena.make(std::move(call), SourceRange{4, 9});
    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{cref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(has_verify_code(result) == false); // verifier skipped on a partial program
}

TEST_CASE("enum struct-payload construct with a missing slot fails closed in the verifier (2b)") {
    // Hand-built AHFL-IR: an Open{ id } literal with NO owner (as if a
    // deserialized IR dropped the materialized default). The Core lowerer must
    // NOT re-materialize; the verifier reports the missing struct-payload slot.
    ir::AhflIr program;

    ir::EnumDecl ticket;
    ticket.name = "Ticket";
    ticket.symbol_ref.kind = ir::SymbolRefKind::Type;
    ticket.symbol_ref.canonical_name = "app::Ticket";
    ticket.symbol_ref.id = 90;
    ir::EnumVariantDecl open;
    open.name = "Open";
    open.payload_kind = ir::EnumVariantPayloadKind::Struct;
    ir::EnumVariantFieldDecl f_id;
    f_id.name = "id";
    f_id.type_ref.kind = ir::TypeRefKind::Int;
    ir::EnumVariantFieldDecl f_owner;
    f_owner.name = "owner";
    f_owner.type_ref.kind = ir::TypeRefKind::String;
    open.fields.push_back(std::move(f_id));
    open.fields.push_back(std::move(f_owner));
    ir::MemberTypeTemplateNode id_template;
    id_template.kind = ir::MemberTypeTemplateKind::Concrete;
    id_template.type_ref.kind = ir::TypeRefKind::Int;
    ticket.member_type_templates.push_back(std::move(id_template));
    ir::MemberTypeTemplateNode owner_template;
    owner_template.kind = ir::MemberTypeTemplateKind::Concrete;
    owner_template.type_ref.kind = ir::TypeRefKind::String;
    ticket.member_type_templates.push_back(std::move(owner_template));
    open.payload_type_template_roots = {0, 1};
    ticket.variants.push_back(std::move(open));
    program.declarations.emplace_back(std::move(ticket));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Mv");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // let t = Ticket::Open { id: 1 };  (owner deliberately omitted, not materialized)
    ir::StructLiteralExpr lit;
    lit.is_enum_variant = true;
    lit.enum_name = "app::Ticket";
    lit.variant_name = "Open";
    lit.type_name = "app::Ticket::Open";
    ir::ExprRef id_val = program.expr_arena.make(ir::IntegerLiteralExpr{"1"}, std::nullopt, int_type());
    lit.fields.push_back(ir::StructFieldInit{"id", id_val});
    ir::ExprRef lit_ref =
        program.expr_arena.make(std::move(lit), SourceRange{5, 9}, enum_type("app::Ticket"));
    auto let = std::make_unique<ir::Statement>();
    ir::LetStatement let_stmt;
    let_stmt.name = "t";
    // Declared annotation is the initializer's resolved enum type.
    let_stmt.type_ref = enum_type("app::Ticket");
    let_stmt.initializer = lit_ref;
    let->node = std::move(let_stmt);
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(let));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result)); // missing struct-payload slot caught by verifier
}

// ---------------------------------------------------------------------------
// RFC 0026 P4-B: a source `let x: T = <init>` records the local's value type.
// AHFL `let` is ASSIGNABLE / SUBTYPE (not structural equality): the local
// carries the initializer's actual value type, which is a valid subtype of the
// declared annotation. The lowerer must NOT reject a legal widening such as
// `let x: Int(0,2) = 1`. The declared-vs-initializer coercion gate (an explicit
// CoreCoerceExpr) is deferred to its own slice.
// ---------------------------------------------------------------------------

// Build a single-handler flow whose only statement is `let <name>: <decl> =
// <init literal typed `init`>`, then a `return`. `decl` and `init` are the
// declared annotation and the initializer's resolved type.
ir::AhflIr make_let_annotation_program(const std::string &name, ir::TypeRef decl_type,
                                       ir::TypeRef init_type,
                                       bool with_int_widen = false) {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "La");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";
    ir::ExprRef init =
        program.expr_arena.make(ir::IntegerLiteralExpr{"0"}, std::nullopt, std::move(init_type));
    ir::LetStatement let;
    let.name = name;
    let.type_ref = std::move(decl_type);
    let.initializer = init;
    if (with_int_widen) {
        ir::AdjustmentPlan plan;
        plan.source = ir::clone_type_ref(init->resolved_type);
        plan.target = ir::clone_type_ref(let.type_ref);
        plan.root = 0;
        ir::AdjustmentNode node;
        node.source = ir::clone_type_ref(plan.source);
        node.target = ir::clone_type_ref(plan.target);
        node.ops.push_back(ir::AdjustmentOp{.kind = ir::AdjustmentOpKind::IntWiden});
        plan.nodes.push_back(std::move(node));
        let.adjustment = std::move(plan);
    }
    auto s = std::make_unique<ir::Statement>();
    s->node = std::move(let);
    handler.body.statements.push_back(std::move(s));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));
    return program;
}

TEST_CASE("P4-B P0-3: a let whose declared type matches the initializer lowers clean") {
    const auto program = make_let_annotation_program("x", int_type(), int_type());
    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    CHECK(result.ok());
}

TEST_CASE("P4-B P0-3: an inferred-style let (declared == initializer nominal) lowers clean") {
    const auto program =
        make_let_annotation_program("x", enum_type("std::option::Option", int_type()),
                                    enum_type("std::option::Option", int_type()));
    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    CHECK(result.ok());
}

TEST_CASE("P4-B P0-3: a let whose declared type is a SUPERTYPE of the initializer lowers clean") {
    // AHFL `let` semantics are ASSIGNABLE / SUBTYPE, not structural equality:
    // `let x: Int(0,2) = 1` is legal (the literal is Int(1,1), a subtype of the
    // declared Int(0,2)). The lowerer must NOT reject this via an exact-equality
    // check. Sema's persisted IntWiden witness makes the boundary explicit, and
    // Core lowering must bind a fresh coercion result rather than re-label the
    // literal's existing SSA value.
    ir::TypeRef declared;
    declared.kind = ir::TypeRefKind::BoundedInt;
    declared.int_bounds = std::pair<std::int64_t, std::int64_t>{0, 2};
    ir::TypeRef init;
    init.kind = ir::TypeRefKind::BoundedInt;
    init.int_bounds = std::pair<std::int64_t, std::int64_t>{1, 1};
    const auto program = make_let_annotation_program("x", std::move(declared), std::move(init), true);
    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    REQUIRE(flow.storage.coercion_plans.size() == 1);
    REQUIRE(flow.storage.coercion_plans[0].ops.size() == 1);
    CHECK(flow.storage.coercion_plans[0].ops[0].kind == ir::core::CoreCoercionOpKind::IntWiden);
    REQUIRE(flow.storage.exprs.size() == 2);
    const auto *coerce = std::get_if<ir::core::CoreCoerceExpr>(&flow.storage.exprs[1].node);
    REQUIRE(coerce != nullptr);
    CHECK(coerce->operand.value == 0);
    REQUIRE(flow.states.size() == 1);
    REQUIRE(flow.states[0].body.statements.size() == 2);
    const auto &coerce_let =
        std::get<ir::core::CoreLetStmt>(flow.states[0].body.statements[1].node);
    CHECK(coerce_let.result.value != coerce->operand.value);
}

// ---------------------------------------------------------------------------
// (3)-3b typed pattern identity bridge: after lowering a real match program to
// AHFL-IR, each variant pattern carries its owner-enum SymbolRef + declaration-
// stable variant name (not just the `path` spelling), and EVERY pattern carries
// the matched-enum fact from the scrutinee's type. This is what lets (3)-3c
// resolve variant identity by symbol and turn a literal `none` into Option::None.
// ---------------------------------------------------------------------------
namespace {

// The first MatchExpr reachable in the AHFL-IR expression arena, or nullptr.
const ir::MatchExpr *find_match_expr(const ir::AhflIr &program) {
    for (const ir::Expr *expr : program.all_exprs()) {
        if (expr == nullptr) {
            continue;
        }
        if (const auto *m = std::get_if<ir::MatchExpr>(&expr->node)) {
            return m;
        }
    }
    return nullptr;
}

const ir::VariantPattern *variant_of(const ir::MatchPattern &pattern) {
    return std::get_if<ir::VariantPattern>(&pattern.node);
}

} // namespace

TEST_CASE("(3)-3b: variant patterns carry typed owner-enum identity + variant name") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_identity", source);
    REQUIRE(program.has_value());
    const ir::MatchExpr *match = find_match_expr(*program);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);

    // Arm 0: Some(x) — tuple-payload variant. Owner enum resolved by symbol,
    // variant name declaration-stable, matched-enum fact present.
    const ir::VariantPattern *some = variant_of(match->arms[0].pattern);
    REQUIRE(some != nullptr);
    CHECK(some->variant_name == "Some");
    CHECK(some->owner_enum.kind == ir::SymbolRefKind::Type);
    CHECK(some->owner_enum.canonical_name.find("Maybe") != std::string::npos);
    CHECK(match->arms[0].pattern.matched_enum.canonical_name.find("Maybe") != std::string::npos);

    // Arm 1: None — unit variant. Same typed identity.
    const ir::VariantPattern *none = variant_of(match->arms[1].pattern);
    REQUIRE(none != nullptr);
    CHECK(none->variant_name == "None");
    CHECK(none->owner_enum.kind == ir::SymbolRefKind::Type);
    CHECK(none->owner_enum.canonical_name.find("Maybe") != std::string::npos);
}

TEST_CASE("(3)-3b: a non-variant (wildcard) arm still carries the matched-enum fact") {
    // A wildcard / literal arm is NOT a variant, so it has no owner_enum of its
    // own; its enum identity comes from the scrutinee's matched type. The
    // matched_enum fact must be set on such a pattern so (3)-3c can still resolve
    // the scrutinee enum (the same mechanism a literal `none` relies on).
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Colour { Red, Green, Blue, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let c: Colour = Colour::Red;
        let r: Int = match c {
            Red => 1,
            _ => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_wildcard_identity", source);
    REQUIRE(program.has_value());
    const ir::MatchExpr *match = find_match_expr(*program);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    // Arm 1 is the wildcard `_` — a non-variant pattern that must still know its
    // matched enum by symbol identity.
    const ir::MatchPattern &wild = match->arms[1].pattern;
    CHECK(std::holds_alternative<ir::WildcardPattern>(wild.node));
    CHECK(wild.matched_enum.kind == ir::SymbolRefKind::Type);
    CHECK(wild.matched_enum.canonical_name.find("Colour") != std::string::npos);
}

TEST_CASE("(3)-3b: variant pattern identity survives IR JSON round-trip byte-exact") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_roundtrip", source);
    REQUIRE(program.has_value());

    std::ostringstream first;
    ahfl::print_program_ir_json(*program, first);
    const std::string json1 = first.str();

    const auto parsed = ahfl::parse_program_ir_json(json1);
    REQUIRE(parsed.has_value());

    std::ostringstream second;
    ahfl::print_program_ir_json(*parsed, second);
    const std::string json2 = second.str();

    // Byte-exact: print -> parse -> print reproduces the JSON verbatim, so the
    // owner_enum SymbolRef + variant_name + matched_enum fields survive.
    CHECK(json1 == json2);

    // And the parsed-back program still carries typed variant identity.
    const ir::MatchExpr *match = find_match_expr(*parsed);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    const ir::VariantPattern *some = variant_of(match->arms[0].pattern);
    REQUIRE(some != nullptr);
    CHECK(some->variant_name == "Some");
    CHECK(some->owner_enum.canonical_name.find("Maybe") != std::string::npos);
    CHECK(match->arms[0].pattern.matched_enum.canonical_name.find("Maybe") != std::string::npos);

    // RFC 0026 P4-B: the resolved matched_type_ref bridge is populated and
    // survives the round-trip. The root arm matches against the enum `Maybe`, so
    // its matched_type_ref is a nominal Enum whose nominal_ref agrees with
    // matched_enum (the BackendReady consistency the verifier locks).
    const ir::TypeRef &some_mt = match->arms[0].pattern.matched_type_ref;
    CHECK(some_mt.kind == ir::TypeRefKind::Enum);
    CHECK(some_mt.canonical_name.find("Maybe") != std::string::npos);
    CHECK(some_mt.nominal_ref.canonical_name ==
          match->arms[0].pattern.matched_enum.canonical_name);
}

// ---------------------------------------------------------------------------
// (3)-3b forward-fix P0-1 / P0-2: the typed pattern identity bridge must FAIL
// CLOSED when the typed pattern index / child is missing or misaligned. These
// drive the real lowerer with a surgically-corrupted TypedProgram and assert it
// throws rather than silently degrading to AST-derived identity.
// ---------------------------------------------------------------------------
namespace {

struct FrontendState {
    ahfl::ParseResult parse;
    std::optional<ahfl::ResolveResult> resolve;
    std::optional<ahfl::TypeCheckResult> typecheck;
    [[nodiscard]] bool ok() const {
        return typecheck.has_value() && !typecheck->has_errors();
    }
};

// Parse + resolve + typecheck a source, RETAINING all state so a test can mutate
// the TypedProgram before lowering. Returns nullopt if any stage errors.
std::optional<FrontendState> frontend_state(const std::string &label, const std::string &source) {
    FrontendState st;
    const Frontend frontend;
    st.parse = frontend.parse_text(label + ".ahfl", source);
    if (st.parse.has_errors() || st.parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    st.resolve = resolver.resolve(*st.parse.program);
    if (st.resolve->has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    st.typecheck = checker.check(*st.parse.program, *st.resolve);
    if (st.typecheck->has_errors()) {
        return std::nullopt;
    }
    return st;
}

// Index of the first Match expression in the typed program (UINT32_MAX if none).
std::uint32_t find_typed_match(const ahfl::TypedProgram &tp) {
    for (std::uint32_t i = 0; i < tp.expressions.size(); ++i) {
        if (tp.expressions[i].kind == ahfl::ast::ExprSyntaxKind::Match) {
            return i;
        }
    }
    return UINT32_MAX;
}

const std::string kMatchProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";

} // namespace

TEST_CASE("(3)-3b P0-1: match with EMPTY pattern-index list fails closed (no AST fallback)") {
    auto st = frontend_state("empty_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    // Corrupt: drop the persisted arm pattern indexes entirely.
    st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with a SHORT pattern-index list fails closed") {
    auto st = frontend_state("short_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    REQUIRE(idxs.size() == 2);
    idxs.pop_back(); // one fewer than the 2 arms
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with an EXTRA pattern-index entry fails closed") {
    auto st = frontend_state("long_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    idxs.push_back(idxs.front()); // one more than the 2 arms
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with an OUT-OF-RANGE pattern index fails closed") {
    auto st = frontend_state("oor_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    REQUIRE_FALSE(idxs.empty());
    idxs[0] = 999999; // out of TypedProgram::patterns range
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-2: deleting a nested TypedPatternChild fails closed") {
    // Some(x) is a tuple-payload variant with one child (the `x` binding). Drop
    // the child so the recursive lower_pattern cannot find its typed sub-pattern.
    auto st = frontend_state("del_child", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    // Find the Some variant pattern (has >= 1 child) and clear its children.
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant && !pat.children.empty()) {
            pat.children.clear();
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: AST variant vs typed Wildcard kind mismatch fails closed") {
    // Corrupt a variant arm's TypedPattern to Wildcard: the AST node is still a
    // VariantPattern, so the AST-vs-typed kind check must fail closed (never
    // lower an AST variant shape with a non-variant typed identity).
    auto st = frontend_state("kind_mismatch_v", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant) {
            pat.kind = ahfl::TypedPatternKind::Wildcard;
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: AST variant vs typed Literal kind mismatch fails closed") {
    // A different mismatch: an AST VariantPattern resolved (corrupted) to a
    // Literal typed kind. This is NOT the legal binding->unit-variant divergence.
    auto st = frontend_state("kind_mismatch_l", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant) {
            pat.kind = ahfl::TypedPatternKind::Literal;
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// --- (3)-3b / P4-B: matched_type_ref bridge fidelity + BindingPattern cross-check ---

TEST_CASE("P4-B: a primitive-payload binding carries its resolved primitive matched type") {
    // Some(x) binds x : Int. The bridge must record a RESOLVED, non-nominal
    // matched_type_ref on the `x` binding node (not just the root enum), so a
    // backend types the binding from it. Drives the real typed-HIR lowering.
    auto st = frontend_state("prim_child_bridge", kMatchProgram);
    REQUIRE(st.has_value());
    const auto program = ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program);
    // BackendReady is now a hard gate on matched_type_ref for EVERY node.
    CHECK_FALSE(
        ir::verify_ir_program(program, ir::IrVerificationMode::BackendReady).has_errors());
    // Locate the Some(x) arm's inner binding pattern and assert its matched type
    // is the resolved primitive Int (non-nominal, no stray matched_enum).
    const ir::MatchExpr *match = nullptr;
    for (const ir::Expr *expr : program.expr_arena.span()) {
        if (expr != nullptr) {
            if (auto *m = std::get_if<ir::MatchExpr>(&expr->node)) {
                match = m;
                break;
            }
        }
    }
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    const auto *variant = std::get_if<ir::VariantPattern>(&match->arms[0].pattern.node);
    REQUIRE(variant != nullptr);
    REQUIRE(variant->subpatterns.size() == 1);
    REQUIRE(variant->subpatterns[0] != nullptr);
    const ir::MatchPattern &binding = *variant->subpatterns[0];
    CHECK(binding.matched_type_ref.kind == ir::TypeRefKind::Int);
    CHECK(binding.matched_type_ref.nominal_ref.kind == ir::SymbolRefKind::Unknown);
    CHECK(binding.matched_enum.kind == ir::SymbolRefKind::Unknown);
}

TEST_CASE("P4-B: a binding whose recorded type disagrees with matched_type fails closed") {
    // Corrupt the `x` binding's TypedPattern so its matched_type no longer equals
    // the type recorded for the binding in `bindings` (here: drop matched_type to
    // null while the binding record keeps its Int type). The bridge cross-check
    // must fail closed rather than silently adopt a mismatched type downstream.
    auto st = frontend_state("binding_bridge_mismatch", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Binding && !pat.bindings.empty() &&
            pat.bindings.front().type != nullptr) {
            // matched_type disagrees with the still-present binding record type.
            pat.matched_type = nullptr;
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)),
                    std::logic_error);
}

TEST_CASE("P4-B: a binding recorded more than once in the TypedPattern fails closed") {
    // Duplicate the binding record for the `x` binding: the cross-check requires
    // the binding NAME to resolve to exactly one TypedPatternBinding.
    auto st = frontend_state("binding_bridge_dup", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Binding && !pat.bindings.empty()) {
            pat.bindings.push_back(pat.bindings.front()); // same name twice
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)),
                    std::logic_error);
}


// A match with a GUARDED first arm (Some(x) if <guard> => ...). Used to prove a
// deleted guard child fails closed.
const std::string kGuardedMatchProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) if x > 0 => x,
            Some(y) => y,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";

TEST_CASE("(3)-3b P0-1: deleting a real MatchArmGuard child fails closed") {
    auto st = frontend_state("del_guard", kGuardedMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    const auto mi = find_typed_match(tp);
    REQUIRE(mi != UINT32_MAX);
    // Drop the FIRST MatchArmGuard child: the AST arm still has an `if` guard, so
    // the guard-existence check must reject the mismatch.
    auto &children = tp.expressions[mi].children;
    bool removed = false;
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->role == ahfl::TypedExprChildRole::MatchArmGuard) {
            children.erase(it);
            removed = true;
            break;
        }
    }
    REQUIRE(removed);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: injecting a guard child on a guardless arm fails closed") {
    auto st = frontend_state("inject_guard", kMatchProgram); // no guards in source
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    const auto mi = find_typed_match(tp);
    REQUIRE(mi != UINT32_MAX);
    // Inject a spurious MatchArmGuard child before the first body child, reusing
    // an existing body child's expr index. The AST arm has no guard, so this must
    // be rejected.
    auto &children = tp.expressions[mi].children;
    std::uint32_t some_body_expr = UINT32_MAX;
    for (const auto &c : children) {
        if (c.role == ahfl::TypedExprChildRole::MatchArmBody) {
            some_body_expr = c.expr_index;
            break;
        }
    }
    REQUIRE(some_body_expr != UINT32_MAX);
    ahfl::TypedExprChild guard_child;
    guard_child.role = ahfl::TypedExprChildRole::MatchArmGuard;
    guard_child.expr_index = some_body_expr;
    children.insert(children.begin(), guard_child);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// --- (3)-3b round-3 P0-2: if-let must resolve its AST pattern or fail closed ---

const std::string kIfLetProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        if let Some(x) = mm {
            ctx.total = x;
        }
        return Resp { id: ctx.total };
    }
}
)AHFL";

TEST_CASE("(3)-3b P0-2: an if-let whose AST lookup fails does NOT degrade to wildcard") {
    auto st = frontend_state("iflet_lookup", kIfLetProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    // Corrupt the if-let TypedStatement's range so find_ast_if_let_stmt (which
    // matches by range) can no longer locate the AST statement. The lowerer must
    // throw rather than lower the pattern as a wildcard.
    bool corrupted = false;
    for (auto &stmt : tp.statements) {
        if (stmt.kind == ahfl::TypedStmtKind::IfLet) {
            stmt.range = ahfl::SourceRange{999990, 999999};
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// ---------------------------------------------------------------------------
// (3)-3c CoreMatchStmt lowering: a real match / if-let program lowers into a
// structured CoreMatchStmt (scrutinee + arms with typed patterns + fallback),
// resolving variant identity by owner_enum symbol. The auto-wired Core verifier
// accepting the program (result.ok()) already proves the (3)-2 invariants hold.
// ---------------------------------------------------------------------------
namespace {

// The first CoreMatchStmt reachable in a flow state body (top-level only).
const ir::core::CoreMatchStmt *find_core_match(const ir::core::CoreProgram &program) {
    for (const auto &flow : program.flows) {
        for (const auto &state : flow.states) {
            for (const auto &stmt : state.body.statements) {
                if (const auto *m = std::get_if<ir::core::CoreMatchStmt>(&stmt.node)) {
                    return m;
                }
            }
        }
    }
    return nullptr;
}

bool region_ends_with_trap(const ir::core::CoreRegion *region) {
    if (region == nullptr || region->statements.empty()) {
        return false;
    }
    return std::holds_alternative<ir::core::CoreTrapStmt>(region->statements.back().node);
}

} // namespace

TEST_CASE("(3)-3c: an expression match lowers into a CoreMatchStmt with typed arms + trap fallback") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_match_expr", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    // Expression match: defines a result value in the parent scope.
    CHECK(m->has_result);
    REQUIRE(m->arms.size() == 2);
    // Fallback is a mandatory trap (non-exhaustive by construction).
    CHECK(region_ends_with_trap(m->fallback_region.get()));

    // Arm 0: Some(x) - a variant pattern with one tuple sub-pattern binding, one
    // arm binding (x). Arm 1: None - a unit variant, no bindings.
    const auto &pats = result.program.flows[0].storage.patterns;
    const auto &arm0 = m->arms[0];
    REQUIRE(arm0.pattern.value < pats.size());
    const auto *v0 = std::get_if<ir::core::CoreVariantPat>(&pats[arm0.pattern.value].node);
    REQUIRE(v0 != nullptr);
    CHECK(v0->tuple_subpatterns.size() == 1);
    CHECK(arm0.bindings.size() == 1); // x
    const auto &arm1 = m->arms[1];
    REQUIRE(arm1.pattern.value < pats.size());
    const auto *v1 = std::get_if<ir::core::CoreVariantPat>(&pats[arm1.pattern.value].node);
    REQUIRE(v1 != nullptr);
    CHECK(v1->tuple_subpatterns.empty());
    CHECK(arm1.bindings.empty());
    // The two arms name distinct variants of the same enum.
    CHECK(v0->owner_enum.value == v1->owner_enum.value);
    CHECK(v0->variant.value != v1->variant.value);
}

TEST_CASE("(3)-3c: an if-let lowers into a statement CoreMatchStmt with an else fallback") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let out: Int = 0;
        if let Some(x) = mm {
            ctx.total = x;
        } else {
            ctx.total = 0;
        }
        return Resp { id: ctx.total };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_if_let", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    // Statement match: no result value.
    CHECK_FALSE(m->has_result);
    REQUIRE(m->arms.size() == 1);
    CHECK(m->arms[0].bindings.size() == 1); // x
    // The fallback is the `else` block (a store), NOT a trap.
    REQUIRE(m->fallback_region);
    CHECK_FALSE(region_ends_with_trap(m->fallback_region.get()));
}

// (3)-3c forward-fix P0-1: an if-let's ELSE branch is lexically scoped exactly
// like its then branch — a branch-local `let` that SHADOWS an outer local must
// NOT leak past the if-let. Previously the else used a bare lower_block with no
// scope snapshot/restore, so the else-local shadow overwrote scope_ and a later
// reference to the OUTER local resolved to the else-local's value id. That value
// id is defined only inside the else region (the verifier's per-branch `visible`
// set never propagates it to the parent), so the leak surfaces as a
// use-before-def and the program is non-executable. The fix restores the outer
// scope around BOTH branches, so the trailing reference resolves to the OUTER
// binding and the program lowers clean.
TEST_CASE("(3)-3c FF P0-1: an if-let else-branch shadow does not leak past the if-let") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; done: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let x: Int = input.id;
        if let Some(y) = mm {
            ctx.total = y;
        } else {
            let x: Int = 999;
            ctx.total = x;
        }
        ctx.done = x;
        return Resp { id: ctx.done };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_iflet_else_shadow", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // With the leak, `ctx.done = x` would use the else-local value id (not
    // visible in the parent) -> use-before-def -> not ok / not executable.
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // Structural: `ctx.done = x` after the if-let stores the OUTER `x` — a value
    // defined at the TOP LEVEL before the match — never a value defined only
    // inside the else region (which would be the leaked shadow). (is_executable
    // already excludes the leak via use-before-def; this pins WHICH value flows.)
    const ir::core::CoreFlowState *state = nullptr;
    for (const auto &flow : result.program.flows) {
        for (const auto &st : flow.states) {
            for (const auto &stmt : st.body.statements) {
                if (std::holds_alternative<ir::core::CoreMatchStmt>(stmt.node)) {
                    state = &st;
                }
            }
        }
    }
    REQUIRE(state != nullptr);
    const auto &stmts = state->body.statements;

    // Value ids defined at the top level BEFORE the match (the parent scope).
    std::unordered_set<std::uint32_t> pre_match_defs;
    const ir::core::CoreStoreStmt *done_store = nullptr;
    const ir::core::CoreMatchStmt *iflet = nullptr;
    for (const auto &stmt : stmts) {
        if (const auto *m = std::get_if<ir::core::CoreMatchStmt>(&stmt.node)) {
            iflet = m;
            continue;
        }
        if (iflet == nullptr) {
            if (const auto *l = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                pre_match_defs.insert(l->result.value);
            } else if (const auto *c =
                           std::get_if<ir::core::CoreCapabilityCallStmt>(&stmt.node)) {
                pre_match_defs.insert(c->result.value);
            }
        } else if (const auto *s = std::get_if<ir::core::CoreStoreStmt>(&stmt.node)) {
            done_store = s; // first store AFTER the match is `ctx.done = x`
            break;
        }
    }
    REQUIRE(iflet != nullptr);
    REQUIRE(done_store != nullptr);
    // The stored value is an outer, pre-match binding — not the else-local shadow.
    CHECK(pre_match_defs.count(done_store->value.value) == 1);
}

// --- (3)-3c forward-fix: payload binding types, none->variant, path fallthrough ---

TEST_CASE("(3)-3c FF P0-1: a struct-payload binding carries its type so u.field resolves") {
    // enum with a struct-typed payload; the arm binds `u: User` and projects
    // `u.id`. Without the binding's typed CoreTypeId this fails with
    // UNLOWERED_FIELD_PROJECTION.
    const std::string source = R"AHFL(
module m;

struct User { id: Int; }
struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum MaybeUser { Some(User), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mu: MaybeUser = MaybeUser::Some(User { id: input.id });
        let r: Int = match mu {
            Some(u) => u.id,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_struct_binding", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // u.id member projection resolved via the binding type
    CHECK(result.is_executable);
    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    REQUIRE(m->arms.size() == 2);
    // RFC 0026 P4-B: the Some(u) arm's binding value carries a nominal value type
    // in the owning flow's value_types table (the binding_type field is gone).
    REQUIRE(m->arms[0].bindings.size() == 1);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    const auto bind_value = m->arms[0].bindings[0].value.value;
    REQUIRE(bind_value < flow.storage.value_types.size());
    const auto vt = flow.storage.value_types[bind_value];
    REQUIRE(vt.value < result.program.value_types.size());
    CHECK(std::holds_alternative<ir::core::CoreVtNominal>(
        result.program.value_types[vt.value].node));
}

TEST_CASE("(P4-B) e2e: a lowering-clean flow body has a DENSE value_types table") {
    // The dense per-body value_types table (RFC 0026 P4-B) is the execution-layer
    // record of every CoreValueId's logical type. Drive a mixed body (primitive
    // let, capability-call result, member projection, nominal constructor) through
    // the REAL front end and assert the density invariant the verifier enforces:
    // value_types.size() == value_count, every slot in range + non-Never, and a
    // couple of representative slots carry the exact interned shape.
    const std::string source = R"AHFL(
module m;

struct Req { amount: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { total: Int; }

capability Scale(factor: Int) -> Int;

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [Scale];
}

flow for A {
    state Done {
        let base: Int = input.amount;
        let doubled: Int = Scale(base);
        return Resp { total: doubled };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_dense_value_types", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    // Density: one recorded logical type per allocated value id (the exact
    // invariant core.verify.VALUE_TYPES_SIZE_MISMATCH guards).
    CHECK(flow.storage.value_types.size() == flow.storage.value_count);
    REQUIRE(flow.storage.value_count > 0);

    // Every slot is a valid, in-range, non-Never interned value type.
    bool saw_int = false;
    for (const auto id : flow.storage.value_types) {
        REQUIRE(id.value != ir::core::CoreValueTypeId::kInvalid);
        REQUIRE(id.value < result.program.value_types.size());
        const auto &node = result.program.value_types[id.value].node;
        CHECK_FALSE(std::holds_alternative<ir::core::CoreVtNever>(node));
        if (std::holds_alternative<ir::core::CoreVtInt>(node)) {
            saw_int = true;
        }
    }
    // `let base: Int` and the capability result `Scale(base): Int` both intern to Int.
    CHECK(saw_int);
}

TEST_CASE("(3)-3c FF P0-3: a match arm whose both if-branches terminate needs no trailing yield") {
    // An if-let then-block whose only statement is an if/else where BOTH branches
    // goto must NOT get a trailing Yield (that would be a stmt-after-terminator).
    // result.ok() proves the auto-wired verifier accepts it (no STMT_AFTER_TERMINATOR).
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Start, Left, Right];
    initial: Start;
    final: [Left, Right];
    capabilities: [];

    transition Start -> Left;
    transition Start -> Right;
}

flow for A {
    state Start {
        let mm: Maybe = Maybe::Some(input.id);
        if let Some(x) = mm {
            if x > 0 { goto Left; } else { goto Right; }
        } else {
            goto Right;
        }
    }
    state Left { return Resp { id: 1 }; }
    state Right { return Resp { id: 2 }; }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_both_terminate", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // no STMT_AFTER_TERMINATOR: the then-arm did NOT get a trailing yield
    CHECK(result.is_executable);
}

TEST_CASE("(3)-3c FF P1: a variant pattern whose source shape disagrees with metadata fails closed") {
    // Lower a real match to AHFL-IR, then corrupt the `Some(x)` arm's variant
    // pattern kind from Tuple to Unit (a shape that disagrees with the declared
    // tuple payload). The Core lowerer must fail closed, not normalize it.
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    auto ahfl_ir = lower_source_to_ahfl_ir("core_shape_mismatch", source);
    REQUIRE(ahfl_ir.has_value());
    // Find the MatchExpr (non-const) and corrupt the first variant arm's kind.
    bool corrupted = false;
    for (ir::Expr *expr : ahfl_ir->all_exprs()) {
        if (expr == nullptr) {
            continue;
        }
        if (auto *mtch = std::get_if<ir::MatchExpr>(&expr->node)) {
            for (auto &arm : mtch->arms) {
                if (auto *v = std::get_if<ir::VariantPattern>(&arm.pattern.node);
                    v != nullptr && v->kind == ir::VariantPatternKind::Tuple) {
                    v->kind = ir::VariantPatternKind::Unit; // disagrees with metadata
                    corrupted = true;
                    break;
                }
            }
        }
        if (corrupted) {
            break;
        }
    }
    REQUIRE(corrupted);
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK_FALSE(result.ok()); // shape mismatch is fail-closed, not normalized
}

// --- KR6.4 workflow lowering (RFC 0026 P3 workflow vertical slice) ---

namespace {
// A self-contained multi-node DAG workflow: two agents, a workflow whose second
// node depends on the first and reads its output, and a return that reads the
// last node. Mirrors the shape of examples/execution-demo (input root, node.field
// projection, struct-literal node input, `after` deps, bare-node return).
const std::string kWorkflowSource = R"AHFL(
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

const ir::core::CoreWorkflowDecl *find_workflow(const ir::core::CoreProgram &program,
                                                const std::string &name) {
    for (const auto &wf : program.workflows) {
        // The lowered name is the canonical name (e.g. "wf::Pipe"); match by the
        // trailing simple name so the test is module-path agnostic.
        if (wf.name == name || (wf.name.size() > name.size() &&
                                wf.name.compare(wf.name.size() - name.size() - 2, 2, "::") == 0 &&
                                wf.name.compare(wf.name.size() - name.size(), name.size(), name) ==
                                    0)) {
            return &wf;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("KR6.4 workflow: a multi-node DAG lowers into a CoreWorkflowDecl and verifies") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("wf_pipe", kWorkflowSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreWorkflowDecl *wf = find_workflow(result.program, "Pipe");
    REQUIRE(wf != nullptr);
    // Typed shell resolved to structs.
    CHECK(wf->input_type.value != ir::core::CoreTypeId::kInvalid);
    CHECK(wf->output_type.value != ir::core::CoreTypeId::kInvalid);
    // Two nodes, ids == declaration index.
    REQUIRE(wf->nodes.size() == 2);
    CHECK(wf->nodes[0].id.value == 0u);
    CHECK(wf->nodes[1].id.value == 1u);
    CHECK(wf->nodes[0].node_name == "first");
    CHECK(wf->nodes[1].node_name == "second");
    // `second after [first]` resolved to a typed node id (not a name string).
    REQUIRE(wf->nodes[1].after.size() == 1);
    CHECK(wf->nodes[1].after[0].value == 0u);
    CHECK(wf->nodes[0].after.empty());
    // Every node has an ANF input region ending in a value-yield; return region too.
    REQUIRE(wf->nodes[0].input_region);
    REQUIRE(wf->nodes[1].input_region);
    REQUIRE(wf->return_region);
    const auto ends_in_value_yield = [](const ir::core::CoreRegion &r) {
        if (r.statements.empty()) {
            return false;
        }
        const auto *y = std::get_if<ir::core::CoreYieldStmt>(&r.statements.back().node);
        return y != nullptr && y->has_value;
    };
    CHECK(ends_in_value_yield(*wf->nodes[0].input_region));
    CHECK(ends_in_value_yield(*wf->nodes[1].input_region));
    CHECK(ends_in_value_yield(*wf->return_region));

    // The node-output reads resolved to WorkflowNodeOutput path roots carrying the
    // typed producer node id: `first.total` in node `second`'s input reads node 0
    // (with a resolved `.total` projection); `return: second` reads node 1 (bare).
    bool saw_first_total = false;
    bool saw_return_second = false;
    for (const auto &expr : wf->storage.exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root != ir::core::CorePathRoot::WorkflowNodeOutput) {
                continue;
            }
            if (p->workflow_node.value == 0u && !p->projection.empty()) {
                saw_first_total = true;
                CHECK(p->projection_resolved); // `.total` resolved to a typed step
            }
            if (p->workflow_node.value == 1u) {
                saw_return_second = true; // bare `second` node reference
            }
        }
    }
    CHECK(saw_first_total);
    CHECK(saw_return_second);

    // Each workflow node is LINKED to a concrete agent instance: its
    // target_instance resolves to a CoreAgentInstance whose base + output type
    // match the nominal target agent. (The instance registry + invocation link
    // pass consumed the frontend's ir::InstanceDecls; nothing re-mangles here.)
    for (const auto &node : wf->nodes) {
        REQUIRE(node.target_instance.value != ir::core::CoreInstanceId::kInvalid);
        REQUIRE(node.target_instance.value < result.program.instances.size());
        const auto &inst = result.program.instances[node.target_instance.value];
        const auto *ai = std::get_if<ir::core::CoreAgentInstance>(&inst.payload);
        REQUIRE(ai != nullptr);
        CHECK(ai->base.value < result.program.agents.size());
        // instance_key came verbatim from the frontend (never empty, never re-mangled).
        CHECK_FALSE(inst.instance_key.empty());
    }
    // node `first` outputs WMid, node `second` outputs WOut (Reply) — the two
    // instances carry distinct output shells.
    const auto &first_ai =
        std::get<ir::core::CoreAgentInstance>(
            result.program.instances[wf->nodes[0].target_instance.value].payload);
    const auto &second_ai =
        std::get<ir::core::CoreAgentInstance>(
            result.program.instances[wf->nodes[1].target_instance.value].payload);
    CHECK(first_ai.output_type.value != second_ai.output_type.value);
    // safety/liveness are erased: the Core workflow has no such field (structural).
}

// ============================================================================
// KR6.4 tail (erasure barrier): populated verification constructs driven
// through the REAL front end. The previous erasure assertions in this file ran
// over a contract with ZERO clauses and a workflow with NO safety/liveness, so
// they passed vacuously. These cases populate every verification construct at
// the source level, prove the AHFL-IR carries it, then prove the Core program
// is byte-identical to the one the same source produces WITHOUT the construct.
// ============================================================================

namespace {

// (1) An agent whose contract populates EVERY clause kind: requires/ensures
// (plain Bool expressions), invariant + forbid (temporal atoms over a
// capability), a concrete `decreases: self.length` metric and the wildcard
// `decreases: *`.
const std::string kPopulatedContractSource = R"AHFL(
module er;

struct Req { id: Int; }
struct Ctx { length: Int = 0; }
struct Resp { ok: Bool; }

capability Decide(req: Req) -> Resp;

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Decide];
    transition Init -> Done;
}

contract for Worker {
    requires: input.id > 0;
    ensures: output.ok;
    invariant: always called(Decide);
    forbid: always called(Decide);
    decreases: self.length;
    decreases: *;
}

flow for Worker {
    state Init {
        goto Done;
    }
    state Done {
        return Resp { ok: true };
    }
}
)AHFL";

// Same module/decls WITHOUT the ContractDecl, retained as a hand check that
// the clause-free source also lowers clean (the strong differential below
// strips the contract from a second lowering of THIS source instead, so
// SourceRange byte offsets stay identical).
const std::string kNoContractSource = R"AHFL(
module er;

struct Req { id: Int; }
struct Ctx { length: Int = 0; }
struct Resp { ok: Bool; }

capability Decide(req: Req) -> Resp;

agent Worker {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Decide];
    transition Init -> Done;
}

flow for Worker {
    state Init {
        goto Done;
    }
    state Done {
        return Resp { ok: true };
    }
}
)AHFL";

// (2) The multi-node DAG workflow WITH non-empty safety/liveness. Shapes copied
// from tests/golden/ir/ok_workflow_simplification.ahfl and the lowering-equiv
// corpus: a `not running(n) or completed(n)` safety and an
// `eventually completed(n, Done)` liveness.
const std::string kWorkflowWithPropsSource = R"AHFL(
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

    safety: always (not running(second) or completed(second));
    liveness: eventually completed(second, Done);

    return: second;
}
)AHFL";

// (3) Pure fns carrying termination measures: `decreases 0`, a prototype
// `length`, and `decreases length(xs)` (the grammar's §3.1 example). Only
// GENERIC call sites (`shrink<Int>`, `id<Int>`) emit Fn InstanceDecls; each
// must reach Core as an EMPTY CoreFnInstance placeholder (fn bodies are a
// later monomorphization slice; the measure is verification-only).
const std::string kFnDecreasesSource = R"AHFL(
module er;

fn length(x: Int) -> Int effect Pure;

fn shrink<T>(xs: Int) -> Int effect Pure decreases length(xs) {
    return xs;
}

fn id<T>(x: T) -> T effect Pure decreases 0 {
    return x;
}

fn use_it(n: Int) -> Int effect Pure decreases 0 {
    let a: Int = shrink<Int>(n);
    return id<Int>(a);
}
)AHFL";

// (4) A capability carrying a FULL CapabilityEffectSpec (domain, idempotency
// key, receipt, retry, timeout, compensation, policies).
const std::string kFullEffectSpecSource = R"AHFL(
module er;

struct Req { id: String; amount: Decimal(2); }
struct Receipt { id: String; }

capability RefundCard(request: Req) -> Receipt;

capability ChargeCard(request: Req) -> Receipt {
    effect: financial_write;
    domain: payments;
    idempotency: request.id;
    receipt: required;
    retry: safe_if_idempotent;
    timeout: 30s;
    compensation: RefundCard;
}

agent Billing {
    input: Req;
    output: Receipt;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [ChargeCard, RefundCard];
}

flow for Billing {
    state Done {
        return ChargeCard(input);
    }
}
)AHFL";

// Same program with ChargeCard's effect block erased (kind falls back to
// Unknown); kept as a hand check that the clause-free source lowers clean.
const std::string kBareCapabilitySource = R"AHFL(
module er;

struct Req { id: String; amount: Decimal(2); }
struct Receipt { id: String; }

capability RefundCard(request: Req) -> Receipt;

capability ChargeCard(request: Req) -> Receipt;

agent Billing {
    input: Req;
    output: Receipt;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [ChargeCard, RefundCard];
}

flow for Billing {
    state Done {
        return ChargeCard(input);
    }
}
)AHFL";

bool has_verify_prefixed_diagnostic(const ir::core::CoreLowerResult &r) {
    for (const auto &d : r.diagnostics) {
        if (d.code.starts_with("core.verify.")) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("erasure (1): a populated agent contract lowers clean and leaves no Core trace") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("populated_contract", kPopulatedContractSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: the AHFL-IR actually carries every clause kind.
    const ir::ContractDecl *contract = nullptr;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *c = std::get_if<ir::ContractDecl>(&d)) {
            contract = c;
        }
    }
    REQUIRE(contract != nullptr);
    REQUIRE(contract->clauses.size() == 6);
    const auto clause_kind = [&](std::size_t i) { return contract->clauses[i].kind; };
    CHECK(clause_kind(0) == ir::ContractClauseKind::Requires);
    CHECK(clause_kind(1) == ir::ContractClauseKind::Ensures);
    CHECK(clause_kind(2) == ir::ContractClauseKind::Invariant);
    CHECK(clause_kind(3) == ir::ContractClauseKind::Forbid);
    CHECK(clause_kind(4) == ir::ContractClauseKind::Decreases);
    CHECK(clause_kind(5) == ir::ContractClauseKind::Decreases);
    // Clause 4 is the concrete metric, clause 5 the wildcard.
    CHECK_FALSE(contract->clauses[4].is_wildcard);
    CHECK(contract->clauses[5].is_wildcard);
    CHECK_FALSE(contract->clauses[4].decreases_terms.empty());

    const auto with_contract = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : with_contract.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(with_contract.ok());
    CHECK(with_contract.is_executable);
    // A ContractDecl produces NO Core decl: there is no contract store at all
    // (structural; A4 pins the missing member), and the one agent / capability
    // / flow from the source are the only decls.
    REQUIRE(with_contract.program.agents.size() == 1);
    REQUIRE(with_contract.program.capabilities.size() == 1);
    REQUIRE(with_contract.program.flows.size() == 1);
    CHECK(with_contract.program.workflows.empty());

    // Sanity: the clause-free spelling of the same program also lowers clean.
    const auto clause_free_ir = lower_source_to_ahfl_ir("no_contract", kNoContractSource);
    REQUIRE(clause_free_ir.has_value());
    REQUIRE(ir::core::lower_ahfl_to_core(*clause_free_ir).ok());

    // Strong differential: lower the SAME source a second time, remove the
    // ContractDecl at the AHFL-IR layer (identical SourceRange byte offsets),
    // re-lower, and require every Core component byte-identical. The contract
    // cannot influence any execution-layer artifact.
    auto stripped_ir = lower_source_to_ahfl_ir("populated_contract_stripped",
                                               kPopulatedContractSource);
    REQUIRE(stripped_ir.has_value());
    std::erase_if(stripped_ir->declarations, [](const ir::Decl &d) {
        return std::holds_alternative<ir::ContractDecl>(d);
    });
    const auto without_contract = ir::core::lower_ahfl_to_core(*stripped_ir);
    REQUIRE(without_contract.ok());
    CHECK(with_contract.program.agents == without_contract.program.agents);
    CHECK(with_contract.program.capabilities == without_contract.program.capabilities);
    CHECK(with_contract.program.flows == without_contract.program.flows);
    CHECK(with_contract.program.types == without_contract.program.types);
    CHECK(with_contract.program.value_types == without_contract.program.value_types);
    CHECK(with_contract.program.instances == without_contract.program.instances);
}

TEST_CASE("erasure (2): workflow safety/liveness vanish from the CoreWorkflowDecl") {
    auto with_props_ir = lower_source_to_ahfl_ir("wf_with_props", kWorkflowWithPropsSource);
    REQUIRE(with_props_ir.has_value());

    // Non-vacuity: the AHFL-IR workflow carries one safety and one liveness.
    const ir::WorkflowDecl *wf_ir = nullptr;
    for (const auto &d : with_props_ir->declarations) {
        if (const auto *w = std::get_if<ir::WorkflowDecl>(&d)) {
            wf_ir = w;
        }
    }
    REQUIRE(wf_ir != nullptr);
    CHECK(wf_ir->safety.size() == 1);
    CHECK(wf_ir->liveness.size() == 1);

    const auto with_props = ir::core::lower_ahfl_to_core(*with_props_ir);
    REQUIRE(with_props.ok());
    CHECK(with_props.is_executable);

    // Compare against the existing clause-free kWorkflowSource: node/region
    // content has the same structural facts (2 nodes, same deps, same region
    // terminators) even though module/struct names differ.
    const auto without_props_ir = lower_source_to_ahfl_ir("wf_pipe", kWorkflowSource);
    REQUIRE(without_props_ir.has_value());
    const auto without_props = ir::core::lower_ahfl_to_core(*without_props_ir);
    REQUIRE(without_props.ok());
    const auto *a = find_workflow(with_props.program, "Pipe");
    const auto *b = find_workflow(without_props.program, "Pipe");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->nodes.size() == b->nodes.size());
    CHECK(a->nodes.size() == 2);
    CHECK(a->nodes[1].after == b->nodes[1].after);

    // Strong differential: strip the properties at the AHFL-IR layer in place
    // (ir::Program is move-only) and re-lower; the resulting CoreWorkflowDecl
    // is byte-identical.
    for (auto &d : with_props_ir->declarations) {
        if (auto *w = std::get_if<ir::WorkflowDecl>(&d)) {
            w->safety.clear();
            w->liveness.clear();
        }
    }
    const auto stripped_result = ir::core::lower_ahfl_to_core(*with_props_ir);
    REQUIRE(stripped_result.ok());
    const auto *stripped_wf = find_workflow(stripped_result.program, "Pipe");
    REQUIRE(stripped_wf != nullptr);
    const auto *full_wf = find_workflow(with_props.program, "Pipe");
    REQUIRE(full_wf != nullptr);
    CHECK(*full_wf == *stripped_wf);
}

TEST_CASE("erasure (3): fn Pure effect + decreases measures leave only empty FnInstance placeholders") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("fn_decreases", kFnDecreasesSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: every fn with a body carries Pure + a decreases measure.
    std::size_t fn_decls = 0;
    std::size_t fn_decreases = 0;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *fn = std::get_if<ir::FnDecl>(&d)) {
            ++fn_decls;
            CHECK(fn->effect.kind == ir::FnEffectKind::Pure);
            if (fn->effect.has_decreases) {
                ++fn_decreases;
                CHECK_FALSE(fn->effect.decreases_terms.empty());
            }
        }
    }
    CHECK(fn_decls == 6); // length prototype + shrink + id + use_it + shrink<Int> + id<Int>
    CHECK(fn_decreases == 5); // shrink, id, use_it plus the two concrete instantiations

    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // FB-1: the two generic call sites (shrink<Int>, id<Int>) became Fn
    // instances WITH outlined bodies, and the non-generic use_it gets the
    // canonical empty-type-args instance, likewise with a body. The Pure grade
    // and the decreases measures exist nowhere on the payload (they live on the
    // AHFL-IR FnDecl / the verifier's purity walk, never on CoreFnInstance).
    std::size_t fn_instances = 0;
    for (const auto &inst : result.program.instances) {
        if (std::holds_alternative<ir::core::CoreFnInstance>(inst.payload)) {
            ++fn_instances;
            CHECK_FALSE(inst.instance_key.empty());
        }
    }
    CHECK(fn_instances == 3);
    CHECK(result.program.fns.size() == 3);
    CHECK(result.program.flows.empty());
    CHECK(result.program.workflows.empty());
    CHECK(result.program.agents.empty());
}

TEST_CASE("erasure (4): a full CapabilityEffectSpec leaves only effect_kind on the Core decl") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("full_effect_spec", kFullEffectSpecSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: the AHFL-IR capability carries the FULL spec.
    const ir::CapabilityDecl *charge = nullptr;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *cap = std::get_if<ir::CapabilityDecl>(&d);
            // The lowered name is canonical ("er::ChargeCard"); match the
            // trailing simple name like find_workflow/callee_is.
            cap != nullptr && callee_is(cap->name, "ChargeCard")) {
            charge = cap;
        }
    }
    REQUIRE(charge != nullptr);
    CHECK(charge->effect.declared);
    CHECK(charge->effect.kind == ir::CapabilityEffectKind::FinancialWrite);
    REQUIRE(charge->effect.domain.has_value());
    CHECK(*charge->effect.domain == "payments");
    CHECK(charge->effect.receipt_mode == ir::CapabilityReceiptMode::Required);
    CHECK(charge->effect.retry_mode == ir::CapabilityRetryMode::SafeIfIdempotent);
    REQUIRE(charge->effect.timeout.has_value());
    REQUIRE(charge->effect.compensation.has_value());

    const auto full = ir::core::lower_ahfl_to_core(*ahfl_ir);
    REQUIRE(full.ok());
    CHECK(full.is_executable);
    REQUIRE(full.program.capabilities.size() == 2);
    const auto *core_charge = [&]() -> const ir::core::CoreCapabilityDecl * {
        for (const auto &c : full.program.capabilities) {
            // The lowered name is the canonical name ("er::ChargeCard"); match
            // the trailing simple name like find_workflow.
            if (c.name == "ChargeCard" || callee_is(c.name, "ChargeCard")) {
                return &c;
            }
        }
        return nullptr;
    }();
    REQUIRE(core_charge != nullptr);
    // effect_kind is the ONE piece of the spec the execution layer keeps.
    CHECK(core_charge->effect_kind == ir::CapabilityEffectKind::FinancialWrite);
    // (No domain/receipt/retry/timeout/compensation member exists — A4 pins
    // that at compile time.)
    REQUIRE(core_charge->param_types.size() == 1);
    CHECK(core_charge->return_type.value < full.program.value_types.size());

    // Sanity: the clause-free spelling (no effect block, kind defaults to
    // Unknown) also lowers clean.
    const auto bare_ir = lower_source_to_ahfl_ir("bare_capability", kBareCapabilitySource);
    REQUIRE(bare_ir.has_value());
    REQUIRE(ir::core::lower_ahfl_to_core(*bare_ir).ok());

    // Differential: lower the SAME source again, reduce every capability spec
    // to its kind at the AHFL-IR layer (SourceRanges stay byte-identical), and
    // re-lower. Because CoreCapabilityDecl keeps ONLY effect_kind, the reduced
    // capability decls are byte-identical and the agent / flow / types /
    // value_types / instances are unaffected — nothing else from the spec
    // reaches Core.
    auto stripped_ir = lower_source_to_ahfl_ir("full_effect_spec_stripped",
                                               kFullEffectSpecSource);
    REQUIRE(stripped_ir.has_value());
    for (auto &d : stripped_ir->declarations) {
        if (auto *cap = std::get_if<ir::CapabilityDecl>(&d)) {
            const auto kind = cap->effect.kind;
            cap->effect = ir::CapabilityEffectSpec{};
            cap->effect.kind = kind;
        }
    }
    const auto stripped = ir::core::lower_ahfl_to_core(*stripped_ir);
    REQUIRE(stripped.ok());
    CHECK(full.program.capabilities == stripped.program.capabilities);
    CHECK(full.program.agents == stripped.program.agents);
    CHECK(full.program.flows == stripped.program.flows);
    CHECK(full.program.types == stripped.program.types);
    CHECK(full.program.value_types == stripped.program.value_types);
    CHECK(full.program.instances == stripped.program.instances);
}

// ============================================================================
// A3: forall/exists QuantifierExpr is a CoreUnsupportedExpr source kind but
// had NO real-frontend test. A quantifier in an executable flow handler makes
// the program non-executable (enumerated unsupported node + stable code); the
// SAME quantifier inside a contract requires/invariant is erased — the program
// lowers ok() and stays executable, with no auto-verifier noise.
// ============================================================================

namespace {

const std::string kQuantifierFlowSource = R"AHFL(
module app::main;

import std::collections as collections;

struct Request { values: collections::List<Int>; }
struct Context { }
struct Response { ok: Bool; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for A {
    state Init {
        let all_pos: Bool = forall x in input.values: x > 0;
        goto Done;
    }
    state Done {
        return Response { ok: true };
    }
}
)AHFL";

const std::string kQuantifierContractSource = R"AHFL(
module app::main;

import std::collections as collections;

struct Request { values: collections::List<Int>; }
struct Context { }
struct Response { ok: Bool; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

contract for A {
    requires: forall x in input.values: x > 0;
    invariant: always (forall x in input.values: x > 0);
    ensures: output.ok;
}

flow for A {
    state Init {
        goto Done;
    }
    state Done {
        return Response { ok: true };
    }
}
)AHFL";

// True iff any pure-expression arena entry records a CoreUnsupportedExpr with
// the given stable source kind.
bool flow_has_unsupported(const ir::core::CoreProgram &program, std::string_view kind) {
    for (const auto &flow : program.flows) {
        for (const auto &expr : flow.storage.exprs) {
            if (const auto *unsupported =
                    std::get_if<ir::core::CoreUnsupportedExpr>(&expr.node)) {
                if (unsupported->source_kind == kind) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("A3(a): a quantifier in a flow handler is a non-executable unsupported Core expr") {
    const auto ahfl_ir =
        lower_sysroot_source_to_ahfl_ir("quant_flow", kQuantifierFlowSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: the quantifier really did reach AHFL-IR inside a body expr.
    bool saw_quantifier = false;
    for (const ir::Expr *expr : ahfl_ir->all_exprs()) {
        if (expr != nullptr && std::holds_alternative<ir::QuantifierExpr>(expr->node)) {
            saw_quantifier = true;
        }
    }
    CHECK(saw_quantifier);

    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    // Exact stable code + the enumerated node kind recorded for diagnostics.
    CHECK(has_lower_code(result, ir::core::diag::kUnloweredExpression));
    CHECK(flow_has_unsupported(result.program, "QuantifierExpr"));
    // A lowering-error candidate SKIPS the auto-wired verifier, so no
    // core.verify.* diagnostics can leak out of the partial artifact.
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

TEST_CASE("A3(b): the same quantifier in a contract requires/invariant is erased and executable") {
    const auto ahfl_ir =
        lower_sysroot_source_to_ahfl_ir("quant_contract", kQuantifierContractSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: two quantifier nodes (requires + invariant) reached AHFL-IR.
    std::size_t quantifiers = 0;
    for (const ir::Expr *expr : ahfl_ir->all_exprs()) {
        if (expr != nullptr && std::holds_alternative<ir::QuantifierExpr>(expr->node)) {
            ++quantifiers;
        }
    }
    CHECK(quantifiers == 2);

    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    // Contract clauses are never walked by the body lowerer: no unsupported
    // node reaches the flow arena, and no verifier diagnostic is emitted.
    CHECK_FALSE(flow_has_unsupported(result.program, "QuantifierExpr"));
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
    REQUIRE(result.program.flows.size() == 1);
}

// ============================================================================
// RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT): lambda lifting lands. A
// real-frontend PURE lambda is no longer an unsupported expression: the Core
// lowerer lifts it to a monomorphic CoreFnDecl + Fn instance and lowers its
// construction site to a CoreClosureExpr. The program lowers ok() +
// is_executable (Core-verify clean); the wasm backend still fails closed only
// at its own funcref/call_indirect codegen gate (FB-3b), not at lowering.
// ============================================================================

namespace {

const std::string kLambdaFlowSource = R"AHFL(
module app::main;

struct Request { seed: Int; }
struct Context { }
struct Response { doubled: Int; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for A {
    state Init {
        // A PURE lambda bound as a first-class Fn value. FB-3a2 lifts it into a
        // dedicated fn; the construction site becomes a CoreClosureExpr.
        let dbl: Fn(Int) -> Int = \(x: Int) -> x * 2;
        goto Done;
    }
    state Done {
        return Response { doubled: input.seed };
    }
}
)AHFL";

} // namespace

// ============================================================================
// RFC 0026 FB-4 (CORE-FNBODY-DESIGN §5.3): a transitive capability effect
// lowers an fn call to the ORDERED CoreCallStmt, never a pure CoreCallExpr.
// ============================================================================
namespace {

const std::string kFb4EffectfulSource = R"AHFL(
module fb4eff;

pub struct Frame {
    n: Int;
}

capability Bump(request: Frame) -> Frame;

fn call_cap(n: Int) -> Frame effect Bump {
    let f: Frame = Frame { n: n };
    return Bump(f);
}

// A transitively-effectful wrapper: it names Bump honestly (the typechecker
// enforces effect soundness), and the Core structural fixed point must STILL
// derive its effect from the body call to call_cap rather than trusting the
// clause spelling.
fn ask_cap(n: Int) -> Frame effect Bump {
    return call_cap(n);
}

pub agent EffectAgent {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Bump];
    transition Init -> Done;
}

flow for EffectAgent {
    state Init {
        let answered: Frame = ask_cap(input.n);
        if (answered.n == 42) { goto Done; } else { goto Done; }
    }
    state Done { return input; }
}
)AHFL";

// Collect ordered CoreCallStmts in one fn body (any nesting depth).
void collect_call_stmts(const ir::core::CoreRegion &region,
                        std::vector<const ir::core::CoreCallStmt *> &out) {
    for (const auto &stmt : region.statements) {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ir::core::CoreCallStmt>) {
                    out.push_back(&node);
                } else if constexpr (std::is_same_v<T, ir::core::CoreIfStmt>) {
                    if (node.then_region) {
                        collect_call_stmts(*node.then_region, out);
                    }
                    if (node.else_region) {
                        collect_call_stmts(*node.else_region, out);
                    }
                } else if constexpr (std::is_same_v<T, ir::core::CoreMatchStmt>) {
                    for (const auto &arm : node.arms) {
                        if (arm.body) {
                            collect_call_stmts(*arm.body, out);
                        }
                    }
                    if (node.fallback_region) {
                        collect_call_stmts(*node.fallback_region, out);
                    }
                }
            },
            stmt.node);
    }
}

} // namespace

TEST_CASE("FB-4 lowerer: a transitive effect fn call lowers to an ordered CoreCallStmt") {
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("fb4_effect", kFb4EffectfulSource);
    REQUIRE(ahfl_ir.has_value());
    auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    REQUIRE(result.is_executable);

    // The flow handler must invoke ask_cap through an ordered statement (not a
    // pure CoreCallExpr), and the wrapper fn must invoke call_cap the same way.
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    std::vector<const ir::core::CoreCallStmt *> handler_calls;
    for (const auto &state : flow.states) {
        collect_call_stmts(state.body, handler_calls);
    }
    CHECK(handler_calls.size() == 1);

    // Every outlined fn body that calls another fn: the effectful ones use the
    // statement; no effectful callee appears as a pure CoreCallExpr.
    const auto effects = ir::core::analyze_fn_effects(result.program);
    std::size_t effectful_fns = 0;
    for (std::size_t i = 0; i < result.program.fns.size(); ++i) {
        if (effects.effectful[i]) {
            ++effectful_fns;
        }
    }
    // call_cap + ask_cap are both effectful (the wrapper via transitivity).
    CHECK(effectful_fns >= 2);

    // No pure CoreCallExpr may target an effectful fn (the verifier also enforces
    // this; assert it on the lowered program directly).
    for (const auto &fn : result.program.fns) {
        for (const auto &expr : fn.storage.exprs) {
            if (const auto *call = std::get_if<ir::core::CoreCallExpr>(&expr.node)) {
                const auto *payload =
                    std::get_if<ir::core::CoreFnInstance>(
                        &result.program.instances[call->callee.value].payload);
                REQUIRE(payload != nullptr);
                CHECK_FALSE(effects.effectful[payload->body.value]);
            }
        }
    }
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

TEST_CASE("FB-3a2: a real-frontend lambda lifts to a closure-constructing fn") {
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("lambda_flow", kLambdaFlowSource);
    REQUIRE(ahfl_ir.has_value());

    // Non-vacuity: a LambdaExpr really reached the AHFL-IR body the Core lowerer
    // walks (so this exercises lifting, not a frontend reject).
    bool saw_lambda = false;
    for (const ir::Expr *expr : ahfl_ir->all_exprs()) {
        if (expr != nullptr && std::holds_alternative<ir::LambdaExpr>(expr->node)) {
            saw_lambda = true;
        }
    }
    CHECK(saw_lambda);

    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    CHECK_FALSE(flow_has_unsupported(result.program, "LambdaExpr"));

    // Lifting produced exactly one extra fn + Fn instance for the construction
    // site, and the handler body now carries a CoreClosureExpr (no indirect call
    // yet — the lambda is constructed but not invoked here).
    REQUIRE(result.program.fns.size() == 1);
    const ir::core::CoreFnDecl &lifted = result.program.fns[0];
    CHECK(lifted.captures.empty());
    CHECK(lifted.env_bindings.empty());
    CHECK(lifted.params.size() == 1);

    bool saw_closure = false;
    bool saw_indirect_call = false;
    for (const auto &flow : result.program.flows) {
        for (const auto &expr : flow.storage.exprs) {
            if (std::holds_alternative<ir::core::CoreClosureExpr>(expr.node)) {
                saw_closure = true;
            }
            if (std::holds_alternative<ir::core::CoreCallClosureExpr>(expr.node)) {
                saw_indirect_call = true;
            }
        }
    }
    CHECK(saw_closure);
    CHECK_FALSE(saw_indirect_call);
    // Lifting leaves no lowering-verify diagnostic (the lifted fn is structurally
    // well-formed: signature / return / SSA all agree).
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// ============================================================================
// FB-3a2 higher-order closure: a PURE top-level fn with an Fn(T)->U parameter
// invokes the parameter through it. The call site is a CoreCallClosureExpr
// (indirect), while a lambda passed in lifts to a CoreClosureExpr. This is the
// `list_map_into` shape (design §5.2). Also pins ByValue capture: a captured
// outer local is read through its env slot, independent of a later outer store.
// ============================================================================
namespace {

const std::string kHigherOrderFnSource = R"AHFL(
module ho;

fn apply2(f: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return f(x);
}

fn use_apply(n: Int) -> Int effect Pure decreases 0 {
    let bump: Int = 10;
    let g: Fn(Int) -> Int = \(y: Int) -> y + bump;
    return apply2(g, n);
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: a closure captured local is invoked through an indirect call") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("ho", kHigherOrderFnSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // The lambda `g` lifted to a fn with ONE declared Int capture slot and the
    // matching pre-bound env value; its body reads the slot via a bare local.
    const auto closure_fns = [&] {
        std::vector<const ir::core::CoreFnDecl *> out;
        for (const auto &fn : result.program.fns) {
            if (!fn.captures.empty()) {
                out.push_back(&fn);
            }
        }
        return out;
    }();
    REQUIRE(closure_fns.size() == 1);
    const ir::core::CoreFnDecl &lifted = *closure_fns[0];
    REQUIRE(lifted.captures.size() == 1);
    REQUIRE(lifted.env_bindings.size() == 1);
    CHECK(lifted.params.size() == 1); // only the logical `y`; bump is NOT a param

    // `apply2`'s body contains an indirect closure call (f(x)).
    bool saw_indirect = false;
    for (const auto &fn : result.program.fns) {
        for (const auto &expr : fn.storage.exprs) {
            if (std::holds_alternative<ir::core::CoreCallClosureExpr>(expr.node)) {
                saw_indirect = true;
            }
        }
    }
    CHECK(saw_indirect);

    // ByValue snapshot (§3.4): the closure's env operand is the exact SSA value
    // the `let bump = 10` bound at the construction site. SSA values are never
    // rewritten by a later store (stores target a CorePlace, not the value id),
    // so the captured value cannot change after construction.
    const auto bump_value = [&]() -> ir::core::CoreValueId {
        for (const auto &fn : result.program.fns) {
            for (const auto &stmt : fn.body.statements) {
                if (const auto *let = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                    // bump's RHS is the only integer-literal expr in this fn.
                    const auto &e = fn.storage.exprs[let->expr.value];
                    if (const auto *lit = std::get_if<ir::core::CoreLiteralExpr>(&e.node)) {
                        if (lit->kind == ir::core::CoreLiteralKind::Integer &&
                            lit->spelling == "10") {
                            return let->result;
                        }
                    }
                }
            }
        }
        return ir::core::CoreValueId{};
    }();
    bool env_reads_snapshot = false;
    for (const auto &fn : result.program.fns) {
        for (const auto &expr : fn.storage.exprs) {
            if (const auto *cl = std::get_if<ir::core::CoreClosureExpr>(&expr.node)) {
                if (cl->fn == lifted.id && cl->env.size() == 1 &&
                    cl->env[0] == bump_value) {
                    env_reads_snapshot = true;
                }
            }
        }
    }
    CHECK(bump_value.value != ir::core::CoreValueId{}.value);
    CHECK(env_reads_snapshot);
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// FB-3a2: an implicit-capture lambda that reads a frame-root projection
// (`input.seed`) materializes the projection ONCE at the construction site and
// captures it by value (design §3.2 rule 4): the lifted fn has one frame
// capture slot and contains no input/ctx path root.
namespace {

const std::string kFrameCaptureSource = R"AHFL(
module app::fc;

struct Request { seed: Int; }
struct Context { }
struct Response { out: Int; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for A {
    state Init {
        let f: Fn(Int) -> Int = \(x: Int) -> x + input.seed;
        goto Done;
    }
    state Done {
        return Response { out: input.seed };
    }
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: a frame-root read is captured by value into the lifted fn") {
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("fc", kFrameCaptureSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const auto lifted =
        std::find_if(result.program.fns.begin(), result.program.fns.end(),
                     [](const ir::core::CoreFnDecl &fn) { return !fn.captures.empty(); });
    REQUIRE(lifted != result.program.fns.end());
    REQUIRE(lifted->captures.size() == 1);
    REQUIRE(lifted->env_bindings.size() == 1);

    // The lifted fn body carries no Input/Context path root: the projection was
    // captured as a pre-bound value.
    for (const auto &expr : lifted->storage.exprs) {
        if (const auto *path = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            CHECK(path->root != ir::core::CorePathRoot::Input);
            CHECK(path->root != ir::core::CorePathRoot::Context);
        }
    }
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// FB-3a2 negative: a lambda that references a name which is neither a
// parameter, an in-body binder, nor a visible outer local fails closed with the
// structured capture diagnostic (this is also the self-referential shape).
namespace {

const std::string kBadCaptureSource = R"AHFL(
module app::bc;

struct Request { seed: Int; }
struct Context { }
struct Response { out: Int; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for A {
    state Init {
        let f: Fn(Int) -> Int = \(x: Int) -> x + missing_thing;
        goto Done;
    }
    state Done {
        return Response { out: input.seed };
    }
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: an unresolvable free reference in a lambda fails closed") {
    // The frontend rejects an unknown name, so this may not even reach Core
    // lowering; guard with has_value and only assert the Core behavior when it
    // does (the structured diagnostic is also exercised by hand-built inputs).
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("bc", kBadCaptureSource);
    if (!ahfl_ir.has_value()) {
        return;
    }
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK(has_lower_code(result, ir::core::diag::kClosureCaptureInvalid));
    CHECK_FALSE(result.is_executable);
}

// --- KR6.4 monomorphization Slice 1: instance registry / dispatch identity ---

// FB-3a2: NESTED lambdas lift independently, and the inner closure captures a
// value the outer closure already captured (a chained env slot). Each
// construction site gets its own lifted fn; the inner fn's capture slot is an
// ordinary pre-bound value of the OUTER lifted fn.
namespace {

const std::string kNestedLambdaSource = R"AHFL(
module nl;

fn hof(g: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return g(x);
}

fn outer() -> Int effect Pure decreases 0 {
    let a: Int = 3;
    let g: Fn(Int) -> Int = \(x: Int) -> hof(\(y: Int) -> y + a + x, x);
    return g(1);
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: nested lambdas lift with chained captures") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("nl", kNestedLambdaSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // Two lambda construction sites -> two lifted fns. The outer captures `a`;
    // the inner captures `a` + `x` as seen from the outer fn's pre-bound scope.
    const auto lifted_count =
        std::count_if(result.program.fns.begin(), result.program.fns.end(),
                      [](const ir::core::CoreFnDecl &fn) {
                          return fn.name.rfind("_lambda_", 0) == 0;
                      });
    CHECK(lifted_count == 2);

    bool inner_found = false;
    for (const auto &fn : result.program.fns) {
        if (fn.name.rfind("_lambda_", 0) == 0 && fn.captures.size() == 2) {
            inner_found = true; // the inner closure sees a + x
        }
    }
    CHECK(inner_found);
    // The inner lifted fn invokes itself indirectly? It calls h(x) -> one
    // CoreCallClosureExpr inside one of the lifted fn bodies.
    bool saw_indirect = false;
    for (const auto &fn : result.program.fns) {
        for (const auto &expr : fn.storage.exprs) {
            if (std::holds_alternative<ir::core::CoreCallClosureExpr>(expr.node)) {
                saw_indirect = true;
            }
        }
    }
    CHECK(saw_indirect);
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// FB-3a2 fix-forward P0: the same nested-lambda shape, but constructed in a
// FLOW HANDLER (Pass 2), not inside an outlined fn (Pass 1.5). The lifter must
// own its structural interner by value: a const reference bound to the
// Pass-1.5 block-local interner dangles once that block closes, so lifting the
// inner lambda from a handler used to call a dead std::function (SIGSEGV /
// ASan stack-use-after-scope).
namespace {

const std::string kNestedFlowLambdaSource = R"AHFL(
module nfl;

struct Request { seed: Int; }
struct Context { }
struct Response { out: Int; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

fn hof(g: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return g(x);
}

flow for A {
    state Init {
        let a: Int = 3;
        let f: Fn(Int) -> Int = \(x: Int) -> hof(\(y: Int) -> y + a + x, x);
        goto Done;
    }
    state Done {
        return Response { out: input.seed };
    }
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: nested lambda constructed in a flow handler lifts without dangling interner") {
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("nfl", kNestedFlowLambdaSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const auto lifted_count =
        std::count_if(result.program.fns.begin(), result.program.fns.end(),
                      [](const ir::core::CoreFnDecl &fn) {
                          return fn.name.rfind("_lambda_", 0) == 0;
                      });
    CHECK(lifted_count == 2);
    bool inner_found = false;
    for (const auto &fn : result.program.fns) {
        if (fn.name.rfind("_lambda_", 0) == 0 && fn.captures.size() == 2) {
            inner_found = true; // the inner closure sees a + x
        }
    }
    CHECK(inner_found);
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// FB-3a2 fix-forward P1: a lifted lambda that invokes another captured callable
// SOLELY in the callee position of a CallExpr (`g(z)`). The implicit-capture
// DFS used to visit only call arguments, so `g` was never recorded as a
// capture and the call fell through to the generic UNLOWERED_EXPRESSION arm
// instead of becoming a CoreCallClosureExpr.
namespace {

const std::string kCalleeCaptureSource = R"AHFL(
module cc;

fn hof(g: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return g(x);
}

fn outer(x: Int) -> Int effect Pure decreases 0 {
    let g: Fn(Int) -> Int = \(y: Int) -> y + 1;
    let h: Fn(Int) -> Int = \(z: Int) -> g(z);
    return hof(h, x);
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: a callable used in callee position is captured and called indirectly") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("cc", kCalleeCaptureSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // Two lifted fns: `g` (zero captures) and `h` (captures g in its one env
    // slot). `h`'s lifted body calls that slot through CoreCallClosureExpr.
    const auto lifted_count =
        std::count_if(result.program.fns.begin(), result.program.fns.end(),
                      [](const ir::core::CoreFnDecl &fn) {
                          return fn.name.rfind("_lambda_", 0) == 0;
                      });
    CHECK(lifted_count == 2);

    const auto h_lifted =
        std::find_if(result.program.fns.begin(), result.program.fns.end(),
                     [](const ir::core::CoreFnDecl &fn) {
                         return fn.name.rfind("_lambda_", 0) == 0 && fn.captures.size() == 1;
                     });
    REQUIRE(h_lifted != result.program.fns.end());
    bool h_calls_indirectly = false;
    for (const auto &expr : h_lifted->storage.exprs) {
        if (std::holds_alternative<ir::core::CoreCallClosureExpr>(expr.node)) {
            h_calls_indirectly = true;
        }
    }
    CHECK(h_calls_indirectly);
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

// FB-3a2 fix-forward P1 (D-FNREP, design §3.1.1): two one-directional result
// widenings the verifier used to reject. (1) A fn declared to return the BARE
// signature returns a lambda that captures, whose concrete return is the
// constructed CoreVtClosure over that signature; the call site is typed with
// the bare signature. (2) A zero-capture lambda minted as CoreVtClosure is
// passed at the callable parameter of an INDIRECT CoreCallClosureExpr (h(...)).
namespace {

const std::string kFnRepWidenSource = R"AHFL(
module fw;

fn apply2(f: Fn(Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return f(x);
}

// Declared return is the bare signature; the body's concrete value return is a
// constructed closure over that same signature.
fn mk(bump: Int) -> Fn(Int) -> Int effect Pure decreases 0 {
    return \(y: Int) -> y + bump;
}

// The argument `h` is itself a callable taking a callable; the indirect call
// h(\y -> y, x) passes a constructed (zero-capture) closure where the bare
// Fn(Int)->Int signature is expected.
fn driver(h: Fn(Fn(Int) -> Int, Int) -> Int, x: Int) -> Int effect Pure decreases 0 {
    return h(\(y: Int) -> y, x);
}

// The lifted outer lambda's own body RETURNS a constructed inner closure, so
// verify_closure_construction compares the outer closure signature's bare
// Fn(Int)->Int return against the concrete closure-typed value return of the
// lifted fn (the same one-directional result widening, site B).
fn use_curried(n: Int) -> Int effect Pure decreases 0 {
    let f: Fn(Int) -> Fn(Int) -> Int = \(a: Int) -> \(b: Int) -> a + b;
    let g: Fn(Int) -> Int = f(1);
    return apply2(g, n);
}

fn use_mk(n: Int) -> Int effect Pure decreases 0 {
    return apply2(mk(5), n);
}
)AHFL";

} // namespace

TEST_CASE("FB-3a2: constructed closures widen to the bare signature at result and indirect slots") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("fw", kFnRepWidenSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // mk(5) is consumed through a DIRECT call whose site result is the bare
    // Fn signature while mk's concrete return is a constructed closure.
    // driver's body carries the INDIRECT call whose callable argument is the
    // zero-capture constructed closure; both widenings must leave the program
    // verifier-clean (a failure here used to surface as
    // FN_CALL_RESULT_TYPE_MISMATCH / CLOSURE_DISPATCH_ARGUMENT_TYPE /
    // CLOSURE_RESULT_TYPE_INVALID).
    CHECK_FALSE(has_verify_prefixed_diagnostic(result));
}

namespace {
// A generic `fn id<T>(x: T) -> T` invoked at Int + a workflow whose agents give
// Agent instances. Exercises the Fn-instance + Agent-instance consumption path.
const std::string kGenericInstanceSource = R"AHFL(
module gi;

struct Req { amount: Int; }
struct Reply { ok: Bool; }

fn id<T>(x: T) -> T { return x; }

fn use_it(n: Int) -> Int { return id<Int>(n); }
)AHFL";
} // namespace

TEST_CASE("KR6.4 mono Slice 1: a generic fn instance is consumed into CoreProgram.instances") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("gen_inst", kGenericInstanceSource);
    REQUIRE(ahfl_ir.has_value());
    // Count AHFL-IR InstanceDecls so the test is meaningful even if the frontend
    // emits zero (then the consumption path has nothing to prove and we skip the
    // strong assertions rather than assert a false invariant).
    std::size_t ahfl_fn_instances = 0;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *inst = std::get_if<ir::InstanceDecl>(&d)) {
            if (inst->kind == ir::InstanceKind::Fn) {
                ++ahfl_fn_instances;
            }
        }
    }
    // The frontend MUST emit exactly one Fn instance for `id<Int>` — if it ever
    // regressed to zero, the consumption assertions below would pass vacuously.
    REQUIRE(ahfl_fn_instances == 1);
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &diag : result.diagnostics) {
        INFO("unexpected core diagnostic: " << diag.code << " — " << diag.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    // Every AHFL-IR InstanceDecl becomes exactly one CoreInstanceDecl, byte-exact
    // key, and (Fn kind) a CoreFnInstance payload.
    // FB-1: the AHFL-IR registry contributes exactly one Fn InstanceDecl
    // (`id<Int>`); the Core table additionally carries the guaranteed
    // canonical empty-type-args instance for the body-bearing non-generic
    // `use_it`, so every body fn has a 1:1 Fn instance + CoreFnDecl.
    std::size_t core_fn_instances = 0;
    for (const auto &inst : result.program.instances) {
        CHECK_FALSE(inst.instance_key.empty());
        if (std::holds_alternative<ir::core::CoreFnInstance>(inst.payload)) {
            ++core_fn_instances;
        }
    }
    CHECK(core_fn_instances == ahfl_fn_instances + 1);
    CHECK(result.program.fns.size() == core_fn_instances);
    // The Core key equals the AHFL InstanceDecl name byte-for-byte (no re-mangle).
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *inst = std::get_if<ir::InstanceDecl>(&d)) {
            bool found = false;
            for (const auto &ci : result.program.instances) {
                if (ci.instance_key == inst->name) {
                    found = true;
                    break;
                }
            }
            INFO("AHFL instance '" << inst->name << "' has no byte-exact Core instance");
            CHECK(found);
        }
    }
    // P4-A2: the Fn instance's dispatch descriptor is now interned into the
    // program's value_types arena. `id<Int>` dispatches on Int, so at least one
    // dispatch id must resolve to a CoreVtInt, and every dispatch id must be a
    // valid arena index.
    bool saw_int_dispatch = false;
    for (const auto &inst : result.program.instances) {
        if (!std::holds_alternative<ir::core::CoreFnInstance>(inst.payload)) {
            continue;
        }
        for (const auto &vt : inst.dispatch_types) {
            REQUIRE(vt.value < result.program.value_types.size());
            if (std::holds_alternative<ir::core::CoreVtInt>(
                    result.program.value_types[vt.value].node)) {
                saw_int_dispatch = true;
            }
        }
    }
    CHECK(saw_int_dispatch);
}

// --- mono Slice 1 verifier negatives (hand-built Core instance table) ---

TEST_CASE("P4-A2 forward-fix P0-2: a REAL inlined std generic decl is decorated from the SSOT") {
    // A program that INLINES a real std::collections::List struct declaration
    // (as include_stdlib / a deserialized program would) must get role=List +
    // type_param_count=1 via register_type's SSOT decoration — NOT left
    // Ordinary/0 (which would make a legal List<Int> fail "expects 0 args").
    ir::AhflIr program;
    ir::StructDecl list;
    list.name = "List";
    list.symbol_ref.kind = ir::SymbolRefKind::Type;
    list.symbol_ref.canonical_name = "std::collections::List";
    list.symbol_ref.id = 900;
    program.declarations.emplace_back(std::move(list));

    const auto result = ir::core::lower_ahfl_to_core(program);
    const ir::core::CoreTypeDecl *list_decl = nullptr;
    for (const auto &t : result.program.types) {
        if (t.name == "std::collections::List") {
            list_decl = &t;
        }
    }
    REQUIRE(list_decl != nullptr);
    CHECK(list_decl->role == ir::core::CoreNominalRole::List);
    CHECK(list_decl->type_param_count == 1);
    // add_builtins must NOT have added a second synthetic List (real decl wins).
    std::size_t list_count = 0;
    for (const auto &t : result.program.types) {
        if (t.name == "std::collections::List") {
            ++list_count;
        }
    }
    CHECK(list_count == 1);

    // A legal List<Int> now lowers against the decorated real decl. Re-run the
    // whole lowering into a fresh mutable program (CoreProgram is move-only, so
    // we can't copy result.program) and intern into that arena.
    auto fresh = ir::core::lower_ahfl_to_core(program);
    ir::TypeRef list_ref;
    list_ref.kind = ir::TypeRefKind::Struct;
    list_ref.canonical_name = "std::collections::List";
    list_ref.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                         .canonical_name = "std::collections::List",
                                         .id = std::size_t{900}};
    auto elem = std::make_unique<ir::TypeRef>();
    elem->kind = ir::TypeRefKind::Int;
    list_ref.params.push_back(std::move(elem));
    std::string reason;
    const auto id = ir::core::lower_value_type_into(fresh.program, list_ref, &reason);
    INFO(reason);
    CHECK(id.has_value());
}

TEST_CASE("mono verifier: a duplicate instance key is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_dup";
    a.payload = ir::core::CoreFnInstance{};
    ir::core::CoreInstanceDecl b;
    b.id = ir::core::CoreInstanceId{1};
    b.instance_key = "_inst_dup"; // same key
    b.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    p.instances.push_back(std::move(b));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceKeyDuplicated));
}

TEST_CASE("mono verifier: an empty instance key is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = ""; // empty
    a.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceKeyEmpty));
}

TEST_CASE("mono verifier: an out-of-range dispatch value-type id is fail-closed") {
    // Concreteness is enforced at lowering time (lower_value_type); the verifier's
    // consumer-context check rejects a dispatch id with no arena entry.
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_bad_dispatch";
    a.dispatch_types.push_back(ir::core::CoreValueTypeId{7}); // empty arena -> out of range
    a.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceDispatchTypeInvalid));
}

TEST_CASE("mono verifier: an out-of-range instance base is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_bad_base";
    a.payload = ir::core::CoreCapabilityInstance{ir::core::CoreCapabilityId{9}}; // no caps
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceBaseInvalid));
}


// ---------------------------------------------------------------------------
// RFC 0026 P4 (coercion) F1: builtin nominal metadata drift is fail-closed.
// A REAL declaration of a well-known stdlib generic whose arity OR per-parameter
// variance disagrees with the builtin descriptor SSOT must fail closed with a
// stable core lowering diagnostic (never assert / silently overwrite). Synthetic
// bases are stamped from the descriptor; user nominals are consumed verbatim.
// ---------------------------------------------------------------------------

// A minimal AhflIr carrying one real std List decl with the given arity/variance.
namespace {
ir::AhflIr make_std_list_program(std::uint32_t arity, std::vector<ir::Variance> variances) {
    ir::AhflIr program;
    ir::StructDecl list;
    list.name = "List";
    list.symbol_ref.kind = ir::SymbolRefKind::Type;
    list.symbol_ref.canonical_name = "std::collections::List";
    list.type_param_count = arity;
    list.type_param_variances = std::move(variances);
    program.declarations.emplace_back(std::move(list));
    return program;
}
} // namespace

TEST_CASE("F1: real std List decl with WRONG arity drifts from the SSOT (fail-closed)") {
    const auto program = make_std_list_program(2, {ir::Variance::Covariant, ir::Variance::Covariant});
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
    CHECK_FALSE(result.is_executable);
}

TEST_CASE("F1: real std List decl with WRONG variance drifts from the SSOT (fail-closed)") {
    // Correct arity (1) but Contravariant where the SSOT says Covariant.
    const auto program = make_std_list_program(1, {ir::Variance::Contravariant});
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
    CHECK_FALSE(result.is_executable);
}

TEST_CASE("F1: real std List decl matching the SSOT exactly does NOT drift") {
    const auto program = make_std_list_program(1, {ir::Variance::Covariant});
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
}

TEST_CASE("F1: a synthetic builtin base (no real decl) is stamped from the SSOT, not drifting") {
    // A program with NO std::collections::List decl: add_builtins registers the
    // synthetic base, stamped with arity 1 + [Covariant] from the descriptor.
    ir::AhflIr program;
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
    // The synthetic List base carries the descriptor's arity + variance.
    const ir::core::CoreTypeDecl *list = nullptr;
    for (const auto &t : result.program.types) {
        if (t.name == "std::collections::List") {
            list = &t;
        }
    }
    REQUIRE(list != nullptr);
    CHECK(list->type_param_count == 1);
    CHECK(list->variances == std::vector<ir::core::CoreVariance>{ir::core::CoreVariance::Covariant});

    const ir::core::CoreTypeDecl *option = nullptr;
    const ir::core::CoreTypeDecl *std_result = nullptr;
    for (const auto &t : result.program.types) {
        if (t.name == "std::option::Option") {
            option = &t;
        } else if (t.name == "std::result::Result") {
            std_result = &t;
        }
    }
    REQUIRE(option != nullptr);
    REQUIRE(std_result != nullptr);
    REQUIRE(option->variant_payloads.size() == 2);
    REQUIRE(option->variant_payloads[0].slot_type_template_roots.size() == 1);
    const auto option_root = option->variant_payloads[0].slot_type_template_roots[0];
    REQUIRE(option_root.value < option->member_type_templates.size());
    CHECK(option->member_type_templates[option_root.value].kind ==
          ir::core::CoreMemberTypeTemplateKind::Param);
    CHECK(option->member_type_templates[option_root.value].param_index == 0);
    REQUIRE(std_result->variant_payloads.size() == 2);
    for (std::size_t variant = 0; variant < 2; ++variant) {
        REQUIRE(std_result->variant_payloads[variant].slot_type_template_roots.size() == 1);
        const auto root = std_result->variant_payloads[variant].slot_type_template_roots[0];
        REQUIRE(root.value < std_result->member_type_templates.size());
        CHECK(std_result->member_type_templates[root.value].kind ==
              ir::core::CoreMemberTypeTemplateKind::Param);
        CHECK(std_result->member_type_templates[root.value].param_index == variant);
    }
}

TEST_CASE("P4-C real builtin payload template drift is fail-closed") {
    ir::AhflIr program;
    ir::EnumDecl result_decl;
    result_decl.name = "Result";
    result_decl.symbol_ref.kind = ir::SymbolRefKind::Type;
    result_decl.symbol_ref.canonical_name = "std::result::Result";
    result_decl.symbol_ref.id = 812;
    result_decl.type_param_count = 2;
    result_decl.type_param_variances = {ir::Variance::Covariant, ir::Variance::Covariant};
    for (std::string_view name : {"Ok", "Err"}) {
        ir::EnumVariantDecl variant;
        variant.name = name;
        variant.payload_kind = ir::EnumVariantPayloadKind::Tuple;
        variant.payload.push_back(ir::TypeRef{});
        variant.payload_type_template_roots.push_back(
            static_cast<std::uint32_t>(result_decl.member_type_templates.size()));
        ir::MemberTypeTemplateNode wrong;
        wrong.kind = ir::MemberTypeTemplateKind::Param;
        wrong.param_index = 0; // Err MUST map to parameter 1, so this drifts.
        result_decl.member_type_templates.push_back(std::move(wrong));
        result_decl.variants.push_back(std::move(variant));
    }
    program.declarations.emplace_back(std::move(result_decl));
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
}

TEST_CASE("P4-C invalid user member template publishes no partial declaration arena") {
    ir::AhflIr program;
    ir::StructDecl bad;
    bad.name = "Bad";
    bad.symbol_ref.kind = ir::SymbolRefKind::Type;
    bad.symbol_ref.canonical_name = "app::Bad";
    bad.symbol_ref.id = 813;
    bad.provenance.source_range = ahfl::SourceRange{11, 29};
    bad.type_param_count = 1;
    bad.type_param_variances = {ir::Variance::Covariant};
    ir::FieldDecl field;
    field.name = "value";
    field.type_ref.kind = ir::TypeRefKind::Any;
    bad.fields.push_back(std::move(field));
    ir::MemberTypeTemplateNode concrete;
    concrete.kind = ir::MemberTypeTemplateKind::Concrete;
    concrete.type_ref.kind = ir::TypeRefKind::Int;
    bad.member_type_templates.push_back(std::move(concrete));
    ir::MemberTypeTemplateNode invalid_param;
    invalid_param.kind = ir::MemberTypeTemplateKind::Param;
    invalid_param.param_index = 1; // owner arity is 1
    bad.member_type_templates.push_back(std::move(invalid_param));
    bad.field_type_template_roots = {1};
    program.declarations.emplace_back(std::move(bad));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kInvalidMemberTemplate));
    const ir::core::CoreTypeDecl *core_bad = nullptr;
    for (const auto &type : result.program.types) {
        if (type.name == "app::Bad") {
            core_bad = &type;
        }
    }
    REQUIRE(core_bad != nullptr);
    CHECK(core_bad->member_type_templates.empty());
    CHECK(core_bad->field_type_template_roots.empty());
    bool ranged = false;
    for (const auto &d : result.diagnostics) {
        if (d.code == ir::core::diag::kInvalidMemberTemplate && d.source_range.has_value()) {
            CHECK(d.source_range->begin_offset == 11);
            CHECK(d.source_range->end_offset == 29);
            ranged = true;
        }
    }
    CHECK(ranged);
}

TEST_CASE("F1 forward-fix (P0-1): a REAL std List decl missing BOTH new fields still drifts") {
    // The exact fail-open the shape-based guess allowed: a real std decl that
    // arrives with type_param_count == 0 AND variances == [] (e.g. legacy /
    // deserialized IR predating the variance fields). The OLD heuristic
    // (`count == 0 && variances.empty()`) would misclassify this as a synthetic
    // base and STAMP it from the descriptor, silently bypassing the drift gate.
    // With explicit RegistrationOrigin::RealIrDecl it must fail closed instead.
    ir::AhflIr program;
    ir::StructDecl list;
    list.name = "List";
    list.symbol_ref.kind = ir::SymbolRefKind::Type;
    list.symbol_ref.canonical_name = "std::collections::List";
    list.symbol_ref.id = 4242;                      // a REAL resolved symbol
    list.provenance.source_range = ahfl::SourceRange{.begin_offset = 5, .end_offset = 9};
    list.type_param_count = 0;                       // BOTH new fields absent —
    list.type_param_variances = {};                  // the old shape guess's blind spot
    program.declarations.emplace_back(std::move(list));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_lower_code(result, ir::core::diag::kBuiltinMetadataDrift));
    CHECK_FALSE(result.is_executable);
    // Principle 5: the drift diagnostic carries the decl's own source range.
    bool ranged = false;
    for (const auto &d : result.diagnostics) {
        if (d.code == ir::core::diag::kBuiltinMetadataDrift && d.source_range.has_value()) {
            CHECK(d.source_range->begin_offset == 5);
            CHECK(d.source_range->end_offset == 9);
            ranged = true;
        }
    }
    CHECK(ranged);
}

namespace {
// A real user generic struct `Box<T> { value: T }` plus a monomorphic consumer
// so the frontend emits + resolves it. Exercises the true vertical bridge:
// Sema variance materialization -> TypedDecl -> AHFL StructDecl -> CoreTypeDecl.
const std::string kUserGenericSource = R"AHFL(
module bx;

struct Box<T> { value: T; }

fn identity(b: Box<Int>) -> Box<Int> effect Pure decreases 0 { return b; }
)AHFL";
} // namespace

TEST_CASE("F1 forward-fix (P1-3): a real frontend Box<T> lowers to Core with its arity + variance") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("user_generic_box", kUserGenericSource);
    REQUIRE(ahfl_ir.has_value());

    // The AHFL-IR StructDecl for Box carries the materialized generic metadata
    // (arity 1, one variance entry) computed by the real Sema variance pass — NOT
    // a hand-set vector. `value: T` is covariant, so T's variance is Covariant.
    const ir::StructDecl *ahfl_box = nullptr;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *s = std::get_if<ir::StructDecl>(&d);
            s != nullptr && (s->name == "Box" || s->symbol_ref.canonical_name == "bx::Box")) {
            ahfl_box = s;
        }
    }
    REQUIRE(ahfl_box != nullptr);
    CHECK(ahfl_box->type_param_count == 1);
    REQUIRE(ahfl_box->type_param_variances.size() == 1);
    CHECK(ahfl_box->type_param_variances[0] == ir::Variance::Covariant);

    auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &diag : result.diagnostics) {
        INFO("unexpected core diagnostic: " << diag.code << " — " << diag.message);
        CHECK(false);
    }
    REQUIRE(result.ok());

    // Box lowers to a CoreTypeDecl preserving arity + variance verbatim (a user
    // nominal is consumed as-is, never cross-checked against the builtin SSOT).
    const ir::core::CoreTypeDecl *core_box = nullptr;
    ir::core::CoreTypeId core_box_id{};
    for (std::uint32_t i = 0; i < result.program.types.size(); ++i) {
        const auto &t = result.program.types[i];
        if (t.name == "bx::Box" || t.name == "Box") {
            core_box = &t;
            core_box_id = ir::core::CoreTypeId{i};
        }
    }
    REQUIRE(core_box != nullptr);
    CHECK(core_box->role == ir::core::CoreNominalRole::Ordinary);
    CHECK(core_box->type_param_count == 1);
    REQUIRE(core_box->variances.size() == 1);
    CHECK(core_box->variances[0] == ir::core::CoreVariance::Covariant);
    // field_nominal_types remains navigation-only, while the logical type is a
    // durable declaration-owned Param(0) template.
    REQUIRE(core_box->fields.size() == 1);
    CHECK(core_box->fields[0] == "value");
    REQUIRE(core_box->field_nominal_types.size() == 1);
    CHECK(core_box->field_nominal_types[0].value == ir::core::CoreTypeId::kInvalid);
    REQUIRE(core_box->member_type_templates.size() == 1);
    CHECK(core_box->member_type_templates[0].kind == ir::core::CoreMemberTypeTemplateKind::Param);
    CHECK(core_box->member_type_templates[0].param_index == 0);
    REQUIRE(core_box->field_type_template_roots ==
            std::vector<ir::core::CoreMemberTypeTemplateNodeId>{
                ir::core::CoreMemberTypeTemplateNodeId{0}});

    ir::TypeRef int_ref;
    int_ref.kind = ir::TypeRefKind::Int;
    std::string reason;
    const auto int_id = ir::core::lower_value_type_into(result.program, int_ref, &reason);
    REQUIRE(int_id.has_value());
    reason.clear();
    const auto field_id = ir::core::instantiate_member_template(
        result.program, core_box_id, ir::core::CoreMemberTypeTemplateNodeId{0}, {*int_id}, &reason);
    INFO(reason);
    REQUIRE(field_id.has_value());
    CHECK(*field_id == *int_id);
}

namespace {
const std::string kRecursiveMemberTemplateSource = R"AHFL(
module mt;

struct Basket<T> {}
struct Assoc<K, V> {}

struct Box<T> {
    value: T;
    nested: Basket<T>;
    transform: Fn(T) -> Basket<T>;
}

enum Packet<T> {
    Empty,
    One(T),
    Named { table: Assoc<String, T> },
}
)AHFL";
} // namespace

TEST_CASE("P4-C C2 materializes recursive member templates through the shared value arena") {
    const auto ahfl =
        lower_source_to_ahfl_ir("recursive_member_templates", kRecursiveMemberTemplateSource);
    REQUIRE(ahfl.has_value());
    auto lowered = ir::core::lower_ahfl_to_core(*ahfl);
    for (const auto &d : lowered.diagnostics) {
        INFO(d.code << ": " << d.message);
    }
    REQUIRE(lowered.ok());

    const auto find_type = [&](std::string_view suffix) -> std::optional<ir::core::CoreTypeId> {
        for (std::uint32_t i = 0; i < lowered.program.types.size(); ++i) {
            const auto &name = lowered.program.types[i].name;
            if (name == suffix || (name.size() > suffix.size() &&
                                   name.ends_with(std::string("::") + std::string(suffix)))) {
                return ir::core::CoreTypeId{i};
            }
        }
        return std::nullopt;
    };
    const auto box_id = find_type("Box");
    const auto packet_id = find_type("Packet");
    REQUIRE(box_id.has_value());
    REQUIRE(packet_id.has_value());
    const auto &box = lowered.program.types[box_id->value];
    REQUIRE(box.field_type_template_roots.size() == 3);
    REQUIRE(box.member_type_templates.size() == 7);
    CHECK(box.member_type_templates[2].kind == ir::core::CoreMemberTypeTemplateKind::Nominal);
    CHECK(box.member_type_templates[6].kind == ir::core::CoreMemberTypeTemplateKind::Fn);

    ir::TypeRef int_ref;
    int_ref.kind = ir::TypeRefKind::Int;
    std::string reason;
    const auto int_id = ir::core::lower_value_type_into(lowered.program, int_ref, &reason);
    REQUIRE(int_id.has_value());
    const std::size_t before = lowered.program.value_types.size();
    const auto nested = ir::core::instantiate_member_template(
        lowered.program, *box_id, box.field_type_template_roots[1], {*int_id}, &reason);
    INFO(reason);
    REQUIRE(nested.has_value());
    const auto *list =
        std::get_if<ir::core::CoreVtNominal>(&lowered.program.value_types[nested->value].node);
    REQUIRE(list != nullptr);
    CHECK(list->args == std::vector<ir::core::CoreValueTypeId>{*int_id});
    CHECK_FALSE(list->capacity.has_value());
    const std::size_t after_first = lowered.program.value_types.size();
    reason.clear();
    const auto nested_again = ir::core::instantiate_member_template(
        lowered.program, *box_id, box.field_type_template_roots[1], {*int_id}, &reason);
    REQUIRE(nested_again.has_value());
    CHECK(*nested_again == *nested);
    CHECK(lowered.program.value_types.size() == after_first);
    CHECK(after_first >= before);

    reason.clear();
    const auto transform = ir::core::instantiate_member_template(
        lowered.program, *box_id, box.field_type_template_roots[2], {*int_id}, &reason);
    INFO(reason);
    REQUIRE(transform.has_value());
    const auto *fn =
        std::get_if<ir::core::CoreVtFn>(&lowered.program.value_types[transform->value].node);
    REQUIRE(fn != nullptr);
    CHECK(fn->params == std::vector<ir::core::CoreValueTypeId>{*int_id});
    CHECK(fn->ret == *nested);

    const auto &packet = lowered.program.types[packet_id->value];
    REQUIRE(packet.variant_payloads.size() == 3);
    REQUIRE(packet.variant_payloads[2].slot_type_template_roots.size() == 1);
    reason.clear();
    const auto table = ir::core::instantiate_member_template(
        lowered.program,
        *packet_id,
        packet.variant_payloads[2].slot_type_template_roots[0],
        {*int_id},
        &reason);
    INFO(reason);
    REQUIRE(table.has_value());
    const auto *map =
        std::get_if<ir::core::CoreVtNominal>(&lowered.program.value_types[table->value].node);
    REQUIRE(map != nullptr);
    REQUIRE(map->args.size() == 2);
    CHECK(std::holds_alternative<ir::core::CoreVtString>(
        lowered.program.value_types[map->args[0].value].node));
    CHECK(map->args[1] == *int_id);
    CHECK_FALSE(map->capacity.has_value());

    const auto second = ir::core::lower_ahfl_to_core(*ahfl);
    REQUIRE(second.ok());
    CHECK(second.program.types == ir::core::lower_ahfl_to_core(*ahfl).program.types);
    CHECK(second.program.value_types == ir::core::lower_ahfl_to_core(*ahfl).program.value_types);
}

// RFC 0026 P6-5 (KR6.6): the `.length` container property. It is a LENGTH READ of
// a BOUNDED collection's inline `(ptr,len)` header, lowered to a typed
// `CoreCollectionExpr{Len}` — NOT a field projection (a collection nominal
// declares no fields). The gate is the LOCAL BINDING's interned logical value
// type judged by the declaration ROLE (never the `length` spelling alone), so a
// struct that legitimately declares a field named `length` keeps the ordinary
// projection path and the two can never be confused.
TEST_CASE("P6-5: `.length` on a bounded collection local lowers to a typed length read") {
    const std::string source = R"(
module app::main;

import std::collections as collections;

pub struct Frame { items: collections::List<Int>(4); }

pub agent A {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Done, Decide];
    initial: Decide;
    final: [Done];
    capabilities: [];
    transition Decide -> Done;
}

flow for A {
    state Decide {
        let xs: collections::List<Int>(4) = input.items;
        let n: Int = xs.length;
        let first: Int = xs[0];
        goto Done;
    }

    state Done { return input; }
}
)";
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("p65_length", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());

    // The length read IS a CoreCollectionExpr{Len} over the container local...
    bool saw_len = false;
    bool saw_element_get = false;
    for (const auto &flow : result.program.flows) {
        for (const auto &expr : flow.storage.exprs) {
            const auto *collection = std::get_if<ir::core::CoreCollectionExpr>(&expr.node);
            if (collection == nullptr) {
                continue;
            }
            if (collection->op == ir::core::CoreCollectionOpKind::Len) {
                saw_len = true;
                // The base's logical value type is the BOUNDED container nominal.
                const auto base_ty = flow.storage.value_types[collection->base.value];
                const auto *nominal = std::get_if<ir::core::CoreVtNominal>(
                    &result.program.value_types[base_ty.value].node);
                REQUIRE(nominal != nullptr);
                CHECK(nominal->capacity.has_value());
            }
            if (collection->op == ir::core::CoreCollectionOpKind::ElementGet) {
                saw_element_get = true;
            }
        }
    }
    CHECK(saw_len);
    CHECK(saw_element_get);
    // ...and never a plain field projection of a `length` member.
    CHECK_FALSE(flow_has_unsupported(result.program, "PathExpr"));
}

TEST_CASE("P6-5: a STRUCT field named `length` is a projection, not a container read") {
    const std::string source = R"(
module app::main;

pub struct Inner { length: Int; }
pub struct Frame { value: String; }

pub agent A {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Done, Decide];
    initial: Decide;
    final: [Done];
    capabilities: [];
    transition Decide -> Done;
}

flow for A {
    state Decide {
        let inner: Inner = Inner { length: 7 };
        let v: Int = inner.length;
        goto Done;
    }

    state Done { return input; }
}
)";
    const auto ahfl_ir = lower_sysroot_source_to_ahfl_ir("p65_length_field", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());

    // The struct field read is a CorePathExpr with a resolved one-step
    // projection, and NO collection op was fabricated for it.
    bool saw_length_path = false;
    for (const auto &flow : result.program.flows) {
        for (const auto &expr : flow.storage.exprs) {
            CHECK_FALSE(std::holds_alternative<ir::core::CoreCollectionExpr>(expr.node));
            if (const auto *path = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
                if (path->members.size() == 1 && path->members.front() == "length") {
                    saw_length_path = true;
                    CHECK(path->projection_resolved);
                    CHECK(path->projection.size() == 1);
                }
            }
        }
    }
    CHECK(saw_length_path);
}

// ===========================================================================
// RFC 0026 FB-2 (CORE-FNBODY-DESIGN §8): end-to-end recursion lattice through
// the REAL front end. These fixtures parse/resolve/typecheck/lower a bounded
// recursive fn (the `decreases` measure is required at the AHFL layer but
// deliberately UNREAD by the Core lattice) and assert the verifier seals /
// rejects them with the FB-2 codes.
// ===========================================================================
namespace {

const std::string kFb2BoundedRecursionSource = R"AHFL(
module std::collections;

pub struct List<T> { }

@builtin("list_raw_get")
fn list_raw_get<T>(xs: List<T>, i: Int) -> T effect Pure;

@builtin("list_raw_length")
fn list_raw_length<T>(xs: List<T>) -> Int effect Pure;

fn sum_into(xs: List<Int>(4), i: Int, acc: Int) -> Int effect Pure
    decreases list_raw_length<Int>(xs) - i {
    if i >= list_raw_length<Int>(xs) {
        return acc;
    }
    return sum_into(xs, i + 1, acc + list_raw_get<Int>(xs, i));
}

struct Frame { items: List<Int>(4); }

agent RecAgent {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for RecAgent {
    state Init {
        let total: Int = sum_into(input.items, 0, 0);
        goto Done;
    }
    state Done {
        return input;
    }
}
)AHFL";

// A self-recursive fn with NO rank evidence (the recursive call does not
// progress an integer parameter) and a base guard that can never fire.
const std::string kFb2UnboundedRecursionSource = R"AHFL(
module er;

fn loop_forever(n: Int) -> Int effect Pure decreases n {
    return loop_forever(n);
}

struct Frame { v: Int; }

agent LoopAgent {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for LoopAgent {
    state Init {
        let x: Int = loop_forever(input.v);
        goto Done;
    }
    state Done {
        return input;
    }
}
)AHFL";

// Self recursion that DOES progress the rank (i+1) but guards against a
// RUNTIME parameter n rather than a bounded value, so the initial depth is not
// statically derivable at the entry.
const std::string kFb2UnboundedMutualSource = R"AHFL(
module er;

fn climb(n: Int, i: Int) -> Int effect Pure decreases n - i {
    if i >= n {
        return i;
    }
    return climb(n, i + 1);
}

struct Frame { v: Int; }

agent ClimbAgent {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for ClimbAgent {
    state Init {
        let x: Int = climb(input.v, 0);
        goto Done;
    }
    state Done {
        return input;
    }
}
)AHFL";

} // namespace

TEST_CASE("FB-2 e2e: bounded list recursion lowers clean and the lattice seals its depth") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("fb2_bounded", kFb2BoundedRecursionSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected: " << d.code << " - " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    REQUIRE(result.is_executable);

    // Exactly one fn body (the non-generic sum_into); it is self-recursive and
    // the lattice sealed it from the capacity-4 bounded list.
    REQUIRE(result.program.fns.size() == 1);
    const auto analysis = ir::core::analyze_fn_recursion(result.program);
    CHECK(analysis.unbounded_issues.empty());
    CHECK(analysis.overflow_sccs.empty());
    REQUIRE(analysis.sccs.size() == 1);
    CHECK(analysis.sccs[0].members == std::vector<std::uint32_t>{0});
    // entry i=0, guard i >= len with len in [0,4]: at most len+1 frames = 5.
    CHECK(analysis.sccs[0].depth_bound == 5);

    // The self call survived lowering as a CoreCallExpr to the same instance.
    const ir::core::CoreFnDecl &fn = result.program.fns[0];
    bool self_call = false;
    for (const ir::core::CoreExpr &expr : fn.storage.exprs) {
        if (const auto *call = std::get_if<ir::core::CoreCallExpr>(&expr.node)) {
            if (call->callee == fn.instance) {
                self_call = true;
            }
        }
    }
    CHECK(self_call);
}

TEST_CASE("FB-2 e2e: self recursion without rank evidence is fail-closed") {
    const auto ahfl_ir =
        lower_source_to_ahfl_ir("fb2_unbounded", kFb2UnboundedRecursionSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(has_verify_prefixed_diagnostic(result));
    bool has_code = false;
    for (const auto &d : result.diagnostics) {
        if (d.code == "core.verify.FN_RECURSION_UNBOUNDED") {
            has_code = true;
        }
    }
    CHECK(has_code);
}

TEST_CASE("FB-2 e2e: recursion bounded by a runtime parameter is fail-closed") {
    const auto ahfl_ir =
        lower_source_to_ahfl_ir("fb2_climb", kFb2UnboundedMutualSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK_FALSE(result.ok());
    bool has_code = false;
    for (const auto &d : result.diagnostics) {
        if (d.code == "core.verify.FN_RECURSION_UNBOUNDED") {
            has_code = true;
        }
    }
    CHECK(has_code);
}

// FB-1 fix-forward: a legal whole-program FORWARD reference (a non-generic fn
// declared BEFORE the callee it calls) must lower with the resolver fixed
// point fully published — declaration order must not decide resolvability.
const std::string kFb1ForwardReferenceSource = R"AHFL(
module fwd;

fn first(a: Int) -> Int {
    return second(a) + 1;
}

fn second(a: Int) -> Int {
    return a * 3;
}

struct Frame { v: Int; }

agent ForwardAgent {
    input: Frame;
    context: Unit;
    output: Frame;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}

flow for ForwardAgent {
    state Init {
        let x: Int = first(10);
        goto Done;
    }
    state Done {
        return input;
    }
}
)AHFL";

TEST_CASE("FB-1 e2e: a forward-referenced callee lowers regardless of declaration order") {
    const auto ahfl_ir =
        lower_source_to_ahfl_ir("fb1_forward", kFb1ForwardReferenceSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK(result.ok());
    CHECK(result.is_executable);
    bool saw_first = false;
    bool saw_second = false;
    for (const auto &fn : result.program.fns) {
        for (const auto &expr : fn.storage.exprs) {
            if (std::holds_alternative<ir::core::CoreCallExpr>(expr.node)) {
                saw_first = true; // first() contains a CoreCallExpr to second()
            }
        }
        if (fn.name.find("second") != std::string::npos) {
            saw_second = true;
        }
    }
    CHECK(saw_first);
    CHECK(saw_second);
}

// ============================================================================
// CORE-GAPS (RFC 0026 P6): `a => b` has no Core opcode and must lower to the
// Sema truth shape `!a || b` — eagerly (both operands lowered, effects hoisted
// by A-normalization), never to a CoreUnsupportedExpr.
// ============================================================================

namespace {

const std::string kImpliesSource = R"AHFL(
module app::main;

struct Request { x: Int; }
struct Context { }
struct Response { ok: Bool; }

agent A {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let p: Bool = (input.x > 0) => (input.x < 10);
        return Response { ok: p };
    }
}
)AHFL";

} // namespace

TEST_CASE("CORE-GAPS: Implies lowers to Not(lhs) Or rhs, not an unsupported node") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("implies_shape", kImpliesSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreFlowDecl *only_flow = nullptr;
    REQUIRE(result.program.flows.size() == 1);
    only_flow = &result.program.flows.front();

    // A-normal form binds the Not to its own let; the Or references it through
    // a CoreValueRefExpr. Build the value -> defining-expr map from lets.
    std::unordered_map<int, int> def_of_value;
    for (const auto &fs : only_flow->states) {
        for (const auto &stmt : fs.body.statements) {
            if (const auto *let = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                def_of_value.emplace(let->result.value, let->expr.value);
            }
        }
    }

    bool saw_or = false;
    bool saw_not = false;
    bool saw_unsupported = false;
    for (const auto &expr : only_flow->storage.exprs) {
        if (const auto *bin = std::get_if<ir::core::CoreBinaryExpr>(&expr.node)) {
            if (bin->op == ir::core::CoreBinaryOp::Or) {
                saw_or = true;
                // The Or's lhs must be the let-bound Not over the implies lhs.
                const auto &lhs_expr = only_flow->storage.exprs[bin->lhs.value];
                const auto *ref = std::get_if<ir::core::CoreValueRefExpr>(&lhs_expr.node);
                REQUIRE(ref != nullptr);
                const auto found = def_of_value.find(ref->value.value);
                REQUIRE(found != def_of_value.end());
                const auto &def = only_flow->storage.exprs[found->second];
                const auto *un = std::get_if<ir::core::CoreUnaryExpr>(&def.node);
                REQUIRE(un != nullptr);
                CHECK(un->op == ir::core::CoreUnaryOp::Not);
                saw_not = true;
            }
        }
        if (std::get_if<ir::core::CoreUnsupportedExpr>(&expr.node) != nullptr) {
            saw_unsupported = true;
        }
    }
    CHECK(saw_or);
    CHECK(saw_not);
    CHECK_FALSE(saw_unsupported);
}
