// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_config.cpp ----

#include "pipeline/persistence/durable_store_import/provider/configuration/config.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_config_detail_kValidationDiagnosticCode{"DSI_PROVIDER_CONFIG"};

void provider_config_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_config_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_config_detail_status_slug(ProviderConfigStatus status) {
    switch (status) {
    case ProviderConfigStatus::Ready:
        return "ready";
    case ProviderConfigStatus::Blocked:
        return "blocked";
    }
    return "invalid";
}

[[nodiscard]] std::string
provider_config_detail_config_load_identity(const ProviderSdkAdapterInterfacePlan &plan,
                                            ProviderConfigStatus status) {
    return "durable-store-import-provider-config-load::" + plan.session_id +
           "::" + provider_config_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_config_detail_snapshot_artifact_identity(const ProviderConfigLoadPlan &plan,
                                                  ProviderConfigStatus status) {
    return "durable-store-import-provider-config-snapshot::" + plan.session_id +
           "::" + provider_config_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_config_detail_profile_identity(const ProviderSdkAdapterInterfacePlan &plan) {
    return "provider-config-profile::" + plan.adapter_descriptor_identity +
           "::local-placeholder-profile";
}

[[nodiscard]] std::string
provider_config_detail_snapshot_placeholder_identity(const std::string &profile_id) {
    return "provider-config-snapshot-placeholder::" + profile_id;
}

[[nodiscard]] ProviderConfigFailureAttribution
provider_config_detail_adapter_interface_not_ready_failure(
    const ProviderSdkAdapterInterfacePlan &plan) {
    std::string message =
        "provider config load cannot proceed because SDK adapter interface is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }
    return ProviderConfigFailureAttribution{
        .kind = ProviderConfigFailureKind::AdapterInterfaceNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderConfigFailureAttribution
provider_config_detail_config_load_not_ready_failure(const ProviderConfigLoadPlan &plan) {
    std::string message =
        "provider config snapshot placeholder cannot proceed because config load is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }
    return ProviderConfigFailureAttribution{
        .kind = ProviderConfigFailureKind::ConfigLoadNotReady,
        .message = std::move(message),
    };
}

void provider_config_detail_validate_failure(
    const std::optional<ProviderConfigFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_config_detail_validate_no_side_effects(bool reads_secret_material,
                                                     bool materializes_secret_value,
                                                     bool materializes_credential_material,
                                                     bool opens_network_connection,
                                                     bool reads_host_environment,
                                                     bool writes_host_filesystem,
                                                     bool materializes_sdk_request_payload,
                                                     bool invokes_provider_sdk,
                                                     DiagnosticBag &diagnostics,
                                                     std::string_view owner) {
    if (reads_secret_material) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (materializes_secret_value) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize secret value");
    }
    if (materializes_credential_material) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize credential material");
    }
    if (opens_network_connection) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_host_environment) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
    if (materializes_sdk_request_payload) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        provider_config_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
}

void provider_config_detail_validate_forbidden_material(const ProviderConfigLoadPlan &plan,
                                                        DiagnosticBag &diagnostics) {
    if (plan.secret_value.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain credential_material");
    }
    if (plan.provider_endpoint_uri.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain provider_endpoint_uri");
    }
    if (plan.remote_config_uri.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain remote_config_uri");
    }
    if (plan.sdk_request_payload.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain sdk_request_payload");
    }
}

[[nodiscard]] ProviderConfigReadinessNextActionKind
provider_config_detail_next_action_for_placeholder(
    const ProviderConfigSnapshotPlaceholder &placeholder) {
    if (placeholder.snapshot_status == ProviderConfigStatus::Ready) {
        return ProviderConfigReadinessNextActionKind::ReadyForSecretHandleResolver;
    }
    if (placeholder.failure_attribution.has_value()) {
        switch (placeholder.failure_attribution->kind) {
        case ProviderConfigFailureKind::AdapterInterfaceNotReady:
        case ProviderConfigFailureKind::ConfigLoadNotReady:
            return ProviderConfigReadinessNextActionKind::WaitForAdapterInterface;
        case ProviderConfigFailureKind::MissingConfigProfile:
        case ProviderConfigFailureKind::IncompatibleConfigSchema:
        case ProviderConfigFailureKind::UnsupportedConfigProfile:
            return ProviderConfigReadinessNextActionKind::ManualReviewRequired;
        }
    }
    return ProviderConfigReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string
provider_config_detail_boundary_summary(const ProviderConfigSnapshotPlaceholder &placeholder) {
    if (placeholder.snapshot_status == ProviderConfigStatus::Ready) {
        return "provider config snapshot placeholder is ready without reading secret material, "
               "materializing credential material, opening network, reading host environment, "
               "writing host filesystem, materializing SDK payload, or invoking provider SDK";
    }
    return "provider config snapshot placeholder is blocked because config load is not ready";
}

[[nodiscard]] std::string
provider_config_detail_next_step_summary(const ProviderConfigSnapshotPlaceholder &placeholder) {
    switch (provider_config_detail_next_action_for_placeholder(placeholder)) {
    case ProviderConfigReadinessNextActionKind::ReadyForSecretHandleResolver:
        return "ready for future secret handle resolver boundary";
    case ProviderConfigReadinessNextActionKind::WaitForAdapterInterface:
        return "wait for ready adapter interface and config load plan";
    case ProviderConfigReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider config can proceed";
    }
    return "manual review required before provider config can proceed";
}

} // namespace

ProviderConfigLoadPlanValidationResult
validate_provider_config_load_plan(const ProviderConfigLoadPlan &plan) {
    ProviderConfigLoadPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderConfigLoadPlanFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan format_version must be '" +
                std::string(kProviderConfigLoadPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version !=
        kProviderSdkAdapterInterfacePlanFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan source format_version must be '" +
                std::string(kProviderSdkAdapterInterfacePlanFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_adapter_interface_identity.empty() ||
        plan.provider_registry_identity.empty() || plan.adapter_descriptor_identity.empty() ||
        plan.provider_key.empty() ||
        plan.durable_store_import_provider_config_load_identity.empty() ||
        plan.provider_config_profile_identity.empty() ||
        plan.provider_config_snapshot_placeholder_identity.empty() ||
        plan.profile_descriptor.profile_key.empty() ||
        plan.profile_descriptor.provider_key.empty() ||
        plan.profile_descriptor.config_schema_version.empty()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan identity and profile "
            "fields must not be empty");
    }
    if (plan.profile_descriptor.config_schema_version != kProviderConfigSchemaVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config load plan config_schema_version must be '" +
                std::string(kProviderConfigSchemaVersion) + "'");
    }
    provider_config_detail_validate_failure(
        plan.failure_attribution, diagnostics, "durable store import provider config load plan");
    provider_config_detail_validate_no_side_effects(
        plan.reads_secret_material,
        plan.materializes_secret_value,
        plan.materializes_credential_material,
        plan.opens_network_connection,
        plan.reads_host_environment,
        plan.writes_host_filesystem,
        plan.materializes_sdk_request_payload,
        plan.invokes_provider_sdk,
        diagnostics,
        "durable store import provider config load plan");
    provider_config_detail_validate_forbidden_material(plan, diagnostics);
    provider_config_detail_validate_no_side_effects(
        false,
        plan.profile_descriptor.materializes_secret_value,
        false,
        plan.profile_descriptor.opens_network_connection,
        false,
        false,
        false,
        false,
        diagnostics,
        "durable store import provider config profile descriptor");
    if (plan.profile_descriptor.contains_endpoint_uri) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config profile descriptor cannot contain endpoint uri");
    }
    if (plan.config_load_status == ProviderConfigStatus::Ready) {
        if (plan.source_adapter_interface_status != ProviderSdkAdapterInterfaceStatus::Ready ||
            plan.operation_kind != ProviderConfigOperationKind::PlanProviderConfigLoad ||
            plan.failure_attribution.has_value()) {
            provider_config_detail_emit_validation_error(
                diagnostics,
                "durable store import provider config load plan ready status "
                "requires ready adapter interface and no failure");
        }
    } else {
        if (plan.operation_kind == ProviderConfigOperationKind::PlanProviderConfigLoad ||
            !plan.failure_attribution.has_value()) {
            provider_config_detail_emit_validation_error(
                diagnostics,
                "durable store import provider config load plan blocked status "
                "requires noop operation and failure_attribution");
        }
    }
    return result;
}

