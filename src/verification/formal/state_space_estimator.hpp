#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ahfl::ir {
struct Program;
} // namespace ahfl::ir

namespace ahfl::formal {

struct AgentMetrics {
    std::string agent_name;
    std::size_t state_count{0};
    std::size_t transition_count{0};
    std::size_t capability_count{0};
};

struct StateSpaceEstimate {
    std::size_t estimated_states{0};
    std::size_t num_agents{0};
    std::size_t max_states_per_agent{0};
    std::size_t total_transitions{0};
    bool likely_tractable{true};
    std::string estimation_method;
    std::string warning;
};

[[nodiscard]] StateSpaceEstimate estimate_state_space(const std::vector<AgentMetrics> &agents);

[[nodiscard]] std::vector<AgentMetrics> collect_agent_metrics(const ahfl::ir::Program &program);
[[nodiscard]] StateSpaceEstimate estimate_state_space(const ahfl::ir::Program &program);

} // namespace ahfl::formal
