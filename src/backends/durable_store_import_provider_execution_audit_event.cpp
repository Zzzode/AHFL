#include "ahfl/backends/durable_store_import_provider_execution_audit_event.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class AuditEventJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit AuditEventJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderExecutionAuditEvent &event) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(event.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(event.workflow_canonical_name); });
            field("session_id", [&]() { write_string(event.session_id); });
            field("run_id", [&]() { print_optional_string(event.run_id); });
            field("input_fixture", [&]() { write_string(event.input_fixture); });
            field("execution_audit_event_identity", [&]() {
                write_string(event.durable_store_import_provider_execution_audit_event_identity);
            });
            field("event_name", [&]() { write_string(event.event_name); });
            field("outcome", [&]() { print_outcome(event.outcome); });
            field("redaction_policy_version",
                  [&]() { write_string(event.redaction_policy_version); });
            field("event_summary", [&]() { write_string(event.event_summary); });
            field("secret_free", [&]() { out_ << (event.secret_free ? "true" : "false"); });
            field("raw_telemetry_persisted",
                  [&]() { out_ << (event.raw_telemetry_persisted ? "true" : "false"); });
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

void print_durable_store_import_provider_execution_audit_event_json(
    const durable_store_import::ProviderExecutionAuditEvent &event, std::ostream &out) {
    AuditEventJsonPrinter(out).print(event);
}

} // namespace ahfl