ProviderConfigSnapshotPlaceholderValidationResult validate_provider_config_snapshot_placeholder(
    const ProviderConfigSnapshotPlaceholder &placeholder) {
    ProviderConfigSnapshotPlaceholderValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (placeholder.format_version != kProviderConfigSnapshotPlaceholderFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config snapshot placeholder format_version must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }
    if (placeholder.source_durable_store_import_provider_config_load_plan_format_version !=
        kProviderConfigLoadPlanFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config snapshot placeholder source "
            "format_version must be '" +
                std::string(kProviderConfigLoadPlanFormatVersion) + "'");
    }
    if (placeholder.workflow_canonical_name.empty() || placeholder.session_id.empty() ||
        placeholder.input_fixture.empty() ||
        placeholder.durable_store_import_provider_config_load_identity.empty() ||
        placeholder.durable_store_import_provider_config_snapshot_identity.empty() ||
        placeholder.provider_config_profile_identity.empty() ||
        placeholder.provider_config_snapshot_placeholder_identity.empty() ||
        placeholder.config_schema_version.empty()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config snapshot placeholder identity "
            "fields must not be empty");
    }
    if (placeholder.config_schema_version != kProviderConfigSchemaVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config snapshot placeholder "
            "config_schema_version must be '" +
                std::string(kProviderConfigSchemaVersion) + "'");
    }
    provider_config_detail_validate_failure(
        placeholder.failure_attribution,
        diagnostics,
        "durable store import provider config snapshot placeholder");
    provider_config_detail_validate_no_side_effects(
        placeholder.reads_secret_material,
        placeholder.materializes_secret_value,
        placeholder.materializes_credential_material,
        placeholder.opens_network_connection,
        placeholder.reads_host_environment,
        placeholder.writes_host_filesystem,
        placeholder.materializes_sdk_request_payload,
        placeholder.invokes_provider_sdk,
        diagnostics,
        "durable store import provider config snapshot placeholder");
    if (placeholder.snapshot_status == ProviderConfigStatus::Ready) {
        if (placeholder.source_config_load_status != ProviderConfigStatus::Ready ||
            placeholder.operation_kind !=
                ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder ||
            placeholder.failure_attribution.has_value()) {
            provider_config_detail_emit_validation_error(
                diagnostics,
                "durable store import provider config snapshot placeholder ready "
                "status requires ready config load and no failure");
        }
    } else {
        if (placeholder.operation_kind ==
                ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder ||
            !placeholder.failure_attribution.has_value()) {
            provider_config_detail_emit_validation_error(
                diagnostics,
                "durable store import provider config snapshot placeholder "
                "blocked status requires noop operation and failure_attribution");
        }
    }
    return result;
}

