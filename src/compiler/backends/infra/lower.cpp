#include "compiler/backends/infra/lower.hpp"

#include "ahfl/compiler/ir/identity.hpp"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl::backends {

namespace {

[[nodiscard]] std::vector<std::string> symbol_names(const std::vector<ir::SymbolRef> &refs) {
    std::vector<std::string> names;
    names.reserve(refs.size());
    for (const auto &ref : refs) {
        const auto name = ir::symbol_canonical_name(ref);
        if (!name.empty()) {
            names.emplace_back(name);
        }
    }
    return names;
}

// RFC 0019 slice 2: map the IR capability effect kind to the backend-local
// mirror used by the effect -> WASI projection.
[[nodiscard]] WasmCapabilityEffect to_wasm_effect(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Read:
        return WasmCapabilityEffect::Read;
    case ir::CapabilityEffectKind::ExternalSideEffect:
        return WasmCapabilityEffect::ExternalSideEffect;
    case ir::CapabilityEffectKind::DurableWrite:
        return WasmCapabilityEffect::DurableWrite;
    case ir::CapabilityEffectKind::FinancialWrite:
        return WasmCapabilityEffect::FinancialWrite;
    case ir::CapabilityEffectKind::Unknown:
        return WasmCapabilityEffect::Unknown;
    }
    return WasmCapabilityEffect::Unknown;
}

// Build a canonical-name -> effect kind index over the program's capability
// declarations, so an agent's capability refs can be resolved to their effect.
[[nodiscard]] std::unordered_map<std::string, WasmCapabilityEffect>
build_capability_effects(const ir::Program &program) {
    std::unordered_map<std::string, WasmCapabilityEffect> effects;
    for (const auto &decl : program.declarations) {
        if (const auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            const auto name = ir::symbol_canonical_name(cap->symbol_ref);
            if (!name.empty()) {
                effects.emplace(std::string(name), to_wasm_effect(cap->effect.kind));
            }
        }
    }
    return effects;
}

} // namespace

std::vector<K8sCrdConfig> lower_k8s_crd(const ir::Program &program) {
    std::vector<K8sCrdConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            K8sCrdConfig config;
            config.agent_name = agent->name;
            config.states = agent->states;
            config.capabilities = symbol_names(agent->capability_refs);
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
                resource.attributes.emplace_back("target",
                                                 ir::symbol_canonical_name(node.target_ref));
                config.resources.push_back(std::move(resource));
            }
            result.push_back(std::move(config));
        }
    }
    return result;
}

std::vector<WasmAgentConfig> lower_wasm(const ir::Program &program) {
    std::vector<WasmAgentConfig> result;
    const auto capability_effects = build_capability_effects(program);
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            WasmAgentConfig config;
            config.agent_name = agent->name;
            config.states = agent->states;
            for (const auto &t : agent->transitions) {
                config.transitions.emplace_back(t.from_state, t.to_state);
            }
            config.capabilities = symbol_names(agent->capability_refs);
            // Resolve each capability's effect kind (parallel to capabilities),
            // defaulting to Unknown (conservative) when the decl is not found.
            config.capability_effects.reserve(config.capabilities.size());
            for (const auto &cap_name : config.capabilities) {
                const auto it = capability_effects.find(cap_name);
                config.capability_effects.push_back(
                    it != capability_effects.end() ? it->second : WasmCapabilityEffect::Unknown);
            }
            result.push_back(std::move(config));
        }
    }
    return result;
}

} // namespace ahfl::backends
