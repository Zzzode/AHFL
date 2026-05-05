// e2e_workflow.cpp - 多 Agent Workflow 端到端测试
//
// 本测试编译 tests/runtime/e2e_multi_agent.ahfl 并使用 WorkflowRuntime 执行，
// 验证完整的 runtime 执行流程。

#include "ahfl/evaluator/value.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/runtime/capability_bridge.hpp"
#include "ahfl/runtime/workflow_runtime.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

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

// 测试统计
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
// 编译 .ahfl 文件到 IR
// ============================================================================

[[nodiscard]] std::optional<ir::Program> compile_ahfl_file(const std::filesystem::path &file_path) {
    // 1. 解析文件
    const Frontend frontend;
    const auto parse_result = frontend.parse_file(file_path);
    if (parse_result.has_errors() || !parse_result.program) {
        parse_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 2. 符号解析
    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 3. 类型检查
    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 4. 语义验证
    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cerr);
        return std::nullopt;
    }

    // 5. 生成 IR
    return lower_program_ir(*parse_result.program, resolve_result, type_check_result);
}

// ============================================================================
// 辅助: 构造 unordered_map<string, Value>（Value 不可拷贝，只能 move）
// ============================================================================

std::unordered_map<std::string, Value> build_fields() {
    return {};
}

template <typename... Rest>
std::unordered_map<std::string, Value>
build_fields(std::string key, Value val, Rest &&...rest) {
    auto map = build_fields(std::forward<Rest>(rest)...);
    map.emplace(std::move(key), std::move(val));
    return map;
}

// ============================================================================
// Mock Capability 实现
// ============================================================================

// 模拟 ClassifyMessage: 返回 ClassifyResult { category: Technical, confidence: "high" }
Value mock_classify_message(const std::vector<Value> & /*args*/) {
    return make_struct("ClassifyResult",
                       build_fields("category", make_enum("Category", "Technical"),
                                    "confidence", make_string("high")));
}

// 模拟 HandleGeneral: 返回 SupportResult { response: "...", resolved: true }
Value mock_handle_general(const std::vector<Value> & /*args*/) {
    return make_struct("SupportResult",
                       build_fields("response", make_string("Your issue has been resolved"),
                                    "resolved", make_bool(true)));
}

// 模拟 HandleTechnical: 返回 SupportResult { response: "...", resolved: true }
Value mock_handle_technical(const std::vector<Value> & /*args*/) {
    return make_struct("SupportResult",
                       build_fields("response", make_string("Escalated to senior engineer"),
                                    "resolved", make_bool(true)));
}

// 模拟 HandleBilling: 返回 SupportResult { response: "...", resolved: true }
Value mock_handle_billing(const std::vector<Value> & /*args*/) {
    return make_struct("SupportResult",
                       build_fields("response", make_string("Billing adjusted"),
                                    "resolved", make_bool(true)));
}

// 模拟 GenerateSummary: 返回 SummaryResult
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
                       build_fields("summary", make_string("Case resolved successfully"),
                                    "category", std::move(category),
                                    "resolved", make_bool(resolved)));
}

// ============================================================================
// 创建 CapabilityRegistry
// ============================================================================

CapabilityRegistry create_mock_registry() {
    CapabilityRegistry registry;
    // IR 使用全限定名，需要匹配 module::capability 格式
    registry.register_function("runtime::e2e_multi_agent::ClassifyMessage", mock_classify_message);
    registry.register_function("runtime::e2e_multi_agent::HandleGeneral", mock_handle_general);
    registry.register_function("runtime::e2e_multi_agent::HandleTechnical", mock_handle_technical);
    registry.register_function("runtime::e2e_multi_agent::HandleBilling", mock_handle_billing);
    registry.register_function("runtime::e2e_multi_agent::GenerateSummary", mock_generate_summary);
    return registry;
}

// ============================================================================
// 打印执行轨迹
// ============================================================================

void print_execution_trace(const WorkflowResult &result) {
    std::cout << "\n=== Workflow Execution Trace ===\n";

    // 状态
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

    // 执行顺序
    std::cout << "Execution Order: [";
    for (std::size_t i = 0; i < result.execution_order.size(); ++i) {
        if (i > 0)
            std::cout << ", ";
        std::cout << result.execution_order[i];
    }
    std::cout << "]\n\n";

    // Node 结果
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

        // 打印输出值
        if (nr.output.has_value()) {
            std::cout << "      Output: ";
            print_value(*nr.output, std::cout);
            std::cout << "\n";
        }
    }
    std::cout << "\n";

    // 最终输出
    std::cout << "Final Output: ";
    if (result.output.has_value()) {
        print_value(*result.output, std::cout);
    } else {
        std::cout << "(none)";
    }
    std::cout << "\n";

    // 错误信息
    if (result.has_errors()) {
        std::cout << "\nErrors:\n";
        result.diagnostics.render(std::cout);
    }

    std::cout << "=== End of Trace ===\n\n";
}

// ============================================================================
// 测试 1: Priority::Low 路径 (Handling)
// ============================================================================

