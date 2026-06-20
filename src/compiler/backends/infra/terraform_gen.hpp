#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ahfl::backends {

struct TerraformResource {
    std::string resource_type;
    std::string resource_name;
    std::vector<std::pair<std::string, std::string>> attributes;
};

struct TerraformConfig {
    std::string workflow_name;
    std::vector<TerraformResource> resources;
    std::vector<std::string> providers;
};

struct TerraformOutput {
    std::string hcl;
};

[[nodiscard]] TerraformOutput generate_terraform(const TerraformConfig &config);

} // namespace ahfl::backends
