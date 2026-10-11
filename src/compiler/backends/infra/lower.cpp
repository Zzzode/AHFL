#include "compiler/backends/infra/lower.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "compiler/backends/infra/k8s_naming.hpp"
#include "compiler/backends/infra/type_schema.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl::backends {

namespace {

namespace ir = ahfl::ir;

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

[[nodiscard]] std::string last_segment(std::string_view qualified) {
    const auto pos = qualified.rfind("::");
    return pos == std::string_view::npos ? std::string(qualified)
                                         : std::string(qualified.substr(pos + 2));
}

// RFC 3986 unreserved set; every other byte of a path segment is percent
// encoded. AHFL identifiers only use unreserved bytes today, so encoding is
// the principled fallback rather than a fixture-driven substitution.
[[nodiscard]] std::string percent_encode_segment(std::string_view segment) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(segment.size());
    for (const unsigned char ch : segment) {
        const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
                                ch == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(kHex[(ch >> 4U) & 0x0FU]);
            out.push_back(kHex[ch & 0x0FU]);
        }
    }
    return out;
}

// Turn a canonical "::"-qualified name into a URI-safe absolute path whose
// segments are individually percent encoded.
[[nodiscard]] std::string uri_path(std::string_view canonical) {
    std::string path;
    std::size_t begin = 0;
    while (begin <= canonical.size()) {
        const auto end = canonical.find("::", begin);
        const auto segment_view = canonical.substr(
            begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        path += '/';
        path += percent_encode_segment(segment_view);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 2;
    }
    return path;
}

// OpenAPI operationIds must be stable across identical compilations and use
// only tooling-friendly identifier bytes.
[[nodiscard]] std::string operation_id(std::string_view method, std::string_view canonical) {
    std::string id = std::string(method) + "_";
    for (const unsigned char ch : canonical) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '_';
        id.push_back(safe ? static_cast<char>(ch) : '_');
    }
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return id;
}

// A capability with a single parameter exposes that parameter type as the
// request body; multiple parameters become an anonymous object schema.
[[nodiscard]] OpenApiSchema request_body_schema(OpenApiTypeMapper &mapper,
                                                const ir::CapabilityDecl &capability) {
    if (capability.params.empty()) {
        return {};
    }
    if (capability.params.size() == 1) {
        return mapper.map_type(capability.params.front().type_ref);
    }
    OpenApiSchema object;
    object.type = "object";
    object.properties.reserve(capability.params.size());
    for (const auto &param : capability.params) {
        object.properties.emplace_back(param.name, mapper.map_type(param.type_ref));
        object.required.push_back(param.name);
    }
    return object;
}

} // namespace

std::expected<std::vector<K8sCrdConfig>, std::string>
lower_k8s_crd(const ir::AhflIr &program) {
    std::vector<K8sCrdConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            K8sCrdConfig config;
            config.agent_name = agent->name;
            config.short_name =
                std::string(ir::symbol_display_name(agent->symbol_ref, last_segment(agent->name)));
            config.module_name = agent->symbol_ref.module_name;
            config.states = agent->states;
            config.initial_state = agent->initial_state;
            config.final_states = agent->final_states;
            config.capabilities = symbol_names(agent->capability_refs);

            // KR7.4: map the agent's port types to real JSON Schema trees via
            // the shared OpenApiTypeMapper. Scalar types render inline;
            // struct/enum types render as $ref to the definitions table.
            // Fail closed on unresolved nominal types (dangling $refs).
            OpenApiTypeMapper mapper(program);
            config.input_schema = mapper.map_type(agent->input_type_ref);
            config.context_schema = mapper.map_type(agent->context_type_ref);
            config.output_schema = mapper.map_type(agent->output_type_ref);
            if (!mapper.errors().empty()) {
                return std::unexpected("K8s CRD lowering failed: " + mapper.errors().front());
            }
            config.definitions = mapper.release_components();

            result.push_back(std::move(config));
        }
    }
    return result;
}

std::expected<std::optional<OpenApiConfig>, std::string> lower_openapi(const ir::AhflIr &program) {
    OpenApiConfig config;
    config.title = "AHFL Generated API";
    OpenApiTypeMapper mapper(program);
    for (const auto &decl : program.declarations) {
        if (const auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            OpenApiEndpoint endpoint;
            endpoint.path = uri_path(cap->name);
            endpoint.method = "post";
            endpoint.summary = "Invoke capability " + cap->name;
            endpoint.operation_id = operation_id(endpoint.method, cap->name);
            endpoint.request_schema = request_body_schema(mapper, *cap);
            endpoint.response_schema = mapper.map_type(cap->return_type_ref);
            config.endpoints.push_back(std::move(endpoint));
        }
    }
    if (config.endpoints.empty()) {
        return std::nullopt;
    }
    if (!mapper.errors().empty()) {
        // Fail closed: one or more capability types referenced a nominal the
        // program does not declare, which would otherwise render dangling
        // $refs. Surface the first error; the full list is deterministic in
        // declaration order.
        return std::unexpected("OpenAPI lowering failed: " + mapper.errors().front());
    }
    config.components = mapper.release_components();
    return config;
}

std::expected<std::vector<TerraformConfig>, std::string>
lower_terraform(const ir::AhflIr &program) {
    // Build a lookup from agent canonical name to AgentDecl so workflow nodes
    // can resolve their target_ref to the agent's kind and initial state.
    std::unordered_map<std::string, const ir::AgentDecl *> agents;
    agents.reserve(program.declarations.size());
    for (const auto &decl : program.declarations) {
        if (const auto *agent = std::get_if<ir::AgentDecl>(&decl)) {
            agents.emplace(agent->name, agent);
        }
    }

    std::vector<TerraformConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *workflow = std::get_if<ir::WorkflowDecl>(&decl)) {
            TerraformConfig config;
            config.workflow_name = workflow->name;
            // KR7.4: declare the hashicorp/kubernetes provider so
            // `terraform init` installs it and `kubernetes_manifest`
            // resources are usable out of the box.
            config.providers.push_back({"kubernetes", "hashicorp/kubernetes", "~> 2.0"});

            for (const auto &node : workflow->nodes) {
                const auto target_name =
                    std::string(ir::symbol_canonical_name(node.target_ref));
                const auto it = agents.find(target_name);
                if (it == agents.end()) {
                    return std::unexpected(
                        "Terraform lowering failed: workflow node '" + node.name +
                        "' references unknown agent '" + target_name + "'");
                }
                const auto &agent = *it->second;

                TerraformResource resource;
                resource.resource_type = "kubernetes_manifest";
                resource.resource_name = node.name;

                // The manifest identifies the custom resource instance:
                // apiVersion/kind match the CRD the k8s-crd backend emits;
                // metadata.name is the workflow node name (sanitized to an
                // RFC 1123 label); spec.state is the agent's initial state.
                TerraformManifest manifest;
                manifest.api_version = "ahfl.io/v1alpha1";
                manifest.kind = capitalize(last_segment(agent.name));
                manifest.metadata_name = dns_safe(node.name);
                manifest.initial_state = agent.initial_state;
                resource.manifest = std::move(manifest);

                // WorkflowNode.after is the DAG edge set. Sort + dedupe so the
                // depends_on list is deterministic independent of source order.
                resource.depends_on = node.after;
                std::sort(resource.depends_on.begin(), resource.depends_on.end());
                resource.depends_on.erase(
                    std::unique(resource.depends_on.begin(), resource.depends_on.end()),
                    resource.depends_on.end());
                config.resources.push_back(std::move(resource));
            }
            result.push_back(std::move(config));
        }
    }
    return result;
}

} // namespace ahfl::backends
