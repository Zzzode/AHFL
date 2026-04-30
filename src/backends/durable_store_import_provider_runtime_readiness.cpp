#include "ahfl/backends/durable_store_import_provider_runtime_readiness.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {
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

} // namespace ahfl
