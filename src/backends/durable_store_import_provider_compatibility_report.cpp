#include "ahfl/backends/durable_store_import_provider_compatibility_report.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class CompatibilityReportJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit CompatibilityReportJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("compatibility_report_identity", [&]() {
                write_string(report.durable_store_import_provider_compatibility_report_identity);
            });
            field("status", [&]() { print_status(report.status); });
            field("mock_adapter_compatible",
                  [&]() { out_ << (report.mock_adapter_compatible ? "true" : "false"); });
            field("local_filesystem_alpha_compatible",
                  [&]() { out_ << (report.local_filesystem_alpha_compatible ? "true" : "false"); });
            field("capability_matrix_complete",
                  [&]() { out_ << (report.capability_matrix_complete ? "true" : "false"); });
            field("external_service_required",
                  [&]() { out_ << (report.external_service_required ? "true" : "false"); });
            field("compatibility_summary", [&]() { write_string(report.compatibility_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderCompatibilityStatus status) {
        switch (status) {
        case durable_store_import::ProviderCompatibilityStatus::Passed:
            write_string("passed");
            return;
        case durable_store_import::ProviderCompatibilityStatus::Failed:
            write_string("failed");
            return;
        case durable_store_import::ProviderCompatibilityStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }
};

} // namespace

void print_durable_store_import_provider_compatibility_report_json(
    const durable_store_import::ProviderCompatibilityReport &report, std::ostream &out) {
    CompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl
