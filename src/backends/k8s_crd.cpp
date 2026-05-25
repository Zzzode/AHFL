#include "backends/k8s_crd.hpp"

#include "support/structured_writer.hpp"

#include <cctype>
#include <cstddef>

namespace ahfl::backends {

namespace {

std::string capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string result = s;
    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
    return result;
}

} // anonymous namespace

K8sCrdOutput generate_crd(const K8sCrdConfig& config) {
    K8sCrdOutput output;
    output.kind = capitalize(config.agent_name);
    output.resource_name = config.agent_name + "s." + config.api_group;

    YamlWriter yaml;
    yaml.key_value("apiVersion", "apiextensions.k8s.io/v1")
        .key_value("kind", "CustomResourceDefinition")
        .begin_mapping("metadata")
            .key_value("name", output.resource_name)
        .end_mapping()
        .begin_mapping("spec")
            .key_value("group", config.api_group)
            .begin_mapping("versions")
                .begin_list_item()
                    .key_value("name", config.api_version)
                    .key_value_bool("served", true)
                    .key_value_bool("storage", true)
                    .begin_mapping("schema")
                        .begin_mapping("openAPIV3Schema")
                            .key_value("type", "object")
                            .begin_mapping("properties")
                                .begin_mapping("spec")
                                    .key_value("type", "object")
                                    .begin_mapping("properties")
                                        .begin_mapping("state")
                                            .key_value("type", "string");

    if (!config.states.empty()) {
        yaml.key_inline_list("enum", config.states);
    }

    yaml                                    .end_mapping() // state
                                    .end_mapping() // properties
                                .end_mapping() // spec
                            .end_mapping() // properties
                        .end_mapping() // openAPIV3Schema
                    .end_mapping() // schema
                .end_list_item() // versions item
            .end_mapping() // versions
            .key_value("scope", "Namespaced")
            .begin_mapping("names")
                .key_value("plural", config.agent_name + "s")
                .key_value("singular", config.agent_name)
                .key_value("kind", output.kind)
            .end_mapping() // names
        .end_mapping(); // spec

    output.yaml = yaml.str();
    return output;
}

} // namespace ahfl::backends
