#include "ahfl/durable_store_import/provider_runtime.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_RUNTIME";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string preflight_status_slug(ProviderRuntimePreflightStatus status) {
    switch (status) {
    case ProviderRuntimePreflightStatus::Ready:
        return "ready";
    case ProviderRuntimePreflightStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string preflight_identity(const ProviderDriverBindingPlan &plan,
                                             ProviderRuntimePreflightStatus status) {
    return "durable-store-import-provider-runtime-preflight::" + plan.session_id +
           "::" + preflight_status_slug(status);
}

[[nodiscard]] bool profile_matches_driver_binding(const ProviderDriverBindingPlan &plan,
                                                  const ProviderRuntimeProfile &profile) {
    return profile.driver_profile_ref == plan.driver_profile.driver_profile_identity &&
           profile.provider_namespace == plan.driver_profile.provider_namespace;
}

[[nodiscard]] std::optional<std::string>
sdk_invocation_envelope_identity(const ProviderDriverBindingPlan &plan) {
    if (!plan.operation_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-invocation-envelope::" + *plan.operation_descriptor_identity;
}

[[nodiscard]] ProviderRuntimePreflightFailureAttribution
driver_binding_not_ready_failure(const ProviderDriverBindingPlan &plan) {
    std::string message =
        "provider runtime preflight cannot proceed because driver binding is not bound";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderRuntimePreflightFailureAttribution{
        .kind = ProviderRuntimePreflightFailureKind::DriverBindingNotReady,
        .message = std::move(message),
        .missing_capability = std::nullopt,
    };
}

[[nodiscard]] ProviderRuntimeCapabilityKind
first_missing_runtime_capability(const ProviderRuntimeProfile &profile) {
    if (!profile.supports_secret_free_runtime_profile_load) {
        return ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile;
    }
    if (!profile.supports_secret_handle_placeholder_resolution) {
        return ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder;
    }
    if (!profile.supports_config_snapshot_placeholder_load) {
        return ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder;
    }
    return ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope;
}

[[nodiscard]] std::string
missing_runtime_capability_message(ProviderRuntimeCapabilityKind capability) {
    switch (capability) {
    case ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile:
        return "provider runtime does not support secret-free runtime profile load";
    case ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder:
        return "provider runtime does not support secret handle placeholder resolution";
    case ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder:
        return "provider runtime does not support config snapshot placeholder load";
    case ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope:
        return "provider runtime does not support SDK invocation envelope planning";
    }

    return "provider runtime capability is unsupported";
}

[[nodiscard]] ProviderRuntimeReadinessNextActionKind
next_action_for_preflight_plan(const ProviderRuntimePreflightPlan &plan) {
    if (plan.preflight_status == ProviderRuntimePreflightStatus::Ready) {
        return ProviderRuntimeReadinessNextActionKind::ReadyForSdkAdapterImplementation;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch:
            return ProviderRuntimeReadinessNextActionKind::FixRuntimeProfile;
        case ProviderRuntimePreflightFailureKind::UnsupportedRuntimeCapability:
            return ProviderRuntimeReadinessNextActionKind::WaitForRuntimeCapability;
        case ProviderRuntimePreflightFailureKind::DriverBindingNotReady:
            return ProviderRuntimeReadinessNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderRuntimeReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderRuntimePreflightPlan &plan) {
    if (plan.preflight_status == ProviderRuntimePreflightStatus::Ready) {
        return "provider runtime preflight planned an SDK invocation envelope without loading "
               "runtime config, resolving secrets, or invoking a provider SDK";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch) {
        return "provider runtime preflight skipped because runtime profile does not match driver "
               "binding";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderRuntimePreflightFailureKind::UnsupportedRuntimeCapability) {
        return "provider runtime preflight skipped because runtime capability is unsupported";
    }

    return "provider runtime preflight skipped because provider driver binding is not ready";
}

[[nodiscard]] std::string next_step_summary(const ProviderRuntimePreflightPlan &plan) {
    switch (next_action_for_preflight_plan(plan)) {
    case ProviderRuntimeReadinessNextActionKind::ReadyForSdkAdapterImplementation:
        return "ready for future SDK adapter implementation; no runtime config, secret, or SDK "
               "call was used";
    case ProviderRuntimeReadinessNextActionKind::FixRuntimeProfile:
        return "fix runtime profile driver reference and provider namespace before preflight";
    case ProviderRuntimeReadinessNextActionKind::WaitForRuntimeCapability:
        return "wait for runtime config, secret placeholder, or envelope planning capability";
    case ProviderRuntimeReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider runtime preflight can proceed";
    }

    return "manual review required before provider runtime preflight can proceed";
}

void validate_failure_attribution(
    const std::optional<ProviderRuntimePreflightFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

} // namespace

ProviderRuntimeProfileValidationResult
validate_provider_runtime_profile(const ProviderRuntimeProfile &profile) {
    ProviderRuntimeProfileValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (profile.format_version != kProviderRuntimeProfileFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime profile format_version must be '" +
                std::string(kProviderRuntimeProfileFormatVersion) + "'");
    }
    if (profile.runtime_profile_identity.empty() || profile.driver_profile_ref.empty() ||
        profile.provider_namespace.empty() || profile.secret_free_runtime_config_ref.empty() ||
        profile.secret_resolver_policy_ref.empty() || profile.config_snapshot_policy_ref.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile identity fields must "
                              "not be empty");
    }
    if (!profile.credential_free) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile must be "
                              "credential_free");
    }
    if (profile.credential_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "credential_reference");
    }
    if (profile.secret_value.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "secret_value");
    }
    if (profile.secret_manager_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "secret_manager_endpoint_uri");
    }
    if (profile.provider_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "provider_endpoint_uri");
    }
    if (profile.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "object_path");
    }
    if (profile.database_table.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "database_table");
    }
    if (profile.sdk_payload_schema.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "sdk_payload_schema");
    }
    if (profile.sdk_request_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime profile cannot contain "
                              "sdk_request_payload");
    }

    return result;
}

