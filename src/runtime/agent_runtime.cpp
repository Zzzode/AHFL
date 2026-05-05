#include "ahfl/runtime/agent_runtime.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "ahfl/evaluator/evaluator.hpp"

namespace ahfl::runtime {

// ============================================================================
// AgentStats
// ============================================================================

std::chrono::milliseconds AgentStats::elapsed_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
}

// ============================================================================
// AgentResult
// ============================================================================

bool AgentResult::is_terminal() const {
    return status != AgentStatus::Running;
}

// ============================================================================
// AgentRuntime Construction
// ============================================================================

AgentRuntime::AgentRuntime(const ir::AgentDecl &agent, const ir::FlowDecl &flow, QuotaConfig quota)
    : agent_(agent), flow_(flow), quota_(std::move(quota)) {
    // 合并 IR 中声明的 quota 到运行时配置（IR 声明优先级低于显式传入）
    auto parsed = parse_quota(agent_.quota);
    if (!quota_.max_state_transitions.has_value()) {
        quota_.max_state_transitions = parsed.max_state_transitions;
    }
    if (!quota_.max_tool_calls.has_value()) {
        quota_.max_tool_calls = parsed.max_tool_calls;
    }
    if (!quota_.max_execution_time.has_value()) {
        quota_.max_execution_time = parsed.max_execution_time;
    }
}

void AgentRuntime::set_capability_invoker(
    std::function<Value(const std::string &, const std::vector<Value> &)> invoker) {
    capability_invoker_ = std::move(invoker);
}

// ============================================================================
// Core Execution Loop
// ============================================================================

AgentResult AgentRuntime::run(Value input) {
    return run_from_state(std::move(input), agent_.initial_state);
}

AgentResult AgentRuntime::run_from_state(Value input, std::string start_state) {
    AgentResult result;
    result.status = AgentStatus::Running;
    result.current_state = std::move(start_state);
    result.visited_states.insert(result.current_state);
    result.stats.start_time = std::chrono::steady_clock::now();

    // 初始化执行上下文
    evaluator::ExecContext exec_ctx;

    // 设置带 capability 调用支持的表达式求值器
    if (capability_invoker_) {
        auto invoker = capability_invoker_;
        exec_ctx.expr_eval = [invoker](const ir::Expr &expr,
                                       const evaluator::EvalContext &eval_ctx) -> evaluator::EvalResult {
            // 如果是 CallExpr，使用 capability invoker 处理
            if (auto *call = std::get_if<ir::CallExpr>(&expr.node)) {
                std::vector<evaluator::Value> arg_values;
                for (const auto &arg_ptr : call->arguments) {
                    // 递归求值参数（参数中可能嵌套 PathExpr 等）
                    auto arg_result = evaluator::eval_expr(*arg_ptr, eval_ctx);
                    if (arg_result.has_errors()) {
                        return arg_result;
                    }
                    arg_values.push_back(std::move(arg_result.value));
                }
                auto result_value = invoker(call->callee, arg_values);
                return evaluator::EvalResult{std::move(result_value), {}};
            }
            // 非 CallExpr 委托给标准求值器
            return evaluator::eval_expr(expr, eval_ctx);
        };
    }

    // 注入 input：假设 input 是 StructValue，将其字段逐个设置到 input_scope
    if (auto *sv = std::get_if<evaluator::StructValue>(&input.node)) {
        for (auto &[field_name, field_value] : sv->fields) {
            if (field_value) {
                exec_ctx.eval_ctx.set_input(field_name, evaluator::clone_value(*field_value));
            }
        }
    }

    // 状态访问计数，用于无限循环检测
    std::unordered_map<std::string, std::size_t> visit_counts;
    visit_counts[result.current_state] = 1;

    // 状态转换主循环
    while (true) {
        // Quota 检查：max_state_transitions
        if (quota_.max_state_transitions.has_value() &&
            result.stats.state_transitions >= *quota_.max_state_transitions) {
            result.status = AgentStatus::QuotaExceeded;
            result.diagnostics.error()
                .message("quota exceeded: max_state_transitions reached")
                .emit();
            break;
        }

        // Quota 检查：max_execution_time
        if (quota_.max_execution_time.has_value()) {
            auto elapsed = std::chrono::steady_clock::now() - result.stats.start_time;
            if (elapsed > *quota_.max_execution_time) {
                result.status = AgentStatus::QuotaExceeded;
                result.diagnostics.error()
                    .message("quota exceeded: max_execution_time reached")
                    .emit();
                break;
            }
        }

        // 查找当前状态的 handler
        const auto *handler = find_handler(result.current_state);
        if (handler == nullptr) {
            result.status = AgentStatus::Failed;
            result.diagnostics.error()
                .message("no handler found for state '" + result.current_state + "'")
                .emit();
            break;
        }

        // 执行 handler body
        auto exec_result = evaluator::exec_block(handler->body, exec_ctx);

        // 合并诊断信息
        result.diagnostics.append(exec_result.diagnostics);

        // 如果执行本身产生了错误诊断，标记为失败
        if (exec_result.has_errors()) {
            result.status = AgentStatus::Failed;
            break;
        }

        // 处理执行结果
        if (std::holds_alternative<evaluator::ExecContinue>(exec_result.outcome)) {
            // Continue: handler 执行完毕但没有 goto/return
            if (is_final_state(result.current_state)) {
                // 在终态 fallthrough -> 完成（无输出）
                result.status = AgentStatus::Completed;
            } else {
                // 非终态 fallthrough -> 错误
                result.status = AgentStatus::Failed;
                result.diagnostics.error()
                    .message("state '" + result.current_state +
                             "' handler completed without goto or return")
                    .emit();
            }
            break;
        }

        if (auto *goto_out = std::get_if<evaluator::ExecGoto>(&exec_result.outcome)) {
            // 验证 transition 合法性
            if (!is_valid_transition(result.current_state, goto_out->target_state)) {
                result.status = AgentStatus::InvalidTransition;
                result.diagnostics.error()
                    .message("invalid transition from '" + result.current_state + "' to '" +
                             goto_out->target_state + "'")
                    .emit();
                break;
            }

            // 更新状态
            result.stats.state_transitions++;
            result.current_state = goto_out->target_state;
            result.visited_states.insert(result.current_state);

            // 无限循环检测
            visit_counts[result.current_state]++;
            if (visit_counts[result.current_state] > 100) {
                result.status = AgentStatus::InfiniteLoop;
                result.diagnostics.error()
                    .message("infinite loop detected: state '" + result.current_state +
                             "' visited more than 100 times")
                    .emit();
                break;
            }

            continue;
        }

        if (auto *ret_out = std::get_if<evaluator::ExecReturn>(&exec_result.outcome)) {
            // Return: 验证当前状态是终态
            if (!is_final_state(result.current_state)) {
                result.status = AgentStatus::Failed;
                result.diagnostics.error()
                    .message("return statement in non-final state '" + result.current_state + "'")
                    .emit();
                break;
            }
            result.status = AgentStatus::Completed;
            result.output = std::move(ret_out->value);
            break;
        }

        if (std::holds_alternative<evaluator::ExecAssertFailed>(exec_result.outcome)) {
            result.status = AgentStatus::Failed;
            auto &af = std::get<evaluator::ExecAssertFailed>(exec_result.outcome);
            result.diagnostics.error()
                .message("assertion failed in state '" + result.current_state + "': " + af.message)
                .emit();
            break;
        }
    }

    result.stats.end_time = std::chrono::steady_clock::now();
    return result;
}

