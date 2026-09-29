#include "runtime/engine/workflow_runtime.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/runtime/execution_projection.hpp"
#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
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

// RFC 0026 C2b stage3 (Option B): a capability declared to return Int, with an
// explicit stable SymbolId matching its refs (durable identity must be
// eyeball-auditable — no runtime hashing).
[[nodiscard]] CapabilityDecl make_int_capability_decl(const std::string &name, std::size_t id) {
    CapabilityDecl capability;
    capability.name = name;
    capability.return_type_ref = TypeRef{.kind = TypeRefKind::Int};
    capability.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = name,
        .local_name = name,
        .id = id,
    };
    return capability;
}

// A CallExpr.callee_ref bound to a capability's stable SymbolId (Option B: the
// resume trust boundary requires a resolvable, id-matched declaration).
[[nodiscard]] SymbolRef make_capability_ref(const std::string &name, std::size_t id) {
    return SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = name,
        .local_name = name,
        .id = id,
    };
}

// RFC 0026 C2b stage3 rich-shape matrix: TypeRef builders for the projectable
// return types. Collections/Option are builtin nominals auto-registered in
// build_core_type_environment; the nominal resolver matches them by canonical name
// (no id needed). Element/key/value types go in `params`.
[[nodiscard]] TypeRefPtr type_ptr(TypeRef t) { return std::make_unique<TypeRef>(std::move(t)); }

[[nodiscard]] TypeRef int_type() { return TypeRef{.kind = TypeRefKind::Int}; }
[[nodiscard]] TypeRef float_type() { return TypeRef{.kind = TypeRefKind::Float}; }

[[nodiscard]] TypeRef nominal_type(const std::string &canonical, TypeRefKind kind,
                                   std::vector<TypeRefPtr> params) {
    TypeRef t;
    t.kind = kind;
    t.canonical_name = canonical;
    t.display_name = canonical;
    t.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                              .canonical_name = canonical,
                              .local_name = canonical};
    t.params = std::move(params);
    return t;
}

[[nodiscard]] TypeRef option_of(TypeRef inner) {
    std::vector<TypeRefPtr> params;
    params.push_back(type_ptr(std::move(inner)));
    return nominal_type("std::option::Option", TypeRefKind::Enum, std::move(params));
}
[[nodiscard]] TypeRef set_of(TypeRef inner) {
    std::vector<TypeRefPtr> params;
    params.push_back(type_ptr(std::move(inner)));
    return nominal_type("std::collections::Set", TypeRefKind::Struct, std::move(params));
}
[[nodiscard]] TypeRef map_of(TypeRef key, TypeRef value) {
    std::vector<TypeRefPtr> params;
    params.push_back(type_ptr(std::move(key)));
    params.push_back(type_ptr(std::move(value)));
    return nominal_type("std::collections::Map", TypeRefKind::Struct, std::move(params));
}
[[nodiscard]] TypeRef decimal_type(std::int64_t scale) {
    TypeRef t{.kind = TypeRefKind::Decimal};
    t.decimal_scale = scale;
    return t;
}
[[nodiscard]] TypeRef duration_type() { return TypeRef{.kind = TypeRefKind::Duration}; }
[[nodiscard]] TypeRef unit_type() { return TypeRef{.kind = TypeRefKind::Unit}; }

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

// RFC 0012 slice 2: an injected monotonic clock makes event offsets fully
// deterministic — the runtime measures every event against the first reading,
// so a clock that advances a fixed step per call yields offsets 0, step, 2*step,
// ... regardless of wall time (Test Plan #3, fake-clock).
void test_injected_monotonic_clock_is_deterministic() {
    Program program;
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_echo_flow("EchoAgent", "42"));

    WorkflowDecl workflow;
    workflow.name = "ClockWorkflow";
    workflow.input_type_ref = make_named_type_ref("WfInput");
    workflow.output_type_ref = make_named_type_ref("WfOutput");
    workflow.nodes.push_back(make_node("echo", "EchoAgent"));
    program.declarations.push_back(std::move(workflow));

    // A fake clock that advances exactly 1ms each time it is read, from a fixed
    // origin. std::function is called once for started_at then once per emitted
    // event, so offsets are (reading_index) * 1ms.
    const auto origin = std::chrono::steady_clock::time_point{};
    std::uint64_t ticks = 0;
    WorkflowRuntimeConfig config;
    config.monotonic_clock = [origin, &ticks]() {
        return origin + std::chrono::milliseconds(static_cast<long long>(ticks++));
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("ClockWorkflow", make_none());

    check(result.status() == WorkflowStatus::Completed, "clock.status_completed");
    const auto &events = result.events.events();
    check(!events.empty(), "clock.events_present");
    // started_at consumed reading 0; event i was emitted at reading (i+1), so
    // its offset is exactly (i+1) ms. Strictly increasing, deterministic.
    bool offsets_deterministic = true;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto expected =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(
                static_cast<long long>(i + 1)));
        if (events[i].monotonic_offset != expected) {
            offsets_deterministic = false;
        }
    }
    check(offsets_deterministic, "clock.offsets_match_injected_sequence");

    // Re-running with a fresh identical fake clock reproduces the same offsets.
    std::uint64_t ticks2 = 0;
    WorkflowRuntimeConfig config2;
    config2.monotonic_clock = [origin, &ticks2]() {
        return origin + std::chrono::milliseconds(static_cast<long long>(ticks2++));
    };
    WorkflowRuntime runtime2(program, std::move(config2));
    auto result2 = runtime2.run("ClockWorkflow", make_none());
    bool reproducible = result2.events.events().size() == events.size();
    for (std::size_t i = 0; reproducible && i < events.size(); ++i) {
        if (result2.events.events()[i].monotonic_offset != events[i].monotonic_offset) {
            reproducible = false;
        }
    }
    check(reproducible, "clock.reproducible_across_runs");
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

namespace {

// ============================================================================
// RFC 0022 slice 3 (C8): durable suspend / resume
// ============================================================================

// Build a single-node workflow whose node input is a struct literal
// `{value: <capability>(7)}`. The node's EchoAgent returns input.value, so the
// workflow output IS the capability result. The capability call lives in the
// node-input expression — a node-scoped, resumable dispatch site.
[[nodiscard]] Program make_single_capability_node_program(const std::string &workflow_name,
                                                          const std::string &capability,
                                                          std::size_t capability_id) {
    Program program;
    // Option B: declare the capability (Int return) with an explicit stable
    // SymbolId so the durable-resume trust boundary can resolve + identity-check it.
    program.declarations.push_back(make_int_capability_decl(capability, capability_id));
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = workflow_name;
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr call;
    call.callee = capability;
    call.callee_ref = make_capability_ref(capability, capability_id);
    call.arguments.push_back(make_expr_ptr(IntegerLiteralExpr{"7"}));
    node_input.fields.push_back(StructFieldInit{
        .name = "value",
        .value = make_expr_ptr(std::move(call)),
    });
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));

    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));

    program.declarations.push_back(std::move(workflow));
    return program;
}

// A capability call that suspends stops the workflow at Suspended (not Failed),
// populates the resume record, and emits WorkflowSuspended — never
// WorkflowFailed.
void test_pending_capability_suspends_without_failure() {
    auto program = make_single_capability_node_program("SuspendWorkflow", "answer", 41);

    WorkflowRuntimeConfig config;
    config.contextual_capability_invoker =
        [](const CapabilityInvocationContext & /*context*/,
           const std::string &name,
           const std::vector<Value> & /*args*/) -> CapabilityCallResult {
        CapabilityCallResult pending;
        pending.status = name == "answer" ? CapabilityCallStatus::Pending
                                          : CapabilityCallStatus::Error;
        return pending;
    };

    WorkflowRuntime runtime(program, std::move(config));
    auto result = runtime.run("SuspendWorkflow", make_none());

    check(result.status() == WorkflowStatus::Suspended, "suspend.status_suspended");
    check(!result.has_errors(), "suspend.no_error_diagnostics");
    check(result.suspended.has_value() && result.suspended->suspended.has_value(),
          "suspend.resume_record_present");

    std::size_t suspended_events = 0;
    std::size_t failed_events = 0;
    for (const auto &event : result.events.events()) {
        suspended_events += std::holds_alternative<WorkflowSuspended>(event.payload) ? 1U : 0U;
        failed_events += std::holds_alternative<WorkflowFailed>(event.payload) ? 1U : 0U;
    }
    check(suspended_events == 1, "suspend.one_workflow_suspended_event");
    check(failed_events == 0, "suspend.no_workflow_failed_event");
    check(result.report.status == RunTerminalStatus::Suspended, "suspend.report_status");
    check(validate_execution_events(result.events.events()).ok(), "suspend.terminal_invariant");
}

// Resuming with the pending capability result produces exactly the same output
// as a synchronous run where the capability returned that value immediately.
void test_resume_round_trip_equals_sync_path() {
    // Synchronous baseline: `answer` returns 42 directly → node output is 42.
    {
        auto program = make_single_capability_node_program("SyncWorkflow", "answer", 41);
        WorkflowRuntimeConfig config;
        config.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult ok;
            ok.status = CapabilityCallStatus::Success;
            ok.value = name == "answer" ? make_int(42) : make_none();
            return ok;
        };
        WorkflowRuntime runtime(program, std::move(config));
        auto sync = runtime.run("SyncWorkflow", make_none());
        check(sync.status() == WorkflowStatus::Completed, "resume.sync_completed");
        const auto *out =
            sync.output() != nullptr ? std::get_if<IntValue>(&sync.output()->node) : nullptr;
        check(out != nullptr && out->value == 42, "resume.sync_output_42");
    }

    // Suspend, capture the resume record, then resume by injecting 42.
    auto program = make_single_capability_node_program("ResumeWorkflow", "answer", 41);
    WorkflowRuntimeConfig suspend_config;
    suspend_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult pending;
        pending.status = CapabilityCallStatus::Pending;
        return pending;
    };
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("ResumeWorkflow", make_none());
    check(suspended.suspended.has_value(), "resume.captured_record");

    WorkflowRuntimeConfig resume_config;
    resume_config.recovery_snapshot = std::move(suspended.suspended);
    resume_config.resume_pending_result = make_int(42);
    // The invoker must NOT be called for the resumed pending ordinal; if it is,
    // returning a wrong value would change the output and fail the check.
    resume_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult wrong;
        wrong.status = CapabilityCallStatus::Success;
        wrong.value = make_int(-1);
        return wrong;
    };
    WorkflowRuntime resume_runtime(program, std::move(resume_config));
    auto resumed = resume_runtime.run("ResumeWorkflow", make_none());

    check(resumed.status() == WorkflowStatus::Completed, "resume.completed");
    check(!resumed.has_errors(), "resume.no_errors");
    const auto *resumed_out =
        resumed.output() != nullptr ? std::get_if<IntValue>(&resumed.output()->node) : nullptr;
    check(resumed_out != nullptr && resumed_out->value == 42, "resume.output_matches_sync");
}

// A resume whose memo integrity cross-check fails (corrupted arg_hash on a
// completed call) fails closed rather than silently re-invoking.
void test_resume_fails_closed_on_memo_mismatch() {
    // The node input struct has two fields, each a capability call. `aux` (an
    // offset) is evaluated + memoized first; `value` (answer) suspends. On
    // resume we corrupt the memoized `aux` arg_hash so replay must fail closed.
    Program program;
    // Option B: declare offset/answer (Int) with distinct stable ids so the
    // coordinate gate (corrupted arg_hash) is shown to fire BEFORE the identity
    // gate rather than the identity gate masking it.
    program.declarations.push_back(make_int_capability_decl("offset", 91));
    program.declarations.push_back(make_int_capability_decl("answer", 92));
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = "MismatchWorkflow";
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");

    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr offset_call;
    offset_call.callee = "offset";
    offset_call.callee_ref = make_capability_ref("offset", 91);
    node_input.fields.push_back(StructFieldInit{
        .name = "aux",
        .value = make_expr_ptr(std::move(offset_call)),
    });
    CallExpr answer_call;
    answer_call.callee = "answer";
    answer_call.callee_ref = make_capability_ref("answer", 92);
    node_input.fields.push_back(StructFieldInit{
        .name = "value",
        .value = make_expr_ptr(std::move(answer_call)),
    });
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));

    // First run: `offset` succeeds (memoized), `answer` suspends.
    WorkflowRuntimeConfig suspend_config;
    suspend_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &name,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "offset") {
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(2);
        } else {
            r.status = CapabilityCallStatus::Pending;
        }
        return r;
    };
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("MismatchWorkflow", make_none());
    check(suspended.suspended.has_value() && suspended.suspended->suspended.has_value() &&
              !suspended.suspended->suspended->memo.empty(),
          "mismatch.memo_captured");

    // Corrupt the memoized call's integrity hash, then resume.
    auto corrupted = std::move(suspended.suspended);
    corrupted->suspended->memo[0].arg_hash ^= 0xDEADBEEFULL;
    WorkflowRuntimeConfig resume_config;
    resume_config.recovery_snapshot = std::move(corrupted);
    resume_config.resume_pending_result = make_int(40);
    resume_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(0);
        return r;
    };
    WorkflowRuntime resume_runtime(program, std::move(resume_config));
    auto resumed = resume_runtime.run("MismatchWorkflow", make_none());

    check(resumed.status() != WorkflowStatus::Completed, "mismatch.not_completed");
    check(resumed.status() != WorkflowStatus::Suspended, "mismatch.not_suspended_again");
    check(diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
          "mismatch.fail_closed_diagnostic");
}

