#include "ahfl/durable_store_import/provider_secret.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_SECRET";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderSecretStatus status) {
    switch (status) {
    case ProviderSecretStatus::Ready:
        return "ready";
    case ProviderSecretStatus::Blocked:
        return "blocked";
    }
    return "invalid";
}

[[nodiscard]] std::string request_identity(const ProviderConfigSnapshotPlaceholder &snapshot,
                                           ProviderSecretStatus status) {
    return "durable-store-import-provider-secret-resolver-request::" + snapshot.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string response_identity(const ProviderSecretResolverRequestPlan &plan,
                                            ProviderSecretStatus status) {
    return "durable-store-import-provider-secret-resolver-response::" + plan.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string handle_identity(const ProviderConfigSnapshotPlaceholder &snapshot) {
    return "provider-secret-handle::" + snapshot.provider_config_profile_identity +
           "::provider-config-credential";
}

[[nodiscard]] ProviderSecretFailureAttribution
config_snapshot_not_ready_failure(const ProviderConfigSnapshotPlaceholder &snapshot) {
    std::string message =
        "provider secret resolver request cannot proceed because config snapshot is not ready";
    if (snapshot.failure_attribution.has_value()) {
        message = snapshot.failure_attribution->message;
    }
    return ProviderSecretFailureAttribution{
        .kind = ProviderSecretFailureKind::ConfigSnapshotNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderSecretFailureAttribution
request_not_ready_failure(const ProviderSecretResolverRequestPlan &plan) {
    std::string message = "provider secret resolver response placeholder cannot proceed because "
                          "resolver request is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }
    return ProviderSecretFailureAttribution{
        .kind = ProviderSecretFailureKind::SecretResolverRequestNotReady,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderSecretFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_handle(const ProviderSecretHandleReference &handle,
                     DiagnosticBag &diagnostics,
                     std::string_view owner) {
    if (handle.handle_schema_version != kProviderSecretHandleSchemaVersion ||
        handle.handle_identity.empty() || handle.provider_key.empty() ||
        handle.profile_key.empty() || handle.purpose.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " secret handle fields must be valid and non-empty");
    }
    if (handle.contains_secret_value || handle.contains_credential_material ||
        handle.contains_token_value) {
        emit_validation_error(
            diagnostics,
            std::string(owner) +
                " secret handle cannot contain secret, credential, or token value");
    }
}

void validate_no_side_effects(bool reads_secret_material,
                              bool materializes_secret_value,
                              bool materializes_credential_material,
                              bool materializes_token_value,
                              bool opens_network_connection,
                              bool reads_host_environment,
                              bool writes_host_filesystem,
                              bool invokes_secret_manager,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (reads_secret_material) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (materializes_secret_value) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot materialize secret value");
    }
    if (materializes_credential_material) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize credential material");
    }
    if (materializes_token_value) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot materialize token value");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_host_environment) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot write host filesystem");
    }
    if (invokes_secret_manager) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke secret manager");
    }
}

void validate_forbidden_material(const ProviderSecretResolverRequestPlan &plan,
                                 DiagnosticBag &diagnostics) {
    if (plan.secret_value.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan cannot "
                              "contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan cannot "
                              "contain credential_material");
    }
    if (plan.token_value.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan cannot "
                              "contain token_value");
    }
    if (plan.secret_manager_response.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan cannot "
                              "contain secret_manager_response");
    }
}

[[nodiscard]] ProviderSecretPolicyNextActionKind
next_action_for_placeholder(const ProviderSecretResolverResponsePlaceholder &placeholder) {
    if (placeholder.response_status == ProviderSecretStatus::Ready) {
        return ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness;
    }
    if (placeholder.failure_attribution.has_value()) {
        switch (placeholder.failure_attribution->kind) {
        case ProviderSecretFailureKind::ConfigSnapshotNotReady:
        case ProviderSecretFailureKind::SecretResolverRequestNotReady:
            return ProviderSecretPolicyNextActionKind::WaitForConfigSnapshot;
        case ProviderSecretFailureKind::SecretPolicyViolation:
            return ProviderSecretPolicyNextActionKind::ManualReviewRequired;
        }
    }
    return ProviderSecretPolicyNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string
policy_summary(const ProviderSecretResolverResponsePlaceholder &placeholder) {
    if (placeholder.response_status == ProviderSecretStatus::Ready) {
        return "provider secret resolver response placeholder is ready with secret handle only; no "
               "secret value, credential material, token value, secret manager call, network, host "
               "env, or filesystem write was used";
    }
    return "provider secret resolver response placeholder is blocked because resolver request is "
           "not ready";
}

} // namespace

