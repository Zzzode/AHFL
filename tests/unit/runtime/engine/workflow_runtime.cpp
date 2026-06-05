#include "runtime/engine/workflow_runtime.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
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

TypeRef make_named_type_ref(const std::string &name) {
    return TypeRef{
        .kind = TypeRefKind::Struct,
        .display_name = name,
        .canonical_name = name,
    };
}

SymbolRef make_agent_ref(const std::string &name) {
    return SymbolRef{
        .kind = SymbolRefKind::Agent,
        .canonical_name = name,
        .local_name = name,
        .module_name = {},
    };
}

// 构造一个简单的 EchoAgent（Init -> Done，返回 input 的某个字段的值）
AgentDecl make_echo_agent(const std::string &name) {
    AgentDecl agent;
    agent.name = name;
    agent.symbol_ref = make_agent_ref(name);
    agent.input_type_ref = make_named_type_ref(name + "Input");
    agent.context_type_ref = make_named_type_ref(name + "Ctx");
    agent.output_type_ref = make_named_type_ref(name + "Output");
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {{"Init", "Done"}};
    return agent;
}

// 构造一个 EchoFlow：Init goto Done; Done return IntegerLiteral
FlowDecl make_echo_flow(const std::string &target, const std::string &return_val) {
    FlowDecl flow;
    flow.target_ref = make_agent_ref(target);

    // Init handler: goto Done
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));

    // Done handler: return <value>
    StateHandler done_handler;
    done_handler.state_name = "Done";
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(IntegerLiteralExpr{return_val})}));
    flow.state_handlers.push_back(std::move(done_handler));

    return flow;
}

// 构造一个 Flow 返回 StructLiteral
FlowDecl make_struct_return_flow(const std::string &target,
                                 const std::string &type_name,
                                 const std::string &field_name,
                                 const std::string &int_val) {
    FlowDecl flow;
    flow.target_ref = make_agent_ref(target);

    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));

    StateHandler done_handler;
    done_handler.state_name = "Done";
    // return StructLiteral { type_name, field_name: int_val }
    StructLiteralExpr struct_expr;
    struct_expr.type_name = type_name;
    struct_expr.fields.push_back(
        StructFieldInit{field_name, make_expr_ptr(IntegerLiteralExpr{int_val})});
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(struct_expr))}));
    flow.state_handlers.push_back(std::move(done_handler));

    return flow;
}

// 构造一个 Flow：Init goto Done; Done return input.<field_name>
FlowDecl make_input_field_return_flow(const std::string &target, const std::string &field_name) {
    FlowDecl flow;
    flow.target_ref = make_agent_ref(target);

    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));

    StateHandler done_handler;
    done_handler.state_name = "Done";
    PathExpr input_path;
    input_path.path.root_kind = PathRootKind::Input;
    input_path.path.root_name = "input";
    input_path.path.members = {field_name};
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(input_path))}));
    flow.state_handlers.push_back(std::move(done_handler));

    return flow;
}

// 构造一个会失败的 flow（Init 状态 assert(false)）
FlowDecl make_failing_flow(const std::string &target) {
    FlowDecl flow;
    flow.target_ref = make_agent_ref(target);

    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(
        make_stmt_ptr(AssertStatement{make_expr_ptr(BoolLiteralExpr{false})}));
    flow.state_handlers.push_back(std::move(init_handler));

    StateHandler done_handler;
    done_handler.state_name = "Done";
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(BoolLiteralExpr{true})}));
    flow.state_handlers.push_back(std::move(done_handler));

    return flow;
}

// 构造 workflow node（无 input 表达式）
WorkflowNode
make_node(const std::string &name, const std::string &target, std::vector<std::string> after = {}) {
    WorkflowNode node;
    node.name = name;
    node.target_ref = make_agent_ref(target);
    node.input = nullptr;
    node.after = std::move(after);
    return node;
}

// 构造 workflow node with PathExpr input referencing another node (node_name.field)
WorkflowNode make_node_with_node_output_path(const std::string &name,
                                             const std::string &target,
                                             const std::string &source_node,
                                             const std::string &field,
                                             std::vector<std::string> after = {}) {
    WorkflowNode node;
    node.name = name;
    node.target_ref = make_agent_ref(target);
    node.after = std::move(after);

    // source_node.field
    PathExpr path_expr;
    path_expr.path.root_kind = PathRootKind::Identifier;
    path_expr.path.root_name = source_node;
    path_expr.path.members = {field};
    node.input = make_expr_ptr(std::move(path_expr));

    return node;
}

