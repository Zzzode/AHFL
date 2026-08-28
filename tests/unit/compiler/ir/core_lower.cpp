#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <cctype>
#include <cstdio>
#include <string>
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
    program.declarations.emplace_back(std::move(cap));

    ir::AgentDecl agent;
    agent.name = "Classifier";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Classifier";
    agent.symbol_ref.local_name = "Classifier";
    agent.symbol_ref.id = 7;
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {ir::TransitionDecl{"Init", "Done"}};
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
    REQUIRE(cap.param_types.size() == 1);
    CHECK(cap.param_types[0].kind == ir::TypeRefKind::Int);
    CHECK(cap.return_type_ref.kind == ir::TypeRefKind::Bool);
}

TEST_CASE("lower_ahfl_to_core is deterministic (decl level)") {
    const auto program = make_capability_program();
    const auto a = ir::core::lower_ahfl_to_core(program);
    const auto b = ir::core::lower_ahfl_to_core(program);
    REQUIRE(a.program.capabilities.size() == b.program.capabilities.size());
    REQUIRE(a.program.agents.size() == b.program.agents.size());
    CHECK(a.program.agents[0].instance_key == b.program.agents[0].instance_key);
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
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    CHECK(result.ok());
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
                CHECK(callee_is(then_calls[0]->callee_name, "Charge"));
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
    REQUIRE(result.ok());
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
    CHECK(result.ok());
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
    for (const auto &expr : flow.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &ctor = std::get<ir::core::CoreConstructExpr>(expr.node);
            for (const auto &arg : ctor.args) {
                if (arg == charge_result) {
                    result_consumed_by_construct = true;
                    // The Wrap variant resolves to a typed index (Wrap=0).
                    CHECK(ctor.is_enum_variant);
                    CHECK(ctor.variant_resolved);
                    CHECK(ctor.variant == 0u);
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
    CHECK(result.diagnostics[0].code == "core.UNRESOLVED_CAPABILITY_CALL");
    CHECK(result.diagnostics[0].source_range.has_value());
}

// ==========================================================================
// Builtin Option/Result variant order must match the sysroot declaration.
// ==========================================================================

namespace {

// Extract the variant names of the first `enum <Name>` block in an AHFL source
// file, in declaration order. Minimal textual scan (no full parse) — enough to
// pin the sysroot declaration order the builtin table must mirror.
std::vector<std::string> read_enum_variant_order(const std::string &path,
                                                 const std::string &enum_name) {
    std::vector<std::string> variants;
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

    const auto enum_pos = text.find("enum " + enum_name);
    if (enum_pos == std::string::npos) {
        return variants;
    }
    const auto open = text.find('{', enum_pos);
    const auto close = text.find('}', open);
    if (open == std::string::npos || close == std::string::npos) {
        return variants;
    }
    const std::string body = text.substr(open + 1, close - open - 1);
    // Each variant is the leading identifier of a comma-separated entry. Skip
    // any parenthesized payload so a payload type name (e.g. the `T` in
    // `Some(T)`) is not mistaken for the next variant.
    std::string token;
    int paren_depth = 0;
    for (char c : body) {
        if (c == '(') {
            // The identifier accumulated so far is the variant name; the payload
            // that follows is skipped.
            if (!token.empty()) {
                variants.push_back(token);
                token.clear();
            }
            ++paren_depth;
            continue;
        }
        if (c == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
            continue;
        }
        if (paren_depth > 0) {
            continue; // inside a payload: ignore
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            token.push_back(c);
        } else {
            if (!token.empty()) {
                variants.push_back(token);
                token.clear();
            }
        }
    }
    if (!token.empty()) {
        variants.push_back(token);
    }
    return variants;
}

} // namespace

TEST_CASE("builtin Option/Result variant order matches stdlib declaration order") {
    // The lowerer's builtin variant table (Option -> [Some, None], Result ->
    // [Ok, Err]) must mirror the sysroot declaration order. If someone reorders
    // std/option.ahfl or std/result.ahfl, this test fails, forcing the table to
    // be updated in lockstep (the drift Codex flagged).
    const auto option_variants = read_enum_variant_order("std/option.ahfl", "Option");
    REQUIRE(option_variants.size() >= 2);
    CHECK(option_variants[0] == "Some"); // builtin table: Some=0
    CHECK(option_variants[1] == "None"); // builtin table: None=1

    const auto result_variants = read_enum_variant_order("std/result.ahfl", "Result");
    REQUIRE(result_variants.size() >= 2);
    CHECK(result_variants[0] == "Ok");  // builtin table: Ok=0
    CHECK(result_variants[1] == "Err"); // builtin table: Err=1
}