ProviderConfigReadinessReviewValidationResult
validate_provider_config_readiness_review(const ProviderConfigReadinessReview &review) {
    ProviderConfigReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderConfigReadinessReviewFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config readiness review format_version must be '" +
                std::string(kProviderConfigReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config readiness review source "
            "format_version must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_config_load_identity.empty() ||
        review.durable_store_import_provider_config_snapshot_identity.empty() ||
        review.provider_config_profile_identity.empty() ||
        review.provider_config_snapshot_placeholder_identity.empty() ||
        review.config_schema_version.empty() || review.config_boundary_summary.empty() ||
        review.next_step_recommendation.empty()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config readiness review identity and "
            "summary fields must not be empty");
    }
    provider_config_detail_validate_failure(
        review.failure_attribution,
        diagnostics,
        "durable store import provider config readiness review");
    provider_config_detail_validate_no_side_effects(
        review.reads_secret_material,
        review.materializes_secret_value,
        review.materializes_credential_material,
        review.opens_network_connection,
        review.reads_host_environment,
        review.writes_host_filesystem,
        review.materializes_sdk_request_payload,
        review.invokes_provider_sdk,
        diagnostics,
        "durable store import provider config readiness review");
    if (review.snapshot_status == ProviderConfigStatus::Ready) {
        if (review.next_action !=
                ProviderConfigReadinessNextActionKind::ReadyForSecretHandleResolver ||
            review.failure_attribution.has_value()) {
            provider_config_detail_emit_validation_error(
                diagnostics,
                "durable store import provider config readiness review ready "
                "status requires secret resolver next action and no failure");
        }
    } else if (!review.failure_attribution.has_value()) {
        provider_config_detail_emit_validation_error(
            diagnostics,
            "durable store import provider config readiness review blocked "
            "status requires failure_attribution");
    }
    return result;
}

