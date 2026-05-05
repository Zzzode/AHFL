#include "ahfl/backends/durable_store_import_provider_schema_compatibility_report.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class SchemaCompatibilityReportJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit SchemaCompatibilityReportJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSchemaCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("version_checks", [&]() { print_version_checks(report.version_checks); });
            field("source_chain_checks",
                  [&]() { print_source_chain_checks(report.source_chain_checks); });
            field("reference_checks",
                  [&]() { print_reference_checks(report.reference_checks); });
            field("compatible_count",
                  [&]() { out_ << report.compatible_count; });
            field("incompatible_count",
                  [&]() { out_ << report.incompatible_count; });
            field("unknown_count",
                  [&]() { out_ << report.unknown_count; });
            field("compatibility_summary",
                  [&]() { write_string(report.compatibility_summary); });
            field("has_schema_drift",
                  [&]() { out_ << (report.has_schema_drift ? "true" : "false"); });
            field("drift_details",
                  [&]() { print_optional_string(report.drift_details); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::SchemaCompatibilityStatus status) {
        switch (status) {
        case durable_store_import::SchemaCompatibilityStatus::Compatible:
            write_string("compatible");
            return;
        case durable_store_import::SchemaCompatibilityStatus::Incompatible:
            write_string("incompatible");
            return;
        case durable_store_import::SchemaCompatibilityStatus::Unknown:
            write_string("unknown");
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

    void print_version_checks(
        const std::vector<durable_store_import::ArtifactVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("artifact_type",
                              [&]() { write_string(check.artifact_type); });
                        field("artifact_identity",
                              [&]() { write_string(check.artifact_identity); });
                        field("format_version",
                              [&]() { write_string(check.format_version); });
                        field("status", [&]() { print_status(check.status); });
                        field("expected_format_version",
                              [&]() { print_optional_string(check.expected_format_version); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void print_source_chain_checks(
        const std::vector<durable_store_import::SourceChainCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("source_artifact_type",
                              [&]() { write_string(check.source_artifact_type); });
                        field("source_artifact_identity",
                              [&]() { write_string(check.source_artifact_identity); });
                        field("source_format_version",
                              [&]() { write_string(check.source_format_version); });
                        field("expected_source_format_version",
                              [&]() { write_string(check.expected_source_format_version); });
                        field("is_compatible",
                              [&]() { out_ << (check.is_compatible ? "true" : "false"); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void print_reference_checks(
        const std::vector<durable_store_import::ReferenceVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("reference_type",
                              [&]() { write_string(check.reference_type); });
                        field("reference_identity",
                              [&]() { write_string(check.reference_identity); });
                        field("referenced_format_version",
                              [&]() { write_string(check.referenced_format_version); });
                        field("expected_format_version",
                              [&]() { write_string(check.expected_format_version); });
                        field("is_compatible",
                              [&]() { out_ << (check.is_compatible ? "true" : "false"); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }
};

} // namespace

void print_durable_store_import_provider_schema_compatibility_report_json(
    const durable_store_import::ProviderSchemaCompatibilityReport &report, std::ostream &out) {
    SchemaCompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl
