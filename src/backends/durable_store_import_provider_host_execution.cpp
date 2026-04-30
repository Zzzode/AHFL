#include "ahfl/backends/durable_store_import_provider_host_execution.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderHostExecutionJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderHostExecutionJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderHostExecutionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("source_durable_store_import_provider_sdk_envelope_policy_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_envelope_policy_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("source_host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.source_host_handoff_descriptor_identity); });
            field("source_envelope_status",
                  [&]() { print_envelope_status(plan.source_envelope_status); });
            field("host_execution_policy", [&]() { print_policy(plan.host_execution_policy, 1); });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("execution_status", [&]() { print_execution_status(plan.execution_status); });
            field("provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(plan.provider_host_execution_descriptor_identity);
            });
            field("provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(plan.provider_host_receipt_placeholder_identity);
            });
            field("starts_host_process",
                  [&]() { out_ << (plan.starts_host_process ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (plan.reads_host_environment ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (plan.opens_network_connection ? "true" : "false"); });
            field("materializes_sdk_request_payload",
                  [&]() { out_ << (plan.materializes_sdk_request_payload ? "true" : "false"); });
            field("invokes_provider_sdk",
                  [&]() { out_ << (plan.invokes_provider_sdk ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (plan.writes_host_filesystem ? "true" : "false"); });
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

    void print_envelope_status(durable_store_import::ProviderSdkEnvelopeStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkEnvelopeStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderSdkEnvelopeStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_capability(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanLocalHostExecutionDescriptor:
            write_string("plan_local_host_execution_descriptor");
            return;
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanDryRunHostReceiptPlaceholder:
            write_string("plan_dry_run_host_receipt_placeholder");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
            write_string("plan_provider_host_execution");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
            write_string("noop_sdk_handoff_not_ready");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
            write_string("noop_host_policy_mismatch");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::
            NoopUnsupportedHostCapability:
            write_string("noop_unsupported_host_capability");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        switch (status) {
        case durable_store_import::ProviderHostExecutionStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderHostExecutionStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderHostExecutionFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady:
            write_string("sdk_handoff_not_ready");
            return;
        case durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch:
            write_string("host_policy_mismatch");
            return;
        case durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability:
            write_string("unsupported_host_capability");
            return;
        }
    }

    void print_policy(const durable_store_import::ProviderHostExecutionPolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("host_execution_policy_identity",
                  [&]() { write_string(policy.host_execution_policy_identity); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("execution_profile_ref", [&]() { write_string(policy.execution_profile_ref); });
            field("sandbox_policy_ref", [&]() { write_string(policy.sandbox_policy_ref); });
            field("timeout_policy_ref", [&]() { write_string(policy.timeout_policy_ref); });
            field("credential_free",
                  [&]() { out_ << (policy.credential_free ? "true" : "false"); });
            field("network_free", [&]() { out_ << (policy.network_free ? "true" : "false"); });
            field("supports_local_host_execution_descriptor_planning", [&]() {
                out_ << (policy.supports_local_host_execution_descriptor_planning ? "true"
                                                                                  : "false");
            });
            field("supports_dry_run_host_receipt_placeholder_planning", [&]() {
                out_ << (policy.supports_dry_run_host_receipt_placeholder_planning ? "true"
                                                                                   : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("host_environment_secret",
                  [&]() { print_optional_string(policy.host_environment_secret); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderHostExecutionFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("missing_capability", [&]() {
                if (failure.missing_capability.has_value()) {
                    print_capability(*failure.missing_capability);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_durable_store_import_provider_host_execution_json(
    const durable_store_import::ProviderHostExecutionPlan &plan, std::ostream &out) {
    DurableStoreImportProviderHostExecutionJsonPrinter(out).print(plan);
}

} // namespace ahfl
