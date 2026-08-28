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

} // namespace
