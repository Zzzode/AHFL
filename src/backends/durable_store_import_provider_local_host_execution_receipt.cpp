#include "ahfl/backends/durable_store_import_provider_local_host_execution_receipt.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter final
    : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostExecutionReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    receipt
                        .source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(receipt.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(receipt.durable_store_import_provider_host_execution_identity);
            });
            field("source_host_execution_status",
                  [&]() { print_host_execution_status(receipt.source_host_execution_status); });
            field("source_provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(receipt.source_provider_host_execution_descriptor_identity);
            });
            field("source_provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(receipt.source_provider_host_receipt_placeholder_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    receipt.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(receipt.operation_kind); });
            field("execution_status", [&]() { print_execution_status(receipt.execution_status); });
            field("provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(receipt.provider_local_host_execution_receipt_identity);
            });
            field("starts_host_process",
                  [&]() { out_ << (receipt.starts_host_process ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (receipt.reads_host_environment ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (receipt.opens_network_connection ? "true" : "false"); });
            field("materializes_sdk_request_payload",
                  [&]() { out_ << (receipt.materializes_sdk_request_payload ? "true" : "false"); });
            field("invokes_provider_sdk",
                  [&]() { out_ << (receipt.invokes_provider_sdk ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (receipt.writes_host_filesystem ? "true" : "false"); });
            field("failure_attribution", [&]() {
                if (receipt.failure_attribution.has_value()) {
                    print_failure_attribution(*receipt.failure_attribution, 1);
                    return;
                }
                out_ << "null";
            });
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

    void print_host_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        switch (status) {
        case durable_store_import::ProviderHostExecutionStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderHostExecutionStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            SimulateProviderLocalHostExecutionReceipt:
            write_string("simulate_provider_local_host_execution_receipt");
            return;
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            NoopHostExecutionNotReady:
            write_string("noop_host_execution_not_ready");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        switch (status) {
        case durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady:
            write_string("simulated_ready");
            return;
        case durable_store_import::ProviderLocalHostExecutionStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady:
            write_string("host_execution_not_ready");
            return;
        }
    }

    void print_failure_attribution(
        const durable_store_import::ProviderLocalHostExecutionFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_json(
    const durable_store_import::ProviderLocalHostExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl
