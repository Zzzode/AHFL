#include "ahfl/durable_store_import/provider_driver.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_DRIVER";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string planning_status_slug(ProviderWritePlanningStatus status) {
    switch (status) {
    case ProviderWritePlanningStatus::Planned:
        return "planned";
    case ProviderWritePlanningStatus::NotPlanned:
        return "not-planned";
    }

    return "invalid";
}

[[nodiscard]] std::string binding_identity(const ProviderWriteAttemptPreview &preview) {
    return "durable-store-import-provider-driver-binding::" + preview.session_id +
           "::" + planning_status_slug(preview.planning_status);
}

[[nodiscard]] std::optional<std::string>
operation_descriptor_identity(const ProviderWriteAttemptPreview &preview) {
    if (!preview.write_intent.provider_persistence_id.has_value()) {
        return std::nullopt;
    }

    return "provider-driver-operation::" + *preview.write_intent.provider_persistence_id;
}

[[nodiscard]] bool profile_matches_write_attempt(const ProviderWriteAttemptPreview &preview,
                                                 const ProviderDriverProfile &profile) {
    return profile.provider_profile_ref == preview.provider_config.provider_profile_ref &&
           profile.provider_namespace == preview.provider_config.provider_namespace;
}

[[nodiscard]] ProviderDriverBindingFailureAttribution
source_not_planned_failure(const ProviderWriteAttemptPreview &preview) {
    std::string message = "provider driver binding cannot proceed because source provider write "
                          "attempt is not planned";
    if (preview.failure_attribution.has_value()) {
        message = preview.failure_attribution->message;
    }

    return ProviderDriverBindingFailureAttribution{
        .kind = ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned,
        .message = std::move(message),
        .missing_capability = std::nullopt,
    };
}

[[nodiscard]] ProviderDriverReadinessNextActionKind
next_action_for_binding_plan(const ProviderDriverBindingPlan &plan) {
    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        return ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderDriverBindingFailureKind::DriverProfileMismatch:
            return ProviderDriverReadinessNextActionKind::FixProviderProfile;
        case ProviderDriverBindingFailureKind::UnsupportedDriverCapability:
            return ProviderDriverReadinessNextActionKind::WaitForDriverCapability;
        case ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned:
            return ProviderDriverReadinessNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderDriverReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderDriverBindingPlan &plan) {
    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        return "provider-specific driver binding is planned without invoking a provider SDK";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderDriverBindingFailureKind::DriverProfileMismatch) {
        return "provider driver binding skipped because driver profile does not match provider "
               "adapter config";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderDriverBindingFailureKind::UnsupportedDriverCapability) {
        return "provider driver binding skipped because driver capability is unsupported";
    }

    return "provider driver binding skipped because source provider write attempt is not planned";
}

[[nodiscard]] std::string next_step_summary(const ProviderDriverBindingPlan &plan) {
    switch (next_action_for_binding_plan(plan)) {
    case ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation:
        return "ready for future provider driver implementation; no provider SDK was invoked";
    case ProviderDriverReadinessNextActionKind::FixProviderProfile:
        return "fix provider profile reference and namespace before binding a driver";
    case ProviderDriverReadinessNextActionKind::WaitForDriverCapability:
        return "wait for driver write intent translation capability before binding";
    case ProviderDriverReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider driver binding can proceed";
    }

    return "manual review required before provider driver binding can proceed";
}

void validate_failure_attribution(
    const std::optional<ProviderDriverBindingFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

} // namespace

ProviderDriverProfileValidationResult
validate_provider_driver_profile(const ProviderDriverProfile &profile) {
    ProviderDriverProfileValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (profile.format_version != kProviderDriverProfileFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver profile format_version must be '" +
                std::string(kProviderDriverProfileFormatVersion) + "'");
    }
    if (profile.driver_profile_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile "
                              "driver_profile_identity must not be empty");
    }
    if (profile.provider_profile_ref.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile provider_profile_ref "
                              "must not be empty");
    }
    if (profile.provider_namespace.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile provider_namespace "
                              "must not be empty");
    }
    if (profile.secret_free_config_ref.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile "
                              "secret_free_config_ref must not be empty");
    }
    if (!profile.credential_free) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile must be "
                              "credential_free");
    }
    if (profile.credential_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "credential_reference");
    }
    if (profile.endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "endpoint_uri");
    }
    if (profile.endpoint_secret_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "endpoint_secret_reference");
    }
    if (profile.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "object_path");
    }
    if (profile.database_table.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "database_table");
    }
    if (profile.sdk_payload_schema.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver profile cannot contain "
                              "sdk_payload_schema");
    }

    return result;
}