// RFC 0022 slice 4 (exactly-once): a durable_write capability fires its
// write-ahead intent before dispatch; on resume it is served from the memo and
// NOT re-invoked, and no second intent is written. Models write-then-crash.
void test_durable_write_exactly_once_across_resume() {
    const auto build_program = []() {
        Program program;
        CapabilityDecl commit;
        commit.name = "commit";
        commit.return_type_ref = TypeRef{.kind = TypeRefKind::Int};
        commit.symbol_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "commit",
            .local_name = "commit",
            .id = 71,
        };
        commit.effect.declared = true;
        commit.effect.kind = CapabilityEffectKind::DurableWrite;
        program.declarations.push_back(std::move(commit));
        // Option B: `later` is also a declared Int capability (distinct id).
        program.declarations.push_back(make_int_capability_decl("later", 72));
        program.declarations.push_back(make_echo_agent("EchoAgent"));
        program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

        WorkflowDecl workflow;
        workflow.name = "ExactlyOnceWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        CallExpr commit_call;
        commit_call.callee = "commit";
        commit_call.callee_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "commit",
            .local_name = "commit",
            .id = 71,
        };
        node_input.fields.push_back(StructFieldInit{
            .name = "value",
            .value = make_expr_ptr(std::move(commit_call)),
        });
        // A second field whose capability suspends AFTER commit succeeds, so the
        // durable write is memoized and the node suspends mid-input.
        CallExpr later_call;
        later_call.callee = "later";
        later_call.callee_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "later",
            .local_name = "later",
            .id = 72,
        };
        node_input.fields.push_back(StructFieldInit{
            .name = "aux",
            .value = make_expr_ptr(std::move(later_call)),
        });
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    // First run: commit (durable_write) fires its intent + succeeds; later
    // suspends. Capture the intent keys and the commit invocation count.
    auto program = build_program();
    std::vector<std::uint64_t> first_intents;
    std::size_t commit_invocations = 0;
    WorkflowRuntimeConfig suspend_config;
    suspend_config.durable_write_intent_sink =
        [&first_intents](std::uint64_t key, std::string_view) { first_intents.push_back(key); };
    suspend_config.contextual_capability_invoker =
        [&commit_invocations](const CapabilityInvocationContext &, const std::string &name,
                              const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "commit") {
            ++commit_invocations;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(1);
        } else {
            r.status = CapabilityCallStatus::Pending;
        }
        return r;
    };
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("ExactlyOnceWorkflow", make_none());

    check(suspended.status() == WorkflowStatus::Suspended, "exactly_once.suspended");
    check(commit_invocations == 1, "exactly_once.commit_called_once_first_run");
    check(first_intents.size() == 1, "exactly_once.one_intent_first_run");

    // Resume: commit must be served from the memo (NOT re-invoked), so no second
    // intent is written and the invocation count stays 1.
    auto resume_program = build_program();
    std::vector<std::uint64_t> resume_intents;
    WorkflowRuntimeConfig resume_config;
    resume_config.recovery_snapshot = std::move(suspended.suspended);
    resume_config.resume_pending_result = make_int(9);
    resume_config.durable_write_intent_sink =
        [&resume_intents](std::uint64_t key, std::string_view) { resume_intents.push_back(key); };
    resume_config.contextual_capability_invoker =
        [&commit_invocations](const CapabilityInvocationContext &, const std::string &name,
                              const std::vector<Value> &) -> CapabilityCallResult {
        if (name == "commit") {
            ++commit_invocations; // must NOT happen on resume
        }
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(0);
        return r;
    };
    WorkflowRuntime resume_runtime(resume_program, std::move(resume_config));
    auto resumed = resume_runtime.run("ExactlyOnceWorkflow", make_none());

    check(resumed.status() == WorkflowStatus::Completed, "exactly_once.resume_completed");
    check(commit_invocations == 1, "exactly_once.commit_not_reinvoked_on_resume");
    check(resume_intents.empty(), "exactly_once.no_second_intent_on_resume");
    // The idempotency key is reproducible: the first run's intent key equals what
    // the resume would have computed for the same coordinate (commit is ordinal 0).
    check(!first_intents.empty() && first_intents[0] != 0, "exactly_once.intent_key_nonzero");
}

// RFC 0026 C2b stage3 (presence-not-value): the write-ahead durable-write intent
// keys off capability-identity PRESENCE (source_capability_symbol_id.has_value()),
// never off cap_symbol_id != 0. A legal id=0 DurableWrite MUST still fire its
// intent (before dispatch, with the invoker's idempotency key); a call whose
// callee_ref carries no id MUST NOT fire an intent even though the effect is
// visible by name.
void test_durable_write_intent_presence_not_value() {
    // A single-node workflow calling one DurableWrite capability `commit`. The
    // capability id and the CallExpr.callee_ref.id are parameterized so we can
    // model (a) a legal id=0 identity and (b) an absent identity that still
    // resolves the effect by name.
    const auto build_program = [](std::optional<std::size_t> decl_id,
                                  std::optional<std::size_t> callee_id) {
        Program program;
        CapabilityDecl commit;
        commit.name = "commit";
        commit.return_type_ref = TypeRef{.kind = TypeRefKind::Int};
        commit.symbol_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "commit",
            .local_name = "commit",
            .id = decl_id,
        };
        commit.effect.declared = true;
        commit.effect.kind = CapabilityEffectKind::DurableWrite;
        program.declarations.push_back(std::move(commit));
        program.declarations.push_back(make_echo_agent("EchoAgent"));
        program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

        WorkflowDecl workflow;
        workflow.name = "IntentPresenceWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        CallExpr commit_call;
        commit_call.callee = "commit";
        // The callee_ref may or may not carry an id; when absent the runtime still
        // resolves the effect by name (find_capability) but source identity is not
        // carried, so the intent must not fire.
        commit_call.callee_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "commit",
            .local_name = "commit",
            .id = callee_id,
        };
        node_input.fields.push_back(StructFieldInit{
            .name = "value",
            .value = make_expr_ptr(std::move(commit_call)),
        });
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    // --- Positive: legal id=0 DurableWrite fires exactly one intent -------------
    {
        auto program = build_program(/*decl_id=*/0, /*callee_id=*/0);
        std::vector<std::uint64_t> intents;
        std::size_t invocations = 0;
        bool intent_before_dispatch = false;
        std::optional<bool> observed_id_present;
        std::optional<std::size_t> observed_id_value;
        std::uint64_t observed_idempotency_key = 0;
        WorkflowRuntimeConfig config;
        config.durable_write_intent_sink =
            [&intents](std::uint64_t key, std::string_view) {
                // The intent must be written BEFORE the invoker runs (invocations
                // still 0 at intent time); the ordering is asserted in the invoker.
                intents.push_back(key);
            };
        config.contextual_capability_invoker =
            [&](const CapabilityInvocationContext &context, const std::string &,
                const std::vector<Value> &) -> CapabilityCallResult {
            ++invocations;
            intent_before_dispatch = intents.size() == 1;
            observed_id_present = context.source_capability_symbol_id.has_value();
            observed_id_value = context.source_capability_symbol_id;
            observed_idempotency_key = context.idempotency_key;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(1);
            return r;
        };
        WorkflowRuntime runtime(program, std::move(config));
        auto result = runtime.run("IntentPresenceWorkflow", make_none());
        check(result.status() == WorkflowStatus::Completed, "intent_id0.completed");
        check(invocations == 1, "intent_id0.invoked_once");
        check(intents.size() == 1, "intent_id0.exactly_one_intent");
        check(intent_before_dispatch, "intent_id0.intent_before_dispatch");
        // The identity is PRESENT and its legal value is 0.
        check(observed_id_present.has_value() && *observed_id_present,
              "intent_id0.source_id_engaged");
        check(observed_id_value.has_value() && *observed_id_value == 0,
              "intent_id0.source_id_is_zero");
        // The intent key equals the idempotency key the invoker saw for the same
        // coordinate (one call, ordinal 0): the write-ahead intent and the dispatch
        // share one reproducible key.
        check(!intents.empty() && intents[0] == observed_idempotency_key,
              "intent_id0.intent_key_matches_context");
        check(!intents.empty() && intents[0] != 0, "intent_id0.intent_key_nonzero");
    }

    // --- Negative: absent callee_ref.id -> effect visible by name, NO intent ----
    {
        // decl carries an id (71) so the DurableWrite decl is well-formed and
        // find_capability(name) sees the effect; the CallExpr.callee_ref.id is
        // absent so source identity is not carried at the call site.
        auto program = build_program(/*decl_id=*/71, /*callee_id=*/std::nullopt);
        std::vector<std::uint64_t> intents;
        std::size_t invocations = 0;
        std::optional<bool> observed_id_present;
        WorkflowRuntimeConfig config;
        config.durable_write_intent_sink =
            [&intents](std::uint64_t key, std::string_view) { intents.push_back(key); };
        config.contextual_capability_invoker =
            [&](const CapabilityInvocationContext &context, const std::string &,
                const std::vector<Value> &) -> CapabilityCallResult {
            ++invocations;
            observed_id_present = context.source_capability_symbol_id.has_value();
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(1);
            return r;
        };
        WorkflowRuntime runtime(program, std::move(config));
        auto result = runtime.run("IntentPresenceWorkflow", make_none());
        check(result.status() == WorkflowStatus::Completed, "intent_absent.completed");
        // The call still dispatched (effect resolved by name) ...
        check(invocations == 1, "intent_absent.invoked_once");
        // ... but the source identity is genuinely absent (no resolver back-filled
        // an id), so NO write-ahead intent fired.
        check(observed_id_present.has_value() && !*observed_id_present,
              "intent_absent.source_id_nullopt");
        check(intents.empty(), "intent_absent.no_intent");
    }
}

// RFC 0022 slice 5 (fail-closed): resuming with a pending result whose shape does
// not match the capability's declared return type aborts with a diagnostic —
// never coerces, never re-invokes.
void test_resume_fails_closed_on_pending_result_type_mismatch() {
    const auto build_program = []() {
        Program program;
        CapabilityDecl answer;
        answer.name = "answer";
        answer.symbol_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "answer",
            .local_name = "answer",
            .id = 81,
        };
        answer.return_type_ref = TypeRef{.kind = TypeRefKind::Int, .display_name = "Int"};
        program.declarations.push_back(std::move(answer));
        program.declarations.push_back(make_echo_agent("EchoAgent"));
        program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

        WorkflowDecl workflow;
        workflow.name = "TypeMismatchWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        CallExpr call;
        call.callee = "answer";
        call.callee_ref = SymbolRef{
            .kind = SymbolRefKind::Capability,
            .canonical_name = "answer",
            .local_name = "answer",
            .id = 81,
        };
        node_input.fields.push_back(StructFieldInit{
            .name = "value",
            .value = make_expr_ptr(std::move(call)),
        });
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    auto program = build_program();
    WorkflowRuntimeConfig suspend_config;
    suspend_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult pending;
        pending.status = CapabilityCallStatus::Pending;
        return pending;
    };
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("TypeMismatchWorkflow", make_none());
    check(suspended.suspended.has_value(), "type_mismatch.captured_record");

    // The capability is declared to return Int; inject a String → fail closed.
    auto resume_program = build_program();
    WorkflowRuntimeConfig resume_config;
    resume_config.recovery_snapshot = std::move(suspended.suspended);
    resume_config.resume_pending_result = make_string("not an int");
    // An invoker must be installed so the replay/inject path (which lives inside
    // the invoker closure) runs; it is not actually called for the pending
    // ordinal (the injected result is validated + returned before dispatch).
    resume_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(0);
        return r;
    };
    WorkflowRuntime resume_runtime(resume_program, std::move(resume_config));
    auto resumed = resume_runtime.run("TypeMismatchWorkflow", make_none());

    check(resumed.status() != WorkflowStatus::Completed, "type_mismatch.not_completed");
    check(diagnostic_message_contains(resumed.diagnostics, "type mismatch"),
          "type_mismatch.fail_closed_diagnostic");
}

// RFC 0022 Test Plan (多次挂起): a node with TWO capability calls can suspend
// twice — once per call — across separate runs, and the memo table is rebuilt
// append-only each time so the earlier result is never re-invoked. This is the
// hard case where resume itself hits a fresh PENDING.
void test_two_suspensions_on_one_node_rebuild_memo_append_only() {
    // Node input struct: {aux: first(), value: second()}. StructLiteral fields
    // evaluate in source order, so first() is ordinal 0 and second() is ordinal 1.
    const auto build_program = []() {
        Program program;
        // Option B: `first` / `second` are declared Int capabilities with distinct
        // stable SymbolIds; each CallExpr binds its matching callee_ref.
        program.declarations.push_back(make_int_capability_decl("first", 81));
        program.declarations.push_back(make_int_capability_decl("second", 82));
        program.declarations.push_back(make_echo_agent("EchoAgent"));
        program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));
        WorkflowDecl workflow;
        workflow.name = "TwoSuspendWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        CallExpr first_call;
        first_call.callee = "first";
        first_call.callee_ref = make_capability_ref("first", 81);
        node_input.fields.push_back(
            StructFieldInit{.name = "aux", .value = make_expr_ptr(std::move(first_call))});
        CallExpr second_call;
        second_call.callee = "second";
        second_call.callee_ref = make_capability_ref("second", 82);
        node_input.fields.push_back(
            StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(second_call))});
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    // An invoker that suspends on `which` and serves the other from a fixed value.
    const auto pending_on = [](const std::string &which) {
        return [which](const CapabilityInvocationContext &, const std::string &name,
                       const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == which) {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(0);
            }
            return r;
        };
    };

    // Run 1: `first` (ordinal 0) suspends; memo is empty.
    auto program = build_program();
    WorkflowRuntimeConfig c1;
    c1.contextual_capability_invoker = pending_on("first");
    WorkflowRuntime r1(program, std::move(c1));
    auto s1 = r1.run("TwoSuspendWorkflow", make_none());
    check(s1.status() == WorkflowStatus::Suspended, "two_suspend.run1_suspended");
    check(s1.suspended.has_value() && s1.suspended->suspended.has_value(),
          "two_suspend.run1_record");
    check(s1.suspended->suspended->memo.empty(), "two_suspend.run1_memo_empty");
    check(s1.suspended->suspended->pending_ordinal == 0, "two_suspend.run1_pending_ord0");

    // Run 2: resume ordinal 0 with first's result; `second` (ordinal 1) now
    // suspends. The memo must be rebuilt append-only to contain exactly first@0.
    auto program2 = build_program();
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(s1.suspended);
    c2.resume_pending_result = make_int(11);
    c2.contextual_capability_invoker = pending_on("second");
    WorkflowRuntime r2(program2, std::move(c2));
    auto s2 = r2.run("TwoSuspendWorkflow", make_none());
    check(s2.status() == WorkflowStatus::Suspended, "two_suspend.run2_suspended_again");
    check(s2.suspended.has_value() && s2.suspended->suspended.has_value(),
          "two_suspend.run2_record");
    check(s2.suspended->suspended->pending_ordinal == 1, "two_suspend.run2_pending_ord1");
    check(s2.suspended->suspended->memo.size() == 1, "two_suspend.run2_memo_one_entry");
    check(!s2.suspended->suspended->memo.empty() &&
              s2.suspended->suspended->memo[0].ordinal == 0,
          "two_suspend.run2_memo_is_ordinal0");

    // Run 3: resume ordinal 1 with second's result; memo replays first@0 (never
    // re-invoked). The echo agent returns input.value, so the output is second's
    // injected result.
    auto program3 = build_program();
    WorkflowRuntimeConfig c3;
    c3.recovery_snapshot = std::move(s2.suspended);
    c3.resume_pending_result = make_int(22);
    std::size_t live_calls = 0;
    c3.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls; // neither call should run live: ord0 from memo, ord1 injected
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r3(program3, std::move(c3));
    auto s3 = r3.run("TwoSuspendWorkflow", make_none());
    check(s3.status() == WorkflowStatus::Completed, "two_suspend.run3_completed");
    check(live_calls == 0, "two_suspend.run3_no_live_calls");
    const auto *out =
        s3.output() != nullptr ? std::get_if<IntValue>(&s3.output()->node) : nullptr;
    check(out != nullptr && out->value == 22, "two_suspend.run3_output_is_second_result");
}