ProviderConfigLoadPlanResult
build_provider_config_load_plan(const ProviderSdkAdapterInterfacePlan &interface_plan) {
    ProviderConfigLoadPlanResult result;
    result.diagnostics.append(
        validate_provider_sdk_adapter_interface_plan(interface_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = interface_plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready;
    const auto status = ready ? ProviderConfigStatus::Ready : ProviderConfigStatus::Blocked;
    ProviderConfigLoadPlan plan;
    plan.workflow_canonical_name = interface_plan.workflow_canonical_name;
    plan.session_id = interface_plan.session_id;
    plan.run_id = interface_plan.run_id;
    plan.input_fixture = interface_plan.input_fixture;
    plan.durable_store_import_provider_sdk_adapter_interface_identity =
        interface_plan.durable_store_import_provider_sdk_adapter_interface_identity;
    plan.source_adapter_interface_status = interface_plan.interface_status;
    plan.provider_registry_identity = interface_plan.provider_registry_identity;
    plan.adapter_descriptor_identity = interface_plan.adapter_descriptor_identity;
    plan.provider_key = interface_plan.provider_key;
    plan.durable_store_import_provider_config_load_identity =
        provider_config_detail_config_load_identity(interface_plan, status);
    plan.config_load_status = status;
    plan.provider_config_profile_identity = provider_config_detail_profile_identity(interface_plan);
    plan.provider_config_snapshot_placeholder_identity =
        provider_config_detail_snapshot_placeholder_identity(plan.provider_config_profile_identity);
    plan.profile_descriptor.provider_key = interface_plan.provider_key;
    if (ready) {
        plan.operation_kind = ProviderConfigOperationKind::PlanProviderConfigLoad;
    } else {
        plan.operation_kind = ProviderConfigOperationKind::NoopAdapterInterfaceNotReady;
        plan.failure_attribution =
            provider_config_detail_adapter_interface_not_ready_failure(interface_plan);
    }
    result.diagnostics.append(validate_provider_config_load_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderConfigSnapshotPlaceholderResult
build_provider_config_snapshot_placeholder(const ProviderConfigLoadPlan &plan) {
    ProviderConfigSnapshotPlaceholderResult result;
    result.diagnostics.append(validate_provider_config_load_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = plan.config_load_status == ProviderConfigStatus::Ready;
    const auto status = ready ? ProviderConfigStatus::Ready : ProviderConfigStatus::Blocked;
    ProviderConfigSnapshotPlaceholder placeholder;
    placeholder.workflow_canonical_name = plan.workflow_canonical_name;
    placeholder.session_id = plan.session_id;
    placeholder.run_id = plan.run_id;
    placeholder.input_fixture = plan.input_fixture;
    placeholder.durable_store_import_provider_config_load_identity =
        plan.durable_store_import_provider_config_load_identity;
    placeholder.source_config_load_status = plan.config_load_status;
    placeholder.durable_store_import_provider_config_snapshot_identity =
        provider_config_detail_snapshot_artifact_identity(plan, status);
    placeholder.snapshot_status = status;
    placeholder.provider_config_profile_identity = plan.provider_config_profile_identity;
    placeholder.provider_config_snapshot_placeholder_identity =
        plan.provider_config_snapshot_placeholder_identity;
    if (ready) {
        placeholder.operation_kind =
            ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder;
    } else {
        placeholder.operation_kind = ProviderConfigOperationKind::NoopConfigLoadNotReady;
        placeholder.failure_attribution =
            provider_config_detail_config_load_not_ready_failure(plan);
    }
    result.diagnostics.append(
        validate_provider_config_snapshot_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.placeholder = std::move(placeholder);
    return result;
}

ProviderConfigReadinessReviewResult
build_provider_config_readiness_review(const ProviderConfigSnapshotPlaceholder &placeholder) {
    ProviderConfigReadinessReviewResult result;
    result.diagnostics.append(
        validate_provider_config_snapshot_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderConfigReadinessReview review;
    review.workflow_canonical_name = placeholder.workflow_canonical_name;
    review.session_id = placeholder.session_id;
    review.run_id = placeholder.run_id;
    review.input_fixture = placeholder.input_fixture;
    review.durable_store_import_provider_config_load_identity =
        placeholder.durable_store_import_provider_config_load_identity;
    review.durable_store_import_provider_config_snapshot_identity =
        placeholder.durable_store_import_provider_config_snapshot_identity;
    review.snapshot_status = placeholder.snapshot_status;
    review.operation_kind = placeholder.operation_kind;
    review.provider_config_profile_identity = placeholder.provider_config_profile_identity;
    review.provider_config_snapshot_placeholder_identity =
        placeholder.provider_config_snapshot_placeholder_identity;
    review.config_schema_version = placeholder.config_schema_version;
    review.failure_attribution = placeholder.failure_attribution;
    review.config_boundary_summary = provider_config_detail_boundary_summary(placeholder);
    review.next_step_recommendation = provider_config_detail_next_step_summary(placeholder);
    review.next_action = provider_config_detail_next_action_for_placeholder(placeholder);
    result.diagnostics.append(validate_provider_config_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_secret.cpp ----

#include "pipeline/persistence/durable_store_import/provider/configuration/secret.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_secret_detail_kValidationDiagnosticCode{"DSI_PROVIDER_SECRET"};

void provider_secret_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_secret_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_secret_detail_status_slug(ProviderSecretStatus status) {
    switch (status) {
    case ProviderSecretStatus::Ready:
        return "ready";
    case ProviderSecretStatus::Blocked:
        return "blocked";
    }
    return "invalid";
}

[[nodiscard]] std::string
provider_secret_detail_request_identity(const ProviderConfigSnapshotPlaceholder &snapshot,
                                        ProviderSecretStatus status) {
    return "durable-store-import-provider-secret-resolver-request::" + snapshot.session_id +
           "::" + provider_secret_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_secret_detail_response_identity(const ProviderSecretResolverRequestPlan &plan,
                                         ProviderSecretStatus status) {
    return "durable-store-import-provider-secret-resolver-response::" + plan.session_id +
           "::" + provider_secret_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_secret_detail_handle_identity(const ProviderConfigSnapshotPlaceholder &snapshot) {
    return "provider-secret-handle::" + snapshot.provider_config_profile_identity +
           "::provider-config-credential";
}

[[nodiscard]] ProviderSecretFailureAttribution
provider_secret_detail_config_snapshot_not_ready_failure(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
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
provider_secret_detail_request_not_ready_failure(const ProviderSecretResolverRequestPlan &plan) {
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

void provider_secret_detail_validate_failure(
    const std::optional<ProviderSecretFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_secret_detail_validate_handle(const ProviderSecretHandleReference &handle,
                                            DiagnosticBag &diagnostics,
                                            std::string_view owner) {
    if (handle.handle_schema_version != kProviderSecretHandleSchemaVersion ||
        handle.handle_identity.empty() || handle.provider_key.empty() ||
        handle.profile_key.empty() || handle.purpose.empty()) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " secret handle fields must be valid and non-empty");
    }
    if (handle.contains_secret_value || handle.contains_credential_material ||
        handle.contains_token_value) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            std::string(owner) +
                " secret handle cannot contain secret, credential, or token value");
    }
}

void provider_secret_detail_validate_no_side_effects(bool reads_secret_material,
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
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (materializes_secret_value) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize secret value");
    }
    if (materializes_credential_material) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize credential material");
    }
    if (materializes_token_value) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize token value");
    }
    if (opens_network_connection) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_host_environment) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
    if (invokes_secret_manager) {
        provider_secret_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke secret manager");
    }
}

void provider_secret_detail_validate_forbidden_material(
    const ProviderSecretResolverRequestPlan &plan, DiagnosticBag &diagnostics) {
    if (plan.secret_value.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan cannot "
            "contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan cannot "
            "contain credential_material");
    }
    if (plan.token_value.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan cannot "
            "contain token_value");
    }
    if (plan.secret_manager_response.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan cannot "
            "contain secret_manager_response");
    }
}

[[nodiscard]] ProviderSecretPolicyNextActionKind provider_secret_detail_next_action_for_placeholder(
    const ProviderSecretResolverResponsePlaceholder &placeholder) {
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

[[nodiscard]] std::string provider_secret_detail_policy_summary(
    const ProviderSecretResolverResponsePlaceholder &placeholder) {
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
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan format_version must be '" +
                std::string(kProviderSecretResolverRequestPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan source "
            "format_version must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_config_snapshot_identity.empty() ||
        plan.provider_config_profile_identity.empty() ||
        plan.provider_config_snapshot_placeholder_identity.empty() ||
        plan.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        plan.provider_secret_resolver_response_placeholder_identity.empty()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver request plan identity "
            "fields must not be empty");
    }
    provider_secret_detail_validate_handle(
        plan.secret_handle,
        diagnostics,
        "durable store import provider secret resolver request plan");
    provider_secret_detail_validate_failure(
        plan.failure_attribution,
        diagnostics,
        "durable store import provider secret resolver request plan");
    provider_secret_detail_validate_no_side_effects(
        plan.reads_secret_material,
        plan.materializes_secret_value,
        plan.materializes_credential_material,
        plan.materializes_token_value,
        plan.opens_network_connection,
        plan.reads_host_environment,
        plan.writes_host_filesystem,
        plan.invokes_secret_manager,
        diagnostics,
        "durable store import provider secret resolver request plan");
    provider_secret_detail_validate_forbidden_material(plan, diagnostics);
    if (plan.request_status == ProviderSecretStatus::Ready) {
        if (plan.source_config_snapshot_status != ProviderConfigStatus::Ready ||
            plan.operation_kind != ProviderSecretOperationKind::PlanSecretResolverRequest ||
            plan.failure_attribution.has_value()) {
            provider_secret_detail_emit_validation_error(
                diagnostics,
                "durable store import provider secret resolver request plan "
                "ready status requires ready config snapshot and no failure");
        }
    } else if (plan.operation_kind == ProviderSecretOperationKind::PlanSecretResolverRequest ||
               !plan.failure_attribution.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
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
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver response placeholder format_version "
            "must be '" +
                std::string(kProviderSecretResolverResponsePlaceholderFormatVersion) + "'");
    }
    if (placeholder
            .source_durable_store_import_provider_secret_resolver_request_plan_format_version !=
        kProviderSecretResolverRequestPlanFormatVersion) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver response placeholder "
            "source format_version must be '" +
                std::string(kProviderSecretResolverRequestPlanFormatVersion) + "'");
    }
    if (placeholder.workflow_canonical_name.empty() || placeholder.session_id.empty() ||
        placeholder.input_fixture.empty() ||
        placeholder.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        placeholder.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        placeholder.credential_lifecycle_version.empty()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver response placeholder "
            "identity fields must not be empty");
    }
    if (placeholder.credential_lifecycle_version != kProviderCredentialLifecycleVersion) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret resolver response placeholder "
            "credential_lifecycle_version must be '" +
                std::string(kProviderCredentialLifecycleVersion) + "'");
    }
    provider_secret_detail_validate_handle(
        placeholder.secret_handle,
        diagnostics,
        "durable store import provider secret resolver response placeholder");
    provider_secret_detail_validate_failure(
        placeholder.failure_attribution,
        diagnostics,
        "durable store import provider secret resolver response placeholder");
    provider_secret_detail_validate_no_side_effects(
        placeholder.reads_secret_material,
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
            provider_secret_detail_emit_validation_error(
                diagnostics,
                "durable store import provider secret resolver response placeholder ready status "
                "requires ready request and pending lifecycle");
        }
    } else if (placeholder.operation_kind ==
                   ProviderSecretOperationKind::PlanSecretResolverResponsePlaceholder ||
               !placeholder.failure_attribution.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
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
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret policy review format_version must be '" +
                std::string(kProviderSecretPolicyReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_secret_resolver_response_placeholder_format_version !=
        kProviderSecretResolverResponsePlaceholderFormatVersion) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret policy review source format_version must be '" +
                std::string(kProviderSecretResolverResponsePlaceholderFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_secret_resolver_request_identity.empty() ||
        review.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        review.secret_policy_summary.empty() || review.next_step_recommendation.empty()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
            "durable store import provider secret policy review identity and "
            "summary fields must not be empty");
    }
    provider_secret_detail_validate_handle(
        review.secret_handle, diagnostics, "durable store import provider secret policy review");
    provider_secret_detail_validate_failure(review.failure_attribution,
                                            diagnostics,
                                            "durable store import provider secret policy review");
    provider_secret_detail_validate_no_side_effects(
        review.reads_secret_material,
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
            provider_secret_detail_emit_validation_error(
                diagnostics,
                "durable store import provider secret policy review ready status "
                "requires local harness next action and no failure");
        }
    } else if (!review.failure_attribution.has_value()) {
        provider_secret_detail_emit_validation_error(
            diagnostics,
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
        provider_secret_detail_request_identity(snapshot, status);
    plan.request_status = status;
    plan.secret_handle.handle_identity = provider_secret_detail_handle_identity(snapshot);
    plan.secret_handle.profile_key = snapshot.provider_config_profile_identity;
    plan.provider_secret_resolver_response_placeholder_identity =
        "provider-secret-resolver-response-placeholder::" + plan.secret_handle.handle_identity;
    if (ready) {
        plan.operation_kind = ProviderSecretOperationKind::PlanSecretResolverRequest;
    } else {
        plan.operation_kind = ProviderSecretOperationKind::NoopConfigSnapshotNotReady;
        plan.failure_attribution =
            provider_secret_detail_config_snapshot_not_ready_failure(snapshot);
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
        provider_secret_detail_response_identity(plan, status);
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
        placeholder.failure_attribution = provider_secret_detail_request_not_ready_failure(plan);
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
    review.secret_policy_summary = provider_secret_detail_policy_summary(placeholder);
    review.next_action = provider_secret_detail_next_action_for_placeholder(placeholder);
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

// ---- provider_config_bundle_validation.cpp ----

#include "pipeline/persistence/durable_store_import/provider/configuration/config_bundle_validation.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation>
    provider_config_bundle_validation_detail_kValidationDiagnosticCode{
        "DSI_PROVIDER_CONFIG_BUNDLE_VALIDATION"};

void provider_config_bundle_validation_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                    std::string message) {
    validation::emit_validation_error(
        diagnostics,
        provider_config_bundle_validation_detail_kValidationDiagnosticCode,
        std::move(message));
}

// 校验 provider reference：selection plan 中的 provider 是否在 config 中引用
[[nodiscard]] ProviderReferenceCheck
provider_config_bundle_validation_detail_check_provider_reference(const ProviderSelectionPlan &plan,
                                                                  std::string_view provider_id) {
    ProviderReferenceCheck check;
    check.provider_name = std::string(provider_id);

    if (provider_id.empty()) {
        check.status = ConfigValidationStatus::Missing;
        check.validation_error = "provider id is empty";
        return check;
    }

    // 判断 provider 是否在 selection plan 中被注册
    if (plan.selected_provider_id == provider_id || plan.fallback_provider_id == provider_id) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Invalid;
        check.validation_error =
            "provider '" + std::string(provider_id) + "' not found in selection plan";
    }

    return check;
}

// 基于 config snapshot 构建 secret handle check（不读取 secret value）
[[nodiscard]] SecretHandleCheck provider_config_bundle_validation_detail_check_secret_handle(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    SecretHandleCheck check;
    check.secret_name = snapshot.provider_config_profile_identity + ".secret-handle";
    check.secret_scope = snapshot.provider_config_profile_identity;

    if (snapshot.reads_secret_material) {
        check.presence_status = ConfigValidationStatus::Valid;
    } else {
        check.presence_status = ConfigValidationStatus::Missing;
    }

    // Scope 校验：profile identity 不能为空
    if (!snapshot.provider_config_profile_identity.empty()) {
        check.scope_status = ConfigValidationStatus::Valid;
    } else {
        check.scope_status = ConfigValidationStatus::Invalid;
    }

    // Redaction policy 要求：不允许 materialize secret value
    check.has_redaction_policy = !snapshot.materializes_secret_value;

    return check;
}

// 校验 endpoint shape：config snapshot 中是否引用了 endpoint uri
[[nodiscard]] EndpointShapeCheck provider_config_bundle_validation_detail_check_endpoint_shape(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    EndpointShapeCheck check;
    check.endpoint_name = snapshot.provider_config_profile_identity + ".endpoint";
    check.expected_shape = "uri";

    // Snapshot 本身不含 endpoint（placeholder），检查 operation kind 是否就绪
    if (snapshot.snapshot_status == ProviderConfigStatus::Ready) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Missing;
    }

    return check;
}

