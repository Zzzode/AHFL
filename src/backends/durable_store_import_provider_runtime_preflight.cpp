#include "ahfl/backends/durable_store_import_provider_runtime_preflight.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderRuntimePreflightJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderRuntimePreflightJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderRuntimePreflightPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_driver_binding_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_driver_binding_plan_format_version);
            });
            field("source_durable_store_import_provider_driver_profile_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_driver_profile_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_driver_binding_identity", [&]() {
                write_string(plan.durable_store_import_provider_driver_binding_identity);
            });
            field("provider_persistence_id",
                  [&]() { print_optional_string(plan.provider_persistence_id); });
            field("source_binding_status",
                  [&]() { print_binding_status(plan.source_binding_status); });
            field("source_operation_descriptor_identity",
                  [&]() { print_optional_string(plan.source_operation_descriptor_identity); });
            field("runtime_profile", [&]() { print_runtime_profile(plan.runtime_profile, 1); });
            field("durable_store_import_provider_runtime_preflight_identity", [&]() {
                write_string(plan.durable_store_import_provider_runtime_preflight_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("preflight_status", [&]() { print_preflight_status(plan.preflight_status); });
            field("sdk_invocation_envelope_identity",
                  [&]() { print_optional_string(plan.sdk_invocation_envelope_identity); });
            field("loads_runtime_config",
                  [&]() { out_ << (plan.loads_runtime_config ? "true" : "false"); });
            field("resolves_secret_handles",
                  [&]() { out_ << (plan.resolves_secret_handles ? "true" : "false"); });
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

    void print_binding_status(durable_store_import::ProviderDriverBindingStatus status) {
        switch (status) {
        case durable_store_import::ProviderDriverBindingStatus::Bound:
            write_string("bound");
            return;
        case durable_store_import::ProviderDriverBindingStatus::NotBound:
            write_string("not_bound");
            return;
        }
    }

    void print_capability(durable_store_import::ProviderRuntimeCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile:
            write_string("load_secret_free_runtime_profile");
            return;
        case durable_store_import::ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder:
            write_string("resolve_secret_handle_placeholder");
            return;
        case durable_store_import::ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder:
            write_string("load_config_snapshot_placeholder");
            return;
        case durable_store_import::ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope:
            write_string("plan_sdk_invocation_envelope");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderRuntimeOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope:
            write_string("plan_provider_sdk_invocation_envelope");
            return;
        case durable_store_import::ProviderRuntimeOperationKind::NoopDriverBindingNotReady:
            write_string("noop_driver_binding_not_ready");
            return;
        case durable_store_import::ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch:
            write_string("noop_runtime_profile_mismatch");
            return;
        case durable_store_import::ProviderRuntimeOperationKind::NoopUnsupportedRuntimeCapability:
            write_string("noop_unsupported_runtime_capability");
            return;
        }
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

    void print_failure_kind(durable_store_import::ProviderRuntimePreflightFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderRuntimePreflightFailureKind::DriverBindingNotReady:
            write_string("driver_binding_not_ready");
            return;
        case durable_store_import::ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch:
            write_string("runtime_profile_mismatch");
            return;
        case durable_store_import::ProviderRuntimePreflightFailureKind::
            UnsupportedRuntimeCapability:
            write_string("unsupported_runtime_capability");
            return;
        }
    }

    void print_runtime_profile(const durable_store_import::ProviderRuntimeProfile &profile,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(profile.format_version); });
            field("runtime_profile_identity",
                  [&]() { write_string(profile.runtime_profile_identity); });
            field("driver_profile_ref", [&]() { write_string(profile.driver_profile_ref); });
            field("provider_namespace", [&]() { write_string(profile.provider_namespace); });
            field("secret_free_runtime_config_ref",
                  [&]() { write_string(profile.secret_free_runtime_config_ref); });
            field("secret_resolver_policy_ref",
                  [&]() { write_string(profile.secret_resolver_policy_ref); });
            field("config_snapshot_policy_ref",
                  [&]() { write_string(profile.config_snapshot_policy_ref); });
            field("credential_free",
                  [&]() { out_ << (profile.credential_free ? "true" : "false"); });
            field("supports_secret_free_runtime_profile_load", [&]() {
                out_ << (profile.supports_secret_free_runtime_profile_load ? "true" : "false");
            });
            field("supports_secret_handle_placeholder_resolution", [&]() {
                out_ << (profile.supports_secret_handle_placeholder_resolution ? "true" : "false");
            });
            field("supports_config_snapshot_placeholder_load", [&]() {
                out_ << (profile.supports_config_snapshot_placeholder_load ? "true" : "false");
            });
            field("supports_sdk_invocation_envelope_planning", [&]() {
                out_ << (profile.supports_sdk_invocation_envelope_planning ? "true" : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(profile.credential_reference); });
            field("secret_value", [&]() { print_optional_string(profile.secret_value); });
            field("secret_manager_endpoint_uri",
                  [&]() { print_optional_string(profile.secret_manager_endpoint_uri); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(profile.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(profile.object_path); });
            field("database_table", [&]() { print_optional_string(profile.database_table); });
            field("sdk_payload_schema",
                  [&]() { print_optional_string(profile.sdk_payload_schema); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(profile.sdk_request_payload); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderRuntimePreflightFailureAttribution &failure,
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

void print_durable_store_import_provider_runtime_preflight_json(
    const durable_store_import::ProviderRuntimePreflightPlan &plan, std::ostream &out) {
    DurableStoreImportProviderRuntimePreflightJsonPrinter(out).print(plan);
}

} // namespace ahfl
