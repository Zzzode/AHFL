#include "ahfl/backends/k8s_crd.hpp"

#include <algorithm>
#include <cctype>

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

    std::string yaml;
    yaml += "apiVersion: apiextensions.k8s.io/v1\n";
    yaml += "kind: CustomResourceDefinition\n";
    yaml += "metadata:\n";
    yaml += "  name: " + output.resource_name + "\n";
    yaml += "spec:\n";
    yaml += "  group: " + config.api_group + "\n";
    yaml += "  versions:\n";
    yaml += "    - name: " + config.api_version + "\n";
    yaml += "      served: true\n";
    yaml += "      storage: true\n";
    yaml += "      schema:\n";
    yaml += "        openAPIV3Schema:\n";
    yaml += "          type: object\n";
    yaml += "          properties:\n";
    yaml += "            spec:\n";
    yaml += "              type: object\n";
    yaml += "              properties:\n";
    yaml += "                state:\n";
    yaml += "                  type: string\n";
    if (!config.states.empty()) {
        yaml += "                  enum: [";
        for (std::size_t i = 0; i < config.states.size(); ++i) {
            if (i > 0) yaml += ", ";
            yaml += config.states[i];
        }
        yaml += "]\n";
    }
    yaml += "  scope: Namespaced\n";
    yaml += "  names:\n";
    yaml += "    plural: " + config.agent_name + "s\n";
    yaml += "    singular: " + config.agent_name + "\n";
    yaml += "    kind: " + output.kind + "\n";

    output.yaml = yaml;
    return output;
}

} // namespace ahfl::backends
