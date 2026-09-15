#pragma once

#include <string>
#include <vector>

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
    // Canonical type names of the agent's input/context/output ports. The CRD
    // schema emits object properties named input/context/output keyed to the
    // OpenAPI components published alongside (placeholders until a shared
    // schema export lands).
    std::string input_type;
    std::string context_type;
    std::string output_type;
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