// RFC 0026 C2b stage3: a node whose input is `{aux: first(), value: second()}`
// (source order => first is ordinal 0, second ordinal 1). Both capabilities are
// declared Int with the given stable ids. `first` becomes a completed memo entry
// and `second` the pending call across a two-suspension resume, so a test can
// exercise BOTH the memo-replay and pending identity gates.
[[nodiscard]] Program make_two_capability_node_program(const std::string &workflow_name,
                                                       const std::string &first_cap,
                                                       std::size_t first_id,
                                                       const std::string &second_cap,
                                                       std::size_t second_id,
                                                       bool declare_first = true,
                                                       bool declare_second = true) {
    Program program;
    if (declare_first) {
        program.declarations.push_back(make_int_capability_decl(first_cap, first_id));
    }
    if (declare_second) {
        program.declarations.push_back(make_int_capability_decl(second_cap, second_id));
    }
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = workflow_name;
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr first_call;
    first_call.callee = first_cap;
    first_call.callee_ref = make_capability_ref(first_cap, first_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "aux", .value = make_expr_ptr(std::move(first_call))});
    CallExpr second_call;
    second_call.callee = second_cap;
    second_call.callee_ref = make_capability_ref(second_cap, second_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(second_call))});
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));
    return program;
}

// RFC 0026 C2b stage3 helper: does the bag carry a diagnostic whose CODE (not
// just message) is exactly EXECUTION_ERROR?
[[nodiscard]] bool diagnostic_has_execution_error_code(const DiagnosticBag &diagnostics) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.code.has_value() &&
            *entry.code == std::string(error_codes::backend::ExecutionError.id)) {
            return true;
        }
    }
    return false;
}

// RFC 0026 C2b stage3 (Option B contract): a resume where a resumed capability has
// a PRESENT source SymbolId but NO CapabilityDecl fails closed at the
// identity/declaration layer — never a schema-free passthrough. Covered on BOTH
// branches: (1) the pending call is undeclared; (2) a completed memo entry's
// capability is undeclared (coordinate-complete: ordinal/cap_id/arg_hash all match)
// while the pending call stays declared. Both fail with the exact EXECUTION_ERROR
// code and zero live invocations.
void test_resume_fails_closed_on_undeclared_capability() {
    // --- Branch 1: undeclared PENDING capability ---------------------------------
    {
        // Run 1: both declared, `second` (pending) suspends.
        auto program = make_two_capability_node_program("UndeclPendWorkflow", "first", 81,
                                                        "second", 82);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "second") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(0);
            }
            return r;
        };
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("UndeclPendWorkflow", make_none());
        check(s1.suspended.has_value(), "undecl_pending.captured");

        // Resume program OMITS the `second` (pending) declaration; keeps `first`.
        auto resume_program = make_two_capability_node_program(
            "UndeclPendWorkflow", "first", 81, "second", 82,
            /*declare_first=*/true, /*declare_second=*/false);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(42);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("UndeclPendWorkflow", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "undecl_pending.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "undecl_pending.execution_error_code");
        // Branch-specific: the failure is the pending-call identity gate, not the
        // coordinate gate or another generic error sharing the same code.
        check(diagnostic_message_contains(resumed.diagnostics, "pending-call identity mismatch"),
              "undecl_pending.identity_mismatch_message");
        check(live_calls == 0, "undecl_pending.no_live_invoke");
    }

    // --- Branch 2: undeclared MEMO capability (coordinate-complete) --------------
    {
        // Run 1: both declared, `first` (ordinal 0) completes into the memo, then
        // `second` (ordinal 1) suspends -> the record carries a completed first@0.
        auto program = make_two_capability_node_program("UndeclMemoWorkflow", "first", 81,
                                                        "second", 82);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "second") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(7);
            }
            return r;
        };
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("UndeclMemoWorkflow", make_none());
        check(s1.suspended.has_value() && s1.suspended->suspended.has_value() &&
                  s1.suspended->suspended->memo.size() == 1,
              "undecl_memo.captured_with_memo");

        // Resume program OMITS `first` (the MEMO capability); keeps `second`. The
        // coordinate gate for first@0 passes (ordinal/cap_id/arg_hash match), so the
        // failure must come from the declaration/identity gate on the memo branch.
        auto resume_program = make_two_capability_node_program(
            "UndeclMemoWorkflow", "first", 81, "second", 82,
            /*declare_first=*/false, /*declare_second=*/true);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(42);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("UndeclMemoWorkflow", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "undecl_memo.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "undecl_memo.execution_error_code");
        // Branch-specific: prove the coordinate gate was PASSED (ordinal/cap_id/
        // arg_hash all matched) and the failure is the memo identity/decl gate — not
        // the coordinate divergence gate (which shares the same code).
        check(diagnostic_message_contains(resumed.diagnostics, "capability identity mismatch"),
              "undecl_memo.identity_mismatch_message");
        check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
              "undecl_memo.not_coordinate_divergence");
        check(live_calls == 0, "undecl_memo.no_live_invoke");
    }
}

// RFC 0026 C2b stage3 (P0-21): a resume whose recovered control flow completes
// WITHOUT ever reaching the recorded pending capability fails closed as a replay
// divergence — with the exact EXECUTION_ERROR code — BEFORE any completion side
// effect (node_completed_hook, checkpoint save, NodeCompleted), and with zero live
// invocations.
void test_resume_fails_closed_when_pending_never_reached() {
    // Suspend on `answer` inside the node input.
    auto program = make_single_capability_node_program("P21Workflow", "answer", 41);
    WorkflowRuntimeConfig suspend_config;
    suspend_config.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult pending;
        pending.status = CapabilityCallStatus::Pending;
        return pending;
    };
    WorkflowRuntime suspend_runtime(program, std::move(suspend_config));
    auto suspended = suspend_runtime.run("P21Workflow", make_none());
    check(suspended.suspended.has_value(), "p21.captured_record");

    // Resume with a program whose node input NO LONGER calls `answer` (the recorded
    // pending capability), so the recovered control flow can complete without ever
    // reaching the pending ordinal. Same workflow/node identity; the node input is
    // now a constant so the node completes directly.
    Program diverged;
    diverged.declarations.push_back(make_int_capability_decl("answer", 41));
    diverged.declarations.push_back(make_echo_agent("EchoAgent"));
    diverged.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));
    {
        WorkflowDecl workflow;
        workflow.name = "P21Workflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        node_input.fields.push_back(StructFieldInit{
            .name = "value", .value = make_expr_ptr(IntegerLiteralExpr{"5"})});
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        diverged.declarations.push_back(std::move(workflow));
    }

    std::size_t live_calls = 0;
    std::size_t node_completed_hooks = 0;
    std::size_t checkpoint_callbacks = 0;
    // A real temp recovery store: the P0-21 gate must fire before any checkpoint is
    // persisted, so the store file must never be created/rewritten by this resume.
    const auto store_path = std::filesystem::temp_directory_path() /
                            ("ahfl-p21-" +
                             std::to_string(std::chrono::steady_clock::now()
                                                .time_since_epoch()
                                                .count())) /
                            "snapshot.json";
    WorkflowRecoveryStore store(store_path);
    WorkflowRuntimeConfig resume_config;
    resume_config.recovery_snapshot = std::move(suspended.suspended);
    resume_config.resume_pending_result = make_int(42);
    resume_config.recovery_store = &store;
    resume_config.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    resume_config.node_completed_hook =
        [&node_completed_hooks](AgentId, std::string_view, const Value &) {
            ++node_completed_hooks;
        };
    resume_config.checkpoint_after_node =
        [&checkpoint_callbacks](WorkflowNodeId) -> std::optional<CheckpointId> {
        ++checkpoint_callbacks;
        return CheckpointId{1};
    };
    WorkflowRuntime resume_runtime(diverged, std::move(resume_config));
    auto resumed = resume_runtime.run("P21Workflow", make_none());
    check(resumed.status() != WorkflowStatus::Completed, "p21.not_completed");
    check(diagnostic_has_execution_error_code(resumed.diagnostics), "p21.execution_error_code");
    check(diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
          "p21.replay_diverged_message");
    check(live_calls == 0, "p21.no_live_invoke");
    check(node_completed_hooks == 0, "p21.no_node_completed_hook");
    // The gate fires before checkpoint_after_node is consulted / any snapshot is
    // persisted for this node: the store file must not exist.
    check(checkpoint_callbacks == 0, "p21.no_checkpoint_callback");
    check(!std::filesystem::exists(store_path), "p21.no_snapshot_file_written");
    // And no NodeCompleted lifecycle event was emitted.
    std::size_t node_completed_events = 0;
    for (const auto &event : resumed.events.events()) {
        node_completed_events +=
            std::holds_alternative<NodeCompleted>(event.payload) ? 1U : 0U;
    }
    check(node_completed_events == 0, "p21.no_node_completed_event");
    std::filesystem::remove_all(store_path.parent_path());
}

// RFC 0026 C2b stage3: a present SymbolId of 0 is a LEGAL capability identity
// (presence != value). Two independent sub-scenarios (first/second cannot BOTH be
// id 0 in one program — that is a duplicate-id collision, not a positive):
//   memo-zero:    first id=0 completes into the memo, second id=1 pending -> the
//                 replay MEMO hit for ordinal 0 with cap id 0 passes.
//   pending-zero: first id=1 completes, second id=0 pending -> the PENDING identity
//                 gate with cap id 0 passes.
// Each asserts 0 live invocations, the correct final value, and that the captured
// snapshot's relevant cap_id is really 0 (so the fixture actually exercises id=0).
void test_resume_with_symbol_id_zero() {
    // --- memo-zero ---------------------------------------------------------------
    {
        auto program =
            make_two_capability_node_program("MemoZeroWorkflow", "first", 0, "second", 1);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "second") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(7);
            }
            return r;
        };
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("MemoZeroWorkflow", make_none());
        check(s1.suspended.has_value() && s1.suspended->suspended.has_value() &&
                  s1.suspended->suspended->memo.size() == 1 &&
                  s1.suspended->suspended->memo[0].cap_id == 0,
              "memo_zero.captured_memo_cap_id_0");

        auto resume_program =
            make_two_capability_node_program("MemoZeroWorkflow", "first", 0, "second", 1);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(22);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("MemoZeroWorkflow", make_none());
        check(resumed.status() == WorkflowStatus::Completed, "memo_zero.completed");
        check(live_calls == 0, "memo_zero.no_live_invoke");
        const auto *out =
            resumed.output() != nullptr ? std::get_if<IntValue>(&resumed.output()->node) : nullptr;
        check(out != nullptr && out->value == 22, "memo_zero.output_22");
    }

    // --- pending-zero -----------------------------------------------------------
    {
        auto program =
            make_two_capability_node_program("PendingZeroWorkflow", "first", 1, "second", 0);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "second") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(7);
            }
            return r;
        };
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("PendingZeroWorkflow", make_none());
        check(s1.suspended.has_value() && s1.suspended->suspended.has_value() &&
                  s1.suspended->suspended->pending_cap_id == 0,
              "pending_zero.captured_pending_cap_id_0");

        auto resume_program =
            make_two_capability_node_program("PendingZeroWorkflow", "first", 1, "second", 0);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(22);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("PendingZeroWorkflow", make_none());
        check(resumed.status() == WorkflowStatus::Completed, "pending_zero.completed");
        check(live_calls == 0, "pending_zero.no_live_invoke");
        const auto *out =
            resumed.output() != nullptr ? std::get_if<IntValue>(&resumed.output()->node) : nullptr;
        check(out != nullptr && out->value == 22, "pending_zero.output_22");
    }
}

// RFC 0026 C2b stage3 (P0-13 order proof): a hostile native StructValue whose
// EXTRA (undeclared) field is a raw nullptr. Under a Struct schema T{f:Int} the
// value must be rejected on the ORIGINAL, in place, BEFORE any clone. This shape
// is the regression-killer: clone_value drops the null `extra` field, leaving
// exactly {f:Int(1)} — which validates VALID — so an old clone-first
// implementation would wrongly ACCEPT it. The permanent negative control below
// asserts that discrimination stays live.

// Build a hand-built program with a projectable Struct T{f:Int} and a capability
// `probe` whose result type is T (nominal_ref = T's symbol). The node input is
// `{value: probe()}` and the echo agent returns input.value, so the workflow
// output IS the capability result.
[[nodiscard]] Program make_struct_capability_program(const std::string &workflow_name,
                                                     std::size_t cap_id) {
    constexpr std::size_t kStructId = 501;
    const std::string struct_canonical = "app::T";

    Program program;

    StructDecl t_decl;
    t_decl.name = "T";
    t_decl.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Type,
        .canonical_name = struct_canonical,
        .local_name = "T",
        .id = kStructId,
    };
    FieldDecl f_field;
    f_field.name = "f";
    f_field.type_ref = TypeRef{.kind = TypeRefKind::Int};
    t_decl.fields.push_back(std::move(f_field));
    // P4-C finalized gate: a hand-built StructDecl does NOT auto-derive member
    // templates from its fields; roots.size() must equal fields.size() or the type
    // environment build fails. Provide the concrete Int template for field 0.
    MemberTypeTemplateNode f_template;
    f_template.kind = MemberTypeTemplateKind::Concrete;
    f_template.type_ref = TypeRef{.kind = TypeRefKind::Int};
    t_decl.member_type_templates.push_back(std::move(f_template));
    t_decl.field_type_template_roots.push_back(0);
    program.declarations.push_back(std::move(t_decl));

    CapabilityDecl probe;
    probe.name = "probe";
    probe.return_type_ref = TypeRef{
        .kind = TypeRefKind::Struct,
        .canonical_name = struct_canonical,
        .nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                 .canonical_name = struct_canonical,
                                 .local_name = "T",
                                 .id = kStructId},
    };
    probe.symbol_ref = SymbolRef{
        .kind = SymbolRefKind::Capability,
        .canonical_name = "probe",
        .local_name = "probe",
        .id = cap_id,
    };
    program.declarations.push_back(std::move(probe));

    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = workflow_name;
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr probe_call;
    probe_call.callee = "probe";
    probe_call.callee_ref = make_capability_ref("probe", cap_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(probe_call))});
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));
    return program;
}

