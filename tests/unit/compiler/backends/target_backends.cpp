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
        primary.attributes.emplace_back("aliases", std::vector<std::string>{"a", "b"});

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
        check(contains(output.hcl, "depends_on = [ahfl_workflow_node.primary]"),
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
            const auto by_name =
                [&](const std::string &name) -> const ahfl::backends::OpenApiSchema * {
                for (const auto &[prop_name, schema] : props) {
                    if (prop_name == name)
                        return &schema;
                }
                return nullptr;
            };
            const auto *ready = by_name("ready");
            const auto *attempts = by_name("attempts");
            const auto *tags = by_name("tags");
            check(ready != nullptr && ready->type == "boolean", "mapper maps Bool to boolean");
            check(attempts != nullptr && attempts->type == "integer" && attempts->minimum == 0 &&
                      attempts->maximum == 5,
                  "mapper maps BoundedInt to integer with minimum/maximum");
            check(tags != nullptr && tags->type == "array" && tags->max_items == 4 &&
                      tags->items != nullptr && tags->items->type == "string",
                  "mapper maps bounded List<String> to array with maxItems");
        }
    }

    // Test 6: a C-style nominal enum maps to a string component with the
    // declared variant list; a payload-carrying enum maps to an externally
    // tagged oneOf component.
    {
        ahfl::ir::Program program;

        ahfl::ir::EnumDecl color;
        color.name = "shapes::Color";
        color.symbol_ref.kind = ahfl::ir::SymbolRefKind::Type;
        color.symbol_ref.canonical_name = "shapes::Color";
        color.symbol_ref.id = 10;
        color.variants.push_back(ahfl::ir::EnumVariantDecl{.name = "Red"});
        color.variants.push_back(ahfl::ir::EnumVariantDecl{.name = "Green"});
        color.variants.push_back(ahfl::ir::EnumVariantDecl{.name = "Blue"});
        program.declarations.emplace_back(std::move(color));

        ahfl::ir::EnumDecl result;
        result.name = "shapes::Result";
        result.symbol_ref.kind = ahfl::ir::SymbolRefKind::Type;
        result.symbol_ref.canonical_name = "shapes::Result";
        result.symbol_ref.id = 11;
        {
            ahfl::ir::EnumVariantDecl ok;
            ok.name = "Ok";
            ok.payload_kind = ahfl::ir::EnumVariantPayloadKind::Tuple;
            ahfl::ir::TypeRef payload;
            payload.kind = ahfl::ir::TypeRefKind::String;
            ok.payload.push_back(std::move(payload));
            result.variants.push_back(std::move(ok));
        }
        {
            ahfl::ir::EnumVariantDecl err;
            err.name = "Err";
            err.payload_kind = ahfl::ir::EnumVariantPayloadKind::Unit;
            result.variants.push_back(std::move(err));
        }
        program.declarations.emplace_back(std::move(result));

        ahfl::ir::TypeRef color_ref;
        color_ref.kind = ahfl::ir::TypeRefKind::Enum;
        color_ref.canonical_name = "shapes::Color";
        color_ref.nominal_ref.kind = ahfl::ir::SymbolRefKind::Type;
        color_ref.nominal_ref.canonical_name = "shapes::Color";
        color_ref.nominal_ref.id = 10;

        ahfl::ir::TypeRef result_ref;
        result_ref.kind = ahfl::ir::TypeRefKind::Enum;
        result_ref.canonical_name = "shapes::Result";
        result_ref.nominal_ref.kind = ahfl::ir::SymbolRefKind::Type;
        result_ref.nominal_ref.canonical_name = "shapes::Result";
        result_ref.nominal_ref.id = 11;

        ahfl::backends::OpenApiTypeMapper mapper(program);
        const ahfl::backends::OpenApiSchema color_mapped = mapper.map_type(color_ref);
        const ahfl::backends::OpenApiSchema result_mapped = mapper.map_type(result_ref);
        const auto components = mapper.release_components();

        check(color_mapped.ref == "#/components/schemas/shapes__Color",
              "mapper emits a $ref for a C-style enum");
        const auto color_component = components.find("shapes__Color");
        check(color_component != components.end() && color_component->second.type == "string" &&
                  color_component->second.enum_values.size() == 3 &&
                  color_component->second.enum_values[0] == "Red" &&
                  color_component->second.enum_values[2] == "Blue",
              "mapper renders an enum as a string with the declared variants");

        const auto result_component = components.find("shapes__Result");
        check(result_component != components.end() && result_component->second.one_of.size() == 2 &&
                  result_mapped.ref == "#/components/schemas/shapes__Result",
              "mapper renders a payload enum as a oneOf component");
    }

    // Test 7: unmappable kinds (Unit / Any) produce an empty schema the
    // renderer treats as "no body"; a nominal the program does not declare is
    // a fail-closed lowering error rather than a dangling $ref.
    {
        ahfl::ir::Program program;

        ahfl::ir::TypeRef unit_ref;
        unit_ref.kind = ahfl::ir::TypeRefKind::Unit;
        ahfl::ir::TypeRef ghost_ref;
        ghost_ref.kind = ahfl::ir::TypeRefKind::Struct;
        ghost_ref.canonical_name = "phantom::Missing";
        ghost_ref.nominal_ref.canonical_name = "phantom::Missing";

        ahfl::backends::OpenApiTypeMapper mapper(program);
        const ahfl::backends::OpenApiSchema unit_mapped = mapper.map_type(unit_ref);
        check(!unit_mapped.has_content(), "Unit maps to a contentless (no-body) schema");
        const ahfl::backends::OpenApiSchema ghost_mapped = mapper.map_type(ghost_ref);
        check(!ghost_mapped.has_content() && !mapper.errors().empty(),
              "mapper fails closed on an unresolved struct nominal");
    }

    // Test 8: distinct canonicals that sanitize to one component token get
    // distinct keys instead of silently sharing one component.
    {
        ahfl::ir::Program program;
        for (const char *canonical : {"collide::Type", "collide_Type"}) {
            ahfl::ir::StructDecl structure;
            structure.name = canonical;
            structure.symbol_ref.kind = ahfl::ir::SymbolRefKind::Type;
            structure.symbol_ref.canonical_name = canonical;
            ahfl::ir::FieldDecl field;
            field.name = "mark";
            field.type_ref.kind = ahfl::ir::TypeRefKind::String;
            structure.fields.push_back(std::move(field));
            program.declarations.emplace_back(std::move(structure));
        }

        ahfl::ir::TypeRef a;
        a.kind = ahfl::ir::TypeRefKind::Struct;
        a.canonical_name = "collide::Type";
        a.nominal_ref.canonical_name = "collide::Type";
        ahfl::ir::TypeRef b;
        b.kind = ahfl::ir::TypeRefKind::Struct;
        b.canonical_name = "collide_Type";
        b.nominal_ref.canonical_name = "collide_Type";

        ahfl::backends::OpenApiTypeMapper mapper(program);
        const ahfl::backends::OpenApiSchema mapped_a = mapper.map_type(a);
        const ahfl::backends::OpenApiSchema mapped_b = mapper.map_type(b);
        const auto components = mapper.release_components();

        check(mapped_a.ref != mapped_b.ref, "colliding canonicals get distinct component keys");
        check(components.size() == 2, "both colliding structs publish their own component");
    }

    // Test 9: the CRD generator omits a context placeholder for an agent with
    // no context port, and keeps metadata.name within the 253-byte RFC 1123
    // subdomain limit even for an extremely long agent name.
    {
        ahfl::backends::K8sCrdConfig config;
        config.agent_name = "infra::long::Bare";
        config.short_name = "Bare";
        config.module_name = "infra::long";
        config.states = {"Idle", "Done"};
        config.initial_state = "Idle";
        config.final_states = {"Done"};
        // Empty context: stateless agent (omitted `context` clause).
        config.input_type = "infra::long::Request";
        config.context_type = "";
        config.output_type = "infra::long::Response";

        auto output = ahfl::backends::generate_crd(config);

        check(contains(output.yaml, "input:") && contains(output.yaml, "output:"),
              "crd still emits declared input/output ports");
        check(!contains(output.yaml, "context:"),
              "crd omits the context port when the agent declares none");
        check(output.resource_name.size() <= 253, "crd metadata.name is within 253 bytes");
        check(output.plural.size() <= 63, "crd plural resource name is a 63-byte DNS label");
    }
    {
        ahfl::backends::K8sCrdConfig config;
        const std::string long_name(244, 'A');
        config.agent_name = "infra::long::" + long_name;
        config.short_name = long_name;
        config.module_name = "infra::long";
        config.states = {"Idle", "Done"};
        config.initial_state = "Idle";
        config.final_states = {"Done"};
        config.input_type = "infra::long::Request";
        config.context_type = "infra::long::Context";
        config.output_type = "infra::long::Response";

        auto output = ahfl::backends::generate_crd(config);

        check(output.resource_name.size() <= 253,
              "long crd metadata.name is clamped to 253 bytes (not 261)");
        check(output.plural.size() <= 63, "long crd plural is clamped to a 63-byte label");
        check(output.resource_name == output.plural + "." + config.api_group,
              "crd metadata.name stays plural + '.' + apiGroup");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
