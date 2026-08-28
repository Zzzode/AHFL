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

} // namespace