// A hostile native StructValue{type_name="app::T", f=Int(1), extra=nullptr}. Built
// directly (not via make_struct, which would not carry a raw null field) so the
// null child survives to the trust gate.
[[nodiscard]] Value make_hostile_struct() {
    StructValue sv;
    sv.type_name = "app::T";
    sv.fields.set("f", std::make_unique<Value>(make_int(1)));
    sv.fields.set("extra", nullptr); // undeclared + raw null child
    return Value{std::move(sv)};
}

void test_resume_fails_closed_on_nested_null_struct() {
    const std::size_t cap_id = 301;
    auto program = make_struct_capability_program("NestedNullWorkflow", cap_id);

    // --- Permanent negative control: prove the shape discriminates old vs new ----
    // Build the SAME binding the runtime cache would, and assert the ORIGINAL is
    // invalid but a clone (which drops the null `extra`) is VALID. If a future
    // clone_value / validator change erased that gap, this goes red immediately.
    {
        auto env = core::build_core_type_environment(program);
        check(env.ok() && env.environment.has_value(), "nested_null.env_built");
        if (env.ok() && env.environment.has_value()) {
            const CapabilityDecl *probe = nullptr;
            for (const auto &decl : program.declarations) {
                if (const auto *cap = std::get_if<CapabilityDecl>(&decl);
                    cap != nullptr && cap->name == "probe") {
                    probe = cap;
                    break;
                }
            }
            check(probe != nullptr, "nested_null.probe_found");
            if (probe != nullptr) {
                auto migration = core::migrate_type_ref_to_wire_binding(
                    probe->return_type_ref, *env.environment);
                check(migration.ok() && migration.binding.has_value(),
                      "nested_null.binding_projected");
                if (migration.ok() && migration.binding.has_value()) {
                    const auto hostile = make_hostile_struct();
                    check(!wire_codec::validate_value(hostile, *migration.binding).valid,
                          "nested_null.original_invalid");
                    const auto cloned = clone_value(hostile);
                    check(wire_codec::validate_value(cloned, *migration.binding).valid,
                          "nested_null.clone_would_pass"); // kills old clone-first impl
                }
            }
        }
    }

    // --- Entry 1: programmatic NativeOnly memo entry (P0-13 in-place validate) ----
    {
        auto suspend_program = make_struct_capability_program("NestedNullWorkflow", cap_id);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult pending;
            pending.status = CapabilityCallStatus::Pending;
            return pending;
        };
        WorkflowRuntime r1(suspend_program, std::move(c1));
        auto s1 = r1.run("NestedNullWorkflow", make_none());
        check(s1.suspended.has_value(), "nested_null.memo.captured");
        // Inject a completed memo entry (ordinal 0) carrying the hostile struct as a
        // programmatic NativeOnly entry, and move the pending ordinal to 1 so the
        // hostile entry is a REPLAY hit (not the pending inject).
        if (s1.suspended.has_value() && s1.suspended->suspended.has_value()) {
            auto &record = *s1.suspended->suspended;
            record.memo.clear();
            record.memo.push_back(CapabilityMemoEntry{
                .ordinal = 0,
                .cap_id = cap_id,
                // Must equal the real coordinate hash for probe()'s empty arg list,
                // or the coordinate gate would fire FIRST and mask the value gate.
                .arg_hash = runtime::hash_values(std::vector<Value>{}).value_or(0),
                .result = make_hostile_struct(),
                .source = PersistedMemoResultSource::NativeOnly,
                .authoritative_json = std::nullopt,
                .result_present = true,
            });
            record.pending_ordinal = 1; // hostile entry becomes a replay hit
        }
        auto resume_program = make_struct_capability_program("NestedNullWorkflow", cap_id);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(0);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("NestedNullWorkflow", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "nested_null.memo.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "nested_null.memo.execution_error_code");
        // The coordinate gate PASSED (arg_hash matches); the failure is the memo
        // value decode/validate gate on the ORIGINAL hostile struct.
        check(diagnostic_message_contains(resumed.diagnostics, "memo Value type mismatch"),
              "nested_null.memo.value_mismatch_message");
        check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
              "nested_null.memo.not_coordinate_divergence");
        check(live_calls == 0, "nested_null.memo.no_live_invoke");
    }

    // --- Entry 2: native pending result (P0-13 in-place validate) ----------------
    {
        auto suspend_program = make_struct_capability_program("NestedNullPendWorkflow", cap_id);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult pending;
            pending.status = CapabilityCallStatus::Pending;
            return pending;
        };
        WorkflowRuntime r1(suspend_program, std::move(c1));
        auto s1 = r1.run("NestedNullPendWorkflow", make_none());
        check(s1.suspended.has_value(), "nested_null.pending.captured");

        auto resume_program = make_struct_capability_program("NestedNullPendWorkflow", cap_id);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_hostile_struct(); // hostile native pending
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("NestedNullPendWorkflow", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "nested_null.pending.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "nested_null.pending.execution_error_code");
        check(diagnostic_message_contains(resumed.diagnostics, "pending-result type mismatch"),
              "nested_null.pending.value_mismatch_message");
        check(live_calls == 0, "nested_null.pending.no_live_invoke");
    }

    // --- Entry 3: ExactSidecar end-to-end (sidecar admission evidence) -----------
    // NOT a P0-13 pre-clone proof: this proves the persisted authoritative_json ->
    // decode_json path rejects an extra/null field and never falls back to the
    // compat native projection. The record is actually SAVED and LOADED through a
    // real WorkflowRecoveryStore so the loader's two-field consistency equation runs
    // and the loaded ExactSidecar state is what the runtime consumes.
    {
        auto suspend_program = make_struct_capability_program("NestedNullSidecarWorkflow", cap_id);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult pending;
            pending.status = CapabilityCallStatus::Pending;
            return pending;
        };
        WorkflowRuntime r1(suspend_program, std::move(c1));
        auto s1 = r1.run("NestedNullSidecarWorkflow", make_none());
        check(s1.suspended.has_value() && s1.suspended->suspended.has_value(),
              "nested_null.sidecar.captured");
        if (s1.suspended.has_value() && s1.suspended->suspended.has_value()) {
            auto &record = *s1.suspended->suspended;
            record.memo.clear();
            // A full ExactSidecar state whose authoritative_json is the hostile wire
            // with an extra null field. `result` is a non-authoritative placeholder
            // (NOT the schema-free projection). On save the legacy `result` field is
            // written as serialize_json(parse(sidecar)), so the loader's consistency
            // equation (legacy_raw == serialize_json(parse(sidecar))) holds and the
            // entry reaches the decode gate on load.
            const std::string wire = R"({"_type":"app::T","f":1,"extra":null})";
            record.memo.push_back(CapabilityMemoEntry{
                .ordinal = 0,
                .cap_id = cap_id,
                .arg_hash = runtime::hash_values(std::vector<Value>{}).value_or(0),
                .result = make_none(), // non-authoritative placeholder
                .source = PersistedMemoResultSource::ExactSidecar,
                .authoritative_json = wire,
                .result_present = true,
            });
            record.pending_ordinal = 1;
        }
        // Persist + reload through a real store so the sidecar authority goes through
        // save -> loader consistency equation -> load before consumption.
        const auto store_path = std::filesystem::temp_directory_path() /
                                ("ahfl-nnsidecar-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count())) /
                                "snapshot.json";
        WorkflowRecoveryStore store(store_path);
        check(store.save(*s1.suspended).has_value(), "nested_null.sidecar.saved");
        auto loaded = store.load();
        check(loaded.has_value(), "nested_null.sidecar.loaded");
        // Prove the entry actually went THROUGH the loader (not the original object):
        // the loaded ExactSidecar state must survive save/load consistency intact.
        if (loaded.has_value() && loaded->suspended.has_value() &&
            loaded->suspended->memo.size() == 1) {
            const auto &e = loaded->suspended->memo[0];
            check(e.source == PersistedMemoResultSource::ExactSidecar,
                  "nested_null.sidecar.loaded_source_exact");
            check(e.authoritative_json.has_value() &&
                      *e.authoritative_json == R"({"_type":"app::T","f":1,"extra":null})",
                  "nested_null.sidecar.loaded_wire_preserved");
            check(e.result_present.has_value() && *e.result_present == true,
                  "nested_null.sidecar.loaded_presence_true");
        } else {
            check(false, "nested_null.sidecar.loaded_entry_present");
        }

        auto resume_program = make_struct_capability_program("NestedNullSidecarWorkflow", cap_id);
        WorkflowRuntimeConfig c2;
        if (loaded.has_value()) {
            c2.recovery_snapshot = std::move(*loaded);
        }
        c2.resume_pending_result = make_int(0);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("NestedNullSidecarWorkflow", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "nested_null.sidecar.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "nested_null.sidecar.execution_error_code");
        check(diagnostic_message_contains(resumed.diagnostics, "memo Value type mismatch"),
              "nested_null.sidecar.value_mismatch_message");
        check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
              "nested_null.sidecar.not_coordinate_divergence");
        check(live_calls == 0, "nested_null.sidecar.no_live_invoke");
        std::filesystem::remove_all(store_path.parent_path());
    }
}

// RFC 0026 C2b stage3 (P0-15): the pending-call identity is verified BEFORE the
// host result is read/validated/cloned. A snapshot whose recorded pending_cap_id
// points at a DIFFERENT capability than the live call at the pending ordinal must
// fail closed on identity — even when the host result's type would also validate.
void test_resume_pending_identity_mismatch() {
    // Run 1: first(81) completes into memo@0, second(82) suspends at ordinal 1.
    auto program =
        make_two_capability_node_program("PendIdentityWorkflow", "first", 81, "second", 82);
    WorkflowRuntimeConfig c1;
    c1.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &name,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "second") {
            r.status = CapabilityCallStatus::Pending;
        } else {
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(0);
        }
        return r;
    };
    WorkflowRuntime r1(program, std::move(c1));
    auto s1 = r1.run("PendIdentityWorkflow", make_none());
    check(s1.suspended.has_value() && s1.suspended->suspended.has_value(),
          "pend_identity.captured");
    // Corrupt the recorded pending_cap_id to point at `first` (81) — a DIFFERENT,
    // still-declared capability whose Int return type would also accept the host
    // result. The live call at the pending ordinal is `second` (82), so the identity
    // gate must reject on the id mismatch (82 != 81) before any result read.
    if (s1.suspended.has_value() && s1.suspended->suspended.has_value()) {
        s1.suspended->suspended->pending_cap_id = 81;
    }

    auto resume_program =
        make_two_capability_node_program("PendIdentityWorkflow", "first", 81, "second", 82);
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(s1.suspended);
    c2.resume_pending_result = make_int(42); // type would pass; identity must not
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    auto resumed = r2.run("PendIdentityWorkflow", make_none());
    check(resumed.status() != WorkflowStatus::Completed, "pend_identity.not_completed");
    check(diagnostic_has_execution_error_code(resumed.diagnostics),
          "pend_identity.execution_error_code");
    check(diagnostic_message_contains(resumed.diagnostics, "pending-call identity mismatch"),
          "pend_identity.identity_mismatch_message");
    check(live_calls == 0, "pend_identity.no_live_invoke");
}

// RFC 0026 C2b stage3: the binding cache is three-state. A capability whose return
// type does NOT project to a wire schema (e.g. an Fn return, non-materializable as
// a value) is a stored SchemaFailure — DISTINCT from a MissingId (no cache entry
// for the id). A memo hit that resolves to a SchemaFailure capability fails closed
// at consume with the exact EXECUTION_ERROR code, and the error is schema-only: it
// must NOT echo the observed memo payload.
void test_resume_cache_schema_failure_is_fail_closed_no_echo() {
    constexpr std::size_t kFnCapId = 201;
    constexpr std::size_t kSecondId = 202;
    // A distinctive secret token in the memo payload that must NOT appear in the
    // diagnostic (P0-8 no payload echo).
    const std::string kSecret = "TOP_SECRET_MEMO_PAYLOAD";

    // Build a program where `fnret` has an Fn return type (does not project) and
    // `second` is a normal Int capability that suspends.
    const auto build_program = [&]() {
        Program program;
        CapabilityDecl fnret;
        fnret.name = "fnret";
        fnret.return_type_ref = TypeRef{.kind = TypeRefKind::Fn}; // non-materializable
        fnret.symbol_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                                     .canonical_name = "fnret",
                                     .local_name = "fnret",
                                     .id = kFnCapId};
        program.declarations.push_back(std::move(fnret));
        program.declarations.push_back(make_int_capability_decl("second", kSecondId));
        program.declarations.push_back(make_echo_agent("EchoAgent"));
        program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));
        WorkflowDecl workflow;
        workflow.name = "CacheFailWorkflow";
        workflow.input_type_ref = make_named_type_ref("Input");
        workflow.output_type_ref = make_named_type_ref("Output");
        WorkflowNode node;
        node.name = "n";
        node.target_ref = make_agent_ref("EchoAgent");
        StructLiteralExpr node_input;
        node_input.type_name = "NodeInput";
        CallExpr fnret_call;
        fnret_call.callee = "fnret";
        fnret_call.callee_ref = make_capability_ref("fnret", kFnCapId);
        node_input.fields.push_back(
            StructFieldInit{.name = "aux", .value = make_expr_ptr(std::move(fnret_call))});
        CallExpr second_call;
        second_call.callee = "second";
        second_call.callee_ref = make_capability_ref("second", kSecondId);
        node_input.fields.push_back(
            StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(second_call))});
        node.input = make_expr_ptr(std::move(node_input));
        workflow.nodes.push_back(std::move(node));
        PathExpr return_path;
        return_path.path.root_kind = PathRootKind::Identifier;
        return_path.path.root_name = "n";
        workflow.return_value = make_expr_ptr(std::move(return_path));
        program.declarations.push_back(std::move(workflow));
        return program;
    };

    // Run 1: fnret(ordinal 0) completes into memo carrying the secret string;
    // second(ordinal 1) suspends.
    auto program = build_program();
    WorkflowRuntimeConfig c1;
    c1.contextual_capability_invoker =
        [&kSecret](const CapabilityInvocationContext &, const std::string &name,
                   const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "second") {
            r.status = CapabilityCallStatus::Pending;
        } else {
            r.status = CapabilityCallStatus::Success;
            r.value = make_string(kSecret);
        }
        return r;
    };
    WorkflowRuntime r1(program, std::move(c1));
    auto s1 = r1.run("CacheFailWorkflow", make_none());
    check(s1.suspended.has_value() && s1.suspended->suspended.has_value() &&
              s1.suspended->suspended->memo.size() == 1,
          "cache_fail.captured_with_memo");

    // Resume: the fnret memo hit at ordinal 0 resolves its identity (decl + id
    // present + matches), then the binding cache lookup returns SchemaFailure ->
    // fail closed. The secret memo payload must not leak into the diagnostic.
    auto resume_program = build_program();
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(s1.suspended);
    c2.resume_pending_result = make_int(0);
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    auto resumed = r2.run("CacheFailWorkflow", make_none());
    check(resumed.status() != WorkflowStatus::Completed, "cache_fail.not_completed");
    check(diagnostic_has_execution_error_code(resumed.diagnostics),
          "cache_fail.execution_error_code");
    check(live_calls == 0, "cache_fail.no_live_invoke");
    // Prove it hit the stored SchemaFailure (not a generic/MissingId branch): the
    // diagnostic carries the fixed schema-failure prefix (which itself embeds the
    // compiler/schema diagnostic code:message from the failed projection).
    check(diagnostic_message_contains(resumed.diagnostics, "wire-schema projection failed"),
          "cache_fail.schema_failure_text");
    // No payload echo: the secret memo string must not appear in ANY diagnostic.
    check(!diagnostic_message_contains(resumed.diagnostics, kSecret),
          "cache_fail.no_payload_echo");
    // It failed at the schema/decode layer, not a coordinate divergence.
    check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
          "cache_fail.not_coordinate_divergence");
}

