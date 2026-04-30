#include "ahfl/backends/durable_store_import_provider_driver_binding.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderDriverBindingJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderDriverBindingJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderDriverBindingPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_write_attempt_preview_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_write_attempt_preview_format_version);
            });
            field("source_durable_store_import_provider_adapter_config_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_adapter_config_format_version);
            });
            field("source_durable_store_import_provider_capability_matrix_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_capability_matrix_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_write_attempt_identity", [&]() {
                write_string(plan.durable_store_import_provider_write_attempt_identity);
            });
            field("durable_store_import_provider_driver_binding_identity", [&]() {
                write_string(plan.durable_store_import_provider_driver_binding_identity);
            });
            field("source_planning_status",
                  [&]() { print_planning_status(plan.source_planning_status); });
            field("provider_persistence_id",
                  [&]() { print_optional_string(plan.provider_persistence_id); });
            field("driver_profile", [&]() { print_driver_profile(plan.driver_profile, 1); });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("binding_status", [&]() { print_binding_status(plan.binding_status); });
            field("operation_descriptor_identity",
                  [&]() { print_optional_string(plan.operation_descriptor_identity); });
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

    void print_driver_kind(durable_store_import::ProviderDriverKind kind) {
        switch (kind) {
        case durable_store_import::ProviderDriverKind::ProviderNeutralPreviewDriver:
            write_string("provider_neutral_preview_driver");
            return;
        case durable_store_import::ProviderDriverKind::AwsObjectStorePreviewDriver:
            write_string("aws_object_store_preview_driver");
            return;
        case durable_store_import::ProviderDriverKind::GcpObjectStorePreviewDriver:
            write_string("gcp_object_store_preview_driver");
            return;
        case durable_store_import::ProviderDriverKind::AzureObjectStorePreviewDriver:
            write_string("azure_object_store_preview_driver");
            return;
        }
    }

    void print_capability(durable_store_import::ProviderDriverCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile:
            write_string("load_secret_free_profile");
            return;
        case durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent:
            write_string("translate_write_intent");
            return;
        case durable_store_import::ProviderDriverCapabilityKind::TranslateRetryPlaceholder:
            write_string("translate_retry_placeholder");
            return;
        case durable_store_import::ProviderDriverCapabilityKind::TranslateResumePlaceholder:
            write_string("translate_resume_placeholder");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderDriverOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderDriverOperationKind::TranslateProviderPersistReceipt:
            write_string("translate_provider_persist_receipt");
            return;
        case durable_store_import::ProviderDriverOperationKind::NoopSourceNotPlanned:
            write_string("noop_source_not_planned");
            return;
        case durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch:
            write_string("noop_profile_mismatch");
            return;
        case durable_store_import::ProviderDriverOperationKind::NoopUnsupportedDriverCapability:
            write_string("noop_unsupported_driver_capability");
            return;
        }
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

    void print_planning_status(durable_store_import::ProviderWritePlanningStatus status) {
        switch (status) {
        case durable_store_import::ProviderWritePlanningStatus::Planned:
            write_string("planned");
            return;
        case durable_store_import::ProviderWritePlanningStatus::NotPlanned:
            write_string("not_planned");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderDriverBindingFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned:
            write_string("source_write_attempt_not_planned");
            return;
        case durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch:
            write_string("driver_profile_mismatch");
            return;
        case durable_store_import::ProviderDriverBindingFailureKind::UnsupportedDriverCapability:
            write_string("unsupported_driver_capability");
            return;
        }
    }

    void print_driver_profile(const durable_store_import::ProviderDriverProfile &profile,
                              int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(profile.format_version); });
            field("driver_kind", [&]() { print_driver_kind(profile.driver_kind); });
            field("driver_profile_identity",
                  [&]() { write_string(profile.driver_profile_identity); });
            field("provider_profile_ref", [&]() { write_string(profile.provider_profile_ref); });
            field("provider_namespace", [&]() { write_string(profile.provider_namespace); });
            field("secret_free_config_ref",
                  [&]() { write_string(profile.secret_free_config_ref); });
            field("credential_free",
                  [&]() { out_ << (profile.credential_free ? "true" : "false"); });
            field("supports_secret_free_profile_load", [&]() {
                out_ << (profile.supports_secret_free_profile_load ? "true" : "false");
            });
            field("supports_write_intent_translation", [&]() {
                out_ << (profile.supports_write_intent_translation ? "true" : "false");
            });
            field("supports_retry_placeholder_translation", [&]() {
                out_ << (profile.supports_retry_placeholder_translation ? "true" : "false");
            });
            field("supports_resume_placeholder_translation", [&]() {
                out_ << (profile.supports_resume_placeholder_translation ? "true" : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(profile.credential_reference); });
            field("endpoint_uri", [&]() { print_optional_string(profile.endpoint_uri); });
            field("endpoint_secret_reference",
                  [&]() { print_optional_string(profile.endpoint_secret_reference); });
            field("object_path", [&]() { print_optional_string(profile.object_path); });
            field("database_table", [&]() { print_optional_string(profile.database_table); });
            field("sdk_payload_schema",
                  [&]() { print_optional_string(profile.sdk_payload_schema); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderDriverBindingFailureAttribution &failure,
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

void print_durable_store_import_provider_driver_binding_json(
    const durable_store_import::ProviderDriverBindingPlan &plan, std::ostream &out) {
    DurableStoreImportProviderDriverBindingJsonPrinter(out).print(plan);
}

} // namespace ahfl
