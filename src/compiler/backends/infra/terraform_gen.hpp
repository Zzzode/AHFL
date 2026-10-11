#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::backends {

// Typed HCL attribute value. The old renderer quoted every value as a string,
// which produced invalid config for numeric/bool/list attributes; the variant
// is visited to emit the correct HCL literal kind.
using TerraformAttributeValue =
    std::variant<std::string, std::int64_t, bool, std::vector<std::string>>;

// Nested manifest object for kubernetes_manifest resources. Carries the
// apiVersion/kind/metadata/spec fields that identify a custom resource
// instance in the cluster.
struct TerraformManifest {
    std::string api_version;   // e.g. "ahfl.io/v1alpha1"
    std::string kind;          // e.g. "ReviewerA"
    std::string metadata_name; // metadata.name
    std::string initial_state; // spec.state
};

struct TerraformResource {
    std::string resource_type;
    std::string resource_name;
    std::vector<std::pair<std::string, TerraformAttributeValue>> attributes;
    // kubernetes_manifest resources carry a nested manifest object; other
    // resource types leave this unset.
    std::optional<TerraformManifest> manifest;
    // Unqualified resource references ("<resource_type>.<dep>"), rendered
    // into depends_on in order.
    std::vector<std::string> depends_on;
};

// Schema-driven provider requirement. Mirrors Terraform's
// terraform.required_providers entries; when `source` is empty the provider is
// considered unconfigured and the renderer omits it (never fabricate one).
struct TerraformProvider {
    std::string name;    // Local provider name used in resource prefixes
    std::string source;  // Registry address, e.g. "hashicorp/aws"
    std::string version; // Optional version constraint
};

struct TerraformConfig {
    std::string workflow_name;
    std::vector<TerraformResource> resources;
    std::vector<TerraformProvider> providers;
};

struct TerraformOutput {
    std::string hcl;
};

[[nodiscard]] TerraformOutput generate_terraform(const TerraformConfig &config);

} // namespace ahfl::backends