// RFC 0026 C2b stage3 rich-shape RESUME matrix (two-call memo shell). Node input
// is `{aux: probe0(), value: hold()}`; StructLiteral fields evaluate in source
// order so probe0 is ordinal 0 and hold is ordinal 1. probe0 returns a rich Value
// (-> completed memo entry), hold suspends (-> pending ordinal 1). The echo agent
// returns input.aux, so the resumed workflow OUTPUT is probe0's decoded memo value
// — exercising the MEMO decode branch (ExactSidecar / LegacyV2), not just pending
// injection. `probe0` return type is `probe0_return`; `hold` returns Int.
[[nodiscard]] Program make_two_call_memo_program(const std::string &workflow_name,
                                                 std::size_t probe0_id, TypeRef probe0_return,
                                                 std::size_t hold_id) {
    Program program;
    CapabilityDecl probe0;
    probe0.name = "probe0";
    probe0.return_type_ref = std::move(probe0_return);
    probe0.symbol_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                                  .canonical_name = "probe0",
                                  .local_name = "probe0",
                                  .id = probe0_id};
    program.declarations.push_back(std::move(probe0));
    program.declarations.push_back(make_int_capability_decl("hold", hold_id));
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "aux"));

    WorkflowDecl workflow;
    workflow.name = workflow_name;
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr probe0_call;
    probe0_call.callee = "probe0";
    probe0_call.callee_ref = make_capability_ref("probe0", probe0_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "aux", .value = make_expr_ptr(std::move(probe0_call))});
    CallExpr hold_call;
    hold_call.callee = "hold";
    hold_call.callee_ref = make_capability_ref("hold", hold_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(hold_call))});
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));
    return program;
}

struct MemoShapeOutcome {
    WorkflowResult result;
    std::size_t live_calls{0};
    bool binding_minted{false};
    bool coordinates_ok{false};              // snapshot coordinates as expected
    PersistedMemoResultSource loaded_source{PersistedMemoResultSource::NativeOnly};
    std::optional<bool> loaded_present{};     // loaded memo[0].result_present
    std::optional<std::string> loaded_wire{}; // loaded memo[0].authoritative_json
    std::string live_canonical_wire{};        // value_to_json(live probe0 result)
};

// Live-produce a rich probe0 result into the memo, save+load through a real store,
// then resume. probe0 is served Success(probe0_live) at ordinal 0; hold suspends.
[[nodiscard]] MemoShapeOutcome
memo_shape_resume(const std::string &workflow_name, std::size_t probe0_id, TypeRef probe0_return,
                  std::size_t hold_id, Value probe0_live) {
    MemoShapeOutcome out;
    out.live_canonical_wire = value_to_json(probe0_live);
    {
        auto probe_program = make_two_call_memo_program(workflow_name, probe0_id,
                                                        clone_type_ref(probe0_return), hold_id);
        auto env = core::build_core_type_environment(probe_program);
        if (env.ok() && env.environment.has_value()) {
            auto migration =
                core::migrate_type_ref_to_wire_binding(probe0_return, *env.environment);
            out.binding_minted = migration.ok() && migration.binding.has_value();
        }
    }

    auto suspend_program =
        make_two_call_memo_program(workflow_name, probe0_id, clone_type_ref(probe0_return),
                                   hold_id);
    WorkflowRuntimeConfig c1;
    auto live = std::make_shared<Value>(std::move(probe0_live));
    c1.contextual_capability_invoker =
        [live](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "hold") {
            r.status = CapabilityCallStatus::Pending;
        } else {
            r.status = CapabilityCallStatus::Success;
            r.value = clone_value(*live);
        }
        return r;
    };
    WorkflowRuntime r1(suspend_program, std::move(c1));
    auto s1 = r1.run(workflow_name, make_none());
    if (!s1.suspended.has_value() || !s1.suspended->suspended.has_value() ||
        s1.suspended->suspended->memo.size() != 1) {
        return out;
    }
    const auto &rec = *s1.suspended->suspended;
    out.coordinates_ok = rec.memo[0].ordinal == 0 && rec.memo[0].cap_id == probe0_id &&
                         rec.pending_ordinal == 1 && rec.pending_cap_id == hold_id;

    const auto store_path = std::filesystem::temp_directory_path() /
                            ("ahfl-memoshape-" +
                             std::to_string(std::chrono::steady_clock::now()
                                                .time_since_epoch()
                                                .count())) /
                            "snapshot.json";
    WorkflowRecoveryStore store(store_path);
    if (!store.save(*s1.suspended).has_value()) {
        std::filesystem::remove_all(store_path.parent_path());
        return out;
    }
    auto loaded = store.load();
    if (!loaded.has_value() || !loaded->suspended.has_value() ||
        loaded->suspended->memo.size() != 1) {
        std::filesystem::remove_all(store_path.parent_path());
        return out;
    }
    out.loaded_source = loaded->suspended->memo[0].source;
    out.loaded_present = loaded->suspended->memo[0].result_present;
    out.loaded_wire = loaded->suspended->memo[0].authoritative_json;

    auto resume_program =
        make_two_call_memo_program(workflow_name, probe0_id, clone_type_ref(probe0_return),
                                   hold_id);
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(*loaded);
    c2.resume_pending_result = make_int(0); // hold's injected result
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    out.result = r2.run(workflow_name, make_none());
    out.live_calls = live_calls;
    std::filesystem::remove_all(store_path.parent_path());
    return out;
}

// The ExactSidecar rich-shape matrix: each shape's live probe0 result is memoized,
// saved (as ExactSidecar), reloaded, and decoded on resume to the correct native
// variant — proving the binding decode path (not the schema-free compat
// projection) governs each historically-lossy shape.
void test_resume_rich_shape_matrix() {
    // Shared ExactSidecar checks: minted binding, coordinates, loaded source ==
    // ExactSidecar, loaded presence == true, loaded wire == the live canonical
    // spelling, resume Completed, 0 live invoke, not a coordinate divergence. The
    // per-shape payload assertions (below) prove the value is the correct canonical
    // native variant, not a compat projection / wrong value.
    auto exact_checks = [](const MemoShapeOutcome &o, const std::string &tag) -> const Value * {
        check(o.binding_minted, tag + ".binding_minted");
        check(o.coordinates_ok, tag + ".coordinates_ok");
        check(o.loaded_source == PersistedMemoResultSource::ExactSidecar,
              tag + ".loaded_exact_sidecar");
        check(o.loaded_present.has_value() && *o.loaded_present == true,
              tag + ".loaded_presence_true");
        check(o.loaded_wire.has_value() && *o.loaded_wire == o.live_canonical_wire,
              tag + ".loaded_wire_matches_live");
        check(o.result.status() == WorkflowStatus::Completed, tag + ".completed");
        check(o.live_calls == 0, tag + ".no_live_invoke");
        check(!diagnostic_message_contains(o.result.diagnostics, "replay diverged"),
              tag + ".not_coordinate_divergence");
        return o.result.output();
    };

    // Decimal: spelling preserved verbatim (schema-free would have been a String).
    {
        auto o = memo_shape_resume("DecMemoWf", 601, decimal_type(2), 651, make_decimal("1.23"));
        const auto *out = exact_checks(o, "shape.decimal");
        check(out != nullptr && std::holds_alternative<DecimalValue>(out->node),
              "shape.decimal.is_decimal");
        check(out != nullptr && std::holds_alternative<DecimalValue>(out->node) &&
                  std::get<DecimalValue>(out->node).spelling == "1.23",
              "shape.decimal.spelling_1_23");
    }
    // Duration: spelling preserved (schema-free would have been a String).
    {
        auto o = memo_shape_resume("DurMemoWf", 602, duration_type(), 652, make_duration("5s"));
        const auto *out = exact_checks(o, "shape.duration");
        check(out != nullptr && std::holds_alternative<DurationValue>(out->node),
              "shape.duration.is_duration");
        check(out != nullptr && std::holds_alternative<DurationValue>(out->node) &&
                  std::get<DurationValue>(out->node).spelling == "5s",
              "shape.duration.spelling_5s");
    }
    // Set<Int>: a SetValue with exactly {1,2} (schema-free would have degraded to List).
    {
        std::vector<Value> items;
        items.push_back(make_int(1));
        items.push_back(make_int(2));
        auto o = memo_shape_resume("SetMemoWf", 603, set_of(int_type()), 653,
                                   make_set(std::move(items)));
        const auto *out = exact_checks(o, "shape.set");
        check(out != nullptr && std::holds_alternative<SetValue>(out->node), "shape.set.is_set");
        if (out != nullptr && std::holds_alternative<SetValue>(out->node)) {
            const auto &sv = std::get<SetValue>(out->node);
            check(sv.items.size() == 2, "shape.set.size_2");
            check(value_to_json(*out) == "[1,2]", "shape.set.elements_1_2");
        }
    }
    // Map<String,Int> incl a reserved-marker key (schema-free would have degraded
    // to a Struct / misinterpreted the "_timestamp" key).
    {
        std::vector<std::pair<Value, Value>> entries;
        entries.emplace_back(make_string("_timestamp"), make_int(7));
        entries.emplace_back(make_string("plain"), make_int(9));
        auto o = memo_shape_resume("MapMemoWf", 604,
                                   map_of(TypeRef{.kind = TypeRefKind::String}, int_type()), 654,
                                   make_map(std::move(entries)));
        const auto *out = exact_checks(o, "shape.map");
        check(out != nullptr && std::holds_alternative<MapValue>(out->node), "shape.map.is_map");
        if (out != nullptr && std::holds_alternative<MapValue>(out->node)) {
            const auto &mv = std::get<MapValue>(out->node);
            check(mv.entries.size() == 2, "shape.map.size_2");
            // Both keys survive as ordinary String keys, incl the reserved marker.
            check(value_to_json(*out) == R"({"_timestamp":7,"plain":9})",
                  "shape.map.keys_and_values");
        }
    }
    // Option<Int> None (schema-free would have degraded to a bare NoneValue).
    {
        auto o = memo_shape_resume("OptNoneMemoWf", 605, option_of(int_type()), 655,
                                   make_option_none());
        const auto *out = exact_checks(o, "shape.option_none");
        check(out != nullptr && is_optional_none(*out), "shape.option_none.is_option_none");
    }
    // Option<Int> Some(3): the wrapper survives AND the inner is Int(3) (schema-free
    // would have degraded to a bare inner value, losing the Option wrapper).
    {
        auto o = memo_shape_resume("OptSomeMemoWf", 606, option_of(int_type()), 656,
                                   make_option_some(make_int(3)));
        const auto *out = exact_checks(o, "shape.option_some");
        check(out != nullptr && is_some(*out), "shape.option_some.is_some");
        const Value *inner = out != nullptr ? optional_inner(*out) : nullptr;
        check(inner != nullptr && std::holds_alternative<IntValue>(inner->node) &&
                  std::get<IntValue>(inner->node).value == 3,
              "shape.option_some.inner_int_3");
    }
    // Explicit Unit: presence=true -> UnitValue (schema-free would have degraded to
    // NoneValue via the null spelling).
    {
        auto o = memo_shape_resume("UnitMemoWf", 607, unit_type(), 657, make_unit());
        const auto *out = exact_checks(o, "shape.unit");
        check(out != nullptr && std::holds_alternative<UnitValue>(out->node),
              "shape.unit.is_unit");
    }
}

