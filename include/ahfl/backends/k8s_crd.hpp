#pragma once

#include <string>
#include <vector>

namespace ahfl::backends {

struct K8sCrdConfig {
    std::string agent_name;
    std::string api_group = "ahfl.io";
    std::string api_version = "v1alpha1";
    std::vector<std::string> states;
    std::vector<std::string> capabilities;
};

struct K8sCrdOutput {
    std::string yaml;
    std::string kind;
    std::string resource_name;
};

[[nodiscard]] K8sCrdOutput generate_crd(const K8sCrdConfig& config);

} // namespace ahfl::backends
