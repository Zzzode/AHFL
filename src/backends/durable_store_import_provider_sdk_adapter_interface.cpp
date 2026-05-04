#include "ahfl/backends/durable_store_import_provider_sdk_adapter_interface.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ProviderSdkAdapterInterfaceJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ProviderSdkAdapterInterfaceJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterInterfacePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("source_sdk_adapter_request_status",
                  [&]() { print_adapter_status(plan.source_sdk_adapter_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(plan.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_interface_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_interface_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("interface_status", [&]() { print_interface_status(plan.interface_status); });
            field("provider_registry_identity",
                  [&]() { write_string(plan.provider_registry_identity); });
            field("adapter_descriptor_identity",
                  [&]() { write_string(plan.adapter_descriptor_identity); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("adapter_name", [&]() { write_string(plan.adapter_name); });
            field("adapter_version", [&]() { write_string(plan.adapter_version); });
            field("interface_abi_version", [&]() { write_string(plan.interface_abi_version); });
            field("capability_descriptor_identity",
                  [&]() { write_string(plan.capability_descriptor_identity); });
            field("capability_descriptor",
                  [&]() { print_capability_descriptor(plan.capability_descriptor, 1); });
            field("error_taxonomy_version", [&]() { write_string(plan.error_taxonomy_version); });
            field("normalized_error_kind",
                  [&]() { print_normalized_error_kind(plan.normalized_error_kind); });
            field("returns_placeholder_result",
                  [&]() { out_ << (plan.returns_placeholder_result ? "true" : "false"); });
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
            field("credential_reference",
                  [&]() { print_optional_string(plan.credential_reference); });
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

    void print_adapter_status(durable_store_import::ProviderSdkAdapterStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkAdapterStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderSdkAdapterStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            PlanProviderSdkAdapterInterface:
            write_string("plan_provider_sdk_adapter_interface");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopUnsupportedCapability:
            write_string("noop_unsupported_capability");
            return;
        }
    }

    void
    print_support_status(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported:
            write_string("supported");
            return;
        case durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported:
            write_string("unsupported");
            return;
        }
    }

    void
    print_normalized_error_kind(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None:
            write_string("none");
            return;
        case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability:
            write_string("unsupported_capability");
            return;
        case durable_store_import::ProviderSdkAdapterNormalizedErrorKind::SdkAdapterRequestNotReady:
            write_string("sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterInterfaceFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::
            SdkAdapterRequestNotReady:
            write_string("sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
            write_string("unsupported_capability");
            return;
        }
    }

    void print_capability_descriptor(
        const durable_store_import::ProviderSdkAdapterCapabilityDescriptor &descriptor,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_key", [&]() { write_string(descriptor.capability_key); });
            field("support_status", [&]() { print_support_status(descriptor.support_status); });
            field("input_contract_format_version",
                  [&]() { write_string(descriptor.input_contract_format_version); });
            field("output_contract_format_version",
                  [&]() { write_string(descriptor.output_contract_format_version); });
            field("supports_placeholder_response",
                  [&]() { out_ << (descriptor.supports_placeholder_response ? "true" : "false"); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterInterfaceFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_json(
    const durable_store_import::ProviderSdkAdapterInterfacePlan &plan, std::ostream &out) {
    ProviderSdkAdapterInterfaceJsonPrinter(out).print(plan);
}

} // namespace ahfl