// RFC 0026 C2b stage3: LegacyV2 (pre-sidecar) memo decode + P0-17/18/19 presence.
// A LegacyV2 memo entry has ONLY `result` (no sidecar/presence fields) and is
// decoded via the recovery-internal legacy decoder on resume. Cases: top-level
// integral Float bytes `1` -> FloatValue; compact Option<Float> Some with disk
// child `1` -> Option Some(FloatValue) (child recursively widened); Unit/null ->
// default presence=false. Plus the live-valueless P0-17 evidence.
//
// Writes a v2 snapshot whose single memo entry (ordinal 0, cap_id=probe0_id) has
// bare-`result` bytes `legacy_result`, and a declared probe0 returning
// probe0_return + a hold(Int) pending at ordinal 1. Returns the resumed result +
// live-invoke count + the loaded memo[0] source/presence.
[[nodiscard]] MemoShapeOutcome
legacy_memo_resume(const std::string &workflow_name, std::size_t probe0_id, TypeRef probe0_return,
                   std::size_t hold_id, const std::string &legacy_result) {
    MemoShapeOutcome out;
    {
        auto probe_program = make_two_call_memo_program(workflow_name, probe0_id,
                                                        clone_type_ref(probe0_return), hold_id);
        auto env = core::build_core_type_environment(probe_program);
        if (env.ok() && env.environment.has_value()) {
            auto migration =
                core::migrate_type_ref_to_wire_binding(probe0_return, *env.environment);
            out.binding_minted = migration.ok() && migration.binding.has_value();
        }
    }
    // Hand-write a v2 snapshot: memo[0] has ONLY `result` (LegacyV2), pending@1.
    const auto store_path = std::filesystem::temp_directory_path() /
                            ("ahfl-legacymemo-" +
                             std::to_string(std::chrono::steady_clock::now()
                                                .time_since_epoch()
                                                .count())) /
                            "snapshot.json";
    std::filesystem::create_directories(store_path.parent_path());
    {
        std::ofstream output(store_path, std::ios::binary | std::ios::trunc);
        output << R"({"schema":"ahfl.workflow-recovery.v2","workflow_id":0,"checkpoint_id":0,)"
                  R"("completed_nodes":[],"suspended":{"node_id":0,"agent_id":0,)"
                  R"("pending_cap_id":)"
               << hold_id << R"(,"pending_ordinal":1,"node_input":null,"memo":[{"ordinal":0,)"
                  R"("cap_id":)"
               << probe0_id << R"(,"arg_hash":")"
               << runtime::hash_values(std::vector<Value>{}).value_or(0) << R"(","result":)" << legacy_result
               << "}]}}";
    }
    WorkflowRecoveryStore store(store_path);
    auto loaded = store.load();
    if (!loaded.has_value() || !loaded->suspended.has_value() ||
        loaded->suspended->memo.size() != 1) {
        std::filesystem::remove_all(store_path.parent_path());
        return out;
    }
    out.coordinates_ok = loaded->suspended->memo[0].ordinal == 0 &&
                         loaded->suspended->memo[0].cap_id == probe0_id &&
                         loaded->suspended->pending_ordinal == 1 &&
                         loaded->suspended->pending_cap_id == hold_id;
    out.loaded_source = loaded->suspended->memo[0].source;
    out.loaded_present = loaded->suspended->memo[0].result_present;
    out.loaded_wire = loaded->suspended->memo[0].authoritative_json;

    auto resume_program =
        make_two_call_memo_program(workflow_name, probe0_id, clone_type_ref(probe0_return),
                                   hold_id);
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(*loaded);
    c2.resume_pending_result = make_int(0);
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    out.result = r2.run(workflow_name, make_none());
    out.live_calls = live_calls;
    std::filesystem::remove_all(store_path.parent_path());
    return out;
}

void test_resume_legacy_memo_matrix() {
    // Common LegacyV2 evidence shared by every case: minted binding, the loaded
    // snapshot's memo/pending coordinates (this helper hand-writes the v2 snapshot;
    // there is no live run1 here — the coordinates are those the loader reconstructs),
    // LegacyV2 source, presence irreversibly UNKNOWN (nullopt), the authoritative_json
    // is the exact on-disk `result` substring, resume Completed with zero live invoke,
    // and no coordinate-divergence. `expected_wire` is the exact legacy bytes the
    // loader must have captured ("1", "null", ...).
    auto legacy_common = [](const MemoShapeOutcome &o, const std::string &tag,
                            const std::string &expected_wire) {
        check(o.binding_minted, tag + ".binding_minted");
        check(o.coordinates_ok, tag + ".coordinates_ok");
        check(o.loaded_source == PersistedMemoResultSource::LegacyV2, tag + ".loaded_legacy_v2");
        check(!o.loaded_present.has_value(), tag + ".presence_nullopt");
        check(o.loaded_wire.has_value() && *o.loaded_wire == expected_wire,
              tag + ".wire_" + expected_wire);
        check(o.result.status() == WorkflowStatus::Completed, tag + ".completed");
        check(o.live_calls == 0, tag + ".no_live_invoke");
        check(!diagnostic_message_contains(o.result.diagnostics, "replay diverged"),
              tag + ".not_coordinate_divergence");
    };

    // Top-level integral Float: disk bytes `1` (a SignedInteger) -> the legacy
    // decoder widens it to FloatValue under a Float binding.
    {
        auto o = legacy_memo_resume("LegacyFloatWf", 701, float_type(), 751, "1");
        legacy_common(o, "legacy.float", "1");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<FloatValue>(out->node),
              "legacy.float.is_float");
        check(out != nullptr && std::holds_alternative<FloatValue>(out->node) &&
                  std::get<FloatValue>(out->node).value == 1.0,
              "legacy.float.value_1_0");
    }
    // P0-16 recursion HARD point: compact Option<Float> Some whose disk child is the
    // bare int `1` -> the legacy decoder widens the OPTION CHILD to FloatValue.
    {
        auto o = legacy_memo_resume("LegacyOptFloatWf", 702, option_of(float_type()), 752, "1");
        legacy_common(o, "legacy.opt_float", "1");
        const auto *out = o.result.output();
        check(out != nullptr && is_some(*out), "legacy.opt_float.is_some");
        const Value *inner = out != nullptr ? optional_inner(*out) : nullptr;
        check(inner != nullptr && std::holds_alternative<FloatValue>(inner->node) &&
                  std::get<FloatValue>(inner->node).value == 1.0,
              "legacy.opt_float.inner_float_1_0");
    }
    // Option<Int> None from LegacyV2 (`null`) -> Option None (NOT the Unit-presence
    // adapter): the wrapper is reconstructed by the binding decode. Explicitly assert
    // it is an Option-None ENUM, not a bare Unit/None value.
    {
        auto o = legacy_memo_resume("LegacyOptNoneWf", 703, option_of(int_type()), 753, "null");
        legacy_common(o, "legacy.opt_none", "null");
        const auto *out = o.result.output();
        check(out != nullptr && is_optional_none(*out), "legacy.opt_none.is_option_none");
        // Hard contrast: the Option None wrapper is a distinct enum, never a bare
        // NoneValue / UnitValue that merely spells JSON null.
        check(out != nullptr && !std::holds_alternative<NoneValue>(out->node) &&
                  !std::holds_alternative<UnitValue>(out->node),
              "legacy.opt_none.not_bare_none_or_unit");
    }
    // Unit/null from LegacyV2: default presence=false -> evaluator sees None.
    {
        auto o = legacy_memo_resume("LegacyUnitWf", 704, unit_type(), 754, "null");
        legacy_common(o, "legacy.unit", "null");
        const auto *out = o.result.output();
        // presence=false -> the memo hit returns nullopt -> evaluator presents None,
        // and the echo agent returns that None as the workflow output.
        check(out != nullptr && std::holds_alternative<NoneValue>(out->node),
              "legacy.unit.presents_none");
    }
}

// RFC 0026 C2b stage3 (P0-17/18/19): the live-valueless vs explicit-Unit contrast,
// via a REAL live run (not a hand-built memo), proving the presence bit is derived
// from the live call AND persisted append-only. A Unit-returning probe0:
//   valueless: live Success with NO value -> run1 CapabilityCompleted(probe0).output
//     is nullopt (live event stream UNCHANGED); snapshot + loaded ExactSidecar
//     wire="null" presence=false; on resume the evaluator presents NoneValue.
//   explicit:  live Success with make_unit() -> run1 event output present and its
//     RuntimeValue is a UnitValue; snapshot + loaded ExactSidecar wire="null"
//     presence=true; on resume UnitValue.
void test_resume_live_valueless_vs_explicit_unit() {
    constexpr std::size_t kProbe0Id = 801;
    constexpr std::size_t kHoldId = 851;

    // Run the two-call shell where probe0 returns Unit and hold suspends, with a
    // configurable probe0 result (valueless vs explicit Unit).
    auto run1_unit = [&](const std::string &wf, bool explicit_unit) {
        auto program = make_two_call_memo_program(wf, kProbe0Id, unit_type(), kHoldId);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [explicit_unit](const CapabilityInvocationContext &, const std::string &name,
                            const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "hold") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                if (explicit_unit) {
                    r.value = make_unit();
                } // else: valueless success (no value)
            }
            return r;
        };
        WorkflowRuntime rt(program, std::move(c1));
        return rt.run(wf, make_none());
    };

    // Identity-matched probe0 completion scan: return the single CapabilityCompleted
    // event whose invocation resolves (via metadata) to the capability with
    // source_symbol_id == kProbe0Id, plus the TOTAL count of CapabilityCompleted
    // events. Matching by identity (not "first completed") makes the assertion
    // robust to event-order changes; the count lets the caller assert uniqueness.
    struct Probe0Completion {
        const CapabilityCompleted *event{nullptr};
        std::size_t total_completed{0};
    };
    auto find_probe0_completed = [&](const WorkflowResult &r) -> Probe0Completion {
        Probe0Completion found;
        for (const auto &event : r.events.events()) {
            const auto *c = std::get_if<CapabilityCompleted>(&event.payload);
            if (c == nullptr) {
                continue;
            }
            ++found.total_completed;
            const auto *inv = r.metadata.invocation(c->invocation);
            if (inv == nullptr) {
                continue;
            }
            const auto *cap = r.metadata.capability(inv->capability);
            if (cap != nullptr && cap->source_symbol_id.has_value() &&
                *cap->source_symbol_id == kProbe0Id) {
                found.event = c;
            }
        }
        return found;
    };

    // One case end-to-end: run1 (live probe0) -> assert live event + coords +
    // snapshot presence -> real save/load -> assert loaded ExactSidecar state ->
    // resume -> assert final presentation. Guarded throughout (no null-optional
    // deref); cleans up the temp store on every exit.
    auto run_case = [&](const std::string &wf, bool explicit_unit) {
        const std::string tag = explicit_unit ? "explicit_unit" : "valueless_unit";
        auto s1 = run1_unit(wf, explicit_unit);
        if (!s1.suspended.has_value() || !s1.suspended->suspended.has_value() ||
            s1.suspended->suspended->memo.size() != 1) {
            check(false, tag + ".captured");
            return;
        }
        check(true, tag + ".captured");
        const auto &rec = *s1.suspended->suspended;

        // (2) run1 coordinates: memo[0] and pending both as expected.
        check(rec.memo[0].ordinal == 0 && rec.memo[0].cap_id == kProbe0Id,
              tag + ".run1_memo_coords");
        check(rec.pending_ordinal == 1 && rec.pending_cap_id == kHoldId,
              tag + ".run1_pending_coords");

        // (4) live event: exactly one CapabilityCompleted (hold is Pending, no
        // completion), and it is probe0's by identity.
        const auto completion = find_probe0_completed(s1);
        check(completion.total_completed == 1, tag + ".single_completed_event");
        if (completion.event == nullptr) {
            check(false, tag + ".probe0_completed_found");
            return;
        }
        check(true, tag + ".probe0_completed_found");
        if (explicit_unit) {
            // Output id present AND the RuntimeValue is a UnitValue.
            check(completion.event->output.has_value(), tag + ".live_event_output_present");
            const Value *live_out = completion.event->output.has_value()
                                        ? s1.value(*completion.event->output)
                                        : nullptr;
            check(live_out != nullptr && std::holds_alternative<UnitValue>(live_out->node),
                  tag + ".live_event_output_is_unit");
        } else {
            // Valueless: the live completion carried NO output id (event unchanged).
            check(!completion.event->output.has_value(), tag + ".live_event_output_nullopt");
        }

        // (3a) run1 snapshot memo presence (NativeOnly, derived from the live call).
        check(rec.memo[0].result_present.has_value() &&
                  *rec.memo[0].result_present == explicit_unit,
              tag + ".run1_memo_presence");

        // Real save/load.
        const auto store_path = std::filesystem::temp_directory_path() /
                                ("ahfl-" + tag + "-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count())) /
                                "snapshot.json";
        WorkflowRecoveryStore store(store_path);
        if (!store.save(*s1.suspended).has_value()) {
            check(false, tag + ".saved");
            std::filesystem::remove_all(store_path.parent_path());
            return;
        }
        check(true, tag + ".saved");
        auto loaded = store.load();
        if (!loaded.has_value() || !loaded->suspended.has_value() ||
            loaded->suspended->memo.size() != 1) {
            check(false, tag + ".loaded");
            std::filesystem::remove_all(store_path.parent_path());
            return;
        }
        check(true, tag + ".loaded");

        // (3b) loaded memo[0]: the NativeOnly presence bit was persisted append-only
        // as an ExactSidecar with wire=="null" and the correct presence bit. This is
        // the append-only persisted-presence proof (not merely the run1 in-memory
        // NativeOnly state).
        const auto &le = loaded->suspended->memo[0];
        check(le.source == PersistedMemoResultSource::ExactSidecar,
              tag + ".loaded_exact_sidecar");
        check(le.authoritative_json.has_value() && *le.authoritative_json == "null",
              tag + ".loaded_wire_null");
        check(le.result_present.has_value() && *le.result_present == explicit_unit,
              tag + ".loaded_presence");

        // Resume: no live invoke; the final presentation matches the presence bit.
        auto resume_program =
            make_two_call_memo_program(wf, kProbe0Id, unit_type(), kHoldId);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(*loaded);
        c2.resume_pending_result = make_int(0);
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run(wf, make_none());
        check(resumed.status() == WorkflowStatus::Completed, tag + ".resume_completed");
        check(live_calls == 0, tag + ".no_live_invoke");
        const auto *out = resumed.output();
        if (explicit_unit) {
            check(out != nullptr && std::holds_alternative<UnitValue>(out->node),
                  tag + ".resume_unit_value");
        } else {
            check(out != nullptr && std::holds_alternative<NoneValue>(out->node),
                  tag + ".resume_presents_none");
        }
        std::filesystem::remove_all(store_path.parent_path());
    };

    run_case("ValuelessUnitWf", /*explicit_unit=*/false);
    run_case("ExplicitUnitWf", /*explicit_unit=*/true);
}

// A memo entry's result payload/secret must NEVER leak into any diagnostic. This
// asserts a fragment is absent across the WHOLE bag (every entry), not just the
// first — a consume failure that echoed the payload in a later diagnostic would
// still be a leak.
[[nodiscard]] bool no_diagnostic_echoes(const DiagnosticBag &diagnostics,
                                        std::string_view fragment) {
    return !diagnostic_message_contains(diagnostics, fragment);
}

