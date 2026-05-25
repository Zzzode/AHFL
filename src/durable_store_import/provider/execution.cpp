// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_host_execution.cpp ----

#include "durable_store_import/provider/execution/host_execution.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_host_execution_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_HOST_EXECUTION";

void provider_host_execution_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                          std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_host_execution_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_host_execution_detail_execution_status_slug(ProviderHostExecutionStatus status) {
    switch (status) {
    case ProviderHostExecutionStatus::Ready:
        return "ready";
    case ProviderHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
provider_host_execution_detail_execution_plan_identity(const ProviderSdkRequestEnvelopePlan &plan,
                                                       ProviderHostExecutionStatus status) {
    return "durable-store-import-provider-host-execution::" + plan.session_id +
           "::" + provider_host_execution_detail_execution_status_slug(status);
}

[[nodiscard]] bool provider_host_execution_detail_policy_matches_sdk_envelope(
    const ProviderSdkRequestEnvelopePlan &plan, const ProviderHostExecutionPolicy &policy) {
    return policy.host_handoff_policy_ref == plan.envelope_policy.host_handoff_policy_ref &&
           policy.provider_namespace == plan.envelope_policy.provider_namespace;
}

[[nodiscard]] std::optional<std::string>
provider_host_execution_detail_provider_host_execution_descriptor_identity(
    const ProviderSdkRequestEnvelopePlan &plan) {
    if (!plan.host_handoff_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-host-execution-descriptor::" + *plan.host_handoff_descriptor_identity;
}

[[nodiscard]] std::optional<std::string>
provider_host_execution_detail_provider_host_receipt_placeholder_identity(
    const ProviderSdkRequestEnvelopePlan &plan) {
    if (!plan.host_handoff_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-host-receipt-placeholder::" + *plan.host_handoff_descriptor_identity;
}

[[nodiscard]] ProviderHostExecutionFailureAttribution
provider_host_execution_detail_sdk_handoff_not_ready_failure(
    const ProviderSdkRequestEnvelopePlan &plan) {
    std::string message = "provider host execution cannot proceed because SDK handoff is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderHostExecutionFailureAttribution{
        .kind = ProviderHostExecutionFailureKind::SdkHandoffNotReady,
        .message = std::move(message),
        .missing_capability = std::nullopt,
    };
}

[[nodiscard]] ProviderHostExecutionCapabilityKind
provider_host_execution_detail_first_missing_host_capability(
    const ProviderHostExecutionPolicy &policy) {
    if (!policy.supports_local_host_execution_descriptor_planning) {
        return ProviderHostExecutionCapabilityKind::PlanLocalHostExecutionDescriptor;
    }
    return ProviderHostExecutionCapabilityKind::PlanDryRunHostReceiptPlaceholder;
}

[[nodiscard]] std::string provider_host_execution_detail_missing_host_capability_message(
    ProviderHostExecutionCapabilityKind capability) {
    switch (capability) {
    case ProviderHostExecutionCapabilityKind::PlanLocalHostExecutionDescriptor:
        return "provider host execution policy does not support local host execution descriptor "
               "planning";
    case ProviderHostExecutionCapabilityKind::PlanDryRunHostReceiptPlaceholder:
        return "provider host execution policy does not support dry-run host receipt placeholder "
               "planning";
    }

    return "provider host execution capability is unsupported";
}

[[nodiscard]] ProviderHostExecutionReadinessNextActionKind
provider_host_execution_detail_next_action_for_host_execution_plan(
    const ProviderHostExecutionPlan &plan) {
    if (plan.execution_status == ProviderHostExecutionStatus::Ready) {
        return ProviderHostExecutionReadinessNextActionKind::ReadyForLocalHostExecutionHarness;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderHostExecutionFailureKind::HostPolicyMismatch:
            return ProviderHostExecutionReadinessNextActionKind::FixHostPolicy;
        case ProviderHostExecutionFailureKind::UnsupportedHostCapability:
            return ProviderHostExecutionReadinessNextActionKind::WaitForHostCapability;
        case ProviderHostExecutionFailureKind::SdkHandoffNotReady:
            return ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string
provider_host_execution_detail_boundary_summary(const ProviderHostExecutionPlan &plan) {
    if (plan.execution_status == ProviderHostExecutionStatus::Ready) {
        return "provider host execution is planned without starting a host process, reading host "
               "environment, opening network, materializing SDK payload, invoking a provider SDK, "
               "or writing host filesystem";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderHostExecutionFailureKind::HostPolicyMismatch) {
        return "provider host execution skipped because host execution policy does not match SDK "
               "handoff";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderHostExecutionFailureKind::UnsupportedHostCapability) {
        return "provider host execution skipped because host execution capability is unsupported";
    }

    return "provider host execution skipped because SDK handoff is not ready";
}

[[nodiscard]] std::string
provider_host_execution_detail_next_step_summary(const ProviderHostExecutionPlan &plan) {
    switch (provider_host_execution_detail_next_action_for_host_execution_plan(plan)) {
    case ProviderHostExecutionReadinessNextActionKind::ReadyForLocalHostExecutionHarness:
        return "ready for future local host execution harness; no process, host env, network, SDK "
               "payload, SDK call, or filesystem write was used";
    case ProviderHostExecutionReadinessNextActionKind::FixHostPolicy:
        return "fix host execution policy handoff reference and provider namespace before host "
               "planning";
    case ProviderHostExecutionReadinessNextActionKind::WaitForHostCapability:
        return "wait for local host execution descriptor or dry-run host receipt placeholder "
               "capability";
    case ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider host execution can proceed";
    }

    return "manual review required before provider host execution can proceed";
}

void provider_host_execution_detail_validate_failure_attribution(
    const std::optional<ProviderHostExecutionFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_host_execution_detail_validate_no_side_effects(bool starts_host_process,
                                                             bool reads_host_environment,
                                                             bool opens_network_connection,
                                                             bool materializes_sdk_request_payload,
                                                             bool invokes_provider_sdk,
                                                             bool writes_host_filesystem,
                                                             DiagnosticBag &diagnostics,
                                                             std::string_view owner) {
    if (starts_host_process) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot start host process");
    }
    if (reads_host_environment) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (opens_network_connection) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (materializes_sdk_request_payload) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (writes_host_filesystem) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

} // namespace

ProviderHostExecutionPolicyValidationResult
validate_provider_host_execution_policy(const ProviderHostExecutionPolicy &policy) {
    ProviderHostExecutionPolicyValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (policy.format_version != kProviderHostExecutionPolicyFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy format_version must be '" +
                std::string(kProviderHostExecutionPolicyFormatVersion) + "'");
    }
    if (policy.host_execution_policy_identity.empty() || policy.host_handoff_policy_ref.empty() ||
        policy.provider_namespace.empty() || policy.execution_profile_ref.empty() ||
        policy.sandbox_policy_ref.empty() || policy.timeout_policy_ref.empty()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy identity "
            "fields must not be empty");
    }
    if (!policy.credential_free) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy must be "
            "credential_free");
    }
    if (!policy.network_free) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy must be "
            "network_free");
    }
    if (policy.credential_reference.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "credential_reference");
    }
    if (policy.secret_value.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "secret_value");
    }
    if (policy.host_command.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "host_command");
    }
    if (policy.host_environment_secret.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "host_environment_secret");
    }
    if (policy.provider_endpoint_uri.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "provider_endpoint_uri");
    }
    if (policy.network_endpoint_uri.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "network_endpoint_uri");
    }
    if (policy.object_path.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "object_path");
    }
    if (policy.database_table.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "database_table");
    }
    if (policy.sdk_request_payload.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "sdk_request_payload");
    }
    if (policy.sdk_response_payload.has_value()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy cannot contain "
            "sdk_response_payload");
    }

    return result;
}

