// ahfl_run.cpp - AHFL Workflow 真实执行 CLI
//
// 用法:
//   ahfl-run <file.ahfl> --workflow <WorkflowName> --llm-config <config.json> --input '<json>'
//
// 该 CLI 编译 .ahfl 文件，使用 LLM Provider 真实执行 workflow。

#include "evaluator/value.hpp"
#include "evaluator/value_json.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/ir/ir.hpp"
#include "llm_provider/llm_capability_provider.hpp"
#include "llm_provider/llm_provider_config.hpp"
#include "runtime/capability_bridge.hpp"
#include "runtime/workflow_runtime.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::ir;
using namespace ahfl::runtime;
using namespace ahfl::llm_provider;

// ============================================================================
// 命令行参数
// ============================================================================

struct CommandLineArgs {
    std::filesystem::path file_path;
    std::string workflow_name;
    std::filesystem::path llm_config_path;
    std::string input_json;
    bool show_help = false;
};

// 获取默认配置文件路径 (~/.ahfl/llm_config.json)
[[nodiscard]] std::filesystem::path get_default_config_path() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return {};
    }
    return std::filesystem::path(home) / ".ahfl" / "llm_config.json";
}

// 打印帮助信息
void print_help(const char *program_name) {
    std::cout << "用法: " << program_name << " <file.ahfl> [选项]\n\n"
              << "选项:\n"
              << "  --workflow <name>     要执行的 workflow 名称\n"
              << "  --llm-config <path>   LLM 配置文件路径 (默认: ~/.ahfl/llm_config.json)\n"
              << "  --input '<json>'      输入 JSON 字符串\n"
              << "  --help                显示此帮助信息\n\n"
              << "示例:\n"
              << "  " << program_name << " app.ahfl --workflow MyWorkflow \\\n"
              << "    --input '{\"message\": \"My server crashed\"}'\n";
}

// 解析命令行参数
std::optional<CommandLineArgs> parse_args(int argc, char **argv) {
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            args.show_help = true;
            return args;
        }

        if (arg == "--workflow") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --workflow 需要参数\n";
                return std::nullopt;
            }
            args.workflow_name = argv[++i];
        } else if (arg == "--llm-config") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --llm-config 需要参数\n";
                return std::nullopt;
            }
            args.llm_config_path = argv[++i];
        } else if (arg == "--input") {
            if (i + 1 >= argc) {
                std::cerr << "错误: --input 需要参数\n";
                return std::nullopt;
            }
            args.input_json = argv[++i];
        } else if (arg[0] != '-') {
            // Positional argument (file path)
            if (args.file_path.empty()) {
                args.file_path = arg;
            } else {
                std::cerr << "错误: 多个文件路径指定\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "错误: 未知选项: " << arg << "\n";
            return std::nullopt;
        }
    }

    // 验证必需参数
    if (args.file_path.empty()) {
        std::cerr << "错误: 未指定 .ahfl 文件\n";
        return std::nullopt;
    }
    if (args.workflow_name.empty()) {
        std::cerr << "错误: 未指定 --workflow\n";
        return std::nullopt;
    }
    // --llm-config 可选，默认使用 ~/.ahfl/llm_config.json
    if (args.llm_config_path.empty()) {
        args.llm_config_path = get_default_config_path();
    }

    return args;
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
// 读取文件内容
// ============================================================================

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ============================================================================
// 打印执行结果
// ============================================================================