// RFC 0026 C2b stage3 (P0-18) CONSUME gate: the three-state well-formedness is a
// REAL fail-closed gate at consume, not merely a save/load construction convenience.
// A hand-built recovery snapshot whose memo[0] is structurally ILLEGAL for its
// source must fail closed at resume — AFTER the coordinate + identity + binding gates
// have all passed (so the failure is attributable to the trust-state gate, not an
// earlier one). Each case asserts EXECUTION_ERROR, zero live invoke, not a
// replay-divergence, and no authoritative_json / payload echo.
void test_resume_consume_rejects_illformed_trust_state() {
    constexpr std::size_t kProbe0Id = 901;
    constexpr std::size_t kHoldId = 951;
    // A distinctive token placed in the entry's compat native result / sidecar so we
    // can prove it never appears in a diagnostic.
    const std::string kSecret = "TOP_SECRET_TRISTATE_PAYLOAD";

    // Drive a probe0(Int)+hold two-call program to a real suspension, overwrite
    // memo[0] with a caller-built entry (coordinate-correct so the coordinate gate
    // passes), then resume. `mutate` installs the illegal entry. `live_calls` is set
    // to the number of resume-time invoker calls (must be 0 on a trust failure).
    auto consume_illegal =
        [&](const std::string &wf, const std::function<void(CapabilityMemoEntry &)> &mutate,
            std::size_t &live_calls) -> WorkflowResult {
        auto suspend_program = make_two_call_memo_program(wf, kProbe0Id, int_type(), kHoldId);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "hold") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(1);
            }
            return r;
        };
        WorkflowRuntime r1(suspend_program, std::move(c1));
        auto s1 = r1.run(wf, make_none());
        if (!s1.suspended.has_value() || !s1.suspended->suspended.has_value() ||
            s1.suspended->suspended->memo.size() != 1) {
            return {};
        }
        auto &entry = s1.suspended->suspended->memo[0];
        // Keep the real coordinate (ordinal 0, cap_id kProbe0Id, arg_hash of empty
        // args) so the coordinate + identity gates pass and the trust-state gate is
        // what fires.
        entry.arg_hash = runtime::hash_values(std::vector<Value>{}).value_or(0);
        mutate(entry);

        auto resume_program = make_two_call_memo_program(wf, kProbe0Id, int_type(), kHoldId);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        c2.resume_pending_result = make_int(0);
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        return r2.run(wf, make_none());
    };

    struct IllegalCase {
        std::string tag;
        std::string wf;
        std::function<void(CapabilityMemoEntry &)> mutate;
    };
    std::vector<IllegalCase> cases;
    // NativeOnly illegal: authoritative_json present (must be absent) — carries the
    // secret in the sidecar-shaped field so a leak would surface it.
    cases.push_back(IllegalCase{
        "consume.native_illegal", "TriNativeWf", [&](CapabilityMemoEntry &e) {
            e.source = PersistedMemoResultSource::NativeOnly;
            e.result = make_string(kSecret);
            e.authoritative_json = std::string("\"") + kSecret + "\"";
            e.result_present = true;
        }});
    // LegacyV2 illegal: presence engaged (must be absent) — secret in authoritative.
    cases.push_back(IllegalCase{
        "consume.legacy_illegal", "TriLegacyWf", [&](CapabilityMemoEntry &e) {
            e.source = PersistedMemoResultSource::LegacyV2;
            e.result = make_string(kSecret);
            e.authoritative_json = std::string("\"") + kSecret + "\"";
            e.result_present = true; // ILLEGAL for LegacyV2
        }});
    // ExactSidecar illegal: presence absent (must be set) — secret in authoritative.
    cases.push_back(IllegalCase{
        "consume.exact_illegal", "TriExactWf", [&](CapabilityMemoEntry &e) {
            e.source = PersistedMemoResultSource::ExactSidecar;
            e.result = make_string(kSecret);
            e.authoritative_json = std::string("\"") + kSecret + "\"";
            e.result_present = std::nullopt; // ILLEGAL for ExactSidecar
        }});

    for (const auto &c : cases) {
        std::size_t live_calls = 0;
        auto resumed = consume_illegal(c.wf, c.mutate, live_calls);
        check(resumed.status() != WorkflowStatus::Completed, c.tag + ".not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              c.tag + ".execution_error_code");
        // The failure is the trust-state gate, surfaced as the memo Value type
        // mismatch fail-closed message (identity + coordinate already passed).
        check(diagnostic_message_contains(resumed.diagnostics, "memo Value type mismatch"),
              c.tag + ".value_mismatch_message");
        check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
              c.tag + ".not_coordinate_divergence");
        // A trust failure must never fall through to a live call: assert the resume
        // invoker was called ZERO times (a silent live invoke with no "host supplied"
        // text would otherwise pass a diagnostics-only check).
        check(live_calls == 0, c.tag + ".no_live_invoke");
        // No payload / authoritative_json echo anywhere in the bag.
        check(no_diagnostic_echoes(resumed.diagnostics, kSecret), c.tag + ".no_payload_echo");
    }
}

// RFC 0026 C2b stage3 (P0-19 real chain / 7th "structurally-legal but presence/
// binding-illegal" negative): a NativeOnly present bare NoneValue (the established
// legacy-valueless compat case) is normalized ON SAVE to an ExactSidecar wire="null"
// presence=false. When that loaded ExactSidecar is consumed under a NON-Unit binding
// (Option<Int> here) it FIRST exact-decodes to Option None, then fails because
// presence=false's valueless seam is only legal under a Unit root — proving an
// Option null must NOT ride the Unit presence seam. This single chain proves (a) save
// does not fossilize true+None into a present value, (b) presence=false is not
// silently upgraded to a Unit under a non-Unit root, (c) load->consume authority is
// continuous, and (d) raw-wins: a secret injected into the non-authoritative compat
// `result` never leaks and never governs the decision. The rich-shape matrix's
// Option None present=true is the one-positive counterpart. Distinct from the
// three-state STRUCTURAL negatives: this entry is structurally well-formed.
void test_resume_p0_19_valueless_none_rejected_under_non_unit_binding() {
    constexpr std::size_t kProbe0Id = 902;
    constexpr std::size_t kHoldId = 952;
    const std::string wf = "P019ChainWf";
    const std::string kSecret = "TOP_SECRET_P019_COMPAT_PAYLOAD";

    // Suspend a probe0(Option<Int>)+hold program. probe0 returns a canonical Option
    // value live (Some(7)) so the FIRST run's memo is a legitimate Option — there is
    // no unrelated live Int-vs-Option mismatch before the tested chain. We then
    // overwrite memo[0] with the deliberate NativeOnly true+None compat case.
    auto suspend_program =
        make_two_call_memo_program(wf, kProbe0Id, option_of(int_type()), kHoldId);
    WorkflowRuntimeConfig c1;
    c1.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &name,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult r;
        if (name == "hold") {
            r.status = CapabilityCallStatus::Pending;
        } else {
            r.status = CapabilityCallStatus::Success;
            r.value = make_option_some(make_int(7)); // canonical Option, clean setup
        }
        return r;
    };
    WorkflowRuntime r1(suspend_program, std::move(c1));
    auto s1 = r1.run(wf, make_none());
    check(s1.suspended.has_value() && s1.suspended->suspended.has_value() &&
              s1.suspended->suspended->memo.size() == 1,
          "p019.captured");
    if (!s1.suspended.has_value() || !s1.suspended->suspended.has_value() ||
        s1.suspended->suspended->memo.size() != 1) {
        return;
    }
    {
        auto &e = s1.suspended->suspended->memo[0];
        e.arg_hash = runtime::hash_values(std::vector<Value>{}).value_or(0);
        e.source = PersistedMemoResultSource::NativeOnly;
        e.result = make_none(); // bare NoneValue (the deliberate true+None compat case)
        e.authoritative_json = std::nullopt;
        e.result_present = true; // present true+None -> save normalizes to false
    }

    // Real save -> load: the save-local normalization must produce an ExactSidecar
    // wire="null" presence=false (proving save does NOT fossilize true+None).
    const auto store_path = std::filesystem::temp_directory_path() /
                            ("ahfl-p019chain-" +
                             std::to_string(std::chrono::steady_clock::now()
                                                .time_since_epoch()
                                                .count())) /
                            "snapshot.json";
    WorkflowRecoveryStore store(store_path);
    check(store.save(*s1.suspended).has_value(), "p019.saved");
    auto loaded = store.load();
    if (!loaded.has_value() || !loaded->suspended.has_value() ||
        loaded->suspended->memo.size() != 1) {
        check(false, "p019.loaded");
        std::filesystem::remove_all(store_path.parent_path());
        return;
    }
    check(true, "p019.loaded");
    {
        const auto &e = loaded->suspended->memo[0];
        check(e.source == PersistedMemoResultSource::ExactSidecar, "p019.loaded_exact_sidecar");
        check(e.authoritative_json.has_value() && *e.authoritative_json == "null",
              "p019.loaded_wire_null");
        check(e.result_present.has_value() && *e.result_present == false,
              "p019.loaded_presence_false");
    }
    // raw-wins: poison the NON-authoritative compat `result` with a secret String
    // AFTER load, keeping authoritative_json = "null". The consume decision must be
    // governed by the sidecar authority (reject), and the secret must never leak.
    loaded->suspended->memo[0].result = make_string(kSecret);

    // Consume under the Option<Int> binding (a NON-Unit root): the loaded ExactSidecar
    // false+"null" first exact-decodes to Option None, then rejects because
    // presence=false is only legal under a Unit root — never fabricating a Unit /
    // Option, never reading the poisoned compat `result`.
    auto resume_program =
        make_two_call_memo_program(wf, kProbe0Id, option_of(int_type()), kHoldId);
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(*loaded);
    c2.resume_pending_result = make_int(0);
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    auto resumed = r2.run(wf, make_none());
    check(resumed.status() != WorkflowStatus::Completed, "p019.not_completed");
    check(diagnostic_has_execution_error_code(resumed.diagnostics), "p019.execution_error_code");
    check(diagnostic_message_contains(resumed.diagnostics, "memo Value type mismatch"),
          "p019.value_mismatch_message");
    check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
          "p019.not_coordinate_divergence");
    check(live_calls == 0, "p019.no_live_invoke");
    // raw-wins + no payload echo: the poisoned compat secret governs nothing and
    // leaks nowhere in the whole bag.
    check(no_diagnostic_echoes(resumed.diagnostics, kSecret), "p019.no_payload_echo");
    std::filesystem::remove_all(store_path.parent_path());
}

// RFC 0026 C2b G4c: raw wire-JSON pending result (WorkflowRuntimeConfig::
// resume_pending_result_wire_json). A single-node workflow whose only capability
// call is the pending one; on resume the host supplies the result as RAW wire
// JSON, decoded EXACTLY under the capability's verified binding at the consume
// gate — never via the schema-free value_from_json materialization.
[[nodiscard]] Program make_typed_pending_node_program(const std::string &workflow_name,
                                                      const std::string &capability,
                                                      std::size_t capability_id,
                                                      TypeRef return_type) {
    Program program;
    CapabilityDecl cap;
    cap.name = capability;
    cap.return_type_ref = std::move(return_type);
    cap.symbol_ref = SymbolRef{.kind = SymbolRefKind::Capability,
                               .canonical_name = capability,
                               .local_name = capability,
                               .id = capability_id};
    program.declarations.push_back(std::move(cap));
    program.declarations.push_back(make_echo_agent("EchoAgent"));
    program.declarations.push_back(make_input_field_return_flow("EchoAgent", "value"));

    WorkflowDecl workflow;
    workflow.name = workflow_name;
    workflow.input_type_ref = make_named_type_ref("Input");
    workflow.output_type_ref = make_named_type_ref("Output");
    WorkflowNode node;
    node.name = "n";
    node.target_ref = make_agent_ref("EchoAgent");
    StructLiteralExpr node_input;
    node_input.type_name = "NodeInput";
    CallExpr call;
    call.callee = capability;
    call.callee_ref = make_capability_ref(capability, capability_id);
    node_input.fields.push_back(
        StructFieldInit{.name = "value", .value = make_expr_ptr(std::move(call))});
    node.input = make_expr_ptr(std::move(node_input));
    workflow.nodes.push_back(std::move(node));
    PathExpr return_path;
    return_path.path.root_kind = PathRootKind::Identifier;
    return_path.path.root_name = "n";
    workflow.return_value = make_expr_ptr(std::move(return_path));
    program.declarations.push_back(std::move(workflow));
    return program;
}

struct RawPendingOutcome {
    WorkflowResult result;
    std::size_t live_calls{0};
    bool suspended_ok{false};
};

// Suspend a single-node workflow on its only (pending) capability, then resume by
// supplying the result as raw wire JSON. `native_override`, when set, is ALSO
// supplied on resume (to exercise the both-source conflict). When `raw_wire` is
// nullopt AND `native_override` is unset, neither source is supplied on resume
// (replay-neither).
[[nodiscard]] RawPendingOutcome
raw_pending_resume(const std::string &workflow_name, std::size_t cap_id, TypeRef return_type,
                   std::optional<std::string> raw_wire,
                   std::optional<Value> native_override = std::nullopt) {
    RawPendingOutcome out;
    auto suspend_program = make_typed_pending_node_program(workflow_name, "answer", cap_id,
                                                           clone_type_ref(return_type));
    WorkflowRuntimeConfig c1;
    c1.contextual_capability_invoker =
        [](const CapabilityInvocationContext &, const std::string &,
           const std::vector<Value> &) -> CapabilityCallResult {
        CapabilityCallResult pending;
        pending.status = CapabilityCallStatus::Pending;
        return pending;
    };
    WorkflowRuntime r1(suspend_program, std::move(c1));
    auto s1 = r1.run(workflow_name, make_none());
    out.suspended_ok = s1.suspended.has_value();
    if (!out.suspended_ok) {
        return out;
    }

    auto resume_program = make_typed_pending_node_program(workflow_name, "answer", cap_id,
                                                          clone_type_ref(return_type));
    WorkflowRuntimeConfig c2;
    c2.recovery_snapshot = std::move(s1.suspended);
    if (raw_wire.has_value()) {
        c2.resume_pending_result_wire_json = std::move(*raw_wire);
    }
    if (native_override.has_value()) {
        c2.resume_pending_result = std::move(*native_override);
    }
    std::size_t live_calls = 0;
    c2.contextual_capability_invoker =
        [&live_calls](const CapabilityInvocationContext &, const std::string &,
                      const std::vector<Value> &) -> CapabilityCallResult {
        ++live_calls;
        CapabilityCallResult r;
        r.status = CapabilityCallStatus::Success;
        r.value = make_int(-1);
        return r;
    };
    WorkflowRuntime r2(resume_program, std::move(c2));
    out.result = r2.run(workflow_name, make_none());
    out.live_calls = live_calls;
    return out;
}

