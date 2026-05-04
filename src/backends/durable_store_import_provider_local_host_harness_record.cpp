#include "ahfl/backends/durable_store_import_provider_local_host_harness_record.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class HarnessRecordJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit HarnessRecordJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(record.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(record.workflow_canonical_name); });
            field("session_id", [&]() { write_string(record.session_id); });
            field("run_id", [&]() { print_optional_string(record.run_id); });
            field("input_fixture", [&]() { write_string(record.input_fixture); });
            field("harness_request_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("harness_record_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_record_identity);
            });
            field("operation_kind", [&]() { print_operation(record.operation_kind); });
            field("record_status", [&]() { print_status(record.record_status); });
            field("outcome_kind", [&]() { print_outcome(record.outcome_kind); });
            field("exit_code", [&]() { out_ << record.exit_code; });
            field("timed_out", [&]() { out_ << (record.timed_out ? "true" : "false"); });
            field("sandbox_denied", [&]() { out_ << (record.sandbox_denied ? "true" : "false"); });
            field("captured_diagnostic_summary",
                  [&]() { write_string(record.captured_diagnostic_summary); });
            field("captured_stdout_excerpt",
                  [&]() { print_optional_string(record.captured_stdout_excerpt); });
            field("captured_stderr_excerpt",
                  [&]() { print_optional_string(record.captured_stderr_excerpt); });
            field("opens_network_connection",
                  [&]() { out_ << (record.opens_network_connection ? "true" : "false"); });
            field("reads_secret_material",
                  [&]() { out_ << (record.reads_secret_material ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (record.writes_host_filesystem ? "true" : "false"); });
            field("failure_attribution", [&]() { print_failure(record.failure_attribution, 1); });
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

    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded:
            write_string("succeeded");
            return;
        case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed:
            write_string("failed");
            return;
        case durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut:
            write_string("timed_out");
            return;
        case durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied:
            write_string("sandbox_denied");
            return;
        case durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady:
            write_string("secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady:
            write_string("harness_request_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit:
            write_string("nonzero_exit");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied:
            write_string("sandbox_denied");
            return;
        }
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
            &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_harness_record_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record,
    std::ostream &out) {
    HarnessRecordJsonPrinter(out).print(record);
}

} // namespace ahfl
