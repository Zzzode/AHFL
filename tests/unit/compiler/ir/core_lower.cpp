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
#include <functional>
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
    for (const auto &expr : flow.exprs) {
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

TEST_CASE("builtin variant table matches stdlib declaration order (production vs sysroot)") {
    // This reads the ACTUAL production builtin table (builtin_enum_table()) and
    // asserts each descriptor's variant order equals the sysroot declaration
    // order parsed from std/*.ahfl. Reordering EITHER the production table OR
    // the stdlib source now fails this test — closing the drift Codex flagged
    // (the previous version only checked the source against a hard-coded copy).
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
        const auto sysroot_order = read_enum_variant_order(path, name);
        REQUIRE(sysroot_order.size() >= desc.variants.size());
        for (std::size_t i = 0; i < desc.variants.size(); ++i) {
            INFO("enum " << name << " variant #" << i);
            CHECK(std::string(desc.variants[i]) == sysroot_order[i]);
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
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    // Option::Some(0) as a variant-constructor CallExpr (callee_ref -> Enum).
    ir::ExprRef arg = program.expr_arena.make(ir::IntegerLiteralExpr{"0"});
    ir::CallExpr call;
    call.callee = "std::option::Option::Some";
    call.callee_ref.kind = ir::SymbolRefKind::Type; // the front end resolves to the Enum symbol
    call.callee_ref.canonical_name = "std::option::Option";
    call.arguments.push_back(arg);
    ir::ExprRef call_ref = program.expr_arena.make(std::move(call));

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
    for (const auto &expr : flow_out.exprs) {
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
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";

    const auto make_let = [&](const std::string &name, const std::string &val) {
        ir::ExprRef init = program.expr_arena.make(ir::IntegerLiteralExpr{val});
        auto s = std::make_unique<ir::Statement>();
        ir::LetStatement let;
        let.name = name;
        let.initializer = init;
        s->node = std::move(let);
        return s;
    };
    // let x = 1;
    handler.body.statements.push_back(make_let("x", "1"));
    // if true { let x = 2; } else { let x = 3; }
    {
        ir::ExprRef cond = program.expr_arena.make(ir::BoolLiteralExpr{true});
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
    // A MatchExpr (no capability) is not yet lowered. Even without an effect,
    // it must FAIL closed — a CoreUnsupportedExpr the backend cannot execute
    // may never be reported as executable while no Core verifier exists.
    const auto program = make_single_handler_flow([](ir::AhflIr &p) {
        // return match on a bare bool literal (unsupported, pure).
        ir::ExprRef scrut = p.expr_arena.make(ir::BoolLiteralExpr{true});
        ir::MatchExpr m;
        m.scrutinee = scrut;
        ir::ExprRef mref = p.expr_arena.make(std::move(m));
        auto stmt = std::make_unique<ir::Statement>();
        stmt->node = ir::ReturnStatement{mref};
        return stmt;
    });
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredExpression);
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
    for (const auto &expr : flow.exprs) {
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
                    if (let->result == v && let->expr.value < flow.exprs.size()) {
                        const auto &e = flow.exprs[let->expr.value].node;
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
    for (const auto &expr : flow.exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 1 &&
                p->members[0] == "top") {
                saw_input_top_read = true;
                CHECK(p->members_resolved);
                REQUIRE(p->member_fields.size() == 1);
                CHECK(p->member_fields[0].value == 0u); // Req.top is field 0
            }
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 2 &&
                p->members[0] == "inner" && p->members[1] == "n") {
                saw_input_inner_n_read = true;
                CHECK(p->members_resolved);
                REQUIRE(p->member_fields.size() == 2);
                CHECK(p->member_fields[0].value == 1u); // Req.inner is field 1
                CHECK(p->member_fields[1].value == 0u); // Inner.n is field 0
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
                CHECK(pl.members_resolved);
                REQUIRE(pl.member_fields.size() == 1);
                CHECK(pl.member_fields[0].value == 0u); // Ctx.saved is field 0
            }
            if (pl.root == ir::core::CorePathRoot::Context && pl.members.size() == 2 &&
                pl.members[0] == "nested" && pl.members[1] == "n") {
                saw_ctx_nested_n_store = true;
                CHECK(pl.members_resolved);
                REQUIRE(pl.member_fields.size() == 2);
                CHECK(pl.member_fields[0].value == 1u); // Ctx.nested is field 1
                CHECK(pl.member_fields[1].value == 0u); // Inner.n is field 0
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
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredFieldProjection);
}
