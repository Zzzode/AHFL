#include <cstdio>
#include <string>

#include "ahfl/compiler/ir/ir.hpp"
#include "compiler/backends/infra/k8s_crd.hpp"
#include "compiler/backends/infra/openapi_spec.hpp"
#include "compiler/backends/infra/terraform_gen.hpp"
#include "compiler/backends/infra/type_schema.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name);
    } else {
        std::printf("  FAIL: %s\n", name);
    }
}

static bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

int main() {
    std::printf("=== Target Backends Tests ===\n\n");

    // Test 1: generate_crd emits an RFC 1123 metadata.name, keeps the fully
    // qualified agent name only in annotations, and surfaces the collected
    // capabilities / initial / final markers.
    {
        ahfl::backends::K8sCrdConfig config;
        config.agent_name = "infra::service_mesh::Router";
        config.short_name = "Router";
        config.module_name = "infra::service_mesh";
        config.states = {"Idle", "Done"};
        config.initial_state = "Idle";
        config.final_states = {"Done"};
        config.capabilities = {"infra::service_mesh::Classify"};
        config.input_type = "infra::service_mesh::Request";
        config.context_type = "infra::service_mesh::Context";
        config.output_type = "infra::service_mesh::Response";

        auto output = ahfl::backends::generate_crd(config);

        check(output.kind == "Router", "crd kind is the capitalized short name");
        check(!contains(output.resource_name, "::"),
              "crd metadata.name contains no '::' module separator");
        check(!contains(output.yaml, "name: infra::service_mesh::Routers"),
              "crd drops the invalid fully qualified plural name");
        check(contains(output.yaml, "ahfl.io/agent: \"infra::service_mesh::Router\""),
              "crd keeps the fully qualified agent name in an annotation");
        check(contains(output.yaml, "enum: [Idle, Done]"), "crd enumerates states");
        check(contains(output.yaml, "default: Idle"), "crd marks the initial state");
        check(contains(output.yaml, "default: [infra::service_mesh::Classify]"),
              "crd emits the collected capabilities array");
        check(contains(output.yaml, "input:") && contains(output.yaml, "context:") &&
                  contains(output.yaml, "output:"),
              "crd emits input/context/output schema ports");
    }

    // Test 2: generate_openapi renders requestBody + 200 response and
    // component schemas referenced via $ref.
    {
        ahfl::backends::OpenApiConfig config;
        config.title = "Agent API";
        config.version = "1.0.0";
        config.description = "AHFL Agent REST API";

        ahfl::backends::OpenApiEndpoint endpoint;
        endpoint.path = "/agents/router";
        endpoint.method = "post";
        endpoint.summary = "Invoke Router";
        endpoint.operation_id = "post_router";
        endpoint.request_schema.ref = "#/components/schemas/Request";
        endpoint.response_schema.ref = "#/components/schemas/Response";
        config.endpoints.push_back(std::move(endpoint));

        ahfl::backends::OpenApiSchema request;
        request.type = "object";
        config.components.emplace("Request", std::move(request));
        ahfl::backends::OpenApiSchema response;
        response.type = "object";
        config.components.emplace("Response", std::move(response));

        auto output = ahfl::backends::generate_openapi(config);

        check(contains(output.json, "\"openapi\": \"3.0.0\""),
              "openapi carries the 3.0.0 version field");
        check(contains(output.json, "\"title\": \"Agent API\""), "openapi carries the title");
        check(contains(output.json, "\"/agents/router\""), "openapi carries the percent-safe path");
        check(contains(output.json, "\"requestBody\""), "openapi emits a requestBody");
        check(contains(output.json, "\"200\"") && contains(output.json, "\"responses\""),
              "openapi emits a 200 response");
        check(contains(output.json, "\"$ref\": \"#/components/schemas/Request\""),
              "openapi references the request component");
        check(contains(output.json, "\"components\"") && contains(output.json, "\"schemas\""),
              "openapi emits components/schemas");
    }

    // Test 3: generate_terraform renders typed attributes and depends_on.
    {
        ahfl::backends::TerraformConfig config;
        config.workflow_name = "deploy_agent";
        config.providers = {{"aws", "hashicorp/aws", "~> 5.0"}};

        ahfl::backends::TerraformResource primary;
        primary.resource_type = "ahfl_workflow_node";
        primary.resource_name = "primary";
        primary.attributes.emplace_back("target", std::string{"router"});
        primary.attributes.emplace_back("max_retries", std::int64_t{3});
        primary.attributes.emplace_back("enabled", true);
        primary.attributes.emplace_back("aliases",
                                       std::vector<std::string>{"a", "b"});

        ahfl::backends::TerraformResource secondary = primary;
        secondary.resource_name = "secondary";
        secondary.attributes.clear();
        secondary.attributes.emplace_back("target", std::string{"router"});
        secondary.depends_on = {"primary"};

        config.resources = {std::move(primary), std::move(secondary)};

        auto output = ahfl::backends::generate_terraform(config);

        check(contains(output.hcl, "required_providers"),
              "terraform emits a required_providers block when configured");
        check(contains(output.hcl, "source = \"hashicorp/aws\"") &&
                  contains(output.hcl, "version = \"~> 5.0\""),
              "terraform emits schema-driven provider requirements");
        check(contains(output.hcl, "resource \"ahfl_workflow_node\" \"primary\"") &&
                  contains(output.hcl, "resource \"ahfl_workflow_node\" \"secondary\""),
              "terraform emits one resource block per workflow node");
        check(contains(output.hcl, "max_retries = 3"),
              "terraform emits numeric attributes unquoted");
        check(contains(output.hcl, "enabled = true"), "terraform emits bool attributes unquoted");
        check(contains(output.hcl, "aliases = [\"a\", \"b\"]"),
              "terraform emits string-list attributes");
        check(contains(output.hcl,
                       "depends_on = [ahfl_workflow_node.primary]"),
              "terraform emits the DAG edge as depends_on");
    }

    // Test 4: an unconfigured TerraformConfig omits the terraform block.
    {
        ahfl::backends::TerraformConfig config;
        config.workflow_name = "solo";
        ahfl::backends::TerraformResource resource;
        resource.resource_type = "ahfl_workflow_node";
        resource.resource_name = "only";
        resource.attributes.emplace_back("target", std::string{"agent"});
        config.resources.push_back(std::move(resource));

        auto output = ahfl::backends::generate_terraform(config);

        check(!contains(output.hcl, "required_providers"),
              "terraform omits required_providers when no providers are configured");
        check(!contains(output.hcl, "provider \""),
              "terraform omits provider config blocks when unconfigured");
    }

    // Test 5: OpenApiTypeMapper maps primitives and nested/bounded collections.
    {
        ahfl::ir::Program program;

        ahfl::ir::StructDecl request;
        request.name = "api::Request";
        request.symbol_ref.kind = ahfl::ir::SymbolRefKind::Type;
        request.symbol_ref.canonical_name = "api::Request";
        request.symbol_ref.id = 0;
        {
            ahfl::ir::FieldDecl field;
            field.name = "ready";
            field.type_ref.kind = ahfl::ir::TypeRefKind::Bool;
            request.fields.push_back(std::move(field));
        }
        {
            ahfl::ir::FieldDecl field;
            field.name = "attempts";
            field.type_ref.kind = ahfl::ir::TypeRefKind::BoundedInt;
            field.type_ref.int_bounds = {0, 5};
            request.fields.push_back(std::move(field));
        }
        {
            ahfl::ir::FieldDecl field;
            field.name = "tags";
            field.type_ref.kind = ahfl::ir::TypeRefKind::Struct;
            field.type_ref.canonical_name = "std::collections::List";
            field.type_ref.collection_capacity = 4;
            auto element = ahfl::make_owned<ahfl::ir::TypeRef>();
            element->kind = ahfl::ir::TypeRefKind::String;
            field.type_ref.params.push_back(std::move(element));
            request.fields.push_back(std::move(field));
        }
        program.declarations.emplace_back(std::move(request));

        ahfl::ir::TypeRef request_ref;
        request_ref.kind = ahfl::ir::TypeRefKind::Struct;
        request_ref.canonical_name = "api::Request";
        request_ref.nominal_ref.kind = ahfl::ir::SymbolRefKind::Type;
        request_ref.nominal_ref.canonical_name = "api::Request";
        request_ref.nominal_ref.id = 0;

        ahfl::backends::OpenApiTypeMapper mapper(program);
        const ahfl::backends::OpenApiSchema mapped = mapper.map_type(request_ref);
        const auto components = mapper.release_components();

        check(mapped.ref == "#/components/schemas/api__Request",
              "mapper emits a $ref for a user struct");
        const auto found = components.find("api__Request");
        check(found != components.end() && found->second.type == "object",
              "mapper emits the user struct as an object component");
        if (found != components.end()) {
            const auto &props = found->second.properties;
            const auto by_name = [&](const std::string &name)
                -> const ahfl::backends::OpenApiSchema * {
                for (const auto &[prop_name, schema] : props) {
                    if (prop_name == name)
                        return &schema;
                }
                return nullptr;
            };
            const auto *ready = by_name("ready");
            const auto *attempts = by_name("attempts");
            const auto *tags = by_name("tags");
            check(ready != nullptr && ready->type == "boolean",
                  "mapper maps Bool to boolean");
            check(attempts != nullptr && attempts->type == "integer" &&
                      attempts->minimum == 0 && attempts->maximum == 5,
                  "mapper maps BoundedInt to integer with minimum/maximum");
            check(tags != nullptr && tags->type == "array" && tags->max_items == 4 &&
                      tags->items != nullptr && tags->items->type == "string",
                  "mapper maps bounded List<String> to array with maxItems");
        }
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
