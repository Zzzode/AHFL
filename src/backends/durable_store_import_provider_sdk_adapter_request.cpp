#include "ahfl/backends/durable_store_import_provider_sdk_adapter_request.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ProviderSdkAdapterRequestJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ProviderSdkAdapterRequestJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterRequestPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_local_host_execution_status",
                  [&]() { print_local_host_status(plan.source_local_host_execution_status); });
            field("source_provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(plan.source_provider_local_host_execution_receipt_identity);
            });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("request_status", [&]() { print_status(plan.request_status); });
            field("provider_sdk_adapter_request_identity",
                  [&]() { print_optional_string(plan.provider_sdk_adapter_request_identity); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(plan.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload",
                  [&]() { out_ << (plan.materializes_sdk_request_payload ? "true" : "false"); });
            field("invokes_provider_sdk",
                  [&]() { out_ << (plan.invokes_provider_sdk ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (plan.opens_network_connection ? "true" : "false"); });
            field("reads_secret_material",
                  [&]() { out_ << (plan.reads_secret_material ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (plan.reads_host_environment ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (plan.writes_host_filesystem ? "true" : "false"); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(plan.object_path); });
            field("database_table", [&]() { print_optional_string(plan.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(plan.sdk_response_payload); });
            field("failure_attribution", [&]() {
                if (plan.failure_attribution.has_value()) {
                    print_failure_attribution(*plan.failure_attribution, 1);
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

    void print_local_host_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        switch (status) {
        case durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady:
            write_string("simulated_ready");
            return;
        case durable_store_import::ProviderLocalHostExecutionStatus::Blocked:
            write_string("blocked");
            return;
        }
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

void print_durable_store_import_provider_sdk_adapter_request_json(
    const durable_store_import::ProviderSdkAdapterRequestPlan &plan, std::ostream &out) {
    ProviderSdkAdapterRequestJsonPrinter(out).print(plan);
}

} // namespace ahfl