ProviderRuntimePreflightPlanValidationResult
validate_provider_runtime_preflight_plan(const ProviderRuntimePreflightPlan &plan) {
    ProviderRuntimePreflightPlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderRuntimePreflightPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime preflight plan format_version must be '" +
                std::string(kProviderRuntimePreflightPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_driver_binding_plan_format_version !=
        kProviderDriverBindingPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime preflight plan "
            "source_durable_store_import_provider_driver_binding_plan_format_version must be '" +
                std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_driver_profile_format_version !=
        kProviderDriverProfileFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime preflight plan "
            "source_durable_store_import_provider_driver_profile_format_version must be '" +
                std::string(kProviderDriverProfileFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_driver_binding_identity.empty() ||
        plan.durable_store_import_provider_runtime_preflight_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime preflight plan identity "
                              "fields must not be empty");
    }

    result.diagnostics.append(validate_provider_runtime_profile(plan.runtime_profile).diagnostics);
    validate_failure_attribution(plan.failure_attribution,
                                 diagnostics,
                                 "durable store import provider runtime preflight plan");

    if (plan.loads_runtime_config) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime preflight plan cannot load "
                              "runtime config");
    }
    if (plan.resolves_secret_handles) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime preflight plan cannot resolve "
                              "secret handles");
    }
    if (plan.invokes_provider_sdk) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime preflight plan cannot invoke "
                              "provider SDK");
    }

    if (plan.preflight_status == ProviderRuntimePreflightStatus::Ready) {
        if (plan.source_binding_status != ProviderDriverBindingStatus::Bound ||
            !plan.provider_persistence_id.has_value() ||
            !plan.source_operation_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan ready "
                                  "status requires bound driver binding and operation descriptor");
        }
        if (!plan.runtime_profile.supports_secret_free_runtime_profile_load ||
            !plan.runtime_profile.supports_secret_handle_placeholder_resolution ||
            !plan.runtime_profile.supports_config_snapshot_placeholder_load ||
            !plan.runtime_profile.supports_sdk_invocation_envelope_planning) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan ready "
                                  "status requires runtime preflight capabilities");
        }
        if (plan.operation_kind !=
                ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope ||
            !plan.sdk_invocation_envelope_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan ready "
                                  "status requires SDK invocation envelope identity");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan ready "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind ==
            ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan blocked "
                                  "status cannot plan SDK invocation envelope");
        }
        if (plan.sdk_invocation_envelope_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan blocked "
                                  "status cannot contain SDK invocation envelope identity");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime preflight plan blocked "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