ProviderDriverBindingPlanValidationResult
validate_provider_driver_binding_plan(const ProviderDriverBindingPlan &plan) {
    ProviderDriverBindingPlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderDriverBindingPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan format_version must be '" +
                std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_adapter_config_format_version !=
        kProviderAdapterConfigFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver binding plan "
                              "source_durable_store_import_provider_adapter_config_format_version "
                              "must be '" +
                                  std::string(kProviderAdapterConfigFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_capability_matrix_format_version !=
        kProviderCapabilityMatrixFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan "
            "source_durable_store_import_provider_capability_matrix_format_version must be '" +
                std::string(kProviderCapabilityMatrixFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_write_attempt_identity.empty() ||
        plan.durable_store_import_provider_driver_binding_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver binding plan identity fields "
                              "must not be empty");
    }

    result.diagnostics.append(validate_provider_driver_profile(plan.driver_profile).diagnostics);
    validate_failure_attribution(
        plan.failure_attribution, diagnostics, "durable store import provider driver binding plan");

    if (plan.invokes_provider_sdk) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver binding plan cannot invoke "
                              "provider SDK");
    }

    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        if (plan.source_planning_status != ProviderWritePlanningStatus::Planned ||
            !plan.provider_persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan bound status "
                                  "requires planned source write and provider_persistence_id");
        }
        if (!plan.driver_profile.supports_secret_free_profile_load) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan bound status "
                                  "requires secret-free profile load capability");
        }
        if (!plan.driver_profile.supports_write_intent_translation) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan bound status "
                                  "requires write intent translation capability");
        }
        if (plan.operation_kind != ProviderDriverOperationKind::TranslateProviderPersistReceipt ||
            !plan.operation_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan bound status "
                                  "requires translation operation descriptor");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan bound status "
                                  "cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderDriverOperationKind::TranslateProviderPersistReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan not-bound "
                                  "status cannot translate provider persist receipt");
        }
        if (plan.operation_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan not-bound "
                                  "status cannot contain operation_descriptor_identity");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver binding plan not-bound "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

ProviderDriverReadinessReviewValidationResult
validate_provider_driver_readiness_review(const ProviderDriverReadinessReview &review) {
    ProviderDriverReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderDriverReadinessReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review format_version must be '" +
                std::string(kProviderDriverReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_driver_binding_plan_format_version !=
        kProviderDriverBindingPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver readiness review "
                              "source_durable_store_import_provider_driver_binding_plan_format_"
                              "version must be '" +
                                  std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_attempt_identity.empty() ||
        review.durable_store_import_provider_driver_binding_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver readiness review identity "
                              "fields must not be empty");
    }
    if (review.invokes_provider_sdk) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver readiness review cannot "
                              "invoke provider SDK");
    }
    if (review.driver_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider driver readiness review summaries "
                              "must not be empty");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider driver readiness review");

    if (review.binding_status == ProviderDriverBindingStatus::Bound) {
        if (review.operation_kind != ProviderDriverOperationKind::TranslateProviderPersistReceipt ||
            !review.operation_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver readiness review bound "
                                  "status requires translation operation descriptor");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver readiness review bound "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (review.operation_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver readiness review "
                                  "not-bound status cannot contain operation_descriptor_identity");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider driver readiness review "
                                  "not-bound status requires failure_attribution");
        }
    }

    return result;
}

