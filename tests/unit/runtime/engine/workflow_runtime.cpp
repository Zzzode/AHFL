#include "runtime/engine/workflow_runtime.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/runtime/execution_projection.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
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

bool diagnostic_message_contains(const DiagnosticBag &diagnostics, std::string_view fragment) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string_view
execution_node_name(const WorkflowResult &result, std::size_t execution_index) {
    if (execution_index >= result.report.execution_order.size()) {
        return {};
    }
    const auto *node = result.metadata.node(result.report.execution_order[execution_index]);
    return node != nullptr ? std::string_view{node->display_name} : std::string_view{};
}

// ============================================================================
// IR construction helper functions
// ============================================================================

ExprArena &test_expr_arena() {
    static ExprArena arena;
    return arena;
}

ExprRef make_expr_ptr(ExprNode node) {
    return test_expr_arena().make(std::move(node));
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

// Build a simple EchoAgent (Init -> Done, returns a field value of input)
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

// Build an EchoFlow: Init goto Done; Done return IntegerLiteral
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

// Build a Flow that returns a StructLiteral
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

// Build a Flow: Init goto Done; Done return input.<field_name>
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

// Build a flow that will fail (Init state asserts false)
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

// Build a workflow node (no input expression)
WorkflowNode
make_node(const std::string &name, const std::string &target, std::vector<std::string> after = {}) {
    WorkflowNode node;
    node.name = name;
    node.target_ref = make_agent_ref(target);
    node.input = nullptr;
    node.after = std::move(after);
    return node;
}

// Build a workflow node with PathExpr input referencing another node (node_name.field)
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
// Test: single-node workflow
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
    // return value: directly return a PathExpr of node output -> here simply set to null
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("SingleNodeWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "single_node.status_completed");
    check(result.report.execution_order.size() == 1, "single_node.exec_order_size");
    check(execution_node_name(result, 0) == "echo", "single_node.exec_order_name");
    check(result.report.nodes.size() == 1, "single_node.node_results_size");
    check(result.report.nodes[0].status == NodeReportStatus::Completed,
          "single_node.node_completed");
    check(!result.has_errors(), "single_node.no_errors");
}

void test_run_uses_event_report_as_canonical_result() {
    Program program;
    program.declarations.push_back(make_echo_agent("ReportAgent"));
    program.declarations.push_back(make_echo_flow("ReportAgent", "42"));

    WorkflowDecl workflow;
    workflow.name = "ReportWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("report_node", "ReportAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("ReportWorkflow", make_none());

    check(validate_execution_events(result.events.events()).ok(),
          "canonical_result.events_validate");
    check(result.report.execution_order.size() == 1, "canonical_result.order_size");
    if (!result.report.execution_order.empty()) {
        const auto node_id = result.report.execution_order.front();
        const auto *node = result.metadata.node(node_id);
        check(node != nullptr, "canonical_result.node_metadata");
        if (node != nullptr) {
            check(node->display_name == "report_node", "canonical_result.node_display_name");
            const auto *agent = result.metadata.agent(node->agent);
            check(agent != nullptr && agent->display_name == "ReportAgent",
                  "canonical_result.agent_display_name");
        }
    }
    check(result.report.nodes.size() == 1, "canonical_result.node_report_size");
    check(result.report.nodes[0].status == NodeReportStatus::Completed,
          "canonical_result.node_completed");
    check(result.report.nodes[0].output.has_value(), "canonical_result.node_output_id");
    if (result.report.nodes[0].output.has_value()) {
        const auto *value = result.value(*result.report.nodes[0].output);
        const auto *integer = value != nullptr ? std::get_if<IntValue>(&value->node) : nullptr;
        check(integer != nullptr && integer->value == 42,
              "canonical_result.value_store_lookup");
    }
}

// ============================================================================
// Test: linear 3-node workflow (A -> B -> C)
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

    check(result.status() == WorkflowStatus::Completed, "linear.status_completed");
    check(result.report.execution_order.size() == 3, "linear.exec_order_size");
    check(execution_node_name(result, 0) == "a", "linear.order_0_a");
    check(execution_node_name(result, 1) == "b", "linear.order_1_b");
    check(execution_node_name(result, 2) == "c", "linear.order_2_c");
    check(result.report.nodes.size() == 3, "linear.node_results_size");
    for (const auto &node : result.report.nodes) {
        check(node.status == NodeReportStatus::Completed, "linear.all_nodes_completed");
    }
}

// ============================================================================
// Test: diamond workflow (A, B -> C)
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

    check(result.status() == WorkflowStatus::Completed, "diamond.status_completed");
    check(result.report.execution_order.size() == 3, "diamond.exec_order_size");
    // a and b execute first (order may vary but both before c)
    check(execution_node_name(result, 2) == "c", "diamond.c_is_last");
    check(result.report.nodes.size() == 3, "diamond.node_results_size");
}

// ============================================================================
// Test: node failure propagation
// ============================================================================

void test_node_failure_propagation() {
    Program program;

    // FailAgent asserts false in Init state
    program.declarations.push_back(make_echo_agent("FailAgent"));
    program.declarations.push_back(make_failing_flow("FailAgent"));

    // SuccessAgent executes normally
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

    check(result.status() == WorkflowStatus::NodeFailed ||
              result.status() == WorkflowStatus::DependencyFailed,
          "failure.status_not_completed");
    check(result.report.nodes.size() == 2, "failure.node_results_size");
    // First node failed
    check(execution_node_name(result, 0) == "fail_node", "failure.first_is_fail_node");
    check(result.report.nodes[0].status == NodeReportStatus::Failed, "failure.first_failed");
    // Second node skipped due to dependency failure
    check(execution_node_name(result, 1) == "succ_node", "failure.second_is_succ_node");
    check(result.report.nodes[1].status == NodeReportStatus::Skipped,
          "failure.second_dep_failed");
}

// ============================================================================
// Test: return value comes from the last node
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

    check(result.status() == WorkflowStatus::Completed, "return_val.status_completed");
    check(result.output() != nullptr, "return_val.has_output");
    if (const auto *output = result.output(); output != nullptr) {
        const auto *iv = std::get_if<IntValue>(&output->node);
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

    check(result.status() == WorkflowStatus::EvalError, "return_eval_error.status_eval_error");
    check(result.has_errors(), "return_eval_error.has_errors");
    check(result.output() == nullptr, "return_eval_error.no_output");
}

// ============================================================================
// Test: workflow return composite expression supports capability call
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

    check(result.status() == WorkflowStatus::Completed, "return_capability.status_completed");
    check(!result.has_errors(), "return_capability.no_errors");
    const auto *result_output = result.output();
    const auto *output =
        result_output != nullptr ? std::get_if<BoolValue>(&result_output->node) : nullptr;
    check(output != nullptr && output->value, "return_capability.output_true");
}

// ============================================================================
// Test: workflow node input composite expression supports capability call
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

    check(result.status() == WorkflowStatus::Completed, "node_input_capability.status_completed");
    check(!result.has_errors(), "node_input_capability.no_errors");
    const auto *result_output = result.output();
    const auto *output =
        result_output != nullptr ? std::get_if<IntValue>(&result_output->node) : nullptr;
    check(output != nullptr && output->value == 123, "node_input_capability.output_123");
}

void test_node_input_capability_failure_fails_workflow_with_diagnostic() {
    Program program;

    program.declarations.push_back(make_echo_agent("InputEchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("InputEchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = "CapabilityNodeInputFailureWorkflow";
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
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.capability_invoker = [](const std::string &name,
                                   const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        if (name == "make_node_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Timeout,
                .value = {},
                .error_message = "upstream deadline exceeded",
                .attempts = 2,
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
    auto result = runtime.run("CapabilityNodeInputFailureWorkflow", make_none());

    check(result.status() == WorkflowStatus::EvalError, "node_input_capability_failure.status");
    check(result.has_errors(), "node_input_capability_failure.has_errors");
    check(result.report.execution_order.size() == 1,
          "node_input_capability_failure.exec_order_size");
    check(execution_node_name(result, 0) == "cap_node",
          "node_input_capability_failure.exec_order");
    check(result.report.nodes.size() == 1, "node_input_capability_failure.node_result_size");
    check(result.report.nodes[0].status == NodeReportStatus::Failed,
          "node_input_capability_failure.node_failed");
    check(result.output() == nullptr, "node_input_capability_failure.no_output");
    check(diagnostic_message_contains(
              result.diagnostics,
              "capability 'make_node_value' failed with status timeout: upstream deadline "
              "exceeded (attempts=2)"),
          "node_input_capability_failure.diagnostic");
}

void test_contextual_invoker_receives_node_input_context() {
    Program program;

    program.declarations.push_back(make_echo_agent("InputEchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("InputEchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = "ContextualNodeInputWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    WorkflowNode node;
    node.name = "ctx_input_node";
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
    program.declarations.push_back(std::move(workflow));

    std::vector<CapabilityInvocationContext> contexts;
    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [&contexts](const CapabilityInvocationContext &context,
                    const std::string &name,
                    const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        contexts.push_back(context);
        if (name == "make_node_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = make_int(321),
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
    auto result = runtime.run("ContextualNodeInputWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "context_node_input.status_completed");
    check(contexts.size() == 1, "context_node_input.context_count");
    if (!contexts.empty()) {
        check(contexts[0].workflow_name == "ContextualNodeInputWorkflow",
              "context_node_input.workflow");
        check(contexts[0].workflow_node_name == "ctx_input_node", "context_node_input.node");
        check(contexts[0].agent_name == "InputEchoAgent", "context_node_input.agent");
        check(contexts[0].state_name.empty(), "context_node_input.no_state");
        check(contexts[0].has_workflow_node_context, "context_node_input.has_node_context");
        check(contexts[0].workflow_node_execution_index == 0, "context_node_input.node_index");
        check(contexts[0].run_id == RunId{0}, "context_node_input.run_id");
        check(contexts[0].workflow_id == WorkflowId{0}, "context_node_input.workflow_id");
        check(contexts[0].workflow_node_id == WorkflowNodeId{0},
              "context_node_input.workflow_node_id");
        check(contexts[0].agent_id == AgentId{0}, "context_node_input.agent_id");
    }
}

void test_contextual_invoker_receives_capability_identity_and_events() {
    Program program;

    CapabilityDecl capability;
    capability.name = "make_node_value";
    capability.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "make_node_value",
        .local_name = "make_node_value",
        .id = 41,
    };
    program.declarations.push_back(std::move(capability));
    program.declarations.push_back(make_echo_agent("InputEchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("InputEchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = "CapabilityIdentityWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    WorkflowNode node;
    node.name = "identity_node";
    node.target_ref = make_agent_ref("InputEchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr value_call;
    value_call.callee = "make_node_value";
    value_call.callee_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "make_node_value",
        .local_name = "make_node_value",
        .id = 41,
    };
    node_input.fields.push_back(StructFieldInit{
        .name = "value",
        .value = make_expr_ptr(std::move(value_call)),
    });
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    program.declarations.push_back(std::move(workflow));

    std::vector<CapabilityInvocationContext> contexts;
    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [&contexts](const CapabilityInvocationContext &context,
                    const std::string &name,
                    const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        contexts.push_back(context);
        if (name == "make_node_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = make_int(456),
                .attempts = 1,
                .cache_hit = true,
            };
        }
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .error_message = "unexpected capability",
        };
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("CapabilityIdentityWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "capability_identity.status_completed");
    check(contexts.size() == 1, "capability_identity.context_count");
    if (!contexts.empty()) {
        check(contexts[0].capability_id.valid(), "capability_identity.id_valid");
        check(contexts[0].capability_id == CapabilityId{0}, "capability_identity.id_index");
        const auto *metadata = result.metadata.capability(contexts[0].capability_id);
        check(metadata != nullptr && metadata->display_name == "make_node_value",
              "capability_identity.metadata");
        check(contexts[0].invocation_id == InvocationId{0},
              "capability_identity.invocation_id");
    }
    std::size_t started = 0;
    std::size_t completed = 0;
    bool cache_hit = false;
    for (const auto &event : result.events.events()) {
        started += std::holds_alternative<CapabilityStarted>(event.payload) ? 1U : 0U;
        completed += std::holds_alternative<CapabilityCompleted>(event.payload) ? 1U : 0U;
        if (const auto *payload = std::get_if<CapabilityCompleted>(&event.payload)) {
            cache_hit = payload->cache_hit;
        }
    }
    check(started == 1, "capability_identity.started_event");
    check(completed == 1, "capability_identity.completed_event");
    check(cache_hit, "capability_identity.cache_hit_propagated");
}

void test_retry_and_fallback_emit_paired_attempt_events() {
    Program program;

    CapabilityDecl capability;
    capability.name = "unstable";
    capability.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "unstable",
        .local_name = "unstable",
        .id = 42,
    };
    program.declarations.push_back(std::move(capability));
    program.declarations.push_back(make_echo_agent("RetryAgent"));

    FlowDecl flow;
    flow.target_ref = make_agent_ref("RetryAgent");
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));
    StateHandler done_handler;
    done_handler.state_name = "Done";
    CallExpr call;
    call.callee = "unstable";
    call.callee_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "unstable",
        .local_name = "unstable",
        .id = 42,
    };
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(call))}));
    flow.state_handlers.push_back(std::move(done_handler));
    program.declarations.push_back(std::move(flow));

    WorkflowDecl workflow;
    workflow.name = "RetryWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("retry_node", "RetryAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &,
           const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Success,
            .value = make_int(7),
            .attempts = 3,
            .provider_degraded = true,
            .degraded_provider_name = "primary",
            .selected_provider_name = "fallback",
        };
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("RetryWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "retry_fallback.status_completed");
    std::size_t started = 0;
    std::size_t failed = 0;
    std::size_t retries = 0;
    std::size_t completed = 0;
    std::size_t degraded = 0;
    for (const auto &event : result.events.events()) {
        started += std::holds_alternative<CapabilityStarted>(event.payload) ? 1U : 0U;
        failed += std::holds_alternative<CapabilityFailed>(event.payload) ? 1U : 0U;
        retries +=
            std::holds_alternative<CapabilityRetryScheduled>(event.payload) ? 1U : 0U;
        completed += std::holds_alternative<CapabilityCompleted>(event.payload) ? 1U : 0U;
        degraded += std::holds_alternative<ProviderDegraded>(event.payload) ? 1U : 0U;
    }
    check(started == 3, "retry_fallback.started_attempts");
    check(failed == 2, "retry_fallback.failed_attempts");
    check(retries == 2, "retry_fallback.retry_edges");
    check(completed == 1, "retry_fallback.completed_attempt");
    check(degraded == 1, "retry_fallback.provider_degraded");
    check(validate_execution_events(result.events.events()).ok(),
          "retry_fallback.terminal_invariant");
}

void test_budget_rejection_is_classified_in_terminal_events() {
    Program program;
    program.declarations.push_back(make_echo_agent("BudgetAgent"));
    FlowDecl flow;
    flow.target_ref = make_agent_ref("BudgetAgent");
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));
    StateHandler done_handler;
    done_handler.state_name = "Done";
    CallExpr call;
    call.callee = "budgeted";
    call.callee_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "budgeted",
        .local_name = "budgeted",
        .id = 43,
    };
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(call))}));
    flow.state_handlers.push_back(std::move(done_handler));
    program.declarations.push_back(std::move(flow));

    CapabilityDecl capability;
    capability.name = "budgeted";
    capability.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "budgeted",
        .local_name = "budgeted",
        .id = 43,
    };
    program.declarations.push_back(std::move(capability));

    WorkflowDecl workflow;
    workflow.name = "BudgetWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("budget_node", "BudgetAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &,
           const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        return CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .error_message = "prompt budget rejected",
            .attempts = 0,
            .failure_kind = CapabilityFailureKind::BudgetRejected,
            .usage = CapabilityUsage{
                .prompt_tokens = 16,
                .completion_tokens = 4,
                .total_tokens = 20,
                .total_cost_usd = 0.000012,
                .cost_estimated = true,
            },
        };
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("BudgetWorkflow", make_none());

    check(result.report.failure_kind == WorkflowFailureKind::BudgetRejected,
          "budget_rejection.workflow_kind");
    bool capability_rejected = false;
    bool usage_recorded = false;
    bool node_rejected = false;
    for (const auto &event : result.events.events()) {
        if (const auto *usage = std::get_if<CapabilityUsageRecorded>(&event.payload)) {
            usage_recorded = usage->prompt_tokens == 16 &&
                             usage->completion_tokens == 4 &&
                             usage->total_tokens == 20 &&
                             usage->total_cost_usd == 0.000012 &&
                             usage->cost_estimated;
        }
        if (const auto *failed = std::get_if<CapabilityFailed>(&event.payload)) {
            capability_rejected =
                failed->kind == CapabilityFailureKind::BudgetRejected;
        }
        if (const auto *failed = std::get_if<NodeFailed>(&event.payload)) {
            node_rejected = failed->kind == NodeFailureKind::BudgetRejected;
        }
    }
    check(usage_recorded, "budget_rejection.usage_recorded");
    check(capability_rejected, "budget_rejection.capability_kind");
    check(node_rejected, "budget_rejection.node_kind");
    check(validate_execution_events(result.events.events()).ok(),
          "budget_rejection.terminal_invariant");
}

void test_cancellation_and_interruption_terminalize_scheduled_nodes() {
    auto build_program = [] {
        Program program;
        program.declarations.push_back(make_echo_agent("ControlAgent"));
        program.declarations.push_back(make_echo_flow("ControlAgent", "1"));
        WorkflowDecl workflow;
        workflow.name = "ControlWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        workflow.nodes.push_back(make_node("first", "ControlAgent"));
        workflow.nodes.push_back(make_node("second", "ControlAgent", {"first"}));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    {
        auto program = build_program();
        WorkflowRuntimeConfig config;
        config.cancellation_requested = [] { return true; };
        WorkflowRuntime runtime(program, std::move(config));
        auto result = runtime.run("ControlWorkflow", make_none());
        check(result.report.status == RunTerminalStatus::Cancelled,
              "cancellation.run_status");
        check(result.report.failure_kind == WorkflowFailureKind::Cancelled,
              "cancellation.workflow_kind");
        check(result.report.nodes.size() == 2, "cancellation.node_count");
        check(result.report.nodes[0].status == NodeReportStatus::Skipped,
              "cancellation.first_skipped");
        check(result.report.nodes[1].status == NodeReportStatus::Skipped,
              "cancellation.second_skipped");
        check(validate_execution_events(result.events.events()).ok(),
              "cancellation.terminal_invariant");
    }

    {
        auto program = build_program();
        WorkflowRuntimeConfig config;
        config.interruption_requested = [] { return true; };
        WorkflowRuntime runtime(program, std::move(config));
        auto result = runtime.run("ControlWorkflow", make_none());
        check(result.report.status == RunTerminalStatus::Interrupted,
              "interruption.run_status");
        check(result.report.failure_kind == WorkflowFailureKind::Interrupted,
              "interruption.workflow_kind");
        check(validate_execution_events(result.events.events()).ok(),
              "interruption.terminal_invariant");
    }
}

void test_checkpoint_and_resume_events_share_run_identity() {
    Program program;
    program.declarations.push_back(make_echo_agent("CheckpointAgent"));
    program.declarations.push_back(make_echo_flow("CheckpointAgent", "1"));
    WorkflowDecl workflow;
    workflow.name = "CheckpointWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("checkpoint_node", "CheckpointAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntimeConfig config;
    config.resume_checkpoint = CheckpointId{4};
    config.checkpoint_after_node =
        [](WorkflowNodeId node) -> std::optional<CheckpointId> {
        return CheckpointId{node.index() + 5};
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("CheckpointWorkflow", make_none());
    bool resumed = false;
    bool saved = false;
    for (const auto &event : result.events.events()) {
        if (const auto *resume = std::get_if<RunResumed>(&event.payload)) {
            resumed = resume->run == RunId{0} && resume->checkpoint == CheckpointId{4};
        }
        if (const auto *checkpoint = std::get_if<CheckpointSaved>(&event.payload)) {
            saved = checkpoint->run == RunId{0} && checkpoint->checkpoint == CheckpointId{5};
        }
    }
    check(resumed, "checkpoint_resume.resumed_event");
    check(saved, "checkpoint_resume.saved_event");
    check(validate_execution_events(result.events.events()).ok(),
          "checkpoint_resume.terminal_invariant");
}

void test_contextual_invoker_receives_agent_state_context() {
    Program program;

    program.declarations.push_back(make_echo_agent("ContextAgent"));
    FlowDecl flow;
    flow.target_ref = make_agent_ref("ContextAgent");
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));
    StateHandler done_handler;
    done_handler.state_name = "Done";
    CallExpr call;
    call.callee = "make_agent_value";
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(call))}));
    flow.state_handlers.push_back(std::move(done_handler));
    program.declarations.push_back(std::move(flow));

    WorkflowDecl workflow;
    workflow.name = "ContextualAgentWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("ctx_agent_node", "ContextAgent"));
    program.declarations.push_back(std::move(workflow));

    std::vector<CapabilityInvocationContext> contexts;
    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [&contexts](const CapabilityInvocationContext &context,
                    const std::string &name,
                    const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        contexts.push_back(context);
        if (name == "make_agent_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::Success,
                .value = make_int(654),
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
    auto result = runtime.run("ContextualAgentWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "context_agent.status_completed");
    check(contexts.size() == 1, "context_agent.context_count");
    if (!contexts.empty()) {
        check(contexts[0].workflow_name == "ContextualAgentWorkflow", "context_agent.workflow");
        check(contexts[0].workflow_node_name == "ctx_agent_node", "context_agent.node");
        check(contexts[0].agent_name == "ContextAgent", "context_agent.agent");
        check(contexts[0].state_name == "Done", "context_agent.state");
        check(contexts[0].has_workflow_node_context, "context_agent.has_node_context");
        check(contexts[0].workflow_node_execution_index == 0, "context_agent.node_index");
        check(contexts[0].workflow_node_id == WorkflowNodeId{0},
              "context_agent.workflow_node_id");
        check(contexts[0].agent_id == AgentId{0}, "context_agent.agent_id");
        check(contexts[0].agent_state_id.valid(), "context_agent.state_id");
        const auto *state = result.metadata.agent_state(contexts[0].agent_state_id);
        check(state != nullptr && state->display_name == "Done",
              "context_agent.state_metadata");
    }
    std::size_t state_events = 0;
    for (const auto &event : result.events.events()) {
        state_events += std::holds_alternative<AgentStateEntered>(event.payload) ? 1U : 0U;
    }
    check(state_events == 2, "context_agent.state_event_count");
}

void test_agent_state_capability_failure_fails_workflow_with_contextual_diagnostic() {
    Program program;

    program.declarations.push_back(make_echo_agent("ContextAgent"));
    FlowDecl flow;
    flow.target_ref = make_agent_ref("ContextAgent");
    StateHandler init_handler;
    init_handler.state_name = "Init";
    init_handler.body.statements.push_back(make_stmt_ptr(GotoStatement{"Done"}));
    flow.state_handlers.push_back(std::move(init_handler));
    StateHandler done_handler;
    done_handler.state_name = "Done";
    CallExpr call;
    call.callee = "make_agent_value";
    done_handler.body.statements.push_back(
        make_stmt_ptr(ReturnStatement{make_expr_ptr(std::move(call))}));
    flow.state_handlers.push_back(std::move(done_handler));
    program.declarations.push_back(std::move(flow));

    WorkflowDecl workflow;
    workflow.name = "ContextualAgentFailureWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("ctx_agent_node", "ContextAgent"));
    program.declarations.push_back(std::move(workflow));

    std::vector<CapabilityInvocationContext> contexts;
    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [&contexts](const CapabilityInvocationContext &context,
                    const std::string &name,
                    const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        contexts.push_back(context);
        if (name == "make_agent_value") {
            return CapabilityCallResult{
                .status = CapabilityCallStatus::RetryExhausted,
                .value = {},
                .error_message = "service unavailable",
                .attempts = 3,
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
    auto result = runtime.run("ContextualAgentFailureWorkflow", make_none());

    check(result.status() == WorkflowStatus::NodeFailed, "agent_capability_failure.status");
    check(result.has_errors(), "agent_capability_failure.has_errors");
    check(result.report.execution_order.size() == 1,
          "agent_capability_failure.exec_order_size");
    check(execution_node_name(result, 0) == "ctx_agent_node",
          "agent_capability_failure.exec_order");
    check(result.report.nodes.size() == 1, "agent_capability_failure.node_result_size");
    check(result.report.nodes[0].status == NodeReportStatus::Failed,
          "agent_capability_failure.node_failed");
    check(result.output() == nullptr, "agent_capability_failure.no_output");
    check(diagnostic_message_contains(
              result.diagnostics,
              "capability 'make_agent_value' failed with status retry_exhausted: service "
              "unavailable (attempts=3)"),
          "agent_capability_failure.diagnostic");

    check(contexts.size() == 1, "agent_capability_failure.context_count");
    if (!contexts.empty()) {
        check(contexts[0].workflow_name == "ContextualAgentFailureWorkflow",
              "agent_capability_failure.workflow");
        check(contexts[0].workflow_node_name == "ctx_agent_node", "agent_capability_failure.node");
        check(contexts[0].agent_name == "ContextAgent", "agent_capability_failure.agent");
        check(contexts[0].state_name == "Done", "agent_capability_failure.state");
        check(contexts[0].has_workflow_node_context, "agent_capability_failure.has_node_context");
        check(contexts[0].workflow_node_execution_index == 0,
              "agent_capability_failure.node_index");
        check(contexts[0].workflow_node_id == WorkflowNodeId{0},
              "agent_capability_failure.workflow_node_id");
        check(contexts[0].agent_id == AgentId{0}, "agent_capability_failure.agent_id");
    }
}

// ============================================================================
// Test: empty workflow (no nodes)
// ============================================================================

void test_empty_workflow() {
    Program program;

    WorkflowDecl workflow;
    workflow.name = "EmptyWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    // No nodes
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("EmptyWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "empty.status_completed");
    check(result.report.execution_order.empty(), "empty.exec_order_empty");
    check(result.report.nodes.empty(), "empty.node_results_empty");
}

// ============================================================================
// Test: missing Agent declaration (error)
// ============================================================================

void test_missing_agent_declaration() {
    Program program;

    // Do not add Agent/Flow declarations, only the workflow
    WorkflowDecl workflow;
    workflow.name = "BrokenWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("orphan", "NonExistentAgent"));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("BrokenWorkflow", make_none());

    check(result.status() == WorkflowStatus::NodeFailed, "missing_agent.status_node_failed");
    check(result.has_errors(), "missing_agent.has_errors");
}

// ============================================================================
// Test: workflow does not exist
// ============================================================================

void test_missing_workflow() {
    Program program;

    WorkflowRuntime runtime(program);
    auto result = runtime.run("NonExistentWorkflow", make_none());

    check(result.status() == WorkflowStatus::NodeFailed, "missing_wf.status_failed");
    check(result.has_errors(), "missing_wf.has_errors");
}

// ============================================================================
// Test: node input uses another node's output
// ============================================================================

void test_node_input_uses_node_output() {
    Program program;

    // AgentA: returns struct {value: 100}
    program.declarations.push_back(make_echo_agent("AgentA"));
    program.declarations.push_back(make_struct_return_flow("AgentA", "AOutput", "value", "100"));

    // AgentB: simply returns the integer 200
    program.declarations.push_back(make_echo_agent("AgentB"));
    program.declarations.push_back(make_echo_flow("AgentB", "200"));

    WorkflowDecl workflow;
    workflow.name = "CrossNodeWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("a", "AgentA"));
    // b's input references a.value (via PathExpr with Identifier root)
    workflow.nodes.push_back(make_node_with_node_output_path("b", "AgentB", "a", "value", {"a"}));
    program.declarations.push_back(std::move(workflow));

    WorkflowRuntime runtime(program);
    auto result = runtime.run("CrossNodeWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "cross_node.status_completed");
    check(result.report.execution_order.size() == 2, "cross_node.exec_order_size");
    check(execution_node_name(result, 0) == "a", "cross_node.a_first");
    check(execution_node_name(result, 1) == "b", "cross_node.b_second");
    check(result.report.nodes.size() == 2, "cross_node.node_results_size");
    // Both completed
    check(result.report.nodes[0].status == NodeReportStatus::Completed,
          "cross_node.a_completed");
    check(result.report.nodes[1].status == NodeReportStatus::Completed,
          "cross_node.b_completed");
}

void test_recovery_snapshot_restores_completed_node_without_reexecution() {
    Program program;
    program.declarations.push_back(make_echo_agent("AgentA"));
    program.declarations.push_back(make_struct_return_flow("AgentA", "AOutput", "value", "999"));
    program.declarations.push_back(make_echo_agent("AgentB"));
    program.declarations.push_back(make_input_field_return_flow("AgentB", "value"));

    WorkflowDecl workflow;
    workflow.name = "RecoveryWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    workflow.nodes.push_back(make_node("a", "AgentA"));
    WorkflowNode second = make_node("b", "AgentB", {"a"});
    PathExpr restored_input;
    restored_input.path.root_kind = PathRootKind::Identifier;
    restored_input.path.root_name = "a";
    second.input = make_expr_ptr(std::move(restored_input));
    workflow.nodes.push_back(std::move(second));
    PathExpr return_value;
    return_value.path.root_kind = PathRootKind::Identifier;
    return_value.path.root_name = "b";
    workflow.return_value = make_expr_ptr(std::move(return_value));
    program.declarations.push_back(std::move(workflow));

    std::unordered_map<std::string, Value> fields;
    fields.emplace("value", make_int(100));
    WorkflowRecoverySnapshot snapshot{
        .workflow = WorkflowId{0},
        .checkpoint = CheckpointId{4},
    };
    snapshot.completed_nodes.push_back(RecoveredNodeState{
        .node = WorkflowNodeId{0},
        .agent = AgentId{0},
        .output = make_struct("AOutput", std::move(fields)),
    });

    WorkflowRuntimeConfig config;
    config.recovery_snapshot = std::move(snapshot);
    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("RecoveryWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "recovery.status_completed");
    const auto *output =
        result.output() != nullptr ? std::get_if<IntValue>(&result.output()->node) : nullptr;
    check(output != nullptr && output->value == 100, "recovery.output_from_restored_dependency");

    std::size_t restored = 0;
    std::size_t started_a = 0;
    for (const auto &event : result.events.events()) {
        if (const auto *payload = std::get_if<NodeRestored>(&event.payload)) {
            restored += payload->node == WorkflowNodeId{0} ? 1U : 0U;
        }
        if (const auto *payload = std::get_if<NodeStarted>(&event.payload)) {
            started_a += payload->node == WorkflowNodeId{0} ? 1U : 0U;
        }
    }
    check(restored == 1, "recovery.node_restored_once");
    check(started_a == 0, "recovery.restored_node_not_reexecuted");
    check(result.report.nodes[0].restored_from_checkpoint == CheckpointId{4},
          "recovery.report_checkpoint");
    check(validate_execution_events(result.events.events()).ok(),
          "recovery.terminal_invariant");

    const auto replay = build_execution_replay_projection(result);
    check(replay.has_value() && replay->nodes[0].restored,
          "recovery.replay_marks_restored");
    const auto audit = build_execution_audit_projection(result);
    check(audit.has_value() && audit->node_restored == 1,
          "recovery.audit_counts_restored");
}

} // anonymous namespace

int main() {
    test_single_node_workflow();
    test_run_uses_event_report_as_canonical_result();
    test_linear_three_node_workflow();
    test_diamond_workflow();
    test_node_failure_propagation();
    test_return_value_from_node();
    test_return_value_eval_error_is_reported();
    test_return_value_can_call_capability_inside_composite_expression();
    test_node_input_can_call_capability_inside_struct_literal();
    test_node_input_capability_failure_fails_workflow_with_diagnostic();
    test_contextual_invoker_receives_node_input_context();
    test_contextual_invoker_receives_capability_identity_and_events();
    test_retry_and_fallback_emit_paired_attempt_events();
    test_budget_rejection_is_classified_in_terminal_events();
    test_cancellation_and_interruption_terminalize_scheduled_nodes();
    test_checkpoint_and_resume_events_share_run_identity();
    test_contextual_invoker_receives_agent_state_context();
    test_agent_state_capability_failure_fails_workflow_with_contextual_diagnostic();
    test_empty_workflow();
    test_missing_agent_declaration();
    test_missing_workflow();
    test_node_input_uses_node_output();
    test_recovery_snapshot_restores_completed_node_without_reexecution();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
