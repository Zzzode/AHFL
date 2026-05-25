#include <cstdio>
#include <string>

#include "backends/k8s_crd.hpp"
#include "backends/openapi_spec.hpp"
#include "backends/terraform_gen.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("=== Target Backends Tests ===\n\n");

    // Test 1: generate_crd produces YAML with correct apiVersion and kind
    {
        ahfl::backends::K8sCrdConfig config;
        config.agent_name = "router";
        config.states = {"idle", "active", "terminated"};
        config.capabilities = {"http", "grpc"};

        auto output = ahfl::backends::generate_crd(config);

        bool has_api_version = output.yaml.find("apiVersion: apiextensions.k8s.io/v1") != std::string::npos;
        bool has_kind = output.yaml.find("kind: CustomResourceDefinition") != std::string::npos;
        bool has_agent_kind = output.kind == "Router";
        bool has_states = output.yaml.find("enum: [idle, active, terminated]") != std::string::npos;

        check(has_api_version && has_kind && has_agent_kind && has_states,
              "generate_crd produces YAML with correct apiVersion and kind");
    }

    // Test 2: generate_openapi produces JSON with openapi 3.0 version field
    {
        ahfl::backends::OpenApiConfig config;
        config.title = "Agent API";
        config.version = "1.0.0";
        config.description = "AHFL Agent REST API";
        config.endpoints = {{"/agents", "get", "List agents", "", ""}};

        auto output = ahfl::backends::generate_openapi(config);

        bool has_openapi_version = output.json.find("\"openapi\": \"3.0.0\"") != std::string::npos;
        bool has_title = output.json.find("\"title\": \"Agent API\"") != std::string::npos;
        bool has_path = output.json.find("\"/agents\"") != std::string::npos;

        check(has_openapi_version && has_title && has_path,
              "generate_openapi produces JSON with openapi 3.0 version field");
    }

    // Test 3: generate_terraform produces HCL with resource blocks
    {
        ahfl::backends::TerraformConfig config;
        config.workflow_name = "deploy_agent";
        config.providers = {"aws"};
        config.resources = {{
            "aws_lambda_function",
            "agent_handler",
            {{"runtime", "provided.al2"}, {"handler", "bootstrap"}}
        }};

        auto output = ahfl::backends::generate_terraform(config);

        bool has_provider = output.hcl.find("provider \"aws\"") != std::string::npos;
        bool has_resource = output.hcl.find("resource \"aws_lambda_function\" \"agent_handler\"") != std::string::npos;
        bool has_attr = output.hcl.find("runtime = \"provided.al2\"") != std::string::npos;

        check(has_provider && has_resource && has_attr,
              "generate_terraform produces HCL with resource blocks");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
