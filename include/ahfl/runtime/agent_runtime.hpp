#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ahfl/evaluator/executor.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::runtime {

using Value = evaluator::Value;

// Agent 运行状态
enum class AgentStatus {
    Running,
    Completed,
    Failed,
    QuotaExceeded,
    InvalidTransition,
    InfiniteLoop,
};

// Agent 执行统计
struct AgentStats {
    std::size_t state_transitions{0};
    std::size_t tool_calls{0};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    [[nodiscard]] std::chrono::milliseconds elapsed_ms() const;
};

// Agent 执行结果
struct AgentResult {
    AgentStatus status;
    std::optional<Value> output;
    std::string current_state;
    std::unordered_set<std::string> visited_states;
    AgentStats stats;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool is_terminal() const;
};

// Quota 配置
struct QuotaConfig {
    std::optional<std::size_t> max_state_transitions;
    std::optional<std::size_t> max_tool_calls;
    std::optional<std::chrono::milliseconds> max_execution_time;
};

// Agent Runtime
class AgentRuntime {
  public:
    AgentRuntime(const ir::AgentDecl &agent, const ir::FlowDecl &flow, QuotaConfig quota = {});

    // 设置 capability invoker（用于执行 flow 中的 capability 调用）
    void set_capability_invoker(
        std::function<Value(const std::string &, const std::vector<Value> &)> invoker);

    // 执行 agent，从初始状态开始
    [[nodiscard]] AgentResult run(Value input);

    // 从指定状态恢复执行
    [[nodiscard]] AgentResult run_from_state(Value input, std::string start_state);

  private:
    const ir::AgentDecl &agent_;
    const ir::FlowDecl &flow_;
    QuotaConfig quota_;
    std::function<Value(const std::string &, const std::vector<Value> &)> capability_invoker_;

    // 查找 state handler
    [[nodiscard]] const ir::StateHandler *find_handler(const std::string &state_name) const;

    // 验证 transition 合法性
    [[nodiscard]] bool is_valid_transition(const std::string &from, const std::string &to) const;

    // 检查是否是终态
    [[nodiscard]] bool is_final_state(const std::string &state_name) const;

    // 解析 quota 字符串
    [[nodiscard]] static QuotaConfig parse_quota(const std::vector<ir::QuotaItem> &items);
};

// 从 IR Program 构建 AgentRuntime
[[nodiscard]] std::optional<AgentRuntime> build_agent_runtime(const ir::Program &program,
                                                              const std::string &agent_name,
                                                              QuotaConfig quota = {});

} // namespace ahfl::runtime
