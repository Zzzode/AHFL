// e2e_workflow.cpp - multi-agent workflow end-to-end test
//
// This test compiles tests/runtime/e2e_multi_agent.ahfl and executes it with WorkflowRuntime,
// validating the complete runtime execution flow.

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::runtime;
using namespace ahfl::ir;

// Test statistics
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
// Compile a .ahfl file into IR
// ============================================================================

[[nodiscard]] std::optional<ir::Program> compile_ahfl_file(const std::filesystem::path &file_path) {
    // 1. Parse the file
    const Frontend frontend;
    const auto parse_result = frontend.parse_file(file_path);
    if (parse_result.has_errors() || !parse_result.program) {
        parse_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 2. Symbol resolution
    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 3. Type checking
    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 4. Semantic validation
    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 5. Generate IR
    return lower_program_ir(*parse_result.program, resolve_result, type_check_result);
}

// ============================================================================
// Helper: build unordered_map<string, Value> (Value is move-only, not copyable)
// ============================================================================

std::unordered_map<std::string, Value> build_fields() {
    return {};
}

template <typename... Rest>
std::unordered_map<std::string, Value> build_fields(std::string key, Value val, Rest &&...rest) {
    auto map = build_fields(std::forward<Rest>(rest)...);
    map.emplace(std::move(key), std::move(val));
    return map;
}

// ============================================================================
// Mock capability implementations
// ============================================================================

// Mock ClassifyMessage: returns ClassifyResult { category: Technical, confidence: "high" }
Value mock_classify_message(const std::vector<Value> & /*args*/) {
    return make_struct(
        "ClassifyResult",
        build_fields(
            "category", make_enum("Category", "Technical"), "confidence", make_string("high")));
}

// Mock HandleGeneral: returns SupportResult { response: "...", resolved: true }
Value mock_handle_general(const std::vector<Value> & /*args*/) {
    return make_struct(
        "SupportResult",
        build_fields(
            "response", make_string("Your issue has been resolved"), "resolved", make_bool(true)));
}

// Mock HandleTechnical: returns SupportResult { response: "...", resolved: true }
Value mock_handle_technical(const std::vector<Value> & /*args*/) {
    return make_struct(
        "SupportResult",
        build_fields(
            "response", make_string("Escalated to senior engineer"), "resolved", make_bool(true)));
}

// Mock HandleBilling: returns SupportResult { response: "...", resolved: true }
Value mock_handle_billing(const std::vector<Value> & /*args*/) {
    return make_struct(
        "SupportResult",
        build_fields("response", make_string("Billing adjusted"), "resolved", make_bool(true)));
}

// Mock GenerateSummary: returns SummaryResult
Value mock_generate_summary(const std::vector<Value> &args) {
    // args[0] = category (Enum), args[1] = response (String), args[2] = resolved (Bool)
    Value category = args.empty() ? make_enum("Category", "General") : clone_value(args[0]);
    bool resolved = true;
    if (args.size() > 2) {
        if (auto *bv = std::get_if<BoolValue>(&args[2].node)) {
            resolved = bv->value;
        }
    }

    return make_struct("SummaryResult",
                       build_fields("summary",
                                    make_string("Case resolved successfully"),
                                    "category",
                                    std::move(category),
                                    "resolved",
                                    make_bool(resolved)));
}

// ============================================================================
// Create the CapabilityRegistry
// ============================================================================

CapabilityRegistry create_mock_registry() {
    CapabilityRegistry registry;
    // IR uses fully-qualified names, must match the module::capability format
    registry.register_function("runtime::e2e_multi_agent::ClassifyMessage", mock_classify_message);
    registry.register_function("runtime::e2e_multi_agent::HandleGeneral", mock_handle_general);
    registry.register_function("runtime::e2e_multi_agent::HandleTechnical", mock_handle_technical);
    registry.register_function("runtime::e2e_multi_agent::HandleBilling", mock_handle_billing);
    registry.register_function("runtime::e2e_multi_agent::GenerateSummary", mock_generate_summary);
    return registry;
}

// ============================================================================
// Print the execution trace
// ============================================================================

void print_execution_trace(const WorkflowResult &result) {
    std::cout << "\n=== Workflow Execution Trace ===\n";

    // Status
    std::cout << "Status: ";
    switch (result.status) {
    case WorkflowStatus::Completed:
        std::cout << "Completed\n";
        break;
    case WorkflowStatus::NodeFailed:
        std::cout << "NodeFailed\n";
        break;
    case WorkflowStatus::DependencyFailed:
        std::cout << "DependencyFailed\n";
        break;
    case WorkflowStatus::EvalError:
        std::cout << "EvalError\n";
        break;
    }
    std::cout << "\n";

    // Execution order
    std::cout << "Execution Order: [";
    for (std::size_t i = 0; i < result.execution_order.size(); ++i) {
        if (i > 0)
            std::cout << ", ";
        std::cout << result.execution_order[i];
    }
    std::cout << "]\n\n";

    // Node results
    std::cout << "Node Results:\n";
    for (const auto &nr : result.node_results) {
        std::cout << "  [" << nr.execution_index << "] " << nr.node_name << " -> " << nr.target;
        std::cout << " (status: ";
        switch (nr.status) {
        case AgentStatus::Completed:
            std::cout << "Completed";
            break;
        case AgentStatus::Failed:
            std::cout << "Failed";
            break;
        case AgentStatus::Running:
            std::cout << "Running";
            break;
        case AgentStatus::QuotaExceeded:
            std::cout << "QuotaExceeded";
            break;
        case AgentStatus::InvalidTransition:
            std::cout << "InvalidTransition";
            break;
        case AgentStatus::InfiniteLoop:
            std::cout << "InfiniteLoop";
            break;
        }
        std::cout << ")\n";

        // Print the output value
        if (nr.output.has_value()) {
            std::cout << "      Output: ";
            print_value(*nr.output, std::cout);
            std::cout << "\n";
        }
    }
    std::cout << "\n";

    // Final output
    std::cout << "Final Output: ";
    if (result.output.has_value()) {
        print_value(*result.output, std::cout);
    } else {
        std::cout << "(none)";
    }
    std::cout << "\n";

    // Error messages
    if (result.has_errors()) {
        std::cout << "\nErrors:\n";
        result.diagnostics.render(std::cout);
    }

    std::cout << "=== End of Trace ===\n\n";
}

// ============================================================================
// Test 1: Priority::Low path (Handling)
// ============================================================================

int test_priority_low_handling_path(const ir::Program &program) {
    std::cout << "\n=== Test 1: Priority::Low (Handling Path) ===\n";

    // Create mock registry
    auto registry = create_mock_registry();

    // Configure WorkflowRuntime
    WorkflowRuntimeConfig config;
    config.capability_invoker = registry.as_invoker();

    // Create runtime
    WorkflowRuntime runtime(program, config);

    // Prepare input: SupportRequest { user_id, message, priority }
    Value input = make_struct("SupportRequest",
                              build_fields("user_id",
                                           make_string("user_123"),
                                           "message",
                                           make_string("My server is crashing"),
                                           "priority",
                                           make_enum("runtime::e2e_multi_agent::Priority", "Low")));

    // Execute the workflow
    auto result =
        runtime.run("runtime::e2e_multi_agent::CustomerSupportWorkflow", std::move(input));

    // Print execution trace
    print_execution_trace(result);

    // Verify results
    check(result.status == WorkflowStatus::Completed, "low.status_completed");
    check(!result.has_errors(), "low.no_errors");

    // Execution order: [classify, support, summary]
    check(result.execution_order.size() == 3, "low.exec_order_size_3");
    if (result.execution_order.size() >= 3) {
        check(result.execution_order[0] == "classify", "low.order_0_classify");
        check(result.execution_order[1] == "support", "low.order_1_support");
        check(result.execution_order[2] == "summary", "low.order_2_summary");
    }

    // Verify the final output
    check(result.output.has_value(), "low.has_output");
    if (result.output.has_value()) {
        auto *sv = std::get_if<StructValue>(&result.output->node);
        check(sv != nullptr, "low.output_is_struct");
        if (sv) {
            // Verify category = Technical
            auto cat_it = sv->fields.find("category");
            if (cat_it != sv->fields.end()) {
                auto *ev = std::get_if<EnumValue>(&cat_it->second->node);
                check(ev != nullptr && ev->variant == "Technical", "low.category_technical");
            }

            // Verify resolved = true
            auto res_it = sv->fields.find("resolved");
            if (res_it != sv->fields.end()) {
                auto *bv = std::get_if<BoolValue>(&res_it->second->node);
                check(bv != nullptr && bv->value == true, "low.resolved_true");
            }

            // Verify summary exists
            check(sv->fields.find("summary") != sv->fields.end(), "low.has_summary");
        }
    }

    // Verify all nodes completed
    for (const auto &nr : result.node_results) {
        check(nr.status == AgentStatus::Completed, "low.node_completed_" + nr.node_name);
    }

    return (pass_count == test_count) ? 0 : 1;
}

// ============================================================================
// Test 2: Priority::High path (Escalated)
// ============================================================================

int test_priority_high_escalated_path(const ir::Program &program) {
    std::cout << "\n=== Test 2: Priority::High (Escalated Path) ===\n";

    // Create mock registry
    auto registry = create_mock_registry();

    // Configure WorkflowRuntime
    WorkflowRuntimeConfig config;
    config.capability_invoker = registry.as_invoker();

    // Create runtime
    WorkflowRuntime runtime(program, config);

    // Prepare input: SupportRequest { priority: High }
    Value input =
        make_struct("SupportRequest",
                    build_fields("user_id",
                                 make_string("user_456"),
                                 "message",
                                 make_string("My server is crashing"),
                                 "priority",
                                 make_enum("runtime::e2e_multi_agent::Priority", "High")));

    // Execute the workflow
    auto result =
        runtime.run("runtime::e2e_multi_agent::CustomerSupportWorkflow", std::move(input));

    // Print execution trace
    print_execution_trace(result);

    // Verify results
    check(result.status == WorkflowStatus::Completed, "high.status_completed");
    check(!result.has_errors(), "high.no_errors");

    // Execution order: [classify, support, summary]
    check(result.execution_order.size() == 3, "high.exec_order_size_3");

    // Verify the final output
    check(result.output.has_value(), "high.has_output");
    if (result.output.has_value()) {
        auto *sv = std::get_if<StructValue>(&result.output->node);
        check(sv != nullptr, "high.output_is_struct");
        if (sv) {
            // Verify category = Technical (from classify)
            auto cat_it = sv->fields.find("category");
            if (cat_it != sv->fields.end()) {
                auto *ev = std::get_if<EnumValue>(&cat_it->second->node);
                check(ev != nullptr && ev->variant == "Technical", "high.category_technical");
            }

            // Verify resolved = true
            auto res_it = sv->fields.find("resolved");
            if (res_it != sv->fields.end()) {
                auto *bv = std::get_if<BoolValue>(&res_it->second->node);
                check(bv != nullptr && bv->value == true, "high.resolved_true");
            }
        }
    }

    // Verify that the support node's output contains the Escalated-path response
    // (HandleTechnical is called instead of HandleGeneral)
    bool found_support_node = false;
    for (const auto &nr : result.node_results) {
        if (nr.node_name == "support") {
            found_support_node = true;
            check(nr.status == AgentStatus::Completed, "high.support_completed");
            if (nr.output.has_value()) {
                auto *sv = std::get_if<StructValue>(&nr.output->node);
                if (sv) {
                    auto resp_it = sv->fields.find("response");
                    if (resp_it != sv->fields.end()) {
                        auto *strv = std::get_if<StringValue>(&resp_it->second->node);
                        // Should be HandleTechnical's response
                        check(strv != nullptr && strv->value == "Escalated to senior engineer",
                              "high.support_technical_response");
                    }
                }
            }
            break;
        }
    }
    check(found_support_node, "high.found_support_node");

    return (pass_count == test_count) ? 0 : 1;
}

} // anonymous namespace

int main(int argc, char **argv) {
    // Determine the test file path
    std::filesystem::path test_file;
    if (argc > 1) {
        test_file = argv[1];
    } else {
        // Default path
        test_file = "/Users/bytedance/Develop/AHFL/tests/runtime/e2e_multi_agent.ahfl";
    }

    std::cout << "=== AHFL E2E Workflow Test ===\n";
    std::cout << "Test file: " << test_file << "\n";

    // Compile the .ahfl file
    std::cout << "\nCompiling " << test_file << "...\n";
    auto program = compile_ahfl_file(test_file);
    if (!program.has_value()) {
        std::cerr << "ERROR: Failed to compile " << test_file << "\n";
        return 1;
    }
    std::cout << "Compilation successful!\n";

    // Run tests
    int result1 = test_priority_low_handling_path(*program);
    int result2 = test_priority_high_escalated_path(*program);

    // Summary
    std::cout << "\n=== Test Summary ===\n";
    std::cout << pass_count << "/" << test_count << " tests passed\n";

    return (result1 == 0 && result2 == 0) ? 0 : 1;
}