ProviderHostExecutionPlanValidationResult
validate_provider_host_execution_plan(const ProviderHostExecutionPlan &plan) {
    ProviderHostExecutionPlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderHostExecutionPlanFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_envelope_policy_format_version !=
        kProviderSdkEnvelopePolicyFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan "
            "source_durable_store_import_provider_sdk_envelope_policy_format_version must be '" +
                std::string(kProviderSdkEnvelopePolicyFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        plan.durable_store_import_provider_host_execution_identity.empty()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan identity fields "
            "must not be empty");
    }

    result.diagnostics.append(
        validate_provider_host_execution_policy(plan.host_execution_policy).diagnostics);
    provider_host_execution_detail_validate_failure_attribution(
        plan.failure_attribution, diagnostics, "durable store import provider host execution plan");
    provider_host_execution_detail_validate_no_side_effects(
        plan.starts_host_process,
        plan.reads_host_environment,
        plan.opens_network_connection,
        plan.materializes_sdk_request_payload,
        plan.invokes_provider_sdk,
        plan.writes_host_filesystem,
        diagnostics,
        "durable store import provider host execution plan");

    if (plan.execution_status == ProviderHostExecutionStatus::Ready) {
        if (plan.source_envelope_status != ProviderSdkEnvelopeStatus::Ready ||
            !plan.source_host_handoff_descriptor_identity.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan ready status "
                "requires ready SDK envelope and host handoff descriptor "
                "identity");
        }
        if (!plan.host_execution_policy.supports_local_host_execution_descriptor_planning ||
            !plan.host_execution_policy.supports_dry_run_host_receipt_placeholder_planning) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan ready status "
                "requires host execution planning capabilities");
        }
        if (plan.operation_kind != ProviderHostExecutionOperationKind::PlanProviderHostExecution ||
            !plan.provider_host_execution_descriptor_identity.has_value() ||
            !plan.provider_host_receipt_placeholder_identity.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan ready status "
                "requires host execution descriptor and receipt placeholder "
                "identities");
        }
        if (plan.failure_attribution.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan ready status "
                "cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderHostExecutionOperationKind::PlanProviderHostExecution) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan blocked "
                "status cannot plan host execution");
        }
        if (plan.provider_host_execution_descriptor_identity.has_value() ||
            plan.provider_host_receipt_placeholder_identity.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan blocked "
                "status cannot contain host execution descriptor or receipt "
                "placeholder identities");
        }
        if (!plan.failure_attribution.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution plan blocked "
                "status requires failure_attribution");
        }
    }

    return result;
}

ProviderHostExecutionReadinessReviewValidationResult
validate_provider_host_execution_readiness_review(
    const ProviderHostExecutionReadinessReview &review) {
    ProviderHostExecutionReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderHostExecutionReadinessReviewFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "format_version must be '" +
                std::string(kProviderHostExecutionReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        review.durable_store_import_provider_host_execution_identity.empty()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "identity fields must not be empty");
    }
    if (review.host_execution_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        provider_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "summaries must not be empty");
    }
    provider_host_execution_detail_validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider host execution readiness review");
    provider_host_execution_detail_validate_no_side_effects(
        review.starts_host_process,
        review.reads_host_environment,
        review.opens_network_connection,
        review.materializes_sdk_request_payload,
        review.invokes_provider_sdk,
        review.writes_host_filesystem,
        diagnostics,
        "durable store import provider host execution readiness review");

    if (review.execution_status == ProviderHostExecutionStatus::Ready) {
        if (review.operation_kind !=
                ProviderHostExecutionOperationKind::PlanProviderHostExecution ||
            !review.provider_host_execution_descriptor_identity.has_value() ||
            !review.provider_host_receipt_placeholder_identity.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution readiness review "
                "ready status requires host execution descriptor and receipt "
                "placeholder identities");
        }
        if (review.failure_attribution.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution readiness review "
                "ready status cannot contain failure_attribution");
        }
    } else {
        if (review.provider_host_execution_descriptor_identity.has_value() ||
            review.provider_host_receipt_placeholder_identity.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution readiness review "
                "blocked status cannot contain host execution descriptor or "
                "receipt placeholder identities");
        }
        if (!review.failure_attribution.has_value()) {
            provider_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider host execution readiness review "
                "blocked status requires failure_attribution");
        }
    }

    return result;
}

ProviderHostExecutionPolicy
build_default_provider_host_execution_policy(const ProviderSdkRequestEnvelopePlan &plan) {
    return ProviderHostExecutionPolicy{
        .format_version = std::string(kProviderHostExecutionPolicyFormatVersion),
        .host_execution_policy_identity =
            "provider-host-execution-policy::" + plan.envelope_policy.host_handoff_policy_ref,
        .host_handoff_policy_ref = plan.envelope_policy.host_handoff_policy_ref,
        .provider_namespace = plan.envelope_policy.provider_namespace,
        .execution_profile_ref =
            "host-execution-profile::" + plan.envelope_policy.host_handoff_policy_ref,
        .sandbox_policy_ref =
            "host-sandbox-policy::" + plan.envelope_policy.host_handoff_policy_ref,
        .timeout_policy_ref =
            "host-timeout-policy::" + plan.envelope_policy.host_handoff_policy_ref,
        .credential_free = true,
        .network_free = true,
        .supports_local_host_execution_descriptor_planning = true,
        .supports_dry_run_host_receipt_placeholder_planning = true,
        .credential_reference = std::nullopt,
        .secret_value = std::nullopt,
        .host_command = std::nullopt,
        .host_environment_secret = std::nullopt,
        .provider_endpoint_uri = std::nullopt,
        .network_endpoint_uri = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_request_payload = std::nullopt,
        .sdk_response_payload = std::nullopt,
    };
}

ProviderHostExecutionPlanResult
build_provider_host_execution_plan(const ProviderSdkRequestEnvelopePlan &plan) {
    auto policy = build_default_provider_host_execution_policy(plan);
    return build_provider_host_execution_plan(plan, policy);
}

ProviderHostExecutionPlanResult
build_provider_host_execution_plan(const ProviderSdkRequestEnvelopePlan &plan,
                                   const ProviderHostExecutionPolicy &policy) {
    ProviderHostExecutionPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_sdk_request_envelope_plan(plan).diagnostics);
    result.diagnostics.append(validate_provider_host_execution_policy(policy).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto policy_matches =
        provider_host_execution_detail_policy_matches_sdk_envelope(plan, policy);
    const auto has_capabilities = policy.supports_local_host_execution_descriptor_planning &&
                                  policy.supports_dry_run_host_receipt_placeholder_planning;
    const auto can_plan = plan.envelope_status == ProviderSdkEnvelopeStatus::Ready &&
                          plan.host_handoff_descriptor_identity.has_value() && policy_matches &&
                          has_capabilities;

    ProviderHostExecutionOperationKind operation_kind{
        ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady};
    std::optional<ProviderHostExecutionFailureAttribution> failure = std::nullopt;
    if (can_plan) {
        operation_kind = ProviderHostExecutionOperationKind::PlanProviderHostExecution;
    } else if (plan.envelope_status != ProviderSdkEnvelopeStatus::Ready ||
               !plan.host_handoff_descriptor_identity.has_value()) {
        operation_kind = ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady;
        failure = provider_host_execution_detail_sdk_handoff_not_ready_failure(plan);
    } else if (!policy_matches) {
        operation_kind = ProviderHostExecutionOperationKind::NoopHostPolicyMismatch;
        failure = ProviderHostExecutionFailureAttribution{
            .kind = ProviderHostExecutionFailureKind::HostPolicyMismatch,
            .message = "provider host execution policy does not match SDK handoff",
            .missing_capability = std::nullopt,
        };
    } else {
        const auto missing_capability =
            provider_host_execution_detail_first_missing_host_capability(policy);
        operation_kind = ProviderHostExecutionOperationKind::NoopUnsupportedHostCapability;
        failure = ProviderHostExecutionFailureAttribution{
            .kind = ProviderHostExecutionFailureKind::UnsupportedHostCapability,
            .message =
                provider_host_execution_detail_missing_host_capability_message(missing_capability),
            .missing_capability = missing_capability,
        };
    }

    const auto status =
        can_plan ? ProviderHostExecutionStatus::Ready : ProviderHostExecutionStatus::Blocked;
    ProviderHostExecutionPlan execution_plan{
        .format_version = std::string(kProviderHostExecutionPlanFormatVersion),
        .source_durable_store_import_provider_sdk_request_envelope_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_sdk_envelope_policy_format_version =
            plan.envelope_policy.format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            plan.durable_store_import_provider_sdk_request_envelope_identity,
        .source_host_handoff_descriptor_identity = plan.host_handoff_descriptor_identity,
        .source_envelope_status = plan.envelope_status,
        .host_execution_policy = policy,
        .durable_store_import_provider_host_execution_identity =
            provider_host_execution_detail_execution_plan_identity(plan, status),
        .operation_kind = operation_kind,
        .execution_status = status,
        .provider_host_execution_descriptor_identity =
            can_plan
                ? provider_host_execution_detail_provider_host_execution_descriptor_identity(plan)
                : std::nullopt,
        .provider_host_receipt_placeholder_identity =
            can_plan
                ? provider_host_execution_detail_provider_host_receipt_placeholder_identity(plan)
                : std::nullopt,
        .starts_host_process = false,
        .reads_host_environment = false,
        .opens_network_connection = false,
        .materializes_sdk_request_payload = false,
        .invokes_provider_sdk = false,
        .writes_host_filesystem = false,
        .failure_attribution = failure,
    };

    result.diagnostics.append(validate_provider_host_execution_plan(execution_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(execution_plan);
    return result;
}

ProviderHostExecutionReadinessReviewResult
build_provider_host_execution_readiness_review(const ProviderHostExecutionPlan &plan) {
    ProviderHostExecutionReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_host_execution_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderHostExecutionReadinessReview review{
        .format_version = std::string(kProviderHostExecutionReadinessReviewFormatVersion),
        .source_durable_store_import_provider_host_execution_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_sdk_request_envelope_plan_format_version =
            plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            plan.durable_store_import_provider_sdk_request_envelope_identity,
        .durable_store_import_provider_host_execution_identity =
            plan.durable_store_import_provider_host_execution_identity,
        .execution_status = plan.execution_status,
        .operation_kind = plan.operation_kind,
        .provider_host_execution_descriptor_identity =
            plan.provider_host_execution_descriptor_identity,
        .provider_host_receipt_placeholder_identity =
            plan.provider_host_receipt_placeholder_identity,
        .starts_host_process = plan.starts_host_process,
        .reads_host_environment = plan.reads_host_environment,
        .opens_network_connection = plan.opens_network_connection,
        .materializes_sdk_request_payload = plan.materializes_sdk_request_payload,
        .invokes_provider_sdk = plan.invokes_provider_sdk,
        .writes_host_filesystem = plan.writes_host_filesystem,
        .failure_attribution = plan.failure_attribution,
        .host_execution_boundary_summary = provider_host_execution_detail_boundary_summary(plan),
        .next_step_recommendation = provider_host_execution_detail_next_step_summary(plan),
        .next_action = provider_host_execution_detail_next_action_for_host_execution_plan(plan),
    };

    result.diagnostics.append(
        validate_provider_host_execution_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_local_host_execution.cpp ----

#include "durable_store_import/provider/execution/local_host_execution.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_local_host_execution_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_HOST_EXECUTION";

void provider_local_host_execution_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_local_host_execution_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_local_host_execution_detail_status_slug(ProviderLocalHostExecutionStatus status) {
    switch (status) {
    case ProviderLocalHostExecutionStatus::SimulatedReady:
        return "simulated-ready";
    case ProviderLocalHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string provider_local_host_execution_detail_receipt_plan_identity(
    const ProviderHostExecutionPlan &plan, ProviderLocalHostExecutionStatus status) {
    return "durable-store-import-provider-local-host-execution-receipt::" + plan.session_id +
           "::" + provider_local_host_execution_detail_status_slug(status);
}

[[nodiscard]] std::optional<std::string>
provider_local_host_execution_detail_provider_local_host_execution_receipt_identity(
    const ProviderHostExecutionPlan &plan) {
    if (!plan.provider_host_execution_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-local-host-execution-receipt::" +
           *plan.provider_host_execution_descriptor_identity;
}

[[nodiscard]] ProviderLocalHostExecutionFailureAttribution
provider_local_host_execution_detail_host_execution_not_ready_failure(
    const ProviderHostExecutionPlan &plan) {
    std::string message =
        "provider local host execution receipt cannot proceed because host execution is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderLocalHostExecutionFailureAttribution{
        .kind = ProviderLocalHostExecutionFailureKind::HostExecutionNotReady,
        .message = std::move(message),
    };
}

void provider_local_host_execution_detail_validate_failure_attribution(
    const std::optional<ProviderLocalHostExecutionFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_local_host_execution_detail_validate_no_side_effects(
    bool starts_host_process,
    bool reads_host_environment,
    bool opens_network_connection,
    bool materializes_sdk_request_payload,
    bool invokes_provider_sdk,
    bool writes_host_filesystem,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (starts_host_process) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot start host process");
    }
    if (reads_host_environment) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (opens_network_connection) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (materializes_sdk_request_payload) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (writes_host_filesystem) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

[[nodiscard]] ProviderLocalHostExecutionReceiptReviewNextActionKind
provider_local_host_execution_detail_next_action_for_receipt(
    const ProviderLocalHostExecutionReceipt &receipt) {
    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        return ProviderLocalHostExecutionReceiptReviewNextActionKind::
            ReadyForProviderSdkAdapterPrototype;
    }

    if (receipt.failure_attribution.has_value() &&
        receipt.failure_attribution->kind ==
            ProviderLocalHostExecutionFailureKind::HostExecutionNotReady) {
        return ProviderLocalHostExecutionReceiptReviewNextActionKind::WaitForHostExecutionPlan;
    }

    return ProviderLocalHostExecutionReceiptReviewNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string provider_local_host_execution_detail_boundary_summary(
    const ProviderLocalHostExecutionReceipt &receipt) {
    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        return "provider local host execution receipt was simulated without starting a host "
               "process, reading host environment, opening network, materializing SDK payload, "
               "invoking a provider SDK, or writing host filesystem";
    }

    return "provider local host execution receipt was not simulated because host execution is not "
           "ready";
}

[[nodiscard]] std::string provider_local_host_execution_detail_next_step_summary(
    const ProviderLocalHostExecutionReceipt &receipt) {
    switch (provider_local_host_execution_detail_next_action_for_receipt(receipt)) {
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::ReadyForProviderSdkAdapterPrototype:
        return "ready for future provider SDK adapter prototype; local host execution remains "
               "simulated and side-effect free";
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::WaitForHostExecutionPlan:
        return "wait for a ready provider host execution plan before local host execution receipt "
               "simulation";
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::ManualReviewRequired:
        return "manual review required before provider local host execution can proceed";
    }

    return "manual review required before provider local host execution can proceed";
}

} // namespace

ProviderLocalHostExecutionReceiptValidationResult
validate_provider_local_host_execution_receipt(const ProviderLocalHostExecutionReceipt &receipt) {
    ProviderLocalHostExecutionReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kProviderLocalHostExecutionReceiptFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt format_version must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty() ||
        receipt.input_fixture.empty() ||
        receipt.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        receipt.durable_store_import_provider_host_execution_identity.empty() ||
        receipt.durable_store_import_provider_local_host_execution_receipt_identity.empty()) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt identity fields must not "
            "be empty");
    }
    provider_local_host_execution_detail_validate_failure_attribution(
        receipt.failure_attribution,
        diagnostics,
        "durable store import provider local host execution receipt");
    provider_local_host_execution_detail_validate_no_side_effects(
        receipt.starts_host_process,
        receipt.reads_host_environment,
        receipt.opens_network_connection,
        receipt.materializes_sdk_request_payload,
        receipt.invokes_provider_sdk,
        receipt.writes_host_filesystem,
        diagnostics,
        "durable store import provider local host execution receipt");

    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        if (receipt.source_host_execution_status != ProviderHostExecutionStatus::Ready ||
            !receipt.source_provider_host_execution_descriptor_identity.has_value() ||
            !receipt.source_provider_host_receipt_placeholder_identity.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status requires ready host execution descriptor and receipt placeholder "
                "identities");
        }
        if (receipt.operation_kind != ProviderLocalHostExecutionOperationKind::
                                          SimulateProviderLocalHostExecutionReceipt ||
            !receipt.provider_local_host_execution_receipt_identity.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status requires local host receipt identity");
        }
        if (receipt.failure_attribution.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status cannot contain failure_attribution");
        }
    } else {
        if (receipt.operation_kind ==
            ProviderLocalHostExecutionOperationKind::SimulateProviderLocalHostExecutionReceipt) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "cannot simulate local host execution");
        }
        if (receipt.provider_local_host_execution_receipt_identity.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "cannot contain local host receipt identity");
        }
        if (!receipt.failure_attribution.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "requires failure_attribution");
        }
    }

    return result;
}

