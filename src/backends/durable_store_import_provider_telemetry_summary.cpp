#include "ahfl/backends/durable_store_import_provider_telemetry_summary.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class TelemetrySummaryJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit TelemetrySummaryJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderTelemetrySummary &summary) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(summary.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(summary.workflow_canonical_name); });
            field("session_id", [&]() { write_string(summary.session_id); });
            field("run_id", [&]() { print_optional_string(summary.run_id); });
            field("input_fixture", [&]() { write_string(summary.input_fixture); });
            field("telemetry_summary_identity", [&]() {
                write_string(summary.durable_store_import_provider_telemetry_summary_identity);
            });
            field("outcome", [&]() { print_outcome(summary.outcome); });
            field("provider_write_attempted",
                  [&]() { out_ << (summary.provider_write_attempted ? "true" : "false"); });
            field("provider_write_committed",
                  [&]() { out_ << (summary.provider_write_committed ? "true" : "false"); });
            field("retry_recommended",
                  [&]() { out_ << (summary.retry_recommended ? "true" : "false"); });
            field("recovery_required",
                  [&]() { out_ << (summary.recovery_required ? "true" : "false"); });
            field("telemetry_summary", [&]() { write_string(summary.telemetry_summary); });
            field("secret_free", [&]() { out_ << (summary.secret_free ? "true" : "false"); });
            field("raw_telemetry_persisted",
                  [&]() { out_ << (summary.raw_telemetry_persisted ? "true" : "false"); });
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

    void print_outcome(durable_store_import::ProviderAuditOutcome outcome) {
        switch (outcome) {
        case durable_store_import::ProviderAuditOutcome::Success:
            write_string("success");
            return;
        case durable_store_import::ProviderAuditOutcome::Failure:
            write_string("failure");
            return;
        case durable_store_import::ProviderAuditOutcome::RetryPending:
            write_string("retry_pending");
            return;
        case durable_store_import::ProviderAuditOutcome::RecoveryPending:
            write_string("recovery_pending");
            return;
        case durable_store_import::ProviderAuditOutcome::Blocked:
            write_string("blocked");
            return;
        }
    }
};

} // namespace

void print_durable_store_import_provider_telemetry_summary_json(
    const durable_store_import::ProviderTelemetrySummary &summary, std::ostream &out) {
    TelemetrySummaryJsonPrinter(out).print(summary);
}

} // namespace ahfl
