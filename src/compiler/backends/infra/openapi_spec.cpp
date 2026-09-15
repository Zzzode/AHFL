#include "compiler/backends/infra/openapi_spec.hpp"

#include <sstream>

#include "base/support/json.hpp"

namespace ahfl::backends {

namespace {

class OpenApiJsonWriter : private PrettyJsonWriter {
  public:
    explicit OpenApiJsonWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    void emit(const OpenApiConfig &config) {
        print_object(0, [&](const auto &field) {
            field("openapi", [&]() { write_string("3.0.0"); });
            field("info", [&]() {
                print_object(1, [&](const auto &f) {
                    f("title", [&]() { write_string(config.title); });
                    f("version", [&]() { write_string(config.version); });
                    if (!config.description.empty()) {
                        f("description", [&]() { write_string(config.description); });
                    }
                });
            });
            field("paths", [&]() {
                print_object(1, [&](const auto &path_field) {
                    for (const auto &endpoint : config.endpoints) {
                        path_field(endpoint.path, [&]() {
                            print_object(2, [&](const auto &method_field) {
                                method_field(endpoint.method, [&]() {
                                    print_object(3, [&](const auto &ef) {
                                        ef("summary", [&]() { write_string(endpoint.summary); });
                                        ef("operationId",
                                           [&]() { write_string(endpoint.operation_id); });
                                        // A zero-parameter capability maps to an
                                        // empty request schema; omit requestBody
                                        // entirely rather than demanding a JSON
                                        // body for an operation that takes none.
                                        if (endpoint.request_schema.has_content()) {
                                            ef("requestBody", [&]() {
                                                print_object(4, [&](const auto &rf) {
                                                    rf("required", [&]() { write_bool(true); });
                                                    rf("content", [&]() {
                                                        print_object(5, [&](const auto &cf) {
                                                            cf("application/json", [&]() {
                                                                print_object(6, [&](const auto &jf) {
                                                                    jf("schema", [&]() {
                                                                        write_schema(
                                                                            7,
                                                                            endpoint
                                                                                .request_schema);
                                                                    });
                                                                });
                                                            });
                                                        });
                                                    });
                                                });
                                            });
                                        }
                                        ef("responses", [&]() {
                                            print_object(4, [&](const auto &rf) {
                                                rf("200", [&]() {
                                                    print_object(5, [&](const auto &sf) {
                                                        sf("description", [&]() {
                                                            write_string("Successful invocation");
                                                        });
                                                        // Unit / unmappable return types
                                                        // map to an empty schema: the
                                                        // operation returns 200 with no
                                                        // content instead of an empty
                                                        // JSON object body.
                                                        if (endpoint.response_schema
                                                                .has_content()) {
                                                            sf("content", [&]() {
                                                                print_object(6, [&](const auto &cf) {
                                                                    cf("application/json", [&]() {
                                                                        print_object(
                                                                            7, [&](const auto &jf) {
                                                                                jf("schema", [&]() {
                                                                                    write_schema(
                                                                                        8,
                                                                                        endpoint
                                                                                            .response_schema);
                                                                                });
                                                                            });
                                                                    });
                                                                });
                                                            });
                                                        }
                                                    });
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                        });
                    }
                });
            });
            if (!config.components.empty()) {
                field("components", [&]() {
                    print_object(1, [&](const auto &cf) {
                        cf("schemas", [&]() {
                            print_object(2, [&](const auto &sf) {
                                for (const auto &[name, schema] : config.components) {
                                    sf(name, [&]() { write_schema(3, schema); });
                                }
                            });
                        });
                    });
                });
            }
        });
        out_ << '\n';
    }

  private:
    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_number(std::int64_t value) {
        out_ << value;
    }

    void write_schema(int level, const OpenApiSchema &schema) {
        print_object(level, [&](const auto &field) {
            if (!schema.ref.empty()) {
                field("$ref", [&]() { write_string(schema.ref); });
                return;
            }
            if (!schema.type.empty()) {
                field("type", [&]() { write_string(schema.type); });
            }
            if (!schema.format.empty()) {
                field("format", [&]() { write_string(schema.format); });
            }
            if (!schema.description.empty()) {
                field("description", [&]() { write_string(schema.description); });
            }
            if (!schema.enum_values.empty()) {
                field("enum", [&]() {
                    print_array(level + 1, [&](const auto &item) {
                        for (const auto &value : schema.enum_values) {
                            item([&]() { write_string(value); });
                        }
                    });
                });
            }
            if (schema.minimum.has_value()) {
                field("minimum", [&]() { write_number(*schema.minimum); });
            }
            if (schema.maximum.has_value()) {
                field("maximum", [&]() { write_number(*schema.maximum); });
            }
            if (schema.min_length.has_value()) {
                field("minLength", [&]() { write_number(*schema.min_length); });
            }
            if (schema.max_length.has_value()) {
                field("maxLength", [&]() { write_number(*schema.max_length); });
            }
            if (schema.min_items.has_value()) {
                field("minItems", [&]() { write_number(*schema.min_items); });
            }
            if (schema.max_items.has_value()) {
                field("maxItems", [&]() { write_number(*schema.max_items); });
            }
            if (schema.unique_items) {
                field("uniqueItems", [&]() { write_bool(true); });
            }
            if (schema.max_properties.has_value()) {
                field("maxProperties", [&]() { write_number(*schema.max_properties); });
            }
            if (schema.items != nullptr) {
                field("items", [&]() { write_schema(level + 1, *schema.items); });
            }
            if (!schema.tuple_items.empty()) {
                field("items", [&]() {
                    print_array(level + 1, [&](const auto &item) {
                        for (const auto &component : schema.tuple_items) {
                            item([&]() { write_schema(level + 2, *component); });
                        }
                    });
                });
            }
            if (!schema.one_of.empty()) {
                field("oneOf", [&]() {
                    print_array(level + 1, [&](const auto &item) {
                        for (const auto &alternative : schema.one_of) {
                            item([&]() { write_schema(level + 2, *alternative); });
                        }
                    });
                });
            }
            if (schema.additional_properties != nullptr) {
                field("additionalProperties",
                      [&]() { write_schema(level + 1, *schema.additional_properties); });
            }
            if (!schema.properties.empty()) {
                field("properties", [&]() {
                    print_object(level + 1, [&](const auto &pf) {
                        for (const auto &[name, property] : schema.properties) {
                            pf(name, [&]() { write_schema(level + 2, property); });
                        }
                    });
                });
            }
            if (!schema.required.empty()) {
                field("required", [&]() {
                    print_array(level + 1, [&](const auto &item) {
                        for (const auto &name : schema.required) {
                            item([&]() { write_string(name); });
                        }
                    });
                });
            }
        });
    }
};

} // namespace

OpenApiOutput generate_openapi(const OpenApiConfig &config) {
    OpenApiOutput output;

    std::ostringstream oss;
    OpenApiJsonWriter writer(oss);
    writer.emit(config);
    output.json = oss.str();

    return output;
}

} // namespace ahfl::backends
