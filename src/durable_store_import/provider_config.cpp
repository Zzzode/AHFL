#include "ahfl/durable_store_import/provider_config.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_CONFIG";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderConfigStatus status) {
    switch (status) {
    case ProviderConfigStatus::Ready:
        return "ready";
    case ProviderConfigStatus::Blocked:
        return "blocked";
    }
    return "invalid";
}

[[nodiscard]] std::string config_load_identity(const ProviderSdkAdapterInterfacePlan &plan,
                                               ProviderConfigStatus status) {
    return "durable-store-import-provider-config-load::" + plan.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string snapshot_artifact_identity(const ProviderConfigLoadPlan &plan,
                                                     ProviderConfigStatus status) {
    return "durable-store-import-provider-config-snapshot::" + plan.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string profile_identity(const ProviderSdkAdapterInterfacePlan &plan) {
    return "provider-config-profile::" + plan.adapter_descriptor_identity +
           "::local-placeholder-profile";
}

[[nodiscard]] std::string snapshot_placeholder_identity(const std::string &profile_id) {
    return "provider-config-snapshot-placeholder::" + profile_id;
}

[[nodiscard]] ProviderConfigFailureAttribution
adapter_interface_not_ready_failure(const ProviderSdkAdapterInterfacePlan &plan) {
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
config_load_not_ready_failure(const ProviderConfigLoadPlan &plan) {
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

void validate_failure(const std::optional<ProviderConfigFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_side_effects(bool reads_secret_material,
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
        emit_validation_error(diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (materializes_secret_value) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot materialize secret value");
    }
    if (materializes_credential_material) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize credential material");
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
    if (materializes_sdk_request_payload) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
}

void validate_forbidden_material(const ProviderConfigLoadPlan &plan, DiagnosticBag &diagnostics) {
    if (plan.secret_value.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain credential_material");
    }
    if (plan.provider_endpoint_uri.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain provider_endpoint_uri");
    }
    if (plan.remote_config_uri.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain remote_config_uri");
    }
    if (plan.sdk_request_payload.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan cannot contain sdk_request_payload");
    }
}

[[nodiscard]] ProviderConfigReadinessNextActionKind
next_action_for_placeholder(const ProviderConfigSnapshotPlaceholder &placeholder) {
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

[[nodiscard]] std::string boundary_summary(const ProviderConfigSnapshotPlaceholder &placeholder) {
    if (placeholder.snapshot_status == ProviderConfigStatus::Ready) {
        return "provider config snapshot placeholder is ready without reading secret material, "
               "materializing credential material, opening network, reading host environment, "
               "writing host filesystem, materializing SDK payload, or invoking provider SDK";
    }
    return "provider config snapshot placeholder is blocked because config load is not ready";
}

[[nodiscard]] std::string next_step_summary(const ProviderConfigSnapshotPlaceholder &placeholder) {
    switch (next_action_for_placeholder(placeholder)) {
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
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan format_version must be '" +
                std::string(kProviderConfigLoadPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version !=
        kProviderSdkAdapterInterfacePlanFormatVersion) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "durable store import provider config load plan identity and profile "
                              "fields must not be empty");
    }
    if (plan.profile_descriptor.config_schema_version != kProviderConfigSchemaVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider config load plan config_schema_version must be '" +
                std::string(kProviderConfigSchemaVersion) + "'");
    }
    validate_failure(
        plan.failure_attribution, diagnostics, "durable store import provider config load plan");
    validate_no_side_effects(plan.reads_secret_material,
                             plan.materializes_secret_value,
                             plan.materializes_credential_material,
                             plan.opens_network_connection,
                             plan.reads_host_environment,
                             plan.writes_host_filesystem,
                             plan.materializes_sdk_request_payload,
                             plan.invokes_provider_sdk,
                             diagnostics,
                             "durable store import provider config load plan");
    validate_forbidden_material(plan, diagnostics);
    validate_no_side_effects(false,
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
        emit_validation_error(
            diagnostics,
            "durable store import provider config profile descriptor cannot contain endpoint uri");
    }
    if (plan.config_load_status == ProviderConfigStatus::Ready) {
        if (plan.source_adapter_interface_status != ProviderSdkAdapterInterfaceStatus::Ready ||
            plan.operation_kind != ProviderConfigOperationKind::PlanProviderConfigLoad ||
            plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider config load plan ready status "
                                  "requires ready adapter interface and no failure");
        }
    } else {
        if (plan.operation_kind == ProviderConfigOperationKind::PlanProviderConfigLoad ||
            !plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
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
        emit_validation_error(
            diagnostics,
            "durable store import provider config snapshot placeholder format_version must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }
    if (placeholder.source_durable_store_import_provider_config_load_plan_format_version !=
        kProviderConfigLoadPlanFormatVersion) {
        emit_validation_error(diagnostics,
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
        emit_validation_error(diagnostics,
                              "durable store import provider config snapshot placeholder identity "
                              "fields must not be empty");
    }
    if (placeholder.config_schema_version != kProviderConfigSchemaVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider config snapshot placeholder "
                              "config_schema_version must be '" +
                                  std::string(kProviderConfigSchemaVersion) + "'");
    }
    validate_failure(placeholder.failure_attribution,
                     diagnostics,
                     "durable store import provider config snapshot placeholder");
    validate_no_side_effects(placeholder.reads_secret_material,
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
            emit_validation_error(diagnostics,
                                  "durable store import provider config snapshot placeholder ready "
                                  "status requires ready config load and no failure");
        }
    } else {
        if (placeholder.operation_kind ==
                ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder ||
            !placeholder.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
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
        emit_validation_error(
            diagnostics,
            "durable store import provider config readiness review format_version must be '" +
                std::string(kProviderConfigReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider config readiness review source "
                              "format_version must be '" +
                                  std::string(kProviderConfigSnapshotPlaceholderFormatVersion) +
                                  "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_config_load_identity.empty() ||
        review.durable_store_import_provider_config_snapshot_identity.empty() ||
        review.provider_config_profile_identity.empty() ||
        review.provider_config_snapshot_placeholder_identity.empty() ||
        review.config_schema_version.empty() || review.config_boundary_summary.empty() ||
        review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider config readiness review identity and "
                              "summary fields must not be empty");
    }
    validate_failure(review.failure_attribution,
                     diagnostics,
                     "durable store import provider config readiness review");
    validate_no_side_effects(review.reads_secret_material,
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
            emit_validation_error(diagnostics,
                                  "durable store import provider config readiness review ready "
                                  "status requires secret resolver next action and no failure");
        }
    } else if (!review.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
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
        config_load_identity(interface_plan, status);
    plan.config_load_status = status;
    plan.provider_config_profile_identity = profile_identity(interface_plan);
    plan.provider_config_snapshot_placeholder_identity =
        snapshot_placeholder_identity(plan.provider_config_profile_identity);
    plan.profile_descriptor.provider_key = interface_plan.provider_key;
    if (ready) {
        plan.operation_kind = ProviderConfigOperationKind::PlanProviderConfigLoad;
    } else {
        plan.operation_kind = ProviderConfigOperationKind::NoopAdapterInterfaceNotReady;
        plan.failure_attribution = adapter_interface_not_ready_failure(interface_plan);
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
        snapshot_artifact_identity(plan, status);
    placeholder.snapshot_status = status;
    placeholder.provider_config_profile_identity = plan.provider_config_profile_identity;
    placeholder.provider_config_snapshot_placeholder_identity =
        plan.provider_config_snapshot_placeholder_identity;
    if (ready) {
        placeholder.operation_kind =
            ProviderConfigOperationKind::PlanProviderConfigSnapshotPlaceholder;
    } else {
        placeholder.operation_kind = ProviderConfigOperationKind::NoopConfigLoadNotReady;
        placeholder.failure_attribution = config_load_not_ready_failure(plan);
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
    review.config_boundary_summary = boundary_summary(placeholder);
    review.next_step_recommendation = next_step_summary(placeholder);
    review.next_action = next_action_for_placeholder(placeholder);
    result.diagnostics.append(validate_provider_config_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