ProviderSecretResolverRequestPlanValidationResult
validate_provider_secret_resolver_request_plan(const ProviderSecretResolverRequestPlan &plan) {
    ProviderSecretResolverRequestPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderSecretResolverRequestPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan format_version must be '" +
                std::string(kProviderSecretResolverRequestPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan source "
                              "format_version must be '" +
                                  std::string(kProviderConfigSnapshotPlaceholderFormatVersion) +
                                  "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_config_snapshot_identity.empty() ||
        plan.provider_config_profile_identity.empty() ||
        plan.provider_config_snapshot_placeholder_identity.empty() ||
        plan.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        plan.provider_secret_resolver_response_placeholder_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan identity "
                              "fields must not be empty");
    }
    validate_handle(plan.secret_handle,
                    diagnostics,
                    "durable store import provider secret resolver request plan");
    validate_failure(plan.failure_attribution,
                     diagnostics,
                     "durable store import provider secret resolver request plan");
    validate_no_side_effects(plan.reads_secret_material,
                             plan.materializes_secret_value,
                             plan.materializes_credential_material,
                             plan.materializes_token_value,
                             plan.opens_network_connection,
                             plan.reads_host_environment,
                             plan.writes_host_filesystem,
                             plan.invokes_secret_manager,
                             diagnostics,
                             "durable store import provider secret resolver request plan");
    validate_forbidden_material(plan, diagnostics);
    if (plan.request_status == ProviderSecretStatus::Ready) {
        if (plan.source_config_snapshot_status != ProviderConfigStatus::Ready ||
            plan.operation_kind != ProviderSecretOperationKind::PlanSecretResolverRequest ||
            plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider secret resolver request plan "
                                  "ready status requires ready config snapshot and no failure");
        }
    } else if (plan.operation_kind == ProviderSecretOperationKind::PlanSecretResolverRequest ||
               !plan.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver request plan blocked "
                              "status requires noop operation and failure_attribution");
    }
    return result;
}

ProviderSecretResolverResponsePlaceholderValidationResult
validate_provider_secret_resolver_response_placeholder(
    const ProviderSecretResolverResponsePlaceholder &placeholder) {
    ProviderSecretResolverResponsePlaceholderValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (placeholder.format_version != kProviderSecretResolverResponsePlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver response placeholder format_version "
            "must be '" +
                std::string(kProviderSecretResolverResponsePlaceholderFormatVersion) + "'");
    }
    if (placeholder
            .source_durable_store_import_provider_secret_resolver_request_plan_format_version !=
        kProviderSecretResolverRequestPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver response placeholder "
                              "source format_version must be '" +
                                  std::string(kProviderSecretResolverRequestPlanFormatVersion) +
                                  "'");
    }
    if (placeholder.workflow_canonical_name.empty() || placeholder.session_id.empty() ||
        placeholder.input_fixture.empty() ||
        placeholder.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        placeholder.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        placeholder.credential_lifecycle_version.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver response placeholder "
                              "identity fields must not be empty");
    }
    if (placeholder.credential_lifecycle_version != kProviderCredentialLifecycleVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver response placeholder "
                              "credential_lifecycle_version must be '" +
                                  std::string(kProviderCredentialLifecycleVersion) + "'");
    }
    validate_handle(placeholder.secret_handle,
                    diagnostics,
                    "durable store import provider secret resolver response placeholder");
    validate_failure(placeholder.failure_attribution,
                     diagnostics,
                     "durable store import provider secret resolver response placeholder");
    validate_no_side_effects(placeholder.reads_secret_material,
                             placeholder.materializes_secret_value,
                             placeholder.materializes_credential_material,
                             placeholder.materializes_token_value,
                             placeholder.opens_network_connection,
                             placeholder.reads_host_environment,
                             placeholder.writes_host_filesystem,
                             placeholder.invokes_secret_manager,
                             diagnostics,
                             "durable store import provider secret resolver response placeholder");
    if (placeholder.response_status == ProviderSecretStatus::Ready) {
        if (placeholder.source_secret_resolver_request_status != ProviderSecretStatus::Ready ||
            placeholder.operation_kind !=
                ProviderSecretOperationKind::PlanSecretResolverResponsePlaceholder ||
            placeholder.credential_lifecycle_state !=
                ProviderCredentialLifecycleState::PlaceholderPendingResolution ||
            placeholder.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider secret resolver response placeholder ready status "
                "requires ready request and pending lifecycle");
        }
    } else if (placeholder.operation_kind ==
                   ProviderSecretOperationKind::PlanSecretResolverResponsePlaceholder ||
               !placeholder.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret resolver response placeholder "
                              "blocked status requires noop operation and failure_attribution");
    }
    return result;
}

ProviderSecretPolicyReviewValidationResult
validate_provider_secret_policy_review(const ProviderSecretPolicyReview &review) {
    ProviderSecretPolicyReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderSecretPolicyReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider secret policy review format_version must be '" +
                std::string(kProviderSecretPolicyReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_secret_resolver_response_placeholder_format_version !=
        kProviderSecretResolverResponsePlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider secret policy review source format_version must be '" +
                std::string(kProviderSecretResolverResponsePlaceholderFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        review.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        review.secret_policy_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret policy review identity and "
                              "summary fields must not be empty");
    }
    validate_handle(
        review.secret_handle, diagnostics, "durable store import provider secret policy review");
    validate_failure(review.failure_attribution,
                     diagnostics,
                     "durable store import provider secret policy review");
    validate_no_side_effects(review.reads_secret_material,
                             review.materializes_secret_value,
                             review.materializes_credential_material,
                             review.materializes_token_value,
                             review.opens_network_connection,
                             review.reads_host_environment,
                             review.writes_host_filesystem,
                             review.invokes_secret_manager,
                             diagnostics,
                             "durable store import provider secret policy review");
    if (review.response_status == ProviderSecretStatus::Ready) {
        if (review.next_action != ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness ||
            review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider secret policy review ready status "
                                  "requires local harness next action and no failure");
        }
    } else if (!review.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider secret policy review blocked status "
                              "requires failure_attribution");
    }
    return result;
}

