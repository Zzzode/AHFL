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
                        path_field(endpoint.path.c_str(), [&]() {
                            print_object(2, [&](const auto &method_field) {
                                method_field(endpoint.method.c_str(), [&]() {
                                    print_object(3, [&](const auto &ef) {
                                        ef("summary", [&]() { write_string(endpoint.summary); });
                                        if (!endpoint.request_schema.empty()) {
                                            ef("requestBody", [&]() {
                                                print_object(4, [&](const auto &rf) {
                                                    rf("schema", [&]() { write_string(endpoint.request_schema); });
                                                });
                                            });
                                        }
                                        if (!endpoint.response_schema.empty()) {
                                            ef("responses", [&]() {
                                                print_object(4, [&](const auto &rf) {
                                                    rf("200", [&]() {
                                                        print_object(5, [&](const auto &sf) {
                                                            sf("schema", [&]() { write_string(endpoint.response_schema); });
                                                        });
                                                    });
                                                });
                                            });
                                        }
                                    });
                                });
                            });
                        });
                    }
                });
            });
        });
        out_ << '\n';
    }
};

} // namespace

OpenApiOutput generate_openapi(const OpenApiConfig& config) {
    OpenApiOutput output;

    std::ostringstream oss;
    OpenApiJsonWriter writer(oss);
    writer.emit(config);
    output.json = oss.str();

    return output;
}

} // namespace ahfl::backends