ProviderLocalHostExecutionReceiptReviewValidationResult
validate_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceiptReview &review) {
    ProviderLocalHostExecutionReceiptReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderLocalHostExecutionReceiptReviewFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review "
            "source_durable_store_import_provider_local_host_execution_receipt_format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        review.durable_store_import_provider_host_execution_identity.empty() ||
        review.durable_store_import_provider_local_host_execution_receipt_identity.empty()) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review identity fields "
            "must not be empty");
    }
    if (review.local_host_execution_boundary_summary.empty() ||
        review.next_step_recommendation.empty()) {
        provider_local_host_execution_detail_emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review summaries must "
            "not be empty");
    }
    provider_local_host_execution_detail_validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider local host execution receipt review");
    provider_local_host_execution_detail_validate_no_side_effects(
        review.starts_host_process,
        review.reads_host_environment,
        review.opens_network_connection,
        review.materializes_sdk_request_payload,
        review.invokes_provider_sdk,
        review.writes_host_filesystem,
        diagnostics,
        "durable store import provider local host execution receipt review");

    if (review.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        if (review.operation_kind != ProviderLocalHostExecutionOperationKind::
                                         SimulateProviderLocalHostExecutionReceipt ||
            !review.provider_local_host_execution_receipt_identity.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review "
                "simulated-ready status requires local host receipt identity");
        }
        if (review.failure_attribution.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review "
                "simulated-ready status cannot contain failure_attribution");
        }
    } else {
        if (review.provider_local_host_execution_receipt_identity.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review blocked "
                "status cannot contain local host receipt identity");
        }
        if (!review.failure_attribution.has_value()) {
            provider_local_host_execution_detail_emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review blocked "
                "status requires failure_attribution");
        }
    }

    return result;
}

ProviderLocalHostExecutionReceiptResult
build_provider_local_host_execution_receipt(const ProviderHostExecutionPlan &plan) {
    ProviderLocalHostExecutionReceiptResult result{
        .receipt = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_host_execution_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto can_simulate = plan.execution_status == ProviderHostExecutionStatus::Ready &&
                              plan.provider_host_execution_descriptor_identity.has_value() &&
                              plan.provider_host_receipt_placeholder_identity.has_value();
    const auto status = can_simulate ? ProviderLocalHostExecutionStatus::SimulatedReady
                                     : ProviderLocalHostExecutionStatus::Blocked;
    const auto operation_kind =
        can_simulate
            ? ProviderLocalHostExecutionOperationKind::SimulateProviderLocalHostExecutionReceipt
            : ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady;

    ProviderLocalHostExecutionReceipt receipt{
        .format_version = std::string(kProviderLocalHostExecutionReceiptFormatVersion),
        .source_durable_store_import_provider_host_execution_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_sdk_request_envelope_plan_format_version =
            plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            plan.durable_store_import_provider_sdk_request_envelope_identity,
        .durable_store_import_provider_host_execution_identity =
            plan.durable_store_import_provider_host_execution_identity,
        .source_host_execution_status = plan.execution_status,
        .source_provider_host_execution_descriptor_identity =
            plan.provider_host_execution_descriptor_identity,
        .source_provider_host_receipt_placeholder_identity =
            plan.provider_host_receipt_placeholder_identity,
        .durable_store_import_provider_local_host_execution_receipt_identity =
            provider_local_host_execution_detail_receipt_plan_identity(plan, status),
        .operation_kind = operation_kind,
        .execution_status = status,
        .provider_local_host_execution_receipt_identity =
            can_simulate
                ? provider_local_host_execution_detail_provider_local_host_execution_receipt_identity(
                      plan)
                : std::nullopt,
        .starts_host_process = false,
        .reads_host_environment = false,
        .opens_network_connection = false,
        .materializes_sdk_request_payload = false,
        .invokes_provider_sdk = false,
        .writes_host_filesystem = false,
        .failure_attribution =
            can_simulate
                ? std::nullopt
                : std::optional<ProviderLocalHostExecutionFailureAttribution>(
                      provider_local_host_execution_detail_host_execution_not_ready_failure(plan)),
    };

    result.diagnostics.append(validate_provider_local_host_execution_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

ProviderLocalHostExecutionReceiptReviewResult build_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceipt &receipt) {
    ProviderLocalHostExecutionReceiptReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_local_host_execution_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderLocalHostExecutionReceiptReview review{
        .format_version = std::string(kProviderLocalHostExecutionReceiptReviewFormatVersion),
        .source_durable_store_import_provider_local_host_execution_receipt_format_version =
            receipt.format_version,
        .source_durable_store_import_provider_host_execution_plan_format_version =
            receipt.source_durable_store_import_provider_host_execution_plan_format_version,
        .workflow_canonical_name = receipt.workflow_canonical_name,
        .session_id = receipt.session_id,
        .run_id = receipt.run_id,
        .input_fixture = receipt.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            receipt.durable_store_import_provider_sdk_request_envelope_identity,
        .durable_store_import_provider_host_execution_identity =
            receipt.durable_store_import_provider_host_execution_identity,
        .durable_store_import_provider_local_host_execution_receipt_identity =
            receipt.durable_store_import_provider_local_host_execution_receipt_identity,
        .execution_status = receipt.execution_status,
        .operation_kind = receipt.operation_kind,
        .provider_local_host_execution_receipt_identity =
            receipt.provider_local_host_execution_receipt_identity,
        .starts_host_process = receipt.starts_host_process,
        .reads_host_environment = receipt.reads_host_environment,
        .opens_network_connection = receipt.opens_network_connection,
        .materializes_sdk_request_payload = receipt.materializes_sdk_request_payload,
        .invokes_provider_sdk = receipt.invokes_provider_sdk,
        .writes_host_filesystem = receipt.writes_host_filesystem,
        .failure_attribution = receipt.failure_attribution,
        .local_host_execution_boundary_summary =
            provider_local_host_execution_detail_boundary_summary(receipt),
        .next_step_recommendation = provider_local_host_execution_detail_next_step_summary(receipt),
        .next_action = provider_local_host_execution_detail_next_action_for_receipt(receipt),
    };

    result.diagnostics.append(
        validate_provider_local_host_execution_receipt_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_local_host_harness.cpp ----

#include "durable_store_import/provider/execution/local_host_harness.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_local_host_harness_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_HOST_HARNESS";

void provider_local_host_harness_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                              std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_local_host_harness_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_local_host_harness_detail_status_slug(ProviderLocalHostHarnessStatus status) {
    return status == ProviderLocalHostHarnessStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string
provider_local_host_harness_detail_request_identity(const ProviderSecretPolicyReview &review,
                                                    ProviderLocalHostHarnessStatus status) {
    return "durable-store-import-provider-local-host-harness-request::" + review.session_id +
           "::" + provider_local_host_harness_detail_status_slug(status);
}

[[nodiscard]] std::string provider_local_host_harness_detail_record_identity(
    const ProviderLocalHostHarnessExecutionRequest &request,
    ProviderLocalHostHarnessOutcomeKind outcome) {
    std::string outcome_slug = "blocked";
    switch (outcome) {
    case ProviderLocalHostHarnessOutcomeKind::Succeeded:
        outcome_slug = "succeeded";
        break;
    case ProviderLocalHostHarnessOutcomeKind::Failed:
        outcome_slug = "failed";
        break;
    case ProviderLocalHostHarnessOutcomeKind::TimedOut:
        outcome_slug = "timeout";
        break;
    case ProviderLocalHostHarnessOutcomeKind::SandboxDenied:
        outcome_slug = "sandbox-denied";
        break;
    case ProviderLocalHostHarnessOutcomeKind::Blocked:
        outcome_slug = "blocked";
        break;
    }
    return "durable-store-import-provider-local-host-harness-record::" + request.session_id +
           "::" + outcome_slug;
}

[[nodiscard]] ProviderLocalHostHarnessFailureAttribution
provider_local_host_harness_detail_secret_policy_not_ready_failure(
    const ProviderSecretPolicyReview &review) {
    std::string message =
        "local host harness request cannot proceed because secret policy is not ready";
    if (review.failure_attribution.has_value()) {
        message = review.failure_attribution->message;
    }
    return ProviderLocalHostHarnessFailureAttribution{
        .kind = ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderLocalHostHarnessFailureAttribution
provider_local_host_harness_detail_request_not_ready_failure(
    const ProviderLocalHostHarnessExecutionRequest &request) {
    std::string message =
        "local host harness execution record cannot proceed because harness request is not ready";
    if (request.failure_attribution.has_value()) {
        message = request.failure_attribution->message;
    }
    return ProviderLocalHostHarnessFailureAttribution{
        .kind = ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
        .message = std::move(message),
    };
}

void provider_local_host_harness_detail_validate_failure(
    const std::optional<ProviderLocalHostHarnessFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_local_host_harness_detail_validate_sandbox_policy(
    const ProviderLocalHostHarnessSandboxPolicy &policy,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (!policy.test_only || policy.allow_network || policy.allow_secret ||
        policy.allow_filesystem_write || policy.allow_host_environment) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " sandbox policy must be test-only with no network, secret, "
                                 "filesystem write, or host environment");
    }
}

void provider_local_host_harness_detail_validate_no_forbidden_host_access(
    bool reads_secret_material,
    bool opens_network_connection,
    bool reads_host_environment,
    bool writes_host_filesystem,
    bool injects_secret,
    bool invokes_provider_sdk,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (reads_secret_material || injects_secret) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read or inject secret");
    }
    if (opens_network_connection) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_host_environment) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
    if (invokes_provider_sdk) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
}

[[nodiscard]] ProviderLocalHostHarnessNextActionKind
provider_local_host_harness_detail_next_action_for_record(
    const ProviderLocalHostHarnessExecutionRecord &record) {
    if (record.record_status == ProviderLocalHostHarnessStatus::Ready &&
        record.outcome_kind == ProviderLocalHostHarnessOutcomeKind::Succeeded) {
        return ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization;
    }
    if (record.failure_attribution.has_value() &&
        record.failure_attribution->kind ==
            ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady) {
        return ProviderLocalHostHarnessNextActionKind::WaitForSecretPolicy;
    }
    return ProviderLocalHostHarnessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string provider_local_host_harness_detail_boundary_summary(
    const ProviderLocalHostHarnessExecutionRecord &record) {
    if (record.outcome_kind == ProviderLocalHostHarnessOutcomeKind::Succeeded) {
        return "test-only local host harness completed without network, secret, host environment, "
               "filesystem write, or provider SDK access";
    }
    return "test-only local host harness did not produce a successful controlled execution record";
}

} // namespace

ProviderLocalHostHarnessRequestValidationResult
validate_provider_local_host_harness_execution_request(
    const ProviderLocalHostHarnessExecutionRequest &request) {
    ProviderLocalHostHarnessRequestValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (request.format_version != kProviderLocalHostHarnessRequestFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness request format_version must be '" +
                std::string(kProviderLocalHostHarnessRequestFormatVersion) + "'");
    }
    if (request.source_durable_store_import_provider_secret_policy_review_format_version !=
        kProviderSecretPolicyReviewFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness request source format_version must be '" +
                std::string(kProviderSecretPolicyReviewFormatVersion) + "'");
    }
    if (request.workflow_canonical_name.empty() || request.session_id.empty() ||
        request.input_fixture.empty() ||
        request.durable_store_import_provider_secret_resolver_response_identity.empty() ||
        request.durable_store_import_provider_local_host_harness_request_identity.empty()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, "provider local host harness request identity fields must not be empty");
    }
    if (request.timeout_milliseconds <= 0) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics, "provider local host harness request timeout must be positive");
    }
    provider_local_host_harness_detail_validate_sandbox_policy(
        request.sandbox_policy, diagnostics, "provider local host harness request");
    provider_local_host_harness_detail_validate_failure(
        request.failure_attribution, diagnostics, "provider local host harness request");
    provider_local_host_harness_detail_validate_no_forbidden_host_access(
        request.reads_secret_material,
        request.opens_network_connection,
        request.reads_host_environment,
        request.writes_host_filesystem,
        request.injects_secret,
        request.invokes_provider_sdk,
        diagnostics,
        "provider local host harness request");
    if (request.request_status == ProviderLocalHostHarnessStatus::Ready) {
        if (request.source_secret_policy_next_action !=
                ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness ||
            request.operation_kind != ProviderLocalHostHarnessOperationKind::PlanHarnessRequest ||
            request.failure_attribution.has_value()) {
            provider_local_host_harness_detail_emit_validation_error(
                diagnostics,
                "provider local host harness request ready status requires ready "
                "secret policy and no failure");
        }
    } else if (request.operation_kind ==
                   ProviderLocalHostHarnessOperationKind::PlanHarnessRequest ||
               !request.failure_attribution.has_value()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness request blocked status requires noop "
            "operation and failure_attribution");
    }
    return result;
}

ProviderLocalHostHarnessRecordValidationResult
validate_provider_local_host_harness_execution_record(
    const ProviderLocalHostHarnessExecutionRecord &record) {
    ProviderLocalHostHarnessRecordValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (record.format_version != kProviderLocalHostHarnessExecutionRecordFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness execution record format_version must be '" +
                std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion) + "'");
    }
    if (record.source_durable_store_import_provider_local_host_harness_request_format_version !=
        kProviderLocalHostHarnessRequestFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness execution record source format_version must be '" +
                std::string(kProviderLocalHostHarnessRequestFormatVersion) + "'");
    }
    if (record.workflow_canonical_name.empty() || record.session_id.empty() ||
        record.input_fixture.empty() ||
        record.durable_store_import_provider_local_host_harness_request_identity.empty() ||
        record.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        record.captured_diagnostic_summary.empty()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness execution record identity and "
            "diagnostic fields must not be empty");
    }
    provider_local_host_harness_detail_validate_sandbox_policy(
        record.sandbox_policy, diagnostics, "provider local host harness record");
    provider_local_host_harness_detail_validate_failure(
        record.failure_attribution, diagnostics, "provider local host harness record");
    provider_local_host_harness_detail_validate_no_forbidden_host_access(
        record.reads_secret_material,
        record.opens_network_connection,
        record.reads_host_environment,
        record.writes_host_filesystem,
        record.injects_secret,
        record.invokes_provider_sdk,
        diagnostics,
        "provider local host harness record");
    if (record.record_status == ProviderLocalHostHarnessStatus::Ready) {
        if (record.source_harness_request_status != ProviderLocalHostHarnessStatus::Ready ||
            record.operation_kind != ProviderLocalHostHarnessOperationKind::RunTestHarness) {
            provider_local_host_harness_detail_emit_validation_error(
                diagnostics,
                "provider local host harness record ready status requires ready "
                "request and test harness run");
        }
    } else if (record.operation_kind == ProviderLocalHostHarnessOperationKind::RunTestHarness ||
               !record.failure_attribution.has_value()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness record blocked status requires noop "
            "operation and failure_attribution");
    }
    return result;
}

ProviderLocalHostHarnessReviewValidationResult
validate_provider_local_host_harness_review(const ProviderLocalHostHarnessReview &review) {
    ProviderLocalHostHarnessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderLocalHostHarnessReviewFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness review format_version must be '" +
                std::string(kProviderLocalHostHarnessReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_local_host_harness_execution_record_format_version !=
        kProviderLocalHostHarnessExecutionRecordFormatVersion) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness review source format_version must be '" +
                std::string(kProviderLocalHostHarnessExecutionRecordFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        review.harness_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness review identity and summary fields must not be empty");
    }
    provider_local_host_harness_detail_validate_sandbox_policy(
        review.sandbox_policy, diagnostics, "provider local host harness review");
    provider_local_host_harness_detail_validate_failure(
        review.failure_attribution, diagnostics, "provider local host harness review");
    if (review.next_action ==
            ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization &&
        (review.record_status != ProviderLocalHostHarnessStatus::Ready ||
         review.outcome_kind != ProviderLocalHostHarnessOutcomeKind::Succeeded ||
         review.failure_attribution.has_value())) {
        provider_local_host_harness_detail_emit_validation_error(
            diagnostics,
            "provider local host harness review payload next action requires "
            "successful record and no failure");
    }
    return result;
}

ProviderLocalHostHarnessRequestResult build_provider_local_host_harness_execution_request(
    const ProviderSecretPolicyReview &secret_policy_review) {
    ProviderLocalHostHarnessRequestResult result;
    result.diagnostics.append(
        validate_provider_secret_policy_review(secret_policy_review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = secret_policy_review.next_action ==
                       ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness;
    const auto status =
        ready ? ProviderLocalHostHarnessStatus::Ready : ProviderLocalHostHarnessStatus::Blocked;
    ProviderLocalHostHarnessExecutionRequest request;
    request.workflow_canonical_name = secret_policy_review.workflow_canonical_name;
    request.session_id = secret_policy_review.session_id;
    request.run_id = secret_policy_review.run_id;
    request.input_fixture = secret_policy_review.input_fixture;
    request.durable_store_import_provider_secret_resolver_response_identity =
        secret_policy_review.durable_store_import_provider_secret_resolver_response_identity;
    request.source_secret_policy_next_action = secret_policy_review.next_action;
    request.durable_store_import_provider_local_host_harness_request_identity =
        provider_local_host_harness_detail_request_identity(secret_policy_review, status);
    request.request_status = status;
    request.secret_handle = secret_policy_review.secret_handle;
    if (ready) {
        request.operation_kind = ProviderLocalHostHarnessOperationKind::PlanHarnessRequest;
    } else {
        request.operation_kind = ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady;
        request.failure_attribution =
            provider_local_host_harness_detail_secret_policy_not_ready_failure(
                secret_policy_review);
    }
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.request = std::move(request);
    return result;
}

ProviderLocalHostHarnessRecordResult
run_provider_local_host_test_harness(const ProviderLocalHostHarnessExecutionRequest &request) {
    ProviderLocalHostHarnessRecordResult result;
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_request(request).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalHostHarnessExecutionRecord record;
    record.workflow_canonical_name = request.workflow_canonical_name;
    record.session_id = request.session_id;
    record.run_id = request.run_id;
    record.input_fixture = request.input_fixture;
    record.durable_store_import_provider_local_host_harness_request_identity =
        request.durable_store_import_provider_local_host_harness_request_identity;
    record.source_harness_request_status = request.request_status;
    record.sandbox_policy = request.sandbox_policy;
    if (request.request_status != ProviderLocalHostHarnessStatus::Ready) {
        record.operation_kind = ProviderLocalHostHarnessOperationKind::NoopHarnessRequestNotReady;
        record.record_status = ProviderLocalHostHarnessStatus::Blocked;
        record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Blocked;
        record.exit_code = -1;
        record.captured_diagnostic_summary = "harness request was blocked before test runner";
        record.failure_attribution =
            provider_local_host_harness_detail_request_not_ready_failure(request);
    } else {
        record.operation_kind = ProviderLocalHostHarnessOperationKind::RunTestHarness;
        record.record_status = ProviderLocalHostHarnessStatus::Ready;
        switch (request.fixture_kind) {
        case ProviderLocalHostHarnessFixtureKind::Success:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Succeeded;
            record.exit_code = 0;
            record.captured_diagnostic_summary = "test harness completed successfully";
            record.captured_stdout_excerpt = "ok";
            break;
        case ProviderLocalHostHarnessFixtureKind::NonzeroExit:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::Failed;
            record.exit_code = 42;
            record.captured_diagnostic_summary = "test harness exited with nonzero status";
            record.captured_stderr_excerpt = "mock provider failed";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::NonzeroExit,
                .message = "test harness exited with nonzero status 42",
            };
            break;
        case ProviderLocalHostHarnessFixtureKind::Timeout:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::TimedOut;
            record.exit_code = -1;
            record.timed_out = true;
            record.captured_diagnostic_summary = "test harness timed out";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::Timeout,
                .message = "test harness exceeded timeout policy",
            };
            break;
        case ProviderLocalHostHarnessFixtureKind::SandboxDenied:
            record.outcome_kind = ProviderLocalHostHarnessOutcomeKind::SandboxDenied;
            record.exit_code = -1;
            record.sandbox_denied = true;
            record.captured_diagnostic_summary = "test harness was denied by sandbox policy";
            record.failure_attribution = ProviderLocalHostHarnessFailureAttribution{
                .kind = ProviderLocalHostHarnessFailureKind::SandboxDenied,
                .message = "test harness requested a sandbox-forbidden operation",
            };
            break;
        }
    }
    record.durable_store_import_provider_local_host_harness_record_identity =
        provider_local_host_harness_detail_record_identity(request, record.outcome_kind);
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_record(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.record = std::move(record);
    return result;
}

ProviderLocalHostHarnessReviewResult
build_provider_local_host_harness_review(const ProviderLocalHostHarnessExecutionRecord &record) {
    ProviderLocalHostHarnessReviewResult result;
    result.diagnostics.append(
        validate_provider_local_host_harness_execution_record(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalHostHarnessReview review;
    review.workflow_canonical_name = record.workflow_canonical_name;
    review.session_id = record.session_id;
    review.run_id = record.run_id;
    review.input_fixture = record.input_fixture;
    review.durable_store_import_provider_local_host_harness_record_identity =
        record.durable_store_import_provider_local_host_harness_record_identity;
    review.record_status = record.record_status;
    review.outcome_kind = record.outcome_kind;
    review.sandbox_policy = record.sandbox_policy;
    review.harness_boundary_summary = provider_local_host_harness_detail_boundary_summary(record);
    review.next_action = provider_local_host_harness_detail_next_action_for_record(record);
    review.next_step_recommendation =
        review.next_action ==
                ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization
            ? "ready for fake SDK payload materialization"
            : "manual review or source readiness is required before payload materialization";
    review.failure_attribution = record.failure_attribution;
    result.diagnostics.append(validate_provider_local_host_harness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_local_filesystem_alpha.cpp ----

#include "durable_store_import/provider/execution/local_filesystem_alpha.hpp"
#include "validation/common.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view provider_local_filesystem_alpha_detail_kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_FILESYSTEM_ALPHA";

void provider_local_filesystem_alpha_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                                  std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_local_filesystem_alpha_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_local_filesystem_alpha_detail_status_slug(ProviderLocalFilesystemAlphaStatus status) {
    return status == ProviderLocalFilesystemAlphaStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string provider_local_filesystem_alpha_detail_plan_identity(
    const ProviderSdkMockAdapterReadiness &readiness, ProviderLocalFilesystemAlphaStatus status) {
    return "durable-store-import-provider-local-filesystem-alpha-plan::" + readiness.session_id +
           "::" + provider_local_filesystem_alpha_detail_status_slug(status);
}

[[nodiscard]] std::string provider_local_filesystem_alpha_detail_result_identity(
    const ProviderLocalFilesystemAlphaPlan &plan, ProviderLocalFilesystemAlphaResultKind result) {
    std::string result_slug = "blocked";
    switch (result) {
    case ProviderLocalFilesystemAlphaResultKind::Accepted:
        result_slug = "accepted";
        break;
    case ProviderLocalFilesystemAlphaResultKind::DryRunOnly:
        result_slug = "dry-run-only";
        break;
    case ProviderLocalFilesystemAlphaResultKind::WriteFailed:
        result_slug = "write-failed";
        break;
    case ProviderLocalFilesystemAlphaResultKind::Blocked:
        result_slug = "blocked";
        break;
    }
    return "durable-store-import-provider-local-filesystem-alpha-result::" + plan.session_id +
           "::" + result_slug;
}

[[nodiscard]] ProviderLocalFilesystemAlphaFailureAttribution
provider_local_filesystem_alpha_detail_mock_readiness_not_ready_failure(
    const ProviderSdkMockAdapterReadiness &readiness) {
    std::string message = "local filesystem alpha provider cannot proceed because mock adapter "
                          "readiness is not ready";
    if (readiness.failure_attribution.has_value()) {
        message = readiness.failure_attribution->message;
    }
    return ProviderLocalFilesystemAlphaFailureAttribution{
        .kind = ProviderLocalFilesystemAlphaFailureKind::MockReadinessNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderLocalFilesystemAlphaFailureAttribution
provider_local_filesystem_alpha_detail_plan_not_ready_failure(
    const ProviderLocalFilesystemAlphaPlan &plan) {
    std::string message =
        "local filesystem alpha provider cannot run because alpha plan is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }
    return ProviderLocalFilesystemAlphaFailureAttribution{
        .kind = ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
        .message = std::move(message),
    };
}

void provider_local_filesystem_alpha_detail_validate_failure(
    const std::optional<ProviderLocalFilesystemAlphaFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_local_filesystem_alpha_detail_validate_no_cloud_access(
    bool opens_network_connection,
    bool reads_secret_material,
    bool invokes_cloud_provider_sdk,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (opens_network_connection || reads_secret_material || invokes_cloud_provider_sdk) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " cannot open network, read secret, or invoke cloud provider SDK");
    }
}

[[nodiscard]] ProviderLocalFilesystemAlphaNextActionKind
provider_local_filesystem_alpha_detail_next_action_for_result(
    const ProviderLocalFilesystemAlphaResult &result) {
    if (result.result_status == ProviderLocalFilesystemAlphaStatus::Ready &&
        (result.normalized_result == ProviderLocalFilesystemAlphaResultKind::Accepted ||
         result.normalized_result == ProviderLocalFilesystemAlphaResultKind::DryRunOnly)) {
        return ProviderLocalFilesystemAlphaNextActionKind::ReadyForIdempotencyContract;
    }
    if (result.failure_attribution.has_value() &&
        result.failure_attribution->kind ==
            ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady) {
        return ProviderLocalFilesystemAlphaNextActionKind::WaitForMockAdapter;
    }
    return ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderLocalFilesystemAlphaPlanValidationResult
validate_provider_local_filesystem_alpha_plan(const ProviderLocalFilesystemAlphaPlan &plan) {
    ProviderLocalFilesystemAlphaPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderLocalFilesystemAlphaPlanFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha plan format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_mock_adapter_readiness_format_version !=
        kProviderSdkMockAdapterReadinessFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha plan source format_version must be '" +
                std::string(kProviderSdkMockAdapterReadinessFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        plan.durable_store_import_provider_local_filesystem_alpha_plan_identity.empty() ||
        plan.provider_key.empty() || plan.target_object_name.empty() ||
        plan.planned_payload_digest.empty()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics, "provider local filesystem alpha plan identity fields must not be empty");
    }
    if (!plan.real_provider_alpha || !plan.fake_adapter_default_path_preserved ||
        !plan.opt_in_required) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha plan must remain opt-in alpha with "
            "fake path preserved");
    }
    provider_local_filesystem_alpha_detail_validate_failure(
        plan.failure_attribution, diagnostics, "provider local filesystem alpha plan");
    provider_local_filesystem_alpha_detail_validate_no_cloud_access(
        plan.opens_network_connection,
        plan.reads_secret_material,
        plan.invokes_cloud_provider_sdk,
        diagnostics,
        "provider local filesystem alpha plan");
    if (plan.plan_status == ProviderLocalFilesystemAlphaStatus::Ready) {
        if (plan.source_mock_adapter_next_action !=
                ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity ||
            plan.operation_kind !=
                ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha ||
            plan.failure_attribution.has_value()) {
            provider_local_filesystem_alpha_detail_emit_validation_error(
                diagnostics,
                "provider local filesystem alpha plan ready status requires "
                "accepted mock adapter and no failure");
        }
    } else if (plan.operation_kind ==
                   ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha ||
               !plan.failure_attribution.has_value()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha plan blocked status requires noop "
            "operation and failure_attribution");
    }
    return result;
}

ProviderLocalFilesystemAlphaResultValidationResult validate_provider_local_filesystem_alpha_result(
    const ProviderLocalFilesystemAlphaResult &result_record) {
    ProviderLocalFilesystemAlphaResultValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (result_record.format_version != kProviderLocalFilesystemAlphaResultFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaResultFormatVersion) + "'");
    }
    if (result_record
            .source_durable_store_import_provider_local_filesystem_alpha_plan_format_version !=
        kProviderLocalFilesystemAlphaPlanFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result source format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaPlanFormatVersion) + "'");
    }
    if (result_record.workflow_canonical_name.empty() || result_record.session_id.empty() ||
        result_record.input_fixture.empty() ||
        result_record.durable_store_import_provider_local_filesystem_alpha_plan_identity.empty() ||
        result_record.durable_store_import_provider_local_filesystem_alpha_result_identity
            .empty() ||
        result_record.provider_commit_reference.empty() ||
        result_record.provider_result_digest.empty()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result identity fields must not be empty");
    }
    provider_local_filesystem_alpha_detail_validate_failure(
        result_record.failure_attribution, diagnostics, "provider local filesystem alpha result");
    provider_local_filesystem_alpha_detail_validate_no_cloud_access(
        result_record.opens_network_connection,
        result_record.reads_secret_material,
        result_record.invokes_cloud_provider_sdk,
        diagnostics,
        "provider local filesystem alpha result");
    if (result_record.result_status == ProviderLocalFilesystemAlphaStatus::Ready) {
        if (result_record.source_alpha_plan_status != ProviderLocalFilesystemAlphaStatus::Ready ||
            result_record.operation_kind !=
                ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha) {
            provider_local_filesystem_alpha_detail_emit_validation_error(
                diagnostics,
                "provider local filesystem alpha result ready status requires "
                "ready plan and alpha run");
        }
    } else if (result_record.operation_kind ==
                   ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha ||
               !result_record.failure_attribution.has_value()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result blocked status requires noop "
            "operation and failure_attribution");
    }
    return result;
}

ProviderLocalFilesystemAlphaReadinessValidationResult
validate_provider_local_filesystem_alpha_readiness(
    const ProviderLocalFilesystemAlphaReadiness &readiness) {
    ProviderLocalFilesystemAlphaReadinessValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (readiness.format_version != kProviderLocalFilesystemAlphaReadinessFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha readiness format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaReadinessFormatVersion) + "'");
    }
    if (readiness
            .source_durable_store_import_provider_local_filesystem_alpha_result_format_version !=
        kProviderLocalFilesystemAlphaResultFormatVersion) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics,
            "provider local filesystem alpha readiness source format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaResultFormatVersion) + "'");
    }
    if (readiness.workflow_canonical_name.empty() || readiness.session_id.empty() ||
        readiness.input_fixture.empty() ||
        readiness.durable_store_import_provider_local_filesystem_alpha_result_identity.empty() ||
        readiness.readiness_summary.empty() || readiness.next_step_recommendation.empty()) {
        provider_local_filesystem_alpha_detail_emit_validation_error(
            diagnostics, "provider local filesystem alpha readiness fields must not be empty");
    }
    provider_local_filesystem_alpha_detail_validate_failure(
        readiness.failure_attribution, diagnostics, "provider local filesystem alpha readiness");
    return result;
}

ProviderLocalFilesystemAlphaPlanResult
build_provider_local_filesystem_alpha_plan(const ProviderSdkMockAdapterReadiness &readiness) {
    ProviderLocalFilesystemAlphaPlanResult result;
    result.diagnostics.append(validate_provider_sdk_mock_adapter_readiness(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready =
        readiness.next_action == ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity;
    const auto status = ready ? ProviderLocalFilesystemAlphaStatus::Ready
                              : ProviderLocalFilesystemAlphaStatus::Blocked;
    ProviderLocalFilesystemAlphaPlan plan;
    plan.workflow_canonical_name = readiness.workflow_canonical_name;
    plan.session_id = readiness.session_id;
    plan.run_id = readiness.run_id;
    plan.input_fixture = readiness.input_fixture;
    plan.durable_store_import_provider_sdk_mock_adapter_result_identity =
        readiness.durable_store_import_provider_sdk_mock_adapter_result_identity;
    plan.source_mock_adapter_next_action = readiness.next_action;
    plan.durable_store_import_provider_local_filesystem_alpha_plan_identity =
        provider_local_filesystem_alpha_detail_plan_identity(readiness, status);
    plan.plan_status = status;
    plan.target_object_name = readiness.session_id + ".ahfl-provider-alpha";
    plan.planned_payload_digest = "sha256:local-filesystem-alpha:" + readiness.session_id;
    if (ready) {
        plan.operation_kind = ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha;
    } else {
        plan.operation_kind = ProviderLocalFilesystemAlphaOperationKind::NoopMockReadinessNotReady;
        plan.failure_attribution =
            provider_local_filesystem_alpha_detail_mock_readiness_not_ready_failure(readiness);
    }
    result.diagnostics.append(validate_provider_local_filesystem_alpha_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderLocalFilesystemAlphaExecutionResult
run_provider_local_filesystem_alpha(const ProviderLocalFilesystemAlphaPlan &plan) {
    ProviderLocalFilesystemAlphaExecutionResult result;
    result.diagnostics.append(validate_provider_local_filesystem_alpha_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalFilesystemAlphaResult record;
    record.workflow_canonical_name = plan.workflow_canonical_name;
    record.session_id = plan.session_id;
    record.run_id = plan.run_id;
    record.input_fixture = plan.input_fixture;
    record.durable_store_import_provider_local_filesystem_alpha_plan_identity =
        plan.durable_store_import_provider_local_filesystem_alpha_plan_identity;
    record.source_alpha_plan_status = plan.plan_status;
    record.provider_commit_reference = "local-filesystem-alpha://" + plan.target_object_name;
    record.provider_result_digest = plan.planned_payload_digest;
    if (plan.plan_status != ProviderLocalFilesystemAlphaStatus::Ready) {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::NoopAlphaPlanNotReady;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Blocked;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::Blocked;
        record.failure_attribution =
            provider_local_filesystem_alpha_detail_plan_not_ready_failure(plan);
    } else if (!plan.opt_in_enabled) {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Ready;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::DryRunOnly;
        record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
            .kind = ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
            .message = "local filesystem alpha write is available but not enabled for default path",
        };
    } else {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Ready;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::Accepted;
        record.opt_in_used = true;
        try {
            const std::filesystem::path target_dir =
                plan.target_directory.value_or(".ahfl-provider-alpha");
            std::filesystem::create_directories(target_dir);
            const auto target_path = target_dir / plan.target_object_name;
            std::ofstream out(target_path);
            out << "provider_commit_reference=" << record.provider_commit_reference << '\n';
            out << "provider_result_digest=" << record.provider_result_digest << '\n';
            record.wrote_local_file = static_cast<bool>(out);
            if (!record.wrote_local_file) {
                record.normalized_result = ProviderLocalFilesystemAlphaResultKind::WriteFailed;
                record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
                    .kind = ProviderLocalFilesystemAlphaFailureKind::FilesystemWriteFailed,
                    .message = "local filesystem alpha write failed",
                };
            }
        } catch (const std::filesystem::filesystem_error &error) {
            record.normalized_result = ProviderLocalFilesystemAlphaResultKind::WriteFailed;
            record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
                .kind = ProviderLocalFilesystemAlphaFailureKind::FilesystemWriteFailed,
                .message = error.what(),
            };
        }
    }
    record.durable_store_import_provider_local_filesystem_alpha_result_identity =
        provider_local_filesystem_alpha_detail_result_identity(plan, record.normalized_result);
    result.diagnostics.append(validate_provider_local_filesystem_alpha_result(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.result = std::move(record);
    return result;
}

ProviderLocalFilesystemAlphaReadinessResult build_provider_local_filesystem_alpha_readiness(
    const ProviderLocalFilesystemAlphaResult &alpha_result) {
    ProviderLocalFilesystemAlphaReadinessResult result;
    result.diagnostics.append(
        validate_provider_local_filesystem_alpha_result(alpha_result).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalFilesystemAlphaReadiness readiness;
    readiness.workflow_canonical_name = alpha_result.workflow_canonical_name;
    readiness.session_id = alpha_result.session_id;
    readiness.run_id = alpha_result.run_id;
    readiness.input_fixture = alpha_result.input_fixture;
    readiness.durable_store_import_provider_local_filesystem_alpha_result_identity =
        alpha_result.durable_store_import_provider_local_filesystem_alpha_result_identity;
    readiness.result_status = alpha_result.result_status;
    readiness.normalized_result = alpha_result.normalized_result;
    readiness.readiness_summary =
        alpha_result.normalized_result == ProviderLocalFilesystemAlphaResultKind::Accepted
            ? "local filesystem alpha provider wrote an opt-in commit marker"
            : "local filesystem alpha provider stayed on safe default path";
    readiness.next_action =
        provider_local_filesystem_alpha_detail_next_action_for_result(alpha_result);
    readiness.next_step_recommendation =
        readiness.next_action ==
                ProviderLocalFilesystemAlphaNextActionKind::ReadyForIdempotencyContract
            ? "ready for idempotency and retry contract"
            : "manual review required before idempotency contract";
    readiness.failure_attribution = alpha_result.failure_attribution;
    result.diagnostics.append(
        validate_provider_local_filesystem_alpha_readiness(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.readiness = std::move(readiness);
    return result;
}

} // namespace ahfl::durable_store_import
