#include "runtime/engine/agent_runtime.hpp"

#include "ahfl/compiler/ir/program_view.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "runtime/engine/capability_eval.hpp"
#include "runtime/evaluator/evaluator.hpp"

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
    // Merge quota declared in IR into runtime configuration (IR declaration has lower priority than explicit input)
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

void AgentRuntime::set_capability_invoker(CapabilityInvoker invoker) {
    capability_invoker_ = std::move(invoker);
}

void AgentRuntime::set_contextual_capability_invoker(ContextualCapabilityInvoker invoker) {
    contextual_capability_invoker_ = std::move(invoker);
}

void AgentRuntime::set_invocation_context(CapabilityInvocationContext context) {
    invocation_context_ = std::move(context);
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

    // Initialize execution context
    evaluator::ExecContext exec_ctx;

    // Set up expression evaluator with capability call support
    if (contextual_capability_invoker_) {
        auto invoker = contextual_capability_invoker_;
        exec_ctx.expr_eval = [this, invoker, &result](
                                 const ir::Expr &expr,
                                 const evaluator::EvalContext &eval_ctx) -> evaluator::EvalResult {
            auto context = invocation_context_;
            context.agent_name = agent_.name;
            context.state_name = result.current_state;
            return eval_expr_with_capabilities(expr, eval_ctx, invoker, context);
        };
    } else if (capability_invoker_) {
        auto invoker = capability_invoker_;
        exec_ctx.expr_eval =
            [invoker](const ir::Expr &expr,
                      const evaluator::EvalContext &eval_ctx) -> evaluator::EvalResult {
            return eval_expr_with_capabilities(expr, eval_ctx, invoker);
        };
    }

    // Inject input: assume input is a StructValue, set its fields one by one into input_scope
    if (auto *sv = std::get_if<evaluator::StructValue>(&input.node)) {
        for (auto &[field_name, field_value] : sv->fields) {
            if (field_value) {
                exec_ctx.eval_ctx.set_input(field_name, evaluator::clone_value(*field_value));
            }
        }
    }

    // State visit count, used for infinite loop detection
    std::unordered_map<std::string, std::size_t> visit_counts;
    visit_counts[result.current_state] = 1;

    // Main state transition loop
    while (true) {
        // Quota check: max_state_transitions
        if (quota_.max_state_transitions.has_value() &&
            result.stats.state_transitions >= *quota_.max_state_transitions) {
            result.status = AgentStatus::QuotaExceeded;
            result.diagnostics.error()
                .message("quota exceeded: max_state_transitions reached")
                .emit();
            break;
        }

        // Quota check: max_execution_time
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

        // Find the handler for the current state
        const auto *handler = find_handler(result.current_state);
        if (handler == nullptr) {
            result.status = AgentStatus::Failed;
            result.diagnostics.error()
                .message("no handler found for state '" + result.current_state + "'")
                .emit();
            break;
        }

        // Execute handler body
        auto exec_result = evaluator::exec_block(handler->body, exec_ctx);

        // Merge diagnostics
        result.diagnostics.append(exec_result.diagnostics);

        // If execution itself produced error diagnostics, mark as failed
        if (exec_result.has_errors()) {
            result.status = AgentStatus::Failed;
            break;
        }

        // Handle execution result
        if (std::holds_alternative<evaluator::ExecContinue>(exec_result.outcome)) {
            // Continue: handler finished but no goto/return
            if (is_final_state(result.current_state)) {
                // Fallthrough in a final state -> completed (no output)
                result.status = AgentStatus::Completed;
            } else {
                // Fallthrough in a non-final state -> error
                result.status = AgentStatus::Failed;
                result.diagnostics.error()
                    .message("state '" + result.current_state +
                             "' handler completed without goto or return")
                    .emit();
            }
            break;
        }

        if (auto *goto_out = std::get_if<evaluator::ExecGoto>(&exec_result.outcome)) {
            // Validate transition legality
            if (!is_valid_transition(result.current_state, goto_out->target_state)) {
                result.status = AgentStatus::InvalidTransition;
                result.diagnostics.error()
                    .message("invalid transition from '" + result.current_state + "' to '" +
                             goto_out->target_state + "'")
                    .emit();
                break;
            }

            // Update state
            result.stats.state_transitions++;
            result.current_state = goto_out->target_state;
            result.visited_states.insert(result.current_state);

            // Infinite loop detection
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
            // Return: validate that the current state is a final state
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
    if (agent_.transitions.empty()) {
        return false;
    }
    return std::any_of(
        agent_.transitions.begin(), agent_.transitions.end(), [&](const ir::TransitionDecl &t) {
            return t.from_state == from && t.to_state == to;
        });
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
            // Parse duration string such as "30s", "5000ms"
            std::string_view sv(item.value);
            if (sv.ends_with("ms")) {
                std::size_t val{0};
                auto num_part = sv.substr(0, sv.size() - 2);
                auto [ptr, ec] =
                    std::from_chars(num_part.data(), num_part.data() + num_part.size(), val);
                if (ec == std::errc{}) {
                    config.max_execution_time = std::chrono::milliseconds(val);
                }
            } else if (sv.ends_with("s")) {
                std::size_t val{0};
                auto num_part = sv.substr(0, sv.size() - 1);
                auto [ptr, ec] =
                    std::from_chars(num_part.data(), num_part.data() + num_part.size(), val);
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

std::optional<AgentRuntime>
build_agent_runtime(const ir::Program &program, const std::string &agent_name, QuotaConfig quota) {
    const ir::ProgramIndex index(program);
    const auto *agent_decl = index.find_agent(agent_name);
    const auto *flow_decl = index.find_flow_for_agent(agent_name);

    if (agent_decl == nullptr || flow_decl == nullptr) {
        return std::nullopt;
    }

    return AgentRuntime(*agent_decl, *flow_decl, std::move(quota));
}

} // namespace ahfl::runtime