ProviderRuntimeReadinessReviewValidationResult
validate_provider_runtime_readiness_review(const ProviderRuntimeReadinessReview &review) {
    ProviderRuntimeReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderRuntimeReadinessReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime readiness review format_version must be '" +
                std::string(kProviderRuntimeReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_runtime_preflight_plan_format_version !=
        kProviderRuntimePreflightPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime readiness review "
            "source_durable_store_import_provider_runtime_preflight_plan_format_version must be '" +
                std::string(kProviderRuntimePreflightPlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_driver_binding_plan_format_version !=
        kProviderDriverBindingPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider runtime readiness review "
            "source_durable_store_import_provider_driver_binding_plan_format_version must be '" +
                std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_driver_binding_identity.empty() ||
        review.durable_store_import_provider_runtime_preflight_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime readiness review identity "
                              "fields must not be empty");
    }
    if (review.loads_runtime_config) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime readiness review cannot load "
                              "runtime config");
    }
    if (review.resolves_secret_handles) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime readiness review cannot "
                              "resolve secret handles");
    }
    if (review.invokes_provider_sdk) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime readiness review cannot "
                              "invoke provider SDK");
    }
    if (review.runtime_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider runtime readiness review summaries "
                              "must not be empty");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider runtime readiness review");

    if (review.preflight_status == ProviderRuntimePreflightStatus::Ready) {
        if (review.operation_kind !=
                ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope ||
            !review.sdk_invocation_envelope_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime readiness review ready "
                                  "status requires SDK invocation envelope identity");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime readiness review ready "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (review.sdk_invocation_envelope_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime readiness review blocked "
                                  "status cannot contain SDK invocation envelope identity");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider runtime readiness review blocked "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

ProviderRuntimeProfile
build_default_provider_runtime_profile(const ProviderDriverBindingPlan &plan) {
    return ProviderRuntimeProfile{
        .format_version = std::string(kProviderRuntimeProfileFormatVersion),
        .runtime_profile_identity =
            "provider-runtime-profile::" + plan.driver_profile.driver_profile_identity,
        .driver_profile_ref = plan.driver_profile.driver_profile_identity,
        .provider_namespace = plan.driver_profile.provider_namespace,
        .secret_free_runtime_config_ref =
            "secret-free-runtime-config::" + plan.driver_profile.driver_profile_identity,
        .secret_resolver_policy_ref =
            "secret-resolver-policy::" + plan.driver_profile.driver_profile_identity,
        .config_snapshot_policy_ref =
            "config-snapshot-policy::" + plan.driver_profile.driver_profile_identity,
        .credential_free = true,
        .supports_secret_free_runtime_profile_load = true,
        .supports_secret_handle_placeholder_resolution = true,
        .supports_config_snapshot_placeholder_load = true,
        .supports_sdk_invocation_envelope_planning = true,
        .credential_reference = std::nullopt,
        .secret_value = std::nullopt,
        .secret_manager_endpoint_uri = std::nullopt,
        .provider_endpoint_uri = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_payload_schema = std::nullopt,
        .sdk_request_payload = std::nullopt,
    };
}

ProviderRuntimePreflightPlanResult
build_provider_runtime_preflight_plan(const ProviderDriverBindingPlan &plan) {
    auto profile = build_default_provider_runtime_profile(plan);
    return build_provider_runtime_preflight_plan(plan, profile);
}