ProviderSecretResolverRequestPlanResult
build_provider_secret_resolver_request_plan(const ProviderConfigSnapshotPlaceholder &snapshot) {
    ProviderSecretResolverRequestPlanResult result;
    result.diagnostics.append(validate_provider_config_snapshot_placeholder(snapshot).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = snapshot.snapshot_status == ProviderConfigStatus::Ready;
    const auto status = ready ? ProviderSecretStatus::Ready : ProviderSecretStatus::Blocked;
    ProviderSecretResolverRequestPlan plan;
    plan.workflow_canonical_name = snapshot.workflow_canonical_name;
    plan.session_id = snapshot.session_id;
    plan.run_id = snapshot.run_id;
    plan.input_fixture = snapshot.input_fixture;
    plan.durable_store_import_provider_config_snapshot_identity =
        snapshot.durable_store_import_provider_config_snapshot_identity;
    plan.source_config_snapshot_status = snapshot.snapshot_status;
    plan.provider_config_profile_identity = snapshot.provider_config_profile_identity;
    plan.provider_config_snapshot_placeholder_identity =
        snapshot.provider_config_snapshot_placeholder_identity;
    plan.durable_store_import_provider_secret_resolver_request_identity =
        request_identity(snapshot, status);
    plan.request_status = status;
    plan.secret_handle.handle_identity = handle_identity(snapshot);
    plan.secret_handle.profile_key = snapshot.provider_config_profile_identity;
    plan.provider_secret_resolver_response_placeholder_identity =
        "provider-secret-resolver-response-placeholder::" + plan.secret_handle.handle_identity;
    if (ready) {
        plan.operation_kind = ProviderSecretOperationKind::PlanSecretResolverRequest;
    } else {
        plan.operation_kind = ProviderSecretOperationKind::NoopConfigSnapshotNotReady;
        plan.failure_attribution = config_snapshot_not_ready_failure(snapshot);
    }
    result.diagnostics.append(validate_provider_secret_resolver_request_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderSecretResolverResponsePlaceholderResult
build_provider_secret_resolver_response_placeholder(const ProviderSecretResolverRequestPlan &plan) {
    ProviderSecretResolverResponsePlaceholderResult result;
    result.diagnostics.append(validate_provider_secret_resolver_request_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = plan.request_status == ProviderSecretStatus::Ready;
    const auto status = ready ? ProviderSecretStatus::Ready : ProviderSecretStatus::Blocked;
    ProviderSecretResolverResponsePlaceholder placeholder;
    placeholder.workflow_canonical_name = plan.workflow_canonical_name;
    placeholder.session_id = plan.session_id;
    placeholder.run_id = plan.run_id;
    placeholder.input_fixture = plan.input_fixture;
    placeholder.durable_store_import_provider_secret_resolver_request_identity =
        plan.durable_store_import_provider_secret_resolver_request_identity;
    placeholder.source_secret_resolver_request_status = plan.request_status;
    placeholder.durable_store_import_provider_secret_resolver_response_identity =
        response_identity(plan, status);
    placeholder.response_status = status;
    placeholder.secret_handle = plan.secret_handle;
    if (ready) {
        placeholder.operation_kind =
            ProviderSecretOperationKind::PlanSecretResolverResponsePlaceholder;
        placeholder.credential_lifecycle_state =
            ProviderCredentialLifecycleState::PlaceholderPendingResolution;
    } else {
        placeholder.operation_kind = ProviderSecretOperationKind::NoopSecretResolverRequestNotReady;
        placeholder.credential_lifecycle_state = ProviderCredentialLifecycleState::Blocked;
        placeholder.failure_attribution = request_not_ready_failure(plan);
    }
    result.diagnostics.append(
        validate_provider_secret_resolver_response_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.placeholder = std::move(placeholder);
    return result;
}

ProviderSecretPolicyReviewResult
build_provider_secret_policy_review(const ProviderSecretResolverResponsePlaceholder &placeholder) {
    ProviderSecretPolicyReviewResult result;
    result.diagnostics.append(
        validate_provider_secret_resolver_response_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderSecretPolicyReview review;
    review.workflow_canonical_name = placeholder.workflow_canonical_name;
    review.session_id = placeholder.session_id;
    review.run_id = placeholder.run_id;
    review.input_fixture = placeholder.input_fixture;
    review.durable_store_import_provider_secret_resolver_request_identity =
        placeholder.durable_store_import_provider_secret_resolver_request_identity;
    review.durable_store_import_provider_secret_resolver_response_identity =
        placeholder.durable_store_import_provider_secret_resolver_response_identity;
    review.response_status = placeholder.response_status;
    review.operation_kind = placeholder.operation_kind;
    review.secret_handle = placeholder.secret_handle;
    review.credential_lifecycle_version = placeholder.credential_lifecycle_version;
    review.credential_lifecycle_state = placeholder.credential_lifecycle_state;
    review.failure_attribution = placeholder.failure_attribution;
    review.secret_policy_summary = policy_summary(placeholder);
    review.next_action = next_action_for_placeholder(placeholder);
    review.next_step_recommendation =
        review.next_action == ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness
            ? "ready for test-only local host harness; only a secret handle placeholder exists"
            : "wait for ready config snapshot and secret resolver placeholder";
    result.diagnostics.append(validate_provider_secret_policy_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
