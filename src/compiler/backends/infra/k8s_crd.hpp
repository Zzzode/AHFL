#pragma once

#include <map>
#include <string>
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
    // KR7.4: real JSON Schema trees for the agent's input/context/output ports,
    // mapped from the IR TypeRef via OpenApiTypeMapper. Scalar types render
    // inline; struct/enum types render as $ref to the definitions table below.
    // A schema without content (Unit / Any / Fn / Never / Unresolved) causes
    // the port to be omitted entirely, matching the previous empty-canonical
    // behavior.
    OpenApiSchema input_schema;
    OpenApiSchema context_schema;
    OpenApiSchema output_schema;
    // definitions/<key> entries for struct/enum types referenced by $ref from
    // the port schemas. Sorted by key for deterministic output.
    std::map<std::string, OpenApiSchema> definitions;
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

[[nodiscard]] K8sCrdOutput generate_crd(const K8sCrdConfig &config);

} // namespace ahfl::backends