int test_priority_low_handling_path(const ir::Program &program) {
    std::cout << "\n=== Test 1: Priority::Low (Handling Path) ===\n";

    // 创建 mock registry
    auto registry = create_mock_registry();

    // 配置 WorkflowRuntime
    WorkflowRuntimeConfig config;
    config.capability_invoker = registry.as_invoker();

    // 创建 runtime
    WorkflowRuntime runtime(program, config);

    // 准备输入: SupportRequest { user_id, message, priority }
    Value input = make_struct(
        "SupportRequest",
        build_fields("user_id", make_string("user_123"),
                     "message", make_string("My server is crashing"),
                     "priority", make_enum("runtime::e2e_multi_agent::Priority", "Low")));

    // 执行 workflow
    auto result = runtime.run("runtime::e2e_multi_agent::CustomerSupportWorkflow", std::move(input));

    // 打印执行轨迹
    print_execution_trace(result);

    // 验证结果
    check(result.status == WorkflowStatus::Completed, "low.status_completed");
    check(!result.has_errors(), "low.no_errors");

    // 执行顺序: [classify, support, summary]
    check(result.execution_order.size() == 3, "low.exec_order_size_3");
    if (result.execution_order.size() >= 3) {
        check(result.execution_order[0] == "classify", "low.order_0_classify");
        check(result.execution_order[1] == "support", "low.order_1_support");
        check(result.execution_order[2] == "summary", "low.order_2_summary");
    }

    // 验证最终输出
    check(result.output.has_value(), "low.has_output");
    if (result.output.has_value()) {
        auto *sv = std::get_if<StructValue>(&result.output->node);
        check(sv != nullptr, "low.output_is_struct");
        if (sv) {
            // 验证 category = Technical
            auto cat_it = sv->fields.find("category");
            if (cat_it != sv->fields.end()) {
                auto *ev = std::get_if<EnumValue>(&cat_it->second->node);
                check(ev != nullptr && ev->variant == "Technical", "low.category_technical");
            }

            // 验证 resolved = true
            auto res_it = sv->fields.find("resolved");
            if (res_it != sv->fields.end()) {
                auto *bv = std::get_if<BoolValue>(&res_it->second->node);
                check(bv != nullptr && bv->value == true, "low.resolved_true");
            }

            // 验证 summary 存在
            check(sv->fields.find("summary") != sv->fields.end(), "low.has_summary");
        }
    }

    // 验证所有 node 都完成
    for (const auto &nr : result.node_results) {
        check(nr.status == AgentStatus::Completed,
              "low.node_completed_" + nr.node_name);
    }

    return (pass_count == test_count) ? 0 : 1;
}

// ============================================================================
// 测试 2: Priority::High 路径 (Escalated)
// ============================================================================

int test_priority_high_escalated_path(const ir::Program &program) {
    std::cout << "\n=== Test 2: Priority::High (Escalated Path) ===\n";

    // 创建 mock registry
    auto registry = create_mock_registry();

    // 配置 WorkflowRuntime
    WorkflowRuntimeConfig config;
    config.capability_invoker = registry.as_invoker();

    // 创建 runtime
    WorkflowRuntime runtime(program, config);

    // 准备输入: SupportRequest { priority: High }
    Value input = make_struct(
        "SupportRequest",
        build_fields("user_id", make_string("user_456"),
                     "message", make_string("My server is crashing"),
                     "priority", make_enum("runtime::e2e_multi_agent::Priority", "High")));

    // 执行 workflow
    auto result = runtime.run("runtime::e2e_multi_agent::CustomerSupportWorkflow", std::move(input));

    // 打印执行轨迹
    print_execution_trace(result);

    // 验证结果
    check(result.status == WorkflowStatus::Completed, "high.status_completed");
    check(!result.has_errors(), "high.no_errors");

    // 执行顺序: [classify, support, summary]
    check(result.execution_order.size() == 3, "high.exec_order_size_3");

    // 验证最终输出
    check(result.output.has_value(), "high.has_output");
    if (result.output.has_value()) {
        auto *sv = std::get_if<StructValue>(&result.output->node);
        check(sv != nullptr, "high.output_is_struct");
        if (sv) {
            // 验证 category = Technical (来自 classify)
            auto cat_it = sv->fields.find("category");
            if (cat_it != sv->fields.end()) {
                auto *ev = std::get_if<EnumValue>(&cat_it->second->node);
                check(ev != nullptr && ev->variant == "Technical", "high.category_technical");
            }

            // 验证 resolved = true
            auto res_it = sv->fields.find("resolved");
            if (res_it != sv->fields.end()) {
                auto *bv = std::get_if<BoolValue>(&res_it->second->node);
                check(bv != nullptr && bv->value == true, "high.resolved_true");
            }
        }
    }

    // 验证 support node 的 output 包含 Escalated 路径的响应
    // (HandleTechnical 被调用而非 HandleGeneral)
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
                        // 应该是 HandleTechnical 的响应
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
    // 确定测试文件路径
    std::filesystem::path test_file;
    if (argc > 1) {
        test_file = argv[1];
    } else {
        // 默认路径
        test_file = "/Users/bytedance/Develop/AHFL/tests/runtime/e2e_multi_agent.ahfl";
    }

    std::cout << "=== AHFL E2E Workflow Test ===\n";
    std::cout << "Test file: " << test_file << "\n";

    // 编译 .ahfl 文件
    std::cout << "\nCompiling " << test_file << "...\n";
    auto program = compile_ahfl_file(test_file);
    if (!program.has_value()) {
        std::cerr << "ERROR: Failed to compile " << test_file << "\n";
        return 1;
    }
    std::cout << "Compilation successful!\n";

    // 运行测试
    int result1 = test_priority_low_handling_path(*program);
    int result2 = test_priority_high_escalated_path(*program);

    // 总结
    std::cout << "\n=== Test Summary ===\n";
    std::cout << pass_count << "/" << test_count << " tests passed\n";

    return (result1 == 0 && result2 == 0) ? 0 : 1;
}
