#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/program.hpp"

#include <string>
#include <variant>
// RFC 0026 slice P3/P4 first increment (KR6.4): scaffold coverage for the
// AhflIr -> Core-IR lowering. Builds a tiny AhflIr with one AgentDecl (plus a
// verification-only ContractDecl that must NOT leak into Core-IR), runs
// `lower_ahfl_to_core`, and asserts the CoreProgram carries the agent's state
// machine with index-based state identity and that no verification construct
// crossed the layer boundary. A determinism check (lower twice -> equal)
// guards the pass's stability.

namespace {

using namespace ahfl;

// Build a minimal single-agent AhflIr: 3 states, 1 initial, 1 final, 2
// transitions, quota + a ContractDecl attached (both verification-layer, must
// be erased). Kept hand-built (no frontend) so the test isolates the lower
// pass from parsing/typechecking.
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
    agent.transitions = {
        ir::TransitionDecl{"Init", "Working"},
        ir::TransitionDecl{"Working", "Done"},
    };

    program.declarations.emplace_back(std::move(agent));

    // A verification-only contract targeting the agent — this must be dropped
    // by the execution-layer lowering (RFC 0026 erasure invariant).
    ir::ContractDecl contract;
    contract.target_ref.kind = ir::SymbolRefKind::Agent;
    contract.target_ref.canonical_name = "app::Classifier";
    program.declarations.emplace_back(std::move(contract));

    return program;
}

// Build a minimal AhflIr containing (in source order) one CapabilityDecl, one
// AgentDecl, and one verification-only ContractDecl. Exercises the effect ->
// explicit capability-call lowering: the capability must produce a
// CoreCapabilityDecl carrying its symbol_ref + effect kind + signature, while
// the contract must be erased and the agent must still lower.
ir::AhflIr make_capability_program() {
    ir::AhflIr program;

    // capability ChargeCard(amount: Int) -> Bool;  effect: financial write.
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
    // Orchestration-only spec fields that must NOT survive to Core-IR.
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

    // Verification-only contract: must be erased.
    ir::ContractDecl contract;
    contract.target_ref.kind = ir::SymbolRefKind::Agent;
    contract.target_ref.canonical_name = "app::Classifier";
    program.declarations.emplace_back(std::move(contract));

    return program;
}

TEST_CASE("lower_ahfl_to_core lowers an agent state machine into Core-IR") {
    const ir::AhflIr program = make_single_agent_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);

    // The ContractDecl is a verification construct: it must NOT produce a
    // Core-IR declaration. Exactly one CoreAgentDecl is expected.
    REQUIRE(core.declarations.size() == 1);
    REQUIRE(std::holds_alternative<ir::core::CoreAgentDecl>(core.declarations[0]));
    const auto &agent = std::get<ir::core::CoreAgentDecl>(core.declarations[0]);

    CHECK(agent.name == "Classifier");
    CHECK(agent.symbol_ref.canonical_name == "app::Classifier");
    CHECK_FALSE(agent.instance_key.empty());

    // States carried, in declaration order (index-based identity).
    REQUIRE(agent.states.size() == 3);
    CHECK(agent.states[0] == "Init");
    CHECK(agent.states[1] == "Working");
    CHECK(agent.states[2] == "Done");

    // Initial and final states addressed by index into `states`.
    CHECK(agent.initial.value == 0);
    CHECK(agent.states[agent.initial.value] == "Init");
    REQUIRE(agent.finals.size() == 1);
    CHECK(agent.states[agent.finals[0].value] == "Done");

    // Transitions resolved to state indices.
    REQUIRE(agent.transitions.size() == 2);
    CHECK(agent.states[agent.transitions[0].from.value] == "Init");
    CHECK(agent.states[agent.transitions[0].to.value] == "Working");
    CHECK(agent.states[agent.transitions[1].from.value] == "Working");
    CHECK(agent.states[agent.transitions[1].to.value] == "Done");

    // The execution layer stamps its own format version.
    CHECK(core.format_version == std::string(ir::core::kCoreFormatVersion));
}

TEST_CASE("lower_ahfl_to_core does not leak verification constructs into Core-IR") {
    const ir::AhflIr program = make_single_agent_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);

    // The CoreDecl variant has no alternative capable of representing a
    // contract / temporal / decreases node — the layer is defined so that a
    // verification construct is *unrepresentable* at Core-IR. The behavioural
    // check: the ContractDecl in the source program produced no declaration,
    // and the sole emitted declaration is the agent skeleton.
    for (const auto &decl : core.declarations) {
        CHECK(std::holds_alternative<ir::core::CoreAgentDecl>(decl));
    }
    CHECK(core.declarations.size() == 1);
}

