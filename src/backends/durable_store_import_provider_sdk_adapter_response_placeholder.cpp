#include "ahfl/backends/durable_store_import_provider_sdk_adapter_response_placeholder.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ProviderSdkAdapterResponsePlaceholderJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ProviderSdkAdapterResponsePlaceholderJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    placeholder
                        .source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        placeholder
                            .source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_request_status",
                  [&]() { print_status(placeholder.source_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(placeholder.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_response_placeholder_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_sdk_adapter_response_placeholder_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(placeholder.operation_kind); });
            field("response_status", [&]() { print_status(placeholder.response_status); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(
                    placeholder.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload", [&]() {
                out_ << (placeholder.materializes_sdk_request_payload ? "true" : "false");
            });
            field("invokes_provider_sdk",
                  [&]() { out_ << (placeholder.invokes_provider_sdk ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (placeholder.opens_network_connection ? "true" : "false"); });
            field("reads_secret_material",
                  [&]() { out_ << (placeholder.reads_secret_material ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (placeholder.reads_host_environment ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (placeholder.writes_host_filesystem ? "true" : "false"); });
            field("failure_attribution", [&]() {
                if (placeholder.failure_attribution.has_value()) {
                    print_failure_attribution(*placeholder.failure_attribution, 1);
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

    void print_status(durable_store_import::ProviderSdkAdapterStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkAdapterStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderSdkAdapterStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
            write_string("plan_provider_sdk_adapter_request");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::
            PlanProviderSdkAdapterResponsePlaceholder:
            write_string("plan_provider_sdk_adapter_response_placeholder");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
            write_string("noop_local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady:
            write_string("local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady:
            write_string("sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_response_placeholder_json(
    const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder,
    std::ostream &out) {
    ProviderSdkAdapterResponsePlaceholderJsonPrinter(out).print(placeholder);
}

} // namespace ahfl
