#include "runtime/engine/agent_runtime.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::ir;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

// ============================================================================
// IR 构造辅助函数
// ============================================================================

ExprPtr make_expr_ptr(ExprNode node) {
    return std::make_unique<Expr>(Expr{std::move(node), {}});
}

StatementPtr make_stmt_ptr(StatementNode node) {
    return std::make_unique<Statement>(Statement{std::move(node), {}});
}

// 构造一个简单的 goto handler
StateHandler make_goto_handler(const std::string &state, const std::string &target) {
    StateHandler handler;
    handler.state_name = state;
    handler.body.statements.push_back(make_stmt_ptr(GotoStatement{target}));
    return handler;
}

// 构造一个 return handler
StateHandler make_return_handler(const std::string &state, ExprNode return_expr) {
    StateHandler handler;
    handler.state_name = state;
    handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(return_expr))}));
    return handler;
}

// 构造一个空 handler（fallthrough）
StateHandler make_empty_handler(const std::string &state) {
    StateHandler handler;
    handler.state_name = state;
    return handler;
}

// 构造一个 assert(false) handler
StateHandler make_assert_fail_handler(const std::string &state) {
    StateHandler handler;
    handler.state_name = state;
    handler.body.statements.push_back(
        make_stmt_ptr(AssertStatement{make_expr_ptr(BoolLiteralExpr{false})}));
    return handler;
}

// 构造带条件分支的 handler
StateHandler make_conditional_handler(const std::string &state,
                                      const std::string &goto_true,
                                      const std::string &goto_false) {
    StateHandler handler;
    handler.state_name = state;

    // if input.approved { goto goto_true } else { goto goto_false }
    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{goto_true}));
    Block else_block;
    else_block.statements.push_back(make_stmt_ptr(GotoStatement{goto_false}));

    // 使用 input.approved 作为条件
    PathExpr path_expr;
    path_expr.path.root_kind = PathRootKind::Input;
    path_expr.path.root_name = "input";
    path_expr.path.members = {"approved"};

    handler.body.statements.push_back(
        make_stmt_ptr(IfStatement{make_expr_ptr(std::move(path_expr)),
                                  std::make_unique<Block>(std::move(then_block)),
                                  std::make_unique<Block>(std::move(else_block))}));
    return handler;
}

// 构造 StructValue 作为 input
Value make_input_struct(const std::string &type_name,
                        const std::string &field_name,
                        Value field_value) {
    std::unordered_map<std::string, std::unique_ptr<Value>> fields;
    fields[field_name] = std::make_unique<Value>(std::move(field_value));
    return Value{StructValue{type_name, std::move(fields)}};
}

// ============================================================================
// Test: 简单线性 agent (Init -> Process -> Final, 返回输出)
// ============================================================================

