#include "compiler/backends/infra/lower.hpp"

#include <variant>

namespace ahfl::backends {

std::vector<K8sCrdConfig> lower_k8s_crd(const ir::Program &program) {
    std::vector<K8sCrdConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            K8sCrdConfig config;
            config.agent_name = agent->name;
            config.states = agent->states;
            config.capabilities = agent->capabilities;
            result.push_back(std::move(config));
        }
    }
    return result;
}

std::optional<OpenApiConfig> lower_openapi(const ir::Program &program) {
    OpenApiConfig config;
    config.title = "AHFL Generated API";
    for (const auto &decl : program.declarations) {
        if (const auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            OpenApiEndpoint endpoint;
            endpoint.path = "/" + cap->name;
            endpoint.method = "POST";
            endpoint.summary = "Invoke capability " + cap->name;
            config.endpoints.push_back(std::move(endpoint));
        }
    }
    if (config.endpoints.empty()) {
        return std::nullopt;
    }
    return config;
}

std::vector<TerraformConfig> lower_terraform(const ir::Program &program) {
    std::vector<TerraformConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *workflow = std::get_if<ir::WorkflowDecl>(&decl)) {
            TerraformConfig config;
            config.workflow_name = workflow->name;
            for (const auto &node : workflow->nodes) {
                TerraformResource resource;
                resource.resource_type = "ahfl_workflow_node";
                resource.resource_name = node.name;
                resource.attributes.emplace_back("target", node.target);
                config.resources.push_back(std::move(resource));
            }
            result.push_back(std::move(config));
        }
    }
    return result;
}

std::vector<WasmAgentConfig> lower_wasm(const ir::Program &program) {
    std::vector<WasmAgentConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            WasmAgentConfig config;
            config.agent_name = agent->name;
            config.states = agent->states;
            for (const auto &t : agent->transitions) {
                config.transitions.emplace_back(t.from_state, t.to_state);
            }
            config.capabilities = agent->capabilities;
            result.push_back(std::move(config));
        }
    }
    return result;
}

} // namespace ahfl::backends