// ============================================================================
// Test: 单节点 workflow
// ============================================================================

void test_single_node_workflow() {
    Program program;

    // Agent + Flow
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_echo_flow("EchoAgent", "42"));

    // Workflow with one node
    WorkflowDecl workflow;
    workflow.name = "SingleNodeWorkflow";
    workflow.input_type_ref = make_named_type_ref("WfInput");
    workflow.output_type_ref = make_named_type_ref("WfOutput");
    workflow.nodes.push_back(make_node("echo", "EchoAgent"));
    // return value: 直接返回 node output 的 PathExpr -> 此处简单设为 null
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("SingleNodeWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "single_node.status_completed");
    check(result.execution_order.size() == 1, "single_node.exec_order_size");
    check(result.execution_order[0] == "echo", "single_node.exec_order_name");
    check(result.node_results.size() == 1, "single_node.node_results_size");
    check(result.node_results[0].status == AgentStatus::Completed, "single_node.node_completed");
    check(!result.has_errors(), "single_node.no_errors");
}

// ============================================================================
// Test: 线性 3 节点 workflow (A -> B -> C)
// ============================================================================

void test_linear_three_node_workflow() {
    Program program;

    program.declarations.push_back(make_echo_agent("AgentA"));
    program.declarations.push_back(make_echo_flow("AgentA", "1"));
    program.declarations.push_back(make_echo_agent("AgentB"));
    program.declarations.push_back(make_echo_flow("AgentB", "2"));
    program.declarations.push_back(make_echo_agent("AgentC"));
    program.declarations.push_back(make_echo_flow("AgentC", "3"));

    WorkflowDecl workflow;
    workflow.name = "LinearWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("a", "AgentA"));
    workflow.nodes.push_back(make_node("b", "AgentB", {"a"}));
    workflow.nodes.push_back(make_node("c", "AgentC", {"b"}));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("LinearWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "linear.status_completed");
    check(result.execution_order.size() == 3, "linear.exec_order_size");
    check(result.execution_order[0] == "a", "linear.order_0_a");
    check(result.execution_order[1] == "b", "linear.order_1_b");
    check(result.execution_order[2] == "c", "linear.order_2_c");
    check(result.node_results.size() == 3, "linear.node_results_size");
    for (const auto &nr : result.node_results) {
        check(nr.status == AgentStatus::Completed, "linear.all_nodes_completed");
    }
}

// ============================================================================
// Test: 菱形 workflow (A, B -> C)
// ============================================================================

void test_diamond_workflow() {
    Program program;

    program.declarations.push_back(make_echo_agent("AgentA"));
    program.declarations.push_back(make_echo_flow("AgentA", "10"));
    program.declarations.push_back(make_echo_agent("AgentB"));
    program.declarations.push_back(make_echo_flow("AgentB", "20"));
    program.declarations.push_back(make_echo_agent("AgentC"));
    program.declarations.push_back(make_echo_flow("AgentC", "30"));

    WorkflowDecl workflow;
    workflow.name = "DiamondWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("a", "AgentA"));
    workflow.nodes.push_back(make_node("b", "AgentB"));
    workflow.nodes.push_back(make_node("c", "AgentC", {"a", "b"}));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("DiamondWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "diamond.status_completed");
    check(result.execution_order.size() == 3, "diamond.exec_order_size");
    // a 和 b 先执行（顺序可能不同但都在 c 之前）
    check(result.execution_order[2] == "c", "diamond.c_is_last");
    check(result.node_results.size() == 3, "diamond.node_results_size");
}

// ============================================================================
// Test: Node 失败传播
// ============================================================================

void test_node_failure_propagation() {
    Program program;

    // FailAgent 会在 Init 断言失败
    program.declarations.push_back(make_echo_agent("FailAgent"));
    program.declarations.push_back(make_failing_flow("FailAgent"));

    // SuccessAgent 正常执行
    program.declarations.push_back(make_echo_agent("SuccessAgent"));
    program.declarations.push_back(make_echo_flow("SuccessAgent", "99"));

    WorkflowDecl workflow;
    workflow.name = "FailWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("fail_node", "FailAgent"));
    workflow.nodes.push_back(make_node("succ_node", "SuccessAgent", {"fail_node"}));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("FailWorkflow", make_none());

    check(result.status == WorkflowStatus::NodeFailed ||
              result.status == WorkflowStatus::DependencyFailed,
          "failure.status_not_completed");
    check(result.node_results.size() == 2, "failure.node_results_size");
    // 第一个 node 失败
    check(result.node_results[0].node_name == "fail_node", "failure.first_is_fail_node");
    check(result.node_results[0].status != AgentStatus::Completed, "failure.first_failed");
    // 第二个 node 因依赖失败而被跳过
    check(result.node_results[1].node_name == "succ_node", "failure.second_is_succ_node");
    check(result.node_results[1].status == AgentStatus::Failed, "failure.second_dep_failed");
}

// ============================================================================
// Test: 返回值来自最后节点
// ============================================================================

void test_return_value_from_node() {
    Program program;

    program.declarations.push_back(make_echo_agent("RetAgent"));
    program.declarations.push_back(
        make_struct_return_flow("RetAgent", "RetOutput", "result", "77"));

    WorkflowDecl workflow;
    workflow.name = "ReturnWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("ret_node", "RetAgent"));

    // return_value: ret_node.result (PathExpr)
    PathExpr ret_path;
    ret_path.path.root_kind = PathRootKind::Identifier;
    ret_path.path.root_name = "ret_node";
    ret_path.path.members = {"result"};
    workflow.return_value = make_expr_ptr(std::move(ret_path));

    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("ReturnWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "return_val.status_completed");
    check(result.output.has_value(), "return_val.has_output");
    if (result.output.has_value()) {
        auto *iv = std::get_if<IntValue>(&result.output->node);
        check(iv != nullptr && iv->value == 77, "return_val.output_is_77");
    }
}

// ============================================================================
// Test: workflow return eval errors are surfaced
// ============================================================================

void test_return_value_eval_error_is_reported() {
    Program program;

    WorkflowDecl workflow;
    workflow.name = "BrokenReturnWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    PathExpr missing_path;
    missing_path.path.root_kind = PathRootKind::Identifier;
    missing_path.path.root_name = "missing_node";
    missing_path.path.members = {"value"};
    workflow.return_value = make_expr_ptr(std::move(missing_path));

    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("BrokenReturnWorkflow", make_none());

    check(result.status == WorkflowStatus::EvalError, "return_eval_error.status_eval_error");
    check(result.has_errors(), "return_eval_error.has_errors");
    check(!result.output.has_value(), "return_eval_error.no_output");
}

// ============================================================================
// Test: workflow return 复合表达式支持 capability call
// ============================================================================

void test_return_value_can_call_capability_inside_composite_expression() {
    Program program;

    WorkflowDecl workflow;
    workflow.name = "CapabilityReturnWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    CallExpr ready_call;
    ready_call.callee = "is_ready";

    BinaryExpr condition;
    condition.op = ExprBinaryOp::Equal;
    condition.lhs = make_expr_ptr(std::move(ready_call));
    condition.rhs = make_expr_ptr(BoolLiteralExpr{true});
    workflow.return_value = make_expr_ptr(std::move(condition));

    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.capability_invoker = [](const std::string &name,
                                   const std::vector<Value> & /*args*/) -> CapabilityCallResult {
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
            .value = {},
            .error_message = "unexpected capability",
            .attempts = 1,
        };
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("CapabilityReturnWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "return_capability.status_completed");
    check(!result.has_errors(), "return_capability.no_errors");
    auto *output =
        result.output.has_value() ? std::get_if<BoolValue>(&result.output->node) : nullptr;
    check(output != nullptr && output->value, "return_capability.output_true");
}

// ============================================================================
// Test: workflow node input 复合表达式支持 capability call
// ============================================================================

void test_node_input_can_call_capability_inside_struct_literal() {
    Program program;

    program.declarations.push_back(make_echo_agent("InputEchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("InputEchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = "CapabilityNodeInputWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    WorkflowNode node;
    node.name = "cap_node";
    node.target_ref = make_agent_ref("InputEchoAgent");

    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr value_call;
    value_call.callee = "make_node_value";
    node_input.fields.push_back(StructFieldInit{
        .name = "value",
        .value = make_expr_ptr(std::move(value_call)),
    });
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));

    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "cap_node";
    workflow.return_value = make_expr_ptr(std::move(return_path));

    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.capability_invoker = [](const std::string &name,
                                   const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        if (name == "make_node_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = make_int(123),
                .error_message = {},
                .attempts = 1,
            };
        }
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = {},
            .error_message = "unexpected capability",
            .attempts = 1,
        };
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("CapabilityNodeInputWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "node_input_capability.status_completed");
    check(!result.has_errors(), "node_input_capability.no_errors");
    auto *output =
        result.output.has_value() ? std::get_if<IntValue>(&result.output->node) : nullptr;
    check(output != nullptr && output->value == 123, "node_input_capability.output_123");
}

// ============================================================================
// Test: 空 workflow（无 nodes）
// ============================================================================

void test_empty_workflow() {
    Program program;

    WorkflowDecl workflow;
    workflow.name = "EmptyWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    // 无 nodes
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("EmptyWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "empty.status_completed");
    check(result.execution_order.empty(), "empty.exec_order_empty");
    check(result.node_results.empty(), "empty.node_results_empty");
}

// ============================================================================
// Test: 缺失 Agent 声明（错误）
// ============================================================================

void test_missing_agent_declaration() {
    Program program;

    // 不添加 Agent/Flow 声明，只有 workflow
    WorkflowDecl workflow;
    workflow.name = "BrokenWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("orphan", "NonExistentAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("BrokenWorkflow", make_none());

    check(result.status == WorkflowStatus::NodeFailed, "missing_agent.status_node_failed");
    check(result.has_errors(), "missing_agent.has_errors");
}

// ============================================================================
// Test: Workflow 不存在
// ============================================================================

void test_missing_workflow() {
    Program program;

    WorkflowRuntime runtime(program);
    auto result = runtime.run("NonExistentWorkflow", make_none());

    check(result.status == WorkflowStatus::NodeFailed, "missing_wf.status_failed");
    check(result.has_errors(), "missing_wf.has_errors");
}

// ============================================================================
// Test: Node input 使用另一个 node 的 output
// ============================================================================

void test_node_input_uses_node_output() {
    Program program;

    // AgentA: 返回 struct {value: 100}
    program.declarations.push_back(make_echo_agent("AgentA"));
    program.declarations.push_back(make_struct_return_flow("AgentA", "AOutput", "value", "100"));

    // AgentB: 简单返回整数 200
    program.declarations.push_back(make_echo_agent("AgentB"));
    program.declarations.push_back(make_echo_flow("AgentB", "200"));

    WorkflowDecl workflow;
    workflow.name = "CrossNodeWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("a", "AgentA"));
    // b 的 input 引用 a.value (via PathExpr with Identifier root)
    workflow.nodes.push_back(make_node_with_node_output_path("b", "AgentB", "a", "value", {"a"}));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("CrossNodeWorkflow", make_none());

    check(result.status == WorkflowStatus::Completed, "cross_node.status_completed");
    check(result.execution_order.size() == 2, "cross_node.exec_order_size");
    check(result.execution_order[0] == "a", "cross_node.a_first");
    check(result.execution_order[1] == "b", "cross_node.b_second");
    check(result.node_results.size() == 2, "cross_node.node_results_size");
    // Both completed
    check(result.node_results[0].status == AgentStatus::Completed, "cross_node.a_completed");
    check(result.node_results[1].status == AgentStatus::Completed, "cross_node.b_completed");
}

} // anonymous namespace

int main() {
    test_single_node_workflow();
    test_linear_three_node_workflow();
    test_diamond_workflow();
    test_node_failure_propagation();
    test_return_value_from_node();
    test_return_value_eval_error_is_reported();
    test_return_value_can_call_capability_inside_composite_expression();
    test_node_input_can_call_capability_inside_struct_literal();
    test_empty_workflow();
    test_missing_agent_declaration();
    test_missing_workflow();
    test_node_input_uses_node_output();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
