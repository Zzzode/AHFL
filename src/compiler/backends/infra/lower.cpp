#include "compiler/backends/infra/lower.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "compiler/backends/infra/type_schema.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
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

std::vector<K8sCrdConfig> lower_k8s_crd(const ir::AhflIr &program) {
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
            config.input_type = agent->input_type_ref.canonical_name;
            config.context_type = agent->context_type_ref.canonical_name;
            config.output_type = agent->output_type_ref.canonical_name;
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

std::vector<TerraformConfig> lower_terraform(const ir::AhflIr &program) {
    std::vector<TerraformConfig> result;
    for (const auto &decl : program.declarations) {
        if (const auto *workflow = std::get_if<ir::WorkflowDecl>(&decl)) {
            TerraformConfig config;
            config.workflow_name = workflow->name;
            // Providers are intentionally left empty: the AHFL IR carries no
            // deployment-target declarations, so the renderer omits the
            // terraform{required_providers} block rather than fabricating one.
            for (const auto &node : workflow->nodes) {
                TerraformResource resource;
                resource.resource_type = "ahfl_workflow_node";
                resource.resource_name = node.name;
                resource.attributes.emplace_back(
                    "target", std::string(ir::symbol_canonical_name(node.target_ref)));
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
