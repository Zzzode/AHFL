#include "compiler/backends/infra/terraform_gen.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "base/support/structured_writer.hpp"

namespace ahfl::backends {

namespace {

void emit_attribute(HclWriter &hcl, std::string_view key, const TerraformAttributeValue &value) {
    std::visit(
        Overloaded{
            [&](const std::string &text) { hcl.attribute(key, text); },
            [&](std::int64_t number) { hcl.attribute_number(key, number); },
            [&](bool flag) { hcl.attribute_bool(key, flag); },
            [&](const std::vector<std::string> &items) { hcl.attribute_string_list(key, items); },
        },
        value);
}

} // namespace

TerraformOutput generate_terraform(const TerraformConfig &config) {
    TerraformOutput output;

    HclWriter hcl;

    // Schema-driven required_providers. The IR carries no deployment target
    // declarations, so an empty/unconfigured provider set omits the whole
    // terraform block instead of fabricating requirements.
    if (!config.providers.empty()) {
        hcl.begin_block("terraform");
        hcl.begin_block("required_providers");
        for (const auto &provider : config.providers) {
            if (provider.name.empty() || provider.source.empty()) {
                continue;
            }
            hcl.begin_object(provider.name);
            hcl.attribute("source", provider.source);
            if (!provider.version.empty()) {
                hcl.attribute("version", provider.version);
            }
            hcl.end_block();
        }
        hcl.end_block(); // required_providers
        hcl.end_block(); // terraform

        // Provider configuration blocks.
        for (const auto &provider : config.providers) {
            if (provider.name.empty() || provider.source.empty()) {
                continue;
            }
            hcl.begin_block("provider", provider.name).end_block();
        }
    }

    // Emit resource blocks
    for (const auto &resource : config.resources) {
        hcl.begin_block("resource", resource.resource_type, resource.resource_name);
        for (const auto &[key, value] : resource.attributes) {
            emit_attribute(hcl, key, value);
        }
        if (!resource.depends_on.empty()) {
            std::vector<std::string> references;
            references.reserve(resource.depends_on.size());
            for (const auto &dependency : resource.depends_on) {
                references.emplace_back(resource.resource_type + "." + dependency);
            }
            hcl.reference_list("depends_on", references);
        }
        hcl.end_block();
    }

    output.hcl = hcl.str();
    return output;
}

} // namespace ahfl::backends
