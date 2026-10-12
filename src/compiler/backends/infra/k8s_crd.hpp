#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/backends/infra/openapi_spec.hpp"

namespace ahfl::backends {

struct K8sCrdConfig {
    // Fully qualified canonical name ("::" separated). Used for annotations /
    // labels only; never embedded verbatim in RFC 1123 fields.
    std::string agent_name;
    // Unqualified source name, e.g. "Router" from infra::service_mesh::Router.
    std::string short_name;
    std::string module_name;
    std::string api_group = "ahfl.io";
    std::string api_version = "v1alpha1";
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<std::string> capabilities;
    // Port schemas are projected to the CRD structural subset before emission.
    // Empty context denotes the omitted Unit context port only.
    OpenApiSchema input_schema;
    OpenApiSchema context_schema;
    OpenApiSchema output_schema;
    // Transient mapper components used for local reference expansion. This
    // table is never emitted: Kubernetes forbids definitions and $ref.
    std::map<std::string, OpenApiSchema> components;
};

struct K8sCrdOutput {
    std::string yaml;
    std::string kind;
    // Composed metadata.name: plural + "." + apiGroup; a DNS subdomain capped
    // at 253 bytes.
    std::string resource_name;
    // RFC 1123-compliant plural resource name: a 63-byte DNS label carrying a
    // deterministic qualified-name hash suffix.
    std::string plural;
};

// Expand mapper references and reject types without a faithful structural
// representation. Errors identify the port/field path for CLI diagnostics.
[[nodiscard]] std::expected<OpenApiSchema, std::string>
project_crd_schema(const OpenApiSchema &schema,
                   const std::map<std::string, OpenApiSchema> &components,
                   std::string_view path);

[[nodiscard]] K8sCrdOutput generate_crd(const K8sCrdConfig &config);

} // namespace ahfl::backends