void test_resume_raw_pending_wire_matrix() {
    // --- POSITIVES: raw wire decodes EXACTLY under the binding, present=true.
    // Each asserts suspended_ok + Completed + 0 live invoke, then the NATIVE
    // VARIANT of the output (not merely its JSON spelling — value_to_json collapses
    // FloatValue(1.0)->1 and both Unit/None->null, so JSON alone cannot prove the
    // variant). ---
    // Int scalar.
    {
        auto o = raw_pending_resume("RawIntWf", 801, int_type(), std::string("42"));
        check(o.suspended_ok, "raw.int.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.int.completed");
        check(o.live_calls == 0, "raw.int.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<IntValue>(out->node) &&
                  std::get<IntValue>(out->node).value == 42,
              "raw.int.value_42");
    }
    // Decimal(2): raw String "1.23" -> DecimalValue (schema-free would keep String).
    {
        auto o = raw_pending_resume("RawDecWf", 802, decimal_type(2), std::string("\"1.23\""));
        check(o.suspended_ok, "raw.decimal.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.decimal.completed");
        check(o.live_calls == 0, "raw.decimal.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<DecimalValue>(out->node) &&
                  std::get<DecimalValue>(out->node).spelling == "1.23",
              "raw.decimal.spelling_1_23");
    }
    // Duration: raw String "5s" -> DurationValue.
    {
        auto o = raw_pending_resume("RawDurWf", 803, duration_type(), std::string("\"5s\""));
        check(o.suspended_ok, "raw.duration.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.duration.completed");
        check(o.live_calls == 0, "raw.duration.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<DurationValue>(out->node) &&
                  std::get<DurationValue>(out->node).spelling == "5s",
              "raw.duration.spelling_5s");
    }
    // Set<Int>: raw array -> SetValue (schema-free would degrade to List).
    {
        auto o = raw_pending_resume("RawSetWf", 804, set_of(int_type()), std::string("[1,2]"));
        check(o.suspended_ok, "raw.set.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.set.completed");
        check(o.live_calls == 0, "raw.set.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<SetValue>(out->node), "raw.set.is_set");
        check(out != nullptr && value_to_json(*out) == "[1,2]", "raw.set.elements_1_2");
    }
    // Map<String,Int> incl a reserved-marker-LOOKING key -> MapValue (ordinary key).
    {
        auto o = raw_pending_resume("RawMapWf", 805,
                                    map_of(TypeRef{.kind = TypeRefKind::String}, int_type()),
                                    std::string(R"({"_timestamp":7,"plain":9})"));
        check(o.suspended_ok, "raw.map.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.map.completed");
        check(o.live_calls == 0, "raw.map.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<MapValue>(out->node), "raw.map.is_map");
        check(out != nullptr && value_to_json(*out) == R"({"_timestamp":7,"plain":9})",
              "raw.map.keys_and_values");
    }
    // Option<Int> None: raw `null` -> Option None as an EnumValue (is_optional_none),
    // present=TRUE (a PRESENT token, NOT the native valueless present=false compat and
    // NOT a bare NoneValue/UnitValue).
    {
        auto o = raw_pending_resume("RawOptNoneWf", 806, option_of(int_type()),
                                    std::string("null"));
        check(o.suspended_ok, "raw.option_none.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.option_none.completed");
        check(o.live_calls == 0, "raw.option_none.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && runtime::is_optional_none(*out),
              "raw.option_none.is_optional_none");
        check(out != nullptr && !std::holds_alternative<runtime::NoneValue>(out->node) &&
                  !std::holds_alternative<runtime::UnitValue>(out->node),
              "raw.option_none.not_bare_none_or_unit");
    }
    // Option<Int> Some: raw `7` -> Option Some(7) (is_some + inner Int 7).
    {
        auto o = raw_pending_resume("RawOptSomeWf", 807, option_of(int_type()), std::string("7"));
        check(o.suspended_ok, "raw.option_some.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.option_some.completed");
        check(o.live_calls == 0, "raw.option_some.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && runtime::is_some(*out), "raw.option_some.is_some");
        const auto *inner = out != nullptr ? runtime::optional_inner(*out) : nullptr;
        check(inner != nullptr && std::holds_alternative<IntValue>(inner->node) &&
                  std::get<IntValue>(inner->node).value == 7,
              "raw.option_some.inner_int_7");
    }
    // Unit: raw `null` -> UnitValue, present=TRUE (raw PRESENT token). Contrast with
    // the native valueless Unit (present=false) covered by the live-valueless test.
    {
        auto o = raw_pending_resume("RawUnitWf", 808, unit_type(), std::string("null"));
        check(o.suspended_ok, "raw.unit.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.unit.completed");
        check(o.live_calls == 0, "raw.unit.no_live_invoke");
        const auto *out = o.result.output();
        // present=true: the resumed call yields a real UnitValue (not absent/None).
        check(out != nullptr && std::holds_alternative<runtime::UnitValue>(out->node),
              "raw.unit.is_unit_value");
    }
    // Float 1.0: raw `1.0` (FloatSyntax) accepted -> FloatValue.
    {
        auto o = raw_pending_resume("RawFloatWf", 809, float_type(), std::string("1.0"));
        check(o.suspended_ok, "raw.float_1_0.suspended");
        check(o.result.status() == WorkflowStatus::Completed, "raw.float_1_0.completed");
        check(o.live_calls == 0, "raw.float_1_0.no_live_invoke");
        const auto *out = o.result.output();
        check(out != nullptr && std::holds_alternative<FloatValue>(out->node),
              "raw.float_1_0.is_float");
    }

    // --- NEGATIVES: exact gate rejects; suspended_ok, NOT Completed, 0 live invoke,
    // not a replay divergence. `echo_token` (optional) is asserted absent from the
    // whole diagnostic bag; a degenerate token like bare "1" is NOT usable as a
    // no-echo secret (it appears in ids/coordinates), so those cases pass nullopt. ---
    auto raw_negative = [](const std::string &wf, std::size_t id, TypeRef ret,
                           const std::string &wire, const std::string &tag,
                           std::optional<std::string> echo_token) {
        auto o = raw_pending_resume(wf, id, std::move(ret), std::string(wire));
        check(o.suspended_ok, tag + ".suspended");
        check(o.result.status() != WorkflowStatus::Completed, tag + ".not_completed");
        check(diagnostic_has_execution_error_code(o.result.diagnostics), tag + ".execution_error");
        check(o.live_calls == 0, tag + ".no_live_invoke");
        check(!diagnostic_message_contains(o.result.diagnostics, "replay diverged"),
              tag + ".not_coordinate_divergence");
        if (echo_token.has_value()) {
            check(no_diagnostic_echoes(o.result.diagnostics, *echo_token), tag + ".no_payload_echo");
        }
    };
    // Float accept->reject FLIP: bare int `1` where the schema wants Float. The old
    // schema-free validator accepted this; the exact codec rejects it. (No no-echo
    // token: "1" is too degenerate to be a payload secret.)
    raw_negative("RawFloatBareIntWf", 810, float_type(), "1", "raw.float_bare_int", std::nullopt);
    // Numeric provenance triad under an Int binding (all report expected integer; the
    // boundary literals themselves are the provenance evidence). Each hostile literal
    // is a distinctive multi-digit token, so no-echo is meaningful.
    raw_negative("RawHighUintWf", 811, int_type(), "18446744073709551615", "raw.high_uint",
                 std::string("18446744073709551615"));
    raw_negative("RawPosFallbackWf", 812, int_type(), "18446744073709551616",
                 "raw.pos_integer_fallback", std::string("18446744073709551616"));
    raw_negative("RawNegFallbackWf", 813, int_type(), "-9223372036854775809",
                 "raw.neg_integer_fallback", std::string("-9223372036854775809"));
    // Malformed raw JSON.
    raw_negative("RawMalformedWf", 814, int_type(), "{not json", "raw.malformed", std::nullopt);

    // --- IDENTITY BEFORE PAYLOAD: a divergent pending identity must fail on identity,
    // never on the (hostile) raw payload, and must not echo it. Mirrors the P0-15
    // native fixture: SAME program/callee id; only the SNAPSHOT's pending_cap_id is
    // corrupted to another still-declared id, so the id mismatch fires before decode. ---
    {
        auto program =
            make_two_capability_node_program("RawPendIdentityWf", "first", 851, "second", 852);
        WorkflowRuntimeConfig c1;
        c1.contextual_capability_invoker =
            [](const CapabilityInvocationContext &, const std::string &name,
               const std::vector<Value> &) -> CapabilityCallResult {
            CapabilityCallResult r;
            if (name == "second") {
                r.status = CapabilityCallStatus::Pending;
            } else {
                r.status = CapabilityCallStatus::Success;
                r.value = make_int(0);
            }
            return r;
        };
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("RawPendIdentityWf", make_none());
        check(s1.suspended.has_value() && s1.suspended->suspended.has_value(),
              "raw.identity.suspended");
        // Corrupt the recorded pending_cap_id to `first` (851): a DIFFERENT, still-
        // declared Int capability. The live pending call is `second` (852), so the
        // identity gate must reject on 852 != 851 BEFORE reading the raw payload.
        if (s1.suspended.has_value() && s1.suspended->suspended.has_value()) {
            s1.suspended->suspended->pending_cap_id = 851;
        }
        auto resume_program =
            make_two_capability_node_program("RawPendIdentityWf", "first", 851, "second", 852);
        WorkflowRuntimeConfig c2;
        c2.recovery_snapshot = std::move(s1.suspended);
        const std::string hostile = "18446744073709551616"; // would reject IF reached
        c2.resume_pending_result_wire_json = hostile;
        std::size_t live_calls = 0;
        c2.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult r;
            r.status = CapabilityCallStatus::Success;
            r.value = make_int(-1);
            return r;
        };
        WorkflowRuntime r2(resume_program, std::move(c2));
        auto resumed = r2.run("RawPendIdentityWf", make_none());
        check(resumed.status() != WorkflowStatus::Completed, "raw.identity.not_completed");
        check(diagnostic_has_execution_error_code(resumed.diagnostics),
              "raw.identity.execution_error");
        check(diagnostic_message_contains(resumed.diagnostics, "pending-call identity mismatch"),
              "raw.identity.identity_mismatch_message");
        check(!diagnostic_message_contains(resumed.diagnostics, "replay diverged"),
              "raw.identity.not_coordinate_divergence");
        check(live_calls == 0, "raw.identity.no_live_invoke");
        check(no_diagnostic_echoes(resumed.diagnostics, hostile), "raw.identity.no_payload_echo");
    }

    // --- BOTH-SOURCE CONFLICT: identity-correct, but BOTH native and raw supplied ->
    // fail closed AFTER identity+binding, 0 live invoke, no raw payload echo. ---
    {
        const std::string raw_secret = "18446744073709551616"; // distinctive raw token
        auto o = raw_pending_resume("RawBothWf", 816, int_type(), raw_secret,
                                    /*native_override=*/make_int(42));
        check(o.suspended_ok, "raw.both.suspended");
        check(o.result.status() != WorkflowStatus::Completed, "raw.both.not_completed");
        check(diagnostic_has_execution_error_code(o.result.diagnostics), "raw.both.execution_error");
        check(diagnostic_message_contains(o.result.diagnostics,
                                          "both a native and a raw wire pending result"),
              "raw.both.conflict_message");
        check(!diagnostic_message_contains(o.result.diagnostics, "replay diverged"),
              "raw.both.not_coordinate_divergence");
        check(o.live_calls == 0, "raw.both.no_live_invoke");
        check(no_diagnostic_echoes(o.result.diagnostics, raw_secret), "raw.both.no_payload_echo");
    }

    // --- REPLAY NEITHER: a resume with neither source supplied keeps the existing
    // exact missing-result Error at the pending ordinal (identity+binding pass). ---
    {
        auto o = raw_pending_resume("RawNeitherWf", 817, int_type(), /*raw_wire=*/std::nullopt);
        check(o.suspended_ok, "raw.neither.suspended");
        check(o.result.status() != WorkflowStatus::Completed, "raw.neither.not_completed");
        check(diagnostic_has_execution_error_code(o.result.diagnostics),
              "raw.neither.execution_error");
        check(diagnostic_message_contains(o.result.diagnostics,
                                          "missing the pending capability result"),
              "raw.neither.missing_result_message");
        check(!diagnostic_message_contains(o.result.diagnostics, "replay diverged"),
              "raw.neither.not_coordinate_divergence");
        check(o.live_calls == 0, "raw.neither.no_live_invoke");
    }

    // --- FRESH / NON-REPLAY, BOTH ABSENT: a normal first run (no recovery snapshot)
    // with neither pending source is NOT an error — the capability is invoked live;
    // if the host returns Pending the workflow suspends normally. Proves the
    // source-state gate is NOT a global/ctor admission. ---
    {
        auto program = make_typed_pending_node_program("RawFreshWf", "answer", 818, int_type());
        WorkflowRuntimeConfig c1;
        std::size_t live_calls = 0;
        c1.contextual_capability_invoker =
            [&live_calls](const CapabilityInvocationContext &, const std::string &,
                          const std::vector<Value> &) -> CapabilityCallResult {
            ++live_calls;
            CapabilityCallResult pending;
            pending.status = CapabilityCallStatus::Pending;
            return pending;
        };
        // No recovery_snapshot, no resume_pending_result, no wire json.
        WorkflowRuntime r1(program, std::move(c1));
        auto s1 = r1.run("RawFreshWf", make_none());
        check(s1.status() == WorkflowStatus::Suspended, "raw.fresh.suspended_not_error");
        check(!diagnostic_has_execution_error_code(s1.diagnostics), "raw.fresh.no_execution_error");
        check(live_calls == 1, "raw.fresh.live_invoked_once");
    }
}

} // anonymous namespace

int main() {
    test_single_node_workflow();
    test_injected_monotonic_clock_is_deterministic();
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
    test_pending_capability_suspends_without_failure();
    test_resume_round_trip_equals_sync_path();
    test_resume_fails_closed_on_memo_mismatch();
    test_durable_write_exactly_once_across_resume();
    test_durable_write_intent_presence_not_value();
    test_resume_fails_closed_on_pending_result_type_mismatch();
    test_two_suspensions_on_one_node_rebuild_memo_append_only();
    test_resume_fails_closed_on_undeclared_capability();
    test_resume_fails_closed_when_pending_never_reached();
    test_resume_with_symbol_id_zero();
    test_resume_fails_closed_on_nested_null_struct();
    test_resume_pending_identity_mismatch();
    test_resume_cache_schema_failure_is_fail_closed_no_echo();
    test_resume_rich_shape_matrix();
    test_resume_legacy_memo_matrix();
    test_resume_live_valueless_vs_explicit_unit();
    test_resume_consume_rejects_illformed_trust_state();
    test_resume_p0_19_valueless_none_rejected_under_non_unit_binding();
    test_resume_raw_pending_wire_matrix();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