// 校验 environment binding：config 是否引用了运行时环境
[[nodiscard]] EnvironmentBindingCheck
provider_config_bundle_validation_detail_check_environment_binding(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    EnvironmentBindingCheck check;
    check.binding_name = "config-snapshot." + snapshot.provider_config_profile_identity;

    if (snapshot.snapshot_status == ProviderConfigStatus::Ready) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Missing;
    }

    return check;
}

// 计算各项汇总
void provider_config_bundle_validation_detail_compute_summary(
    ProviderConfigBundleValidationReport &report) {
    report.valid_count = 0;
    report.invalid_count = 0;
    report.missing_count = 0;

    auto tally = [&](ConfigValidationStatus s) {
        switch (s) {
        case ConfigValidationStatus::Valid:
            ++report.valid_count;
            break;
        case ConfigValidationStatus::Invalid:
            ++report.invalid_count;
            break;
        case ConfigValidationStatus::Missing:
            ++report.missing_count;
            break;
        }
    };

    for (const auto &ref : report.provider_references) {
        tally(ref.status);
    }
    for (const auto &sh : report.secret_handles) {
        tally(sh.presence_status);
        tally(sh.scope_status);
    }
    for (const auto &ep : report.endpoint_shapes) {
        tally(ep.status);
    }
    for (const auto &eb : report.environment_bindings) {
        tally(eb.status);
    }

    report.validation_summary = std::to_string(report.valid_count) + " valid, " +
                                std::to_string(report.invalid_count) + " invalid, " +
                                std::to_string(report.missing_count) + " missing";

    if (report.invalid_count > 0) {
        report.blocking_summary =
            std::to_string(report.invalid_count) + " blocking issue(s) detected";
    } else {
        report.blocking_summary = "no blocking issues";
    }
}

} // namespace