ProviderRuntimePreflightPlanResult
build_provider_runtime_preflight_plan(const ProviderDriverBindingPlan &plan,
                                      const ProviderRuntimeProfile &profile) {
    ProviderRuntimePreflightPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_driver_binding_plan(plan).diagnostics);
    result.diagnostics.append(validate_provider_runtime_profile(profile).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto profile_matches = profile_matches_driver_binding(plan, profile);
    const auto has_capabilities = profile.supports_secret_free_runtime_profile_load &&
                                  profile.supports_secret_handle_placeholder_resolution &&
                                  profile.supports_config_snapshot_placeholder_load &&
                                  profile.supports_sdk_invocation_envelope_planning;
    const auto can_preflight = plan.binding_status == ProviderDriverBindingStatus::Bound &&
                               plan.operation_descriptor_identity.has_value() &&
                               plan.provider_persistence_id.has_value() && profile_matches &&
                               has_capabilities;

    ProviderRuntimeOperationKind operation_kind{
        ProviderRuntimeOperationKind::NoopDriverBindingNotReady};
    std::optional<ProviderRuntimePreflightFailureAttribution> failure = std::nullopt;
    if (can_preflight) {
        operation_kind = ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope;
    } else if (plan.binding_status != ProviderDriverBindingStatus::Bound ||
               !plan.operation_descriptor_identity.has_value()) {
        operation_kind = ProviderRuntimeOperationKind::NoopDriverBindingNotReady;
        failure = driver_binding_not_ready_failure(plan);
    } else if (!profile_matches) {
        operation_kind = ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch;
        failure = ProviderRuntimePreflightFailureAttribution{
            .kind = ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch,
            .message = "provider runtime profile does not match provider driver binding",
            .missing_capability = std::nullopt,
        };
    } else {
        const auto missing_capability = first_missing_runtime_capability(profile);
        operation_kind = ProviderRuntimeOperationKind::NoopUnsupportedRuntimeCapability;
        failure = ProviderRuntimePreflightFailureAttribution{
            .kind = ProviderRuntimePreflightFailureKind::UnsupportedRuntimeCapability,
            .message = missing_runtime_capability_message(missing_capability),
            .missing_capability = missing_capability,
        };
    }

    const auto status = can_preflight ? ProviderRuntimePreflightStatus::Ready
                                      : ProviderRuntimePreflightStatus::Blocked;
    ProviderRuntimePreflightPlan preflight_plan{
        .format_version = std::string(kProviderRuntimePreflightPlanFormatVersion),
        .source_durable_store_import_provider_driver_binding_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_driver_profile_format_version =
            plan.driver_profile.format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_driver_binding_identity =
            plan.durable_store_import_provider_driver_binding_identity,
        .provider_persistence_id = plan.provider_persistence_id,
        .source_binding_status = plan.binding_status,
        .source_operation_descriptor_identity = plan.operation_descriptor_identity,
        .runtime_profile = profile,
        .durable_store_import_provider_runtime_preflight_identity =
            preflight_identity(plan, status),
        .operation_kind = operation_kind,
        .preflight_status = status,
        .sdk_invocation_envelope_identity =
            can_preflight ? sdk_invocation_envelope_identity(plan) : std::nullopt,
        .loads_runtime_config = false,
        .resolves_secret_handles = false,
        .invokes_provider_sdk = false,
        .failure_attribution = failure,
    };

    result.diagnostics.append(validate_provider_runtime_preflight_plan(preflight_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(preflight_plan);
    return result;
}

ProviderRuntimeReadinessReviewResult
build_provider_runtime_readiness_review(const ProviderRuntimePreflightPlan &plan) {
    ProviderRuntimeReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_runtime_preflight_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderRuntimeReadinessReview review{
        .format_version = std::string(kProviderRuntimeReadinessReviewFormatVersion),
        .source_durable_store_import_provider_runtime_preflight_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_driver_binding_plan_format_version =
            plan.source_durable_store_import_provider_driver_binding_plan_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_driver_binding_identity =
            plan.durable_store_import_provider_driver_binding_identity,
        .durable_store_import_provider_runtime_preflight_identity =
            plan.durable_store_import_provider_runtime_preflight_identity,
        .preflight_status = plan.preflight_status,
        .operation_kind = plan.operation_kind,
        .sdk_invocation_envelope_identity = plan.sdk_invocation_envelope_identity,
        .loads_runtime_config = plan.loads_runtime_config,
        .resolves_secret_handles = plan.resolves_secret_handles,
        .invokes_provider_sdk = plan.invokes_provider_sdk,
        .failure_attribution = plan.failure_attribution,
        .runtime_boundary_summary = boundary_summary(plan),
        .next_step_recommendation = next_step_summary(plan),
        .next_action = next_action_for_preflight_plan(plan),
    };

    result.diagnostics.append(validate_provider_runtime_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
