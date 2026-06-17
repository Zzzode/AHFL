#include "verification/formal/state_space_estimator.hpp"

#include "ahfl/compiler/ir/ir.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <variant>

namespace ahfl::formal {

namespace {

constexpr std::size_t tractability_threshold = 1'000'000; // 10^6

} // namespace

[[nodiscard]] StateSpaceEstimate estimate_state_space(const std::vector<AgentMetrics> &agents) {
    StateSpaceEstimate estimate;
    estimate.num_agents = agents.size();
    estimate.estimation_method = "multiplicative";

    if (agents.empty()) {
        estimate.estimated_states = 0;
        estimate.likely_tractable = true;
        return estimate;
    }

    // Multiplicative state space: product of all agent state counts
    std::size_t product = 1;
    bool overflow = false;
    std::size_t max_states = 0;
    std::size_t total_trans = 0;

    for (const auto &agent : agents) {
        if (agent.state_count == 0) {
            product = 0;
            break;
        }

        // Check for overflow before multiplying
        if (product > std::numeric_limits<std::size_t>::max() / agent.state_count) {
            overflow = true;
            product = std::numeric_limits<std::size_t>::max();
            break;
        }
        product *= agent.state_count;

        max_states = std::max(max_states, agent.state_count);
        total_trans += agent.transition_count;
    }

    estimate.estimated_states = product;
    estimate.max_states_per_agent = max_states;
    estimate.total_transitions = total_trans;

    if (overflow) {
        estimate.likely_tractable = false;
        estimate.warning =
            "State space exceeds representable limit; verification likely intractable";
    } else if (product > tractability_threshold) {
        estimate.likely_tractable = false;
        estimate.warning =
            "State space exceeds tractability threshold (10^6); consider abstraction";
    } else {
        estimate.likely_tractable = true;
    }

    return estimate;
}

std::vector<AgentMetrics> collect_agent_metrics(const ahfl::ir::Program &program) {
    std::vector<AgentMetrics> agents;
    for (const auto &declaration : program.declarations) {
        const auto *agent = std::get_if<ahfl::ir::AgentDecl>(&declaration);
        if (agent == nullptr) {
            continue;
        }

        agents.push_back(AgentMetrics{
            .agent_name = agent->name,
            .state_count = agent->states.size(),
            .transition_count = agent->transitions.size(),
            .capability_count = agent->capability_refs.size(),
        });
    }
    return agents;
}

StateSpaceEstimate estimate_state_space(const ahfl::ir::Program &program) {
    return estimate_state_space(collect_agent_metrics(program));
}

} // namespace ahfl::formal
