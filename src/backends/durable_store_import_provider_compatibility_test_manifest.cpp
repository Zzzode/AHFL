#include "ahfl/backends/durable_store_import_provider_compatibility_test_manifest.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ManifestJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ManifestJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityTestManifest &manifest) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(manifest.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(manifest.workflow_canonical_name); });
            field("session_id", [&]() { write_string(manifest.session_id); });
            field("run_id", [&]() { print_optional_string(manifest.run_id); });
            field("input_fixture", [&]() { write_string(manifest.input_fixture); });
            field("compatibility_test_manifest_identity", [&]() {
                write_string(
                    manifest.durable_store_import_provider_compatibility_test_manifest_identity);
            });
            field("includes_mock_adapter",
                  [&]() { out_ << (manifest.includes_mock_adapter ? "true" : "false"); });
            field("includes_local_filesystem_alpha",
                  [&]() { out_ << (manifest.includes_local_filesystem_alpha ? "true" : "false"); });
            field("provider_count", [&]() { out_ << manifest.provider_count; });
            field("fixture_count", [&]() { out_ << manifest.fixture_count; });
            field("capability_matrix_reference",
                  [&]() { write_string(manifest.capability_matrix_reference); });
            field("external_service_required",
                  [&]() { out_ << (manifest.external_service_required ? "true" : "false"); });
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

void print_durable_store_import_provider_compatibility_test_manifest_json(
    const durable_store_import::ProviderCompatibilityTestManifest &manifest, std::ostream &out) {
    ManifestJsonPrinter(out).print(manifest);
}

} // namespace ahfl
