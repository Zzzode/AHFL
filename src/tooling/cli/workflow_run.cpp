#include "tooling/cli/workflow_run.hpp"

#include "ahfl/base/support/source.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"
#include "runtime/providers/llm/llm_capability_provider.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace ahfl::cli {
namespace {

using ahfl::evaluator::print_value;
using ahfl::llm_provider::LLMCapabilityProvider;
using ahfl::llm_provider::load_config;
using ahfl::runtime::AgentStatus;
using ahfl::runtime::CapabilityRegistry;
using ahfl::runtime::WorkflowResult;
using ahfl::runtime::WorkflowRuntime;
using ahfl::runtime::WorkflowRuntimeConfig;
using ahfl::runtime::WorkflowStatus;

[[nodiscard]] std::filesystem::path default_llm_config_path() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return {};
    }
    return std::filesystem::path(home) / ".ahfl" / "llm_config.json";
}

[[nodiscard]] std::filesystem::path
llm_config_path_from_options(const CommandLineOptions &options) {
    if (options.llm_config_descriptor.has_value()) {
        return std::filesystem::path(std::string(*options.llm_config_descriptor));
    }
    return default_llm_config_path();
}

[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path &path,
                                                        std::ostream &err) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        err << "error: failed to open LLM config: " << ahfl::display_path(path) << '\n';
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] const char *workflow_status_name(WorkflowStatus status) {
    switch (status) {
    case WorkflowStatus::Completed:
        return "Completed";
    case WorkflowStatus::NodeFailed:
        return "NodeFailed";
    case WorkflowStatus::DependencyFailed:
        return "DependencyFailed";
    case WorkflowStatus::EvalError:
        return "EvalError";
    }
    return "Unknown";
}

[[nodiscard]] const char *agent_status_name(AgentStatus status) {
    switch (status) {
    case AgentStatus::Running:
        return "Running";
    case AgentStatus::Completed:
        return "Completed";
    case AgentStatus::Failed:
        return "Failed";
    case AgentStatus::QuotaExceeded:
        return "QuotaExceeded";
    case AgentStatus::InvalidTransition:
        return "InvalidTransition";
    case AgentStatus::InfiniteLoop:
        return "InfiniteLoop";
    }
    return "Unknown";
}

void print_workflow_result(const WorkflowResult &result,
                           std::string_view workflow_name,
                           const std::filesystem::path &config_path,
                           std::string_view model_name,
                           std::ostream &out) {
    out << "\n=== AHFL Workflow Execution ===\n"
        << "Workflow: " << workflow_name << '\n'
        << "LLM Config: " << config_path << " (model: " << model_name << ")\n"
        << "\n--- Execution ---\n"
        << "Status: " << workflow_status_name(result.status) << '\n'
        << "Execution Order: ";

    for (std::size_t index = 0; index < result.execution_order.size(); ++index) {
        if (index > 0) {
            out << " -> ";
        }
        out << result.execution_order[index];
    }
    out << "\n\n--- Node Results ---\n";

    for (const auto &node_result : result.node_results) {
        out << "[" << node_result.execution_index << "] " << node_result.node_name;
        if (!node_result.target.empty()) {
            out << " (" << node_result.target << ")";
        }
        out << ": " << agent_status_name(node_result.status) << '\n';

        if (node_result.output.has_value()) {
            out << "    Output: ";
            print_value(*node_result.output, out);
            out << '\n';
        }
    }

    out << "\n--- Final Output ---\n";
    if (result.output.has_value()) {
        print_value(*result.output, out);
    } else {
        out << "(none)";
    }
    out << '\n';

    if (result.has_errors()) {
        out << "\n--- Errors ---\n";
        result.diagnostics.render(out);
    }

    out << '\n';
}

} // namespace

int run_workflow_with_llm(const ahfl::ir::Program &program,
                          const CommandLineOptions &options,
                          std::ostream &out,
                          std::ostream &err) {
    if (!options.workflow_name.has_value()) {
        err << "error: run requires --workflow\n";
        return 2;
    }
    if (!options.runtime_input_json.has_value()) {
        err << "error: run requires --input\n";
        return 2;
    }

    const auto config_path = llm_config_path_from_options(options);
    if (config_path.empty()) {
        err << "error: --llm-config is required when HOME is not set\n";
        return 2;
    }

    auto config_content = read_text_file(config_path, err);
    if (!config_content.has_value()) {
        return 1;
    }

    auto llm_config = load_config(*config_content);
    if (llm_config.endpoint.empty() || llm_config.model.empty() || llm_config.api_key.empty()) {
        err << "error: LLM config requires endpoint, model, and api_key\n";
        return 1;
    }

    auto input_value = ahfl::evaluator::value_from_json(*options.runtime_input_json);
    if (!input_value.has_value()) {
        err << "error: failed to parse --input JSON\n";
        return 1;
    }

    LLMCapabilityProvider llm_provider(program, llm_config);
    CapabilityRegistry registry;
    llm_provider.register_all(registry);

    WorkflowRuntimeConfig runtime_config;
    runtime_config.capability_invoker = registry.as_invoker();

    WorkflowRuntime runtime(program, std::move(runtime_config));
    const auto workflow_name = std::string(*options.workflow_name);
    auto result = runtime.run(workflow_name, std::move(*input_value));

    print_workflow_result(result, workflow_name, config_path, llm_config.model, out);
    return result.status == WorkflowStatus::Completed && !result.has_errors() ? 0 : 1;
}

} // namespace ahfl::cli
