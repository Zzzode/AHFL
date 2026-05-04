#include "ahfl/backends/durable_store_import_provider_registry.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class RegistryJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit RegistryJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderRegistry &registry) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(registry.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(registry.workflow_canonical_name); });
            field("session_id", [&]() { write_string(registry.session_id); });
            field("run_id", [&]() { print_optional_string(registry.run_id); });
            field("input_fixture", [&]() { write_string(registry.input_fixture); });
            field("registry_identity", [&]() {
                write_string(registry.durable_store_import_provider_registry_identity);
            });
            field("primary_provider_id", [&]() { write_string(registry.primary_provider_id); });
            field("fallback_provider_id", [&]() { write_string(registry.fallback_provider_id); });
            field("mock_adapter_registered",
                  [&]() { out_ << (registry.mock_adapter_registered ? "true" : "false"); });
            field("local_filesystem_alpha_registered", [&]() {
                out_ << (registry.local_filesystem_alpha_registered ? "true" : "false");
            });
            field("unaudited_fallback_allowed",
                  [&]() { out_ << (registry.unaudited_fallback_allowed ? "true" : "false"); });
            field("registered_provider_count",
                  [&]() { out_ << registry.registered_provider_count; });
        });
        out_ << '\n';
    }

  private:
    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }
};

} // namespace

void print_durable_store_import_provider_registry_json(
    const durable_store_import::ProviderRegistry &registry, std::ostream &out) {
    RegistryJsonPrinter(out).print(registry);
}

} // namespace ahfl