ProviderConfigBundleValidationReportValidationResult
validate_provider_config_bundle_validation_report(
    const ProviderConfigBundleValidationReport &report) {
    ProviderConfigBundleValidationReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (report.format_version != kProviderConfigBundleValidationReportFormatVersion) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics,
            "provider config bundle validation report format_version must be '" +
                std::string(kProviderConfigBundleValidationReportFormatVersion) + "'");
    }

    if (report.source_durable_store_import_provider_selection_plan_format_version !=
        kProviderSelectionPlanFormatVersion) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics,
            "provider config bundle validation report source selection plan format_version "
            "must be '" +
                std::string(kProviderSelectionPlanFormatVersion) + "'");
    }

    if (report.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics,
            "provider config bundle validation report source config snapshot format_version "
            "must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }

    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() || report.config_bundle_identity.empty() ||
        report.durable_store_import_provider_selection_plan_identity.empty() ||
        report.durable_store_import_provider_config_snapshot_identity.empty()) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics,
            "provider config bundle validation report identity fields must not be empty");
    }

    if (report.validation_summary.empty() || report.blocking_summary.empty()) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics,
            "provider config bundle validation report summary fields must not be empty");
    }

    // 安全约束断言
    if (report.reads_secret_value) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics, "provider config bundle validation must not read secret values");
    }
    if (report.opens_network_connection) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics, "provider config bundle validation must not open network connections");
    }
    if (report.generates_production_config) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics, "provider config bundle validation must not generate production config");
    }

    return result;
}