void test_simple_linear_agent() {
    // Agent 定义
    AgentDecl agent;
    agent.name = "SimpleAgent";
    agent.input_type = "SimpleInput";
    agent.context_type = "SimpleCtx";
    agent.output_type = "SimpleOutput";
    agent.states = {"Init", "Process", "Final"};
    agent.initial_state = "Init";
    agent.final_states = {"Final"};
    agent.transitions = {{"Init", "Process"}, {"Process", "Final"}};

    // Flow 定义
    FlowDecl flow;
    flow.target = "SimpleAgent";
    flow.state_handlers.push_back(make_goto_handler("Init", "Process"));
    flow.state_handlers.push_back(make_goto_handler("Process", "Final"));
    flow.state_handlers.push_back(make_return_handler("Final", IntegerLiteralExpr{"42"}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Completed, "simple_linear.status_completed");
    check(result.current_state == "Final", "simple_linear.final_state");
    check(result.output.has_value(), "simple_linear.has_output");
    if (result.output.has_value()) {
        auto *iv = std::get_if<IntValue>(&result.output->node);
        check(iv != nullptr && iv->value == 42, "simple_linear.output_is_42");
    }
    check(result.visited_states.count("Init") == 1, "simple_linear.visited_init");
    check(result.visited_states.count("Process") == 1, "simple_linear.visited_process");
    check(result.visited_states.count("Final") == 1, "simple_linear.visited_final");
    check(result.stats.state_transitions == 2, "simple_linear.transition_count");
    check(result.is_terminal(), "simple_linear.is_terminal");
}

// ============================================================================
// Test: 带条件分支的 agent
// ============================================================================

void test_conditional_goto_agent() {
    AgentDecl agent;
    agent.name = "ConditionalAgent";
    agent.input_type = "AuditInput";
    agent.context_type = "AuditCtx";
    agent.output_type = "AuditOutput";
    agent.states = {"Init", "Approved", "Rejected", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {
        {"Init", "Approved"},
        {"Init", "Rejected"},
        {"Approved", "Done"},
        {"Rejected", "Done"},
    };

    FlowDecl flow;
    flow.target = "ConditionalAgent";
    // Init: if input.approved { goto Approved } else { goto Rejected }
    flow.state_handlers.push_back(make_conditional_handler("Init", "Approved", "Rejected"));
    flow.state_handlers.push_back(make_goto_handler("Approved", "Done"));
    flow.state_handlers.push_back(make_goto_handler("Rejected", "Done"));
    flow.state_handlers.push_back(make_return_handler("Done", StringLiteralExpr{"completed"}));

    // 测试 approved=true 路径
    {
        auto input = make_input_struct("AuditInput", "approved", make_bool(true));
        AgentRuntime runtime(agent, flow);
        auto result = runtime.run(std::move(input));

        check(result.status == AgentStatus::Completed, "conditional.approved.status");
        check(result.visited_states.count("Approved") == 1, "conditional.approved.visited");
        check(result.visited_states.count("Rejected") == 0, "conditional.approved.not_rejected");
    }

    // 测试 approved=false 路径
    {
        auto input = make_input_struct("AuditInput", "approved", make_bool(false));
        AgentRuntime runtime(agent, flow);
        auto result = runtime.run(std::move(input));

        check(result.status == AgentStatus::Completed, "conditional.rejected.status");
        check(result.visited_states.count("Rejected") == 1, "conditional.rejected.visited");
        check(result.visited_states.count("Approved") == 0, "conditional.rejected.not_approved");
    }
}

// ============================================================================
// Test: 多 transition 验证
// ============================================================================

void test_multiple_transitions() {
    AgentDecl agent;
    agent.name = "MultiAgent";
    agent.states = {"A", "B", "C", "D"};
    agent.initial_state = "A";
    agent.final_states = {"D"};
    // 定义允许的 transitions
    agent.transitions = {{"A", "B"}, {"B", "C"}, {"C", "D"}};

    FlowDecl flow;
    flow.target = "MultiAgent";
    flow.state_handlers.push_back(make_goto_handler("A", "B"));
    flow.state_handlers.push_back(make_goto_handler("B", "C"));
    flow.state_handlers.push_back(make_goto_handler("C", "D"));
    flow.state_handlers.push_back(make_return_handler("D", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Completed, "multi_trans.status_completed");
    check(result.stats.state_transitions == 3, "multi_trans.transition_count_3");
    check(result.visited_states.size() == 4, "multi_trans.visited_4_states");
}

// ============================================================================
// Test: 非终态 fallthrough（error）
// ============================================================================

void test_non_final_state_fallthrough() {
    AgentDecl agent;
    agent.name = "FallthroughAgent";
    agent.states = {"Init", "Final"};
    agent.initial_state = "Init";
    agent.final_states = {"Final"};
    agent.transitions = {{"Init", "Final"}};

    FlowDecl flow;
    flow.target = "FallthroughAgent";
    // Init handler 没有 goto 也没有 return -> fallthrough error
    flow.state_handlers.push_back(make_empty_handler("Init"));
    flow.state_handlers.push_back(make_return_handler("Final", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Failed, "fallthrough.status_failed");
    check(result.diagnostics.has_error(), "fallthrough.has_diagnostic");
}

// ============================================================================
// Test: 终态 fallthrough（成功，无输出）
// ============================================================================

void test_final_state_fallthrough() {
    AgentDecl agent;
    agent.name = "FinalFallAgent";
    agent.states = {"Init", "Final"};
    agent.initial_state = "Init";
    agent.final_states = {"Final"};
    agent.transitions = {{"Init", "Final"}};

    FlowDecl flow;
    flow.target = "FinalFallAgent";
    flow.state_handlers.push_back(make_goto_handler("Init", "Final"));
    // Final handler 没有 return -> fallthrough 但它是终态，所以 Completed
    flow.state_handlers.push_back(make_empty_handler("Final"));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Completed, "final_fallthrough.status_completed");
    check(!result.output.has_value(), "final_fallthrough.no_output");
}

// ============================================================================
// Test: 非法 transition
// ============================================================================

void test_invalid_transition() {
    AgentDecl agent;
    agent.name = "InvalidTransAgent";
    agent.states = {"A", "B", "C"};
    agent.initial_state = "A";
    agent.final_states = {"C"};
    // 只允许 A->B，不允许 A->C
    agent.transitions = {{"A", "B"}, {"B", "C"}};

    FlowDecl flow;
    flow.target = "InvalidTransAgent";
    // A 直接 goto C（违反 transition 规则）
    flow.state_handlers.push_back(make_goto_handler("A", "C"));
    flow.state_handlers.push_back(make_goto_handler("B", "C"));
    flow.state_handlers.push_back(make_return_handler("C", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::InvalidTransition, "invalid_trans.status");
    check(result.diagnostics.has_error(), "invalid_trans.has_diagnostic");
}

// ============================================================================
// Test: Quota exceeded (max_state_transitions)
// ============================================================================

void test_quota_exceeded() {
    AgentDecl agent;
    agent.name = "QuotaAgent";
    agent.states = {"A", "B", "Final"};
    agent.initial_state = "A";
    agent.final_states = {"Final"};
    agent.transitions = {{"A", "B"}, {"B", "A"}};

    FlowDecl flow;
    flow.target = "QuotaAgent";
    // A -> B -> A -> B -> ... (循环) 但 quota=3 transition 就会超
    flow.state_handlers.push_back(make_goto_handler("A", "B"));
    flow.state_handlers.push_back(make_goto_handler("B", "A"));
    flow.state_handlers.push_back(make_return_handler("Final", BoolLiteralExpr{true}));

    QuotaConfig quota;
    quota.max_state_transitions = 3;

    AgentRuntime runtime(agent, flow, quota);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::QuotaExceeded, "quota.status_exceeded");
    check(result.stats.state_transitions == 3, "quota.transitions_at_limit");
}

// ============================================================================
// Test: 无限循环检测
// ============================================================================

void test_infinite_loop_detection() {
    AgentDecl agent;
    agent.name = "LoopAgent";
    agent.states = {"Loop", "Final"};
    agent.initial_state = "Loop";
    agent.final_states = {"Final"};
    agent.transitions = {{"Loop", "Loop"}};

    FlowDecl flow;
    flow.target = "LoopAgent";
    // Loop -> Loop -> Loop -> ... 永不到达 Final
    flow.state_handlers.push_back(make_goto_handler("Loop", "Loop"));
    flow.state_handlers.push_back(make_return_handler("Final", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::InfiniteLoop, "loop.status_infinite_loop");
    check(result.diagnostics.has_error(), "loop.has_diagnostic");
}

// ============================================================================
// Test: Assert 失败
// ============================================================================

void test_assert_failure() {
    AgentDecl agent;
    agent.name = "AssertAgent";
    agent.states = {"Init", "Final"};
    agent.initial_state = "Init";
    agent.final_states = {"Final"};

    FlowDecl flow;
    flow.target = "AssertAgent";
    flow.state_handlers.push_back(make_assert_fail_handler("Init"));
    flow.state_handlers.push_back(make_return_handler("Final", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Failed, "assert.status_failed");
    check(result.current_state == "Init", "assert.stopped_at_init");
}

// ============================================================================
// Test: 无 transitions 定义（strict 模式）
// ============================================================================

void test_strict_no_transitions() {
    AgentDecl agent;
    agent.name = "StrictAgent";
    agent.states = {"A", "B", "C"};
    agent.initial_state = "A";
    agent.final_states = {"C"};
    // 不定义 transitions -> 拒绝所有 goto

    FlowDecl flow;
    flow.target = "StrictAgent";
    flow.state_handlers.push_back(make_goto_handler("A", "C")); // 直接跳到 C
    flow.state_handlers.push_back(make_goto_handler("B", "C"));
    flow.state_handlers.push_back(make_return_handler("C", StringLiteralExpr{"done"}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::InvalidTransition, "strict_missing_transitions.status");
    check(result.current_state == "A", "strict_missing_transitions.stays_at_source");
}

// ============================================================================
// Test: build_agent_runtime 工厂函数
// ============================================================================

void test_build_agent_runtime() {
    Program program;

    AgentDecl agent;
    agent.name = "TestAgent";
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {{"Init", "Done"}};
    program.declarations.push_back(std::move(agent));

    FlowDecl flow;
    flow.target = "TestAgent";
    flow.state_handlers.push_back(make_goto_handler("Init", "Done"));
    flow.state_handlers.push_back(make_return_handler("Done", IntegerLiteralExpr{"1"}));
    program.declarations.push_back(std::move(flow));

    // 存在的 agent
    auto rt = build_agent_runtime(program, "TestAgent");
    check(rt.has_value(), "build.found_agent");

    if (rt.has_value()) {
        auto result = rt->run(make_none());
        check(result.status == AgentStatus::Completed, "build.run_completed");
    }

    // 不存在的 agent
    auto rt2 = build_agent_runtime(program, "NonExistent");
    check(!rt2.has_value(), "build.not_found");
}

// ============================================================================
// Test: Handler 缺失
// ============================================================================

void test_missing_handler() {
    AgentDecl agent;
    agent.name = "MissingHandler";
    agent.states = {"Init", "Process", "Final"};
    agent.initial_state = "Init";
    agent.final_states = {"Final"};
    agent.transitions = {{"Init", "Process"}, {"Process", "Final"}};

    FlowDecl flow;
    flow.target = "MissingHandler";
    // 只有 Init handler，Process 缺失
    flow.state_handlers.push_back(make_goto_handler("Init", "Process"));
    flow.state_handlers.push_back(make_return_handler("Final", BoolLiteralExpr{true}));

    AgentRuntime runtime(agent, flow);
    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Failed, "missing_handler.status_failed");
    check(result.current_state == "Process", "missing_handler.stuck_at_process");
}

// ============================================================================
// Test: capability failure is surfaced as diagnostics
// ============================================================================

void test_capability_failure_becomes_diagnostic() {
    AgentDecl agent;
    agent.name = "CapabilityAgent";
    agent.states = {"Final"};
    agent.initial_state = "Final";
    agent.final_states = {"Final"};

    FlowDecl flow;
    flow.target = "CapabilityAgent";

    CallExpr call;
    call.callee = "UnavailableCapability";
    flow.state_handlers.push_back(make_return_handler("Final", std::move(call)));

    AgentRuntime runtime(agent, flow);
    runtime.set_capability_invoker(
        [](const std::string &name, const std::vector<Value> & /*args*/) -> CapabilityCallResult {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::RetryExhausted,
                .value = std::nullopt,
                .error_message = "upstream unavailable for " + name,
                .attempts = 3,
            };
        });

    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Failed, "capability_failure.status_failed");
    check(result.diagnostics.has_error(), "capability_failure.has_diagnostic");
    check(!result.output.has_value(), "capability_failure.no_output");
}

void test_capability_call_inside_composite_condition() {
    AgentDecl agent;
    agent.name = "CompositeCapabilityAgent";
    agent.states = {"Init", "Approved", "Rejected"};
    agent.initial_state = "Init";
    agent.final_states = {"Approved", "Rejected"};
    agent.transitions = {{"Init", "Approved"}, {"Init", "Rejected"}};

    CallExpr call;
    call.callee = "is_ready";

    BinaryExpr condition;
    condition.op = ExprBinaryOp::Equal;
    condition.lhs = make_expr_ptr(std::move(call));
    condition.rhs = make_expr_ptr(BoolLiteralExpr{true});

    Block then_block;
    then_block.statements.push_back(make_stmt_ptr(GotoStatement{"Approved"}));
    Block else_block;
    else_block.statements.push_back(make_stmt_ptr(GotoStatement{"Rejected"}));

    StateHandler init;
    init.state_name = "Init";
    init.body.statements.push_back(make_stmt_ptr(IfStatement{
        make_expr_ptr(std::move(condition)),
        std::make_unique<Block>(std::move(then_block)),
        std::make_unique<Block>(std::move(else_block)),
    }));

    FlowDecl flow;
    flow.target = "CompositeCapabilityAgent";
    flow.state_handlers.push_back(std::move(init));
    flow.state_handlers.push_back(make_return_handler("Approved", StringLiteralExpr{"approved"}));
    flow.state_handlers.push_back(make_return_handler("Rejected", StringLiteralExpr{"rejected"}));

    AgentRuntime runtime(agent, flow);
    runtime.set_capability_invoker(
        [](const std::string &name, const std::vector<Value> & /*args*/) -> CapabilityCallResult {
            if (name == "is_ready") {
                return CapabilityCallResult{
                    .status = CapabilityCallStatus::Success,
                    .value = make_bool(true),
                    .error_message = {},
                    .attempts = 1,
                };
            }
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "unexpected capability",
                .attempts = 1,
            };
        });

    auto result = runtime.run(make_none());

    check(result.status == AgentStatus::Completed, "capability_composite.status_completed");
    check(result.current_state == "Approved", "capability_composite.approved_state");
    auto *output =
        result.output.has_value() ? std::get_if<StringValue>(&result.output->node) : nullptr;
    check(output != nullptr && output->value == "approved", "capability_composite.output");
}

} // anonymous namespace

int main() {
    test_simple_linear_agent();
    test_conditional_goto_agent();
    test_multiple_transitions();
    test_non_final_state_fallthrough();
    test_final_state_fallthrough();
    test_invalid_transition();
    test_quota_exceeded();
    test_infinite_loop_detection();
    test_assert_failure();
    test_strict_no_transitions();
    test_build_agent_runtime();
    test_missing_handler();
    test_capability_failure_becomes_diagnostic();
    test_capability_call_inside_composite_condition();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
