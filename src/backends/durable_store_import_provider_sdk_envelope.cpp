#include "ahfl/backends/durable_store_import_provider_sdk_envelope.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderSdkEnvelopeJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderSdkEnvelopeJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkRequestEnvelopePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_runtime_preflight_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_preflight_plan_format_version);
            });
            field("source_durable_store_import_provider_runtime_profile_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_profile_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_runtime_preflight_identity", [&]() {
                write_string(plan.durable_store_import_provider_runtime_preflight_identity);
            });
            field("source_sdk_invocation_envelope_identity",
                  [&]() { print_optional_string(plan.source_sdk_invocation_envelope_identity); });
            field("source_preflight_status",
                  [&]() { print_preflight_status(plan.source_preflight_status); });
            field("envelope_policy", [&]() { print_policy(plan.envelope_policy, 1); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("envelope_status", [&]() { print_envelope_status(plan.envelope_status); });
            field("provider_sdk_request_envelope_identity",
                  [&]() { print_optional_string(plan.provider_sdk_request_envelope_identity); });
            field("host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.host_handoff_descriptor_identity); });
            field("materializes_sdk_request_payload",
                  [&]() { out_ << (plan.materializes_sdk_request_payload ? "true" : "false"); });
            field("starts_host_process",
                  [&]() { out_ << (plan.starts_host_process ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (plan.opens_network_connection ? "true" : "false"); });
            field("invokes_provider_sdk",
                  [&]() { out_ << (plan.invokes_provider_sdk ? "true" : "false"); });
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

    void print_preflight_status(durable_store_import::ProviderRuntimePreflightStatus status) {
        switch (status) {
        case durable_store_import::ProviderRuntimePreflightStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderRuntimePreflightStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_capability(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope:
            write_string("plan_secret_free_request_envelope");
            return;
        case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor:
            write_string("plan_host_handoff_descriptor");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
            write_string("plan_provider_sdk_request_envelope");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
            write_string("noop_runtime_preflight_not_ready");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
            write_string("noop_envelope_policy_mismatch");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::
            NoopUnsupportedEnvelopeCapability:
            write_string("noop_unsupported_envelope_capability");
            return;
        }
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

    void print_failure_kind(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady:
            write_string("runtime_preflight_not_ready");
            return;
        case durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch:
            write_string("envelope_policy_mismatch");
            return;
        case durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability:
            write_string("unsupported_envelope_capability");
            return;
        }
    }

    void print_policy(const durable_store_import::ProviderSdkEnvelopePolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("sdk_envelope_policy_identity",
                  [&]() { write_string(policy.sdk_envelope_policy_identity); });
            field("runtime_profile_ref", [&]() { write_string(policy.runtime_profile_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("secret_free_request_schema_ref",
                  [&]() { write_string(policy.secret_free_request_schema_ref); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("credential_free",
                  [&]() { out_ << (policy.credential_free ? "true" : "false"); });
            field("supports_secret_free_request_envelope_planning", [&]() {
                out_ << (policy.supports_secret_free_request_envelope_planning ? "true" : "false");
            });
            field("supports_host_handoff_descriptor_planning", [&]() {
                out_ << (policy.supports_host_handoff_descriptor_planning ? "true" : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkEnvelopeFailureAttribution &failure,
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

void print_durable_store_import_provider_sdk_envelope_json(
    const durable_store_import::ProviderSdkRequestEnvelopePlan &plan, std::ostream &out) {
    DurableStoreImportProviderSdkEnvelopeJsonPrinter(out).print(plan);
}

} // namespace ahfl