// ============================================================================
// Helper Methods
// ============================================================================

const ir::StateHandler *AgentRuntime::find_handler(const std::string &state_name) const {
    for (const auto &handler : flow_.state_handlers) {
        if (handler.state_name == state_name) {
            return &handler;
        }
    }
    return nullptr;
}

bool AgentRuntime::is_valid_transition(const std::string &from, const std::string &to) const {
    // 如果 transitions 为空，允许所有转换（permissive 模式）
    if (agent_.transitions.empty()) {
        return true;
    }
    return std::any_of(
        agent_.transitions.begin(), agent_.transitions.end(),
        [&](const ir::TransitionDecl &t) { return t.from_state == from && t.to_state == to; });
}

bool AgentRuntime::is_final_state(const std::string &state_name) const {
    return std::find(agent_.final_states.begin(), agent_.final_states.end(), state_name) !=
           agent_.final_states.end();
}

// ============================================================================
// Quota Parsing
// ============================================================================

QuotaConfig AgentRuntime::parse_quota(const std::vector<ir::QuotaItem> &items) {
    QuotaConfig config;
    for (const auto &item : items) {
        if (item.name == "max_state_transitions") {
            std::size_t val{0};
            auto [ptr, ec] =
                std::from_chars(item.value.data(), item.value.data() + item.value.size(), val);
            if (ec == std::errc{}) {
                config.max_state_transitions = val;
            }
        } else if (item.name == "max_tool_calls") {
            std::size_t val{0};
            auto [ptr, ec] =
                std::from_chars(item.value.data(), item.value.data() + item.value.size(), val);
            if (ec == std::errc{}) {
                config.max_tool_calls = val;
            }
        } else if (item.name == "max_execution_time") {
            // 解析 duration 字符串如 "30s", "5000ms"
            std::string_view sv(item.value);
            if (sv.ends_with("ms")) {
                std::size_t val{0};
                auto num_part = sv.substr(0, sv.size() - 2);
                auto [ptr, ec] = std::from_chars(num_part.data(), num_part.data() + num_part.size(), val);
                if (ec == std::errc{}) {
                    config.max_execution_time = std::chrono::milliseconds(val);
                }
            } else if (sv.ends_with("s")) {
                std::size_t val{0};
                auto num_part = sv.substr(0, sv.size() - 1);
                auto [ptr, ec] = std::from_chars(num_part.data(), num_part.data() + num_part.size(), val);
                if (ec == std::errc{}) {
                    config.max_execution_time = std::chrono::milliseconds(val * 1000);
                }
            }
        }
    }
    return config;
}

// ============================================================================
// Factory Function
// ============================================================================

std::optional<AgentRuntime> build_agent_runtime(const ir::Program &program,
                                                 const std::string &agent_name,
                                                 QuotaConfig quota) {
    const ir::AgentDecl *agent_decl = nullptr;
    const ir::FlowDecl *flow_decl = nullptr;

    for (const auto &decl : program.declarations) {
        if (auto *ad = std::get_if<ir::AgentDecl>(&decl)) {
            if (ad->name == agent_name) {
                agent_decl = ad;
            }
        }
        if (auto *fd = std::get_if<ir::FlowDecl>(&decl)) {
            if (fd->target == agent_name) {
                flow_decl = fd;
            }
        }
    }

    if (agent_decl == nullptr || flow_decl == nullptr) {
        return std::nullopt;
    }

    return AgentRuntime(*agent_decl, *flow_decl, std::move(quota));
}

} // namespace ahfl::runtime
