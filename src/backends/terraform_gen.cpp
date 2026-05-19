#include "ahfl/backends/terraform_gen.hpp"

namespace ahfl::backends {

TerraformOutput generate_terraform(const TerraformConfig& config) {
    TerraformOutput output;

    std::string hcl;

    // Emit providers
    for (const auto& provider : config.providers) {
        hcl += "provider \"" + provider + "\" {\n";
        hcl += "}\n\n";
    }

    // Emit resources
    for (const auto& resource : config.resources) {
        hcl += "resource \"" + resource.resource_type + "\" \"" + resource.resource_name + "\" {\n";
        for (const auto& [key, value] : resource.attributes) {
            hcl += "  " + key + " = \"" + value + "\"\n";
        }
        hcl += "}\n\n";
    }

    output.hcl = hcl;
    return output;
}

} // namespace ahfl::backends