TEST_CASE("lower_ahfl_to_core is deterministic") {
    const ir::AhflIr program = make_single_agent_program();
    const ir::core::CoreProgram a = ir::core::lower_ahfl_to_core(program);
    const ir::core::CoreProgram b = ir::core::lower_ahfl_to_core(program);

    REQUIRE(a.declarations.size() == b.declarations.size());
    CHECK(a.format_version == b.format_version);

    const auto &agent_a = std::get<ir::core::CoreAgentDecl>(a.declarations[0]);
    const auto &agent_b = std::get<ir::core::CoreAgentDecl>(b.declarations[0]);
    CHECK(agent_a.name == agent_b.name);
    CHECK(agent_a.instance_key == agent_b.instance_key);
    CHECK(agent_a.states == agent_b.states);
    CHECK(agent_a.initial == agent_b.initial);
    CHECK(agent_a.finals == agent_b.finals);
    CHECK(agent_a.transitions == agent_b.transitions);
}

TEST_CASE("lower_ahfl_to_core lowers a capability into an explicit capability-call") {
    const ir::AhflIr program = make_capability_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);

    // Source order: CapabilityDecl, AgentDecl, ContractDecl. The contract is a
    // verification construct and must be erased, so exactly two Core-IR
    // declarations are expected — the capability-call and the agent skeleton,
    // in that source order.
    REQUIRE(core.declarations.size() == 2);
    REQUIRE(std::holds_alternative<ir::core::CoreCapabilityDecl>(core.declarations[0]));
    REQUIRE(std::holds_alternative<ir::core::CoreAgentDecl>(core.declarations[1]));

    const auto &cap = std::get<ir::core::CoreCapabilityDecl>(core.declarations[0]);

    // Canonical identity (Principle 2) is reused from the source symbol_ref.
    CHECK(cap.name == "ChargeCard");
    CHECK(cap.symbol_ref.kind == ir::SymbolRefKind::Capability);
    CHECK(cap.symbol_ref.canonical_name == "app::ChargeCard");
    REQUIRE(cap.symbol_ref.id.has_value());
    CHECK(cap.symbol_ref.id.value() == 11);

    // The effect KIND survives as the import's classification.
    CHECK(cap.effect_kind == ir::CapabilityEffectKind::FinancialWrite);

    // Marshalling signature: one Int param, Bool return.
    REQUIRE(cap.param_types.size() == 1);
    CHECK(cap.param_types[0].kind == ir::TypeRefKind::Int);
    CHECK(cap.return_type_ref.kind == ir::TypeRefKind::Bool);
}

TEST_CASE("lower_ahfl_to_core erases capability effect-spec metadata beyond the kind") {
    const ir::AhflIr program = make_capability_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);

    // Every emitted declaration is either a capability-call or an agent — no
    // Core-IR alternative can represent the erased orchestration metadata
    // (domain / receipt / retry / …) or a verification construct. The layer is
    // defined so those are *unrepresentable*; this is the behavioural witness.
    for (const auto &decl : core.declarations) {
        CHECK((std::holds_alternative<ir::core::CoreCapabilityDecl>(decl) ||
               std::holds_alternative<ir::core::CoreAgentDecl>(decl)));
    }
    REQUIRE(core.declarations.size() == 2);
}

TEST_CASE("lower_ahfl_to_core is deterministic for capability + agent programs") {
    const ir::AhflIr program = make_capability_program();
    const ir::core::CoreProgram a = ir::core::lower_ahfl_to_core(program);
    const ir::core::CoreProgram b = ir::core::lower_ahfl_to_core(program);

    REQUIRE(a.declarations.size() == b.declarations.size());
    REQUIRE(a.declarations.size() == 2);

    const auto &cap_a = std::get<ir::core::CoreCapabilityDecl>(a.declarations[0]);
    const auto &cap_b = std::get<ir::core::CoreCapabilityDecl>(b.declarations[0]);
    CHECK(cap_a.name == cap_b.name);
    CHECK(cap_a.symbol_ref.canonical_name == cap_b.symbol_ref.canonical_name);
    CHECK(cap_a.effect_kind == cap_b.effect_kind);
    REQUIRE(cap_a.param_types.size() == cap_b.param_types.size());
    CHECK(cap_a.param_types[0].kind == cap_b.param_types[0].kind);
    CHECK(cap_a.return_type_ref.kind == cap_b.return_type_ref.kind);
}

