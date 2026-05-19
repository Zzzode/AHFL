#include "ahfl/backends/openapi_spec.hpp"

namespace ahfl::backends {

OpenApiOutput generate_openapi(const OpenApiConfig& config) {
    OpenApiOutput output;

    std::string json;
    json += "{\n";
    json += "  \"openapi\": \"3.0.0\",\n";
    json += "  \"info\": {\n";
    json += "    \"title\": \"" + config.title + "\",\n";
    json += "    \"version\": \"" + config.version + "\",\n";
    json += "    \"description\": \"" + config.description + "\"\n";
    json += "  },\n";
    json += "  \"paths\": {\n";

    for (std::size_t i = 0; i < config.endpoints.size(); ++i) {
        const auto& ep = config.endpoints[i];
        json += "    \"" + ep.path + "\": {\n";
        json += "      \"" + ep.method + "\": {\n";
        json += "        \"summary\": \"" + ep.summary + "\",\n";
        json += "        \"responses\": {\n";
        json += "          \"200\": {\n";
        json += "            \"description\": \"Success\"\n";
        json += "          }\n";
        json += "        }\n";
        json += "      }\n";
        json += "    }";
        if (i + 1 < config.endpoints.size()) {
            json += ",";
        }
        json += "\n";
    }

    json += "  }\n";
    json += "}\n";

    output.json = json;
    return output;
}

} // namespace ahfl::backends
