#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/executor.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

using Value = evaluator::Value;

// Agent runtime status
enum class AgentStatus {
    Running,
    Completed,
    Failed,
    QuotaExceeded,
    InvalidTransition,
    InfiniteLoop,
};

// Agent execution statistics
struct AgentStats {
    std::size_t state_transitions{0};
    std::size_t tool_calls{0};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    [[nodiscard]] std::chrono::milliseconds elapsed_ms() const;
};

// Agent execution result
struct AgentResult {
    AgentStatus status;
    std::optional<Value> output;
    std::string current_state;
    std::unordered_set<std::string> visited_states;
    AgentStats stats;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool is_terminal() const;
};

// Quota configuration
struct QuotaConfig {
    std::optional<std::size_t> max_state_transitions;
    std::optional<std::size_t> max_tool_calls;
    std::optional<std::chrono::milliseconds> max_execution_time;
};

// Agent Runtime engine
class AgentRuntime {
  public:
    AgentRuntime(const ir::AgentDecl &agent, const ir::FlowDecl &flow, QuotaConfig quota = {});

    // Set the capability invoker (used to execute capability calls within the flow)
    void set_capability_invoker(CapabilityInvoker invoker);
    void set_contextual_capability_invoker(ContextualCapabilityInvoker invoker);
    void set_invocation_context(CapabilityInvocationContext context);

    // Execute the agent, starting from the initial state
    [[nodiscard]] AgentResult run(Value input);

    // Resume execution from the specified state
    [[nodiscard]] AgentResult run_from_state(Value input, std::string start_state);

  private:
    const ir::AgentDecl &agent_;
    const ir::FlowDecl &flow_;
    QuotaConfig quota_;
    CapabilityInvoker capability_invoker_;
    ContextualCapabilityInvoker contextual_capability_invoker_;
    CapabilityInvocationContext invocation_context_;

    // Find the state handler
    [[nodiscard]] const ir::StateHandler *find_handler(const std::string &state_name) const;

    // Validate the legality of the transition
    [[nodiscard]] bool is_valid_transition(const std::string &from, const std::string &to) const;

    // Check whether the state is a final state
    [[nodiscard]] bool is_final_state(const std::string &state_name) const;

    // Parse quota string
    [[nodiscard]] static QuotaConfig parse_quota(const std::vector<ir::QuotaItem> &items);
};

// Build an AgentRuntime from an IR Program
[[nodiscard]] std::optional<AgentRuntime> build_agent_runtime(const ir::Program &program,
                                                              const std::string &agent_name,
                                                              QuotaConfig quota = {});

} // namespace ahfl::runtime