// ---------------------------------------------------------------------------
// Flow capability call-site lowering (KR6.4 call-site sub-slice)
// ---------------------------------------------------------------------------

// Build an AhflIr with a capability decl and a flow whose two state handlers
// invoke capabilities: state Init calls Fetch("q") directly; state Work calls
// Charge(amount) inside an if-branch and also has a nested-call argument that
// must lower to an Opaque arg (arity preserved). Exercises effect-kind
// resolution, argument lowering, and if-branch descent.
ir::AhflIr make_flow_program() {
    ir::AhflIr program;

    // capability Fetch(query: String) -> String;  effect: Read.
    ir::CapabilityDecl fetch;
    fetch.name = "Fetch";
    fetch.symbol_ref.kind = ir::SymbolRefKind::Capability;
    fetch.symbol_ref.canonical_name = "app::Fetch";
    fetch.symbol_ref.local_name = "Fetch";
    fetch.symbol_ref.id = 21;
    fetch.effect.declared = true;
    fetch.effect.kind = ir::CapabilityEffectKind::Read;
    program.declarations.emplace_back(std::move(fetch));

    // capability Charge(amount: Int) -> Bool;  effect: FinancialWrite.
    ir::CapabilityDecl charge;
    charge.name = "Charge";
    charge.symbol_ref.kind = ir::SymbolRefKind::Capability;
    charge.symbol_ref.canonical_name = "app::Charge";
    charge.symbol_ref.local_name = "Charge";
    charge.symbol_ref.id = 22;
    charge.effect.declared = true;
    charge.effect.kind = ir::CapabilityEffectKind::FinancialWrite;
    program.declarations.emplace_back(std::move(charge));

    ir::AgentDecl agent;
    agent.name = "Payer";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Payer";
    agent.symbol_ref.local_name = "Payer";
    agent.symbol_ref.id = 7;
    agent.states = {"Init", "Work"};
    agent.initial_state = "Init";
    agent.final_states = {"Work"};
    program.declarations.emplace_back(std::move(agent));

    // flow for Payer { state Init { Fetch("q"); }  state Work { if (c) { Charge(input.n); } } }
    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::Payer";
    flow.target_ref.local_name = "Payer";
    flow.target_ref.id = 7;

    // --- state Init: Fetch("q"); as an ExprStatement ---
    {
        ir::CallExpr call;
        call.callee = "Fetch";
        call.callee_ref.kind = ir::SymbolRefKind::Capability;
        call.callee_ref.canonical_name = "app::Fetch";
        call.callee_ref.id = 21;
        ir::ExprRef arg =
            program.expr_arena.make(ir::StringLiteralExpr{"\"q\""});
        call.arguments.push_back(arg);
        ir::ExprRef call_ref = program.expr_arena.make(std::move(call));

        auto init_stmt = std::make_unique<ir::Statement>();
        init_stmt->node = ir::ExprStatement{call_ref};

        ir::StateHandler init;
        init.state_name = "Init";
        init.body.statements.push_back(std::move(init_stmt));
        flow.state_handlers.push_back(std::move(init));
    }

    // --- state Work: if (c) { Charge(input.n + 1); } ---
    // The argument is a BinaryExpr (input.n + 1) -> must lower to Opaque.
    {
        // Build the opaque nested argument expr: input.n + 1.
        ir::PathExpr lhs_path;
        lhs_path.path.root_name = "input";
        lhs_path.path.members = {"n"};
        ir::ExprRef lhs = program.expr_arena.make(std::move(lhs_path));
        ir::ExprRef rhs = program.expr_arena.make(ir::IntegerLiteralExpr{"1"});
        ir::BinaryExpr sum;
        sum.op = ir::ExprBinaryOp::Add;
        sum.lhs = lhs;
        sum.rhs = rhs;
        ir::ExprRef sum_ref = program.expr_arena.make(std::move(sum));

        ir::CallExpr call;
        call.callee = "Charge";
        call.callee_ref.kind = ir::SymbolRefKind::Capability;
        call.callee_ref.canonical_name = "app::Charge";
        call.callee_ref.id = 22;
        call.arguments.push_back(sum_ref);
        ir::ExprRef call_ref = program.expr_arena.make(std::move(call));

        auto charge_stmt = std::make_unique<ir::Statement>();
        charge_stmt->node = ir::ExprStatement{call_ref};

        auto then_block = std::make_unique<ir::Block>();
        then_block->statements.push_back(std::move(charge_stmt));

        ir::ExprRef cond = program.expr_arena.make(ir::BoolLiteralExpr{true});
        ir::IfStatement if_stmt;
        if_stmt.condition = cond;
        if_stmt.then_block = std::move(then_block);

        auto work_stmt = std::make_unique<ir::Statement>();
        work_stmt->node = std::move(if_stmt);

        ir::StateHandler work;
        work.state_name = "Work";
        work.body.statements.push_back(std::move(work_stmt));
        flow.state_handlers.push_back(std::move(work));
    }

    program.declarations.emplace_back(std::move(flow));
    return program;
}