ProviderDriverProfile
build_default_provider_driver_profile(const ProviderWriteAttemptPreview &preview) {
    return ProviderDriverProfile{
        .format_version = std::string(kProviderDriverProfileFormatVersion),
        .driver_kind = ProviderDriverKind::ProviderNeutralPreviewDriver,
        .driver_profile_identity =
            "provider-driver-profile::" + preview.provider_config.config_identity,
        .provider_profile_ref = preview.provider_config.provider_profile_ref,
        .provider_namespace = preview.provider_config.provider_namespace,
        .secret_free_config_ref =
            "secret-free-driver-config::" + preview.provider_config.config_identity,
        .credential_free = true,
        .supports_secret_free_profile_load = true,
        .supports_write_intent_translation = true,
        .supports_retry_placeholder_translation = true,
        .supports_resume_placeholder_translation = true,
        .credential_reference = std::nullopt,
        .endpoint_uri = std::nullopt,
        .endpoint_secret_reference = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_payload_schema = std::nullopt,
    };
}

ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview) {
    auto profile = build_default_provider_driver_profile(preview);
    return build_provider_driver_binding_plan(preview, profile);
}

ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview,
                                   const ProviderDriverProfile &profile) {
    ProviderDriverBindingPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_write_attempt_preview(preview).diagnostics);
    result.diagnostics.append(validate_provider_driver_profile(profile).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto profile_matches = profile_matches_write_attempt(preview, profile);
    const auto can_bind = preview.planning_status == ProviderWritePlanningStatus::Planned &&
                          preview.write_intent.provider_persistence_id.has_value() &&
                          profile_matches && profile.supports_secret_free_profile_load &&
                          profile.supports_write_intent_translation;

    ProviderDriverOperationKind operation_kind{ProviderDriverOperationKind::NoopSourceNotPlanned};
    std::optional<ProviderDriverBindingFailureAttribution> failure = std::nullopt;
    if (can_bind) {
        operation_kind = ProviderDriverOperationKind::TranslateProviderPersistReceipt;
    } else if (preview.planning_status != ProviderWritePlanningStatus::Planned) {
        operation_kind = ProviderDriverOperationKind::NoopSourceNotPlanned;
        failure = source_not_planned_failure(preview);
    } else if (!profile_matches) {
        operation_kind = ProviderDriverOperationKind::NoopProfileMismatch;
        failure = ProviderDriverBindingFailureAttribution{
            .kind = ProviderDriverBindingFailureKind::DriverProfileMismatch,
            .message = "provider driver profile does not match provider adapter config",
            .missing_capability = std::nullopt,
        };
    } else {
        operation_kind = ProviderDriverOperationKind::NoopUnsupportedDriverCapability;
        auto missing_capability = ProviderDriverCapabilityKind::TranslateWriteIntent;
        std::string message = "provider driver does not support write intent translation";
        if (!profile.supports_secret_free_profile_load) {
            missing_capability = ProviderDriverCapabilityKind::LoadSecretFreeProfile;
            message = "provider driver does not support secret-free profile load";
        }
        failure = ProviderDriverBindingFailureAttribution{
            .kind = ProviderDriverBindingFailureKind::UnsupportedDriverCapability,
            .message = std::move(message),
            .missing_capability = missing_capability,
        };
    }

    ProviderDriverBindingPlan plan{
        .format_version = std::string(kProviderDriverBindingPlanFormatVersion),
        .source_durable_store_import_provider_write_attempt_preview_format_version =
            preview.format_version,
        .source_durable_store_import_provider_adapter_config_format_version =
            preview.provider_config.format_version,
        .source_durable_store_import_provider_capability_matrix_format_version =
            preview.capability_matrix.format_version,
        .workflow_canonical_name = preview.workflow_canonical_name,
        .session_id = preview.session_id,
        .run_id = preview.run_id,
        .input_fixture = preview.input_fixture,
        .durable_store_import_provider_write_attempt_identity =
            preview.durable_store_import_provider_write_attempt_identity,
        .durable_store_import_provider_driver_binding_identity = binding_identity(preview),
        .source_planning_status = preview.planning_status,
        .provider_persistence_id = preview.write_intent.provider_persistence_id,
        .driver_profile = profile,
        .operation_kind = operation_kind,
        .binding_status =
            can_bind ? ProviderDriverBindingStatus::Bound : ProviderDriverBindingStatus::NotBound,
        .operation_descriptor_identity =
            can_bind ? operation_descriptor_identity(preview) : std::nullopt,
        .invokes_provider_sdk = false,
        .failure_attribution = failure,
    };

    result.diagnostics.append(validate_provider_driver_binding_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(plan);
    return result;
}

ProviderDriverReadinessReviewResult
build_provider_driver_readiness_review(const ProviderDriverBindingPlan &plan) {
    ProviderDriverReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_driver_binding_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderDriverReadinessReview review{
        .format_version = std::string(kProviderDriverReadinessReviewFormatVersion),
        .source_durable_store_import_provider_driver_binding_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_write_attempt_preview_format_version =
            plan.source_durable_store_import_provider_write_attempt_preview_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_write_attempt_identity =
            plan.durable_store_import_provider_write_attempt_identity,
        .durable_store_import_provider_driver_binding_identity =
            plan.durable_store_import_provider_driver_binding_identity,
        .binding_status = plan.binding_status,
        .operation_kind = plan.operation_kind,
        .operation_descriptor_identity = plan.operation_descriptor_identity,
        .invokes_provider_sdk = plan.invokes_provider_sdk,
        .failure_attribution = plan.failure_attribution,
        .driver_boundary_summary = boundary_summary(plan),
        .next_step_recommendation = next_step_summary(plan),
        .next_action = next_action_for_binding_plan(plan),
    };

    result.diagnostics.append(validate_provider_driver_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