ProviderConfigBundleValidationReportResult build_provider_config_bundle_validation_report(
    const ProviderSelectionPlan &selection_plan,
    const ProviderConfigSnapshotPlaceholder &config_snapshot) {
    ProviderConfigBundleValidationReportResult result;
    auto &diagnostics = result.diagnostics;

    // 前置校验：selection plan 和 snapshot 状态
    if (selection_plan.workflow_canonical_name.empty() || selection_plan.session_id.empty()) {
        provider_config_bundle_validation_detail_emit_validation_error(
            diagnostics, "selection plan workflow_canonical_name and session_id required");
        return result;
    }

    ProviderConfigBundleValidationReport report;
    report.workflow_canonical_name = selection_plan.workflow_canonical_name;
    report.session_id = selection_plan.session_id;
    report.run_id = selection_plan.run_id;
    report.input_fixture = selection_plan.input_fixture;
    report.durable_store_import_provider_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    report.durable_store_import_provider_config_snapshot_identity =
        config_snapshot.durable_store_import_provider_config_snapshot_identity;
    report.config_bundle_identity =
        "config-bundle-validation::" + selection_plan.workflow_canonical_name +
        "::" + selection_plan.session_id;

    // 1. 校验 provider references
    report.provider_references.push_back(
        provider_config_bundle_validation_detail_check_provider_reference(
            selection_plan, selection_plan.selected_provider_id));
    report.provider_references.push_back(
        provider_config_bundle_validation_detail_check_provider_reference(
            selection_plan, selection_plan.fallback_provider_id));

    // 2. 校验 secret handles（基于 config snapshot，不读取 value）
    report.secret_handles.push_back(
        provider_config_bundle_validation_detail_check_secret_handle(config_snapshot));

    // 3. 校验 endpoint shapes
    report.endpoint_shapes.push_back(
        provider_config_bundle_validation_detail_check_endpoint_shape(config_snapshot));

    // 4. 校验 environment bindings
    report.environment_bindings.push_back(
        provider_config_bundle_validation_detail_check_environment_binding(config_snapshot));

    // 汇总计数
    provider_config_bundle_validation_detail_compute_summary(report);

    // 安全约束：确保不读取 secret value、不连接网络、不生成配置
    report.reads_secret_value = false;
    report.opens_network_connection = false;
    report.generates_production_config = false;

    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import
