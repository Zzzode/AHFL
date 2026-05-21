#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_preflight {
namespace {

class DurableStoreImportProviderRuntimePreflightJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderRuntimePreflightJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

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
            field("loads_runtime_config", [&]() { write_bool(plan.loads_runtime_config); });
            field("resolves_secret_handles", [&]() { write_bool(plan.resolves_secret_handles); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_binding_status(durable_store_import::ProviderDriverBindingStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::Bound,
                                    "bound");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::NotBound,
                                    "not_bound"));
    }

    void print_capability(durable_store_import::ProviderRuntimeCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile,
                "load_secret_free_runtime_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder,
                "resolve_secret_handle_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder,
                "load_config_snapshot_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope,
                "plan_sdk_invocation_envelope"));
    }

    void print_operation_kind(durable_store_import::ProviderRuntimeOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimeOperationKind::
                                        PlanProviderSdkInvocationEnvelope,
                                    "plan_provider_sdk_invocation_envelope");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeOperationKind::NoopDriverBindingNotReady,
                "noop_driver_binding_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch,
                "noop_runtime_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimeOperationKind::
                                        NoopUnsupportedRuntimeCapability,
                                    "noop_unsupported_runtime_capability"));
    }

    void print_preflight_status(durable_store_import::ProviderRuntimePreflightStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Blocked,
                                    "blocked"));
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
            field("credential_free", [&]() { write_bool(profile.credential_free); });
            field("supports_secret_free_runtime_profile_load",
                  [&]() { write_bool(profile.supports_secret_free_runtime_profile_load); });
            field("supports_secret_handle_placeholder_resolution",
                  [&]() { write_bool(profile.supports_secret_handle_placeholder_resolution); });
            field("supports_config_snapshot_placeholder_load",
                  [&]() { write_bool(profile.supports_config_snapshot_placeholder_load); });
            field("supports_sdk_invocation_envelope_planning",
                  [&]() { write_bool(profile.supports_sdk_invocation_envelope_planning); });
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
                print_optional(failure.missing_capability,
                               [&](const auto &value) { print_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_provider_runtime_preflight_json(
    const durable_store_import::ProviderRuntimePreflightPlan &plan, std::ostream &out) {
    DurableStoreImportProviderRuntimePreflightJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_preflight

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
preflight_status_name(durable_store_import::ProviderRuntimePreflightStatus status) {
    switch (status) {
    case durable_store_import::ProviderRuntimePreflightStatus::Ready:
        return "ready";
    case durable_store_import::ProviderRuntimePreflightStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderRuntimeOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope:
        return "plan_provider_sdk_invocation_envelope";
    case durable_store_import::ProviderRuntimeOperationKind::NoopDriverBindingNotReady:
        return "noop_driver_binding_not_ready";
    case durable_store_import::ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch:
        return "noop_runtime_profile_mismatch";
    case durable_store_import::ProviderRuntimeOperationKind::NoopUnsupportedRuntimeCapability:
        return "noop_unsupported_runtime_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderRuntimeReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::
        ReadyForSdkAdapterImplementation:
        return "ready_for_sdk_adapter_implementation";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::FixRuntimeProfile:
        return "fix_runtime_profile";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::WaitForRuntimeCapability:
        return "wait_for_runtime_capability";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderRuntimeCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile:
        return "load_secret_free_runtime_profile";
    case durable_store_import::ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder:
        return "resolve_secret_handle_placeholder";
    case durable_store_import::ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder:
        return "load_config_snapshot_placeholder";
    case durable_store_import::ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope:
        return "plan_sdk_invocation_envelope";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderRuntimePreflightFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderRuntimePreflightFailureKind::DriverBindingNotReady:
        return "driver_binding_not_ready";
    case durable_store_import::ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch:
        return "runtime_profile_mismatch";
    case durable_store_import::ProviderRuntimePreflightFailureKind::UnsupportedRuntimeCapability:
        return "unsupported_runtime_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderRuntimePreflightFailureAttribution>
        &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "missing_capability " + (failure->missing_capability.has_value()
                                      ? capability_name(*failure->missing_capability)
                                      : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_runtime_readiness(
    const durable_store_import::ProviderRuntimeReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_runtime_preflight_plan_format " +
             review.source_durable_store_import_provider_runtime_preflight_plan_format_version);
    line(out,
         0,
         "source_provider_driver_binding_plan_format " +
             review.source_durable_store_import_provider_driver_binding_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_driver_binding_identity " +
             review.durable_store_import_provider_driver_binding_identity);
    line(out,
         0,
         "durable_store_import_provider_runtime_preflight_identity " +
             review.durable_store_import_provider_runtime_preflight_identity);
    line(out, 0, "preflight_status " + preflight_status_name(review.preflight_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "sdk_invocation_envelope_identity " + (review.sdk_invocation_envelope_identity.has_value()
                                                    ? *review.sdk_invocation_envelope_identity
                                                    : std::string("none")));
    line(out,
         0,
         "loads_runtime_config " + std::string(review.loads_runtime_config ? "true" : "false"));
    line(out,
         0,
         "resolves_secret_handles " +
             std::string(review.resolves_secret_handles ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "runtime_boundary " + review.runtime_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_readiness
