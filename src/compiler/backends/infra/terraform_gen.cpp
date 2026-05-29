#include "compiler/backends/infra/terraform_gen.hpp"

#include "base/support/structured_writer.hpp"

namespace ahfl::backends {

TerraformOutput generate_terraform(const TerraformConfig& config) {
    TerraformOutput output;

    HclWriter hcl;

    // Emit provider blocks
    for (const auto& provider : config.providers) {
        hcl.begin_block("provider", provider)
            .end_block();
    }

    // Emit resource blocks
    for (const auto& resource : config.resources) {
        hcl.begin_block("resource", resource.resource_type, resource.resource_name);
        for (const auto& [key, value] : resource.attributes) {
            hcl.attribute(key, value);
        }
        hcl.end_block();
    }

    output.hcl = hcl.str();
    return output;
}

} // namespace ahfl::backends