TEST_CASE("lower_ahfl_to_core extracts flow capability call sites") {
    const ir::AhflIr program = make_flow_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);

    // Source order: Fetch cap, Charge cap, Payer agent, flow. All four lower
    // (two CoreCapabilityDecl, one CoreAgentDecl, one CoreFlowDecl).
    REQUIRE(core.declarations.size() == 4);
    REQUIRE(std::holds_alternative<ir::core::CoreFlowDecl>(core.declarations[3]));
    const auto &flow = std::get<ir::core::CoreFlowDecl>(core.declarations[3]);

    CHECK(flow.agent_name == "Payer");
    CHECK(flow.target_ref.canonical_name == "app::Payer");
    REQUIRE(flow.states.size() == 2);

    // state Init: one Fetch call with a single string-literal arg, effect Read.
    const auto &init = flow.states[0];
    CHECK(init.state_name == "Init");
    REQUIRE(init.calls.size() == 1);
    CHECK(init.calls[0].callee_name == "Fetch");
    CHECK(init.calls[0].callee_ref.canonical_name == "app::Fetch");
    CHECK(init.calls[0].effect_kind == ir::CapabilityEffectKind::Read);
    REQUIRE(init.calls[0].args.size() == 1);
    CHECK(init.calls[0].args[0].kind == ir::core::CoreCallArg::Kind::Literal);
    CHECK(init.calls[0].args[0].text == "\"q\"");

    // state Work: the Charge call is INSIDE an if-branch and must still be
    // found; its arg is a nested expression -> Opaque (arity preserved).
    const auto &work = flow.states[1];
    CHECK(work.state_name == "Work");
    REQUIRE(work.calls.size() == 1);
    CHECK(work.calls[0].callee_name == "Charge");
    CHECK(work.calls[0].effect_kind == ir::CapabilityEffectKind::FinancialWrite);
    REQUIRE(work.calls[0].args.size() == 1);
    CHECK(work.calls[0].args[0].kind == ir::core::CoreCallArg::Kind::Opaque);
}

TEST_CASE("lower_ahfl_to_core flow lowering is deterministic") {
    const ir::AhflIr program = make_flow_program();
    const ir::core::CoreProgram a = ir::core::lower_ahfl_to_core(program);
    const ir::core::CoreProgram b = ir::core::lower_ahfl_to_core(program);

    REQUIRE(a.declarations.size() == b.declarations.size());
    const auto &flow_a = std::get<ir::core::CoreFlowDecl>(a.declarations[3]);
    const auto &flow_b = std::get<ir::core::CoreFlowDecl>(b.declarations[3]);
    // The new value-type equality (SymbolRef == plus derived node ==) makes the
    // whole flow projection comparable structurally.
    CHECK(flow_a == flow_b);
}

TEST_CASE("lower_ahfl_to_core never silently drops a capability-call argument") {
    const ir::AhflIr program = make_flow_program();
    const ir::core::CoreProgram core = ir::core::lower_ahfl_to_core(program);
    const auto &flow = std::get<ir::core::CoreFlowDecl>(core.declarations[3]);

    // Every call site preserves arity: the arg count equals the source arg
    // count even when an argument is an unlowered (Opaque) expression.
    for (const auto &state : flow.states) {
        for (const auto &call : state.calls) {
            CHECK(call.args.size() >= 1);
            for (const auto &arg : call.args) {
                // No arg is left with an empty descriptor — Opaque args still
                // carry an observable reason string.
                CHECK_FALSE(arg.text.empty());
            }
        }
    }
}

} // namespace