void print_workflow_result(const WorkflowResult &result,
                           const std::filesystem::path &file_path,
                           const std::string &workflow_name,
                           const std::filesystem::path &config_path,
                           const std::string &model_name) {
    std::cout << "\n=== AHFL Workflow Execution ===\n";
    std::cout << "File: " << file_path << "\n";
    std::cout << "Workflow: " << workflow_name << "\n";
    std::cout << "LLM Config: " << config_path << " (model: " << model_name << ")\n";

    std::cout << "\n--- Execution ---\n";

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

    // 执行顺序
    std::cout << "Execution Order: ";
    for (std::size_t i = 0; i < result.execution_order.size(); ++i) {
        if (i > 0) {
            std::cout << " → ";
        }
        std::cout << result.execution_order[i];
    }
    std::cout << "\n";

    // Node 结果
    std::cout << "\n--- Node Results ---\n";
    for (const auto &nr : result.node_results) {
        std::cout << "[" << nr.execution_index << "] " << nr.node_name;
        if (!nr.target.empty()) {
            std::cout << " (" << nr.target << ")";
        }
        std::cout << ": ";

        switch (nr.status) {
        case AgentStatus::Completed:
            std::cout << "Completed\n";
            break;
        case AgentStatus::Failed:
            std::cout << "Failed\n";
            break;
        case AgentStatus::Running:
            std::cout << "Running\n";
            break;
        case AgentStatus::QuotaExceeded:
            std::cout << "QuotaExceeded\n";
            break;
        case AgentStatus::InvalidTransition:
            std::cout << "InvalidTransition\n";
            break;
        case AgentStatus::InfiniteLoop:
            std::cout << "InfiniteLoop\n";
            break;
        }

        // 打印输出值
        if (nr.output.has_value()) {
            std::cout << "    Output: ";
            print_value(*nr.output, std::cout);
            std::cout << "\n";
        }
    }

    // 最终输出
    std::cout << "\n--- Final Output ---\n";
    if (result.output.has_value()) {
        print_value(*result.output, std::cout);
    } else {
        std::cout << "(none)";
    }
    std::cout << "\n";

    // 错误信息
    if (result.has_errors()) {
        std::cout << "\n--- Errors ---\n";
        result.diagnostics.render(std::cout);
    }

    std::cout << "\n";
}

} // anonymous namespace

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    // 解析命令行参数
    auto args_opt = parse_args(argc, argv);
    if (!args_opt.has_value()) {
        return 1;
    }

    const auto &args = *args_opt;

    // 显示帮助
    if (args.show_help) {
        print_help(argv[0]);
        return 0;
    }

    // 1. 编译 .ahfl 文件
    std::cout << "编译: " << args.file_path << "\n";
    auto program_opt = compile_ahfl_file(args.file_path);
    if (!program_opt.has_value()) {
        std::cerr << "错误: 编译失败\n";
        return 1;
    }
    std::cout << "编译成功\n";

    const auto &program = *program_opt;

    // 2. 加载 LLM 配置
    auto config_content_opt = read_file(args.llm_config_path);
    if (!config_content_opt.has_value()) {
        std::cerr << "错误: 无法读取 LLM 配置文件: " << args.llm_config_path << "\n\n";
        std::cerr << "请创建配置文件:\n";
        std::cerr << "  mkdir -p ~/.ahfl\n";
        std::cerr << "  cat > ~/.ahfl/llm_config.json << 'EOF'\n";
        std::cerr << "  {\n";
        std::cerr << "    \"endpoint\": \"https://open.bigmodel.cn/api/paas/v4\",\n";
        std::cerr << "    \"model\": \"glm-4\",\n";
        std::cerr << "    \"api_key\": \"${GLM_API_KEY}\",\n";
        std::cerr << "    \"temperature\": 0.1,\n";
        std::cerr << "    \"max_tokens\": 1024,\n";
        std::cerr << "    \"json_mode\": true\n";
        std::cerr << "  }\n";
        std::cerr << "  EOF\n\n";
        std::cerr << "并设置环境变量: export GLM_API_KEY=\"your-key\"\n";
        return 1;
    }

    auto llm_config = load_config(*config_content_opt);
    std::cout << "LLM 配置已加载: model=" << llm_config.model << "\n";

    // 3. 解析输入 JSON
    auto input_value_opt = ahfl::evaluator::value_from_json(args.input_json);
    if (!input_value_opt.has_value()) {
        std::cerr << "错误: 无法解析输入 JSON: " << args.input_json << "\n";
        return 1;
    }
    std::cout << "输入已解析\n";

    // 4. 创建 LLM Provider 和 Registry
    LLMCapabilityProvider llm_provider(program, llm_config);
    CapabilityRegistry registry;
    llm_provider.register_all(registry);
    std::cout << "LLM Provider 已注册 " << registry.registered_names().size()
              << " 个 capabilities\n";

    // 5. 配置 WorkflowRuntime
    WorkflowRuntimeConfig wf_config;
    wf_config.capability_invoker = registry.as_invoker();

    // 6. 创建 runtime 并执行
    WorkflowRuntime workflow_runtime(program, wf_config);

    std::cout << "\n执行 workflow: " << args.workflow_name << "...\n";
    auto result = workflow_runtime.run(args.workflow_name, std::move(*input_value_opt));

    // 7. 输出结果
    print_workflow_result(
        result, args.file_path, args.workflow_name, args.llm_config_path, llm_config.model);

    // 返回状态码
    return (result.status == WorkflowStatus::Completed && !result.has_errors()) ? 0 : 1;
}
