#pragma once

#include <string>
#include <vector>

namespace ahfl::backends {

struct OpenApiEndpoint {
    std::string path;
    std::string method;
    std::string summary;
    std::string request_schema;
    std::string response_schema;
};

struct OpenApiConfig {
    std::string title;
    std::string version = "1.0.0";
    std::string description;
    std::vector<OpenApiEndpoint> endpoints;
};

struct OpenApiOutput {
    std::string json;
};

[[nodiscard]] OpenApiOutput generate_openapi(const OpenApiConfig &config);

} // namespace ahfl::backends
