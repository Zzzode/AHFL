#include "ahfl/durable_store_import/provider_host_execution.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_HOST_EXECUTION";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string execution_status_slug(ProviderHostExecutionStatus status) {
    switch (status) {
    case ProviderHostExecutionStatus::Ready:
        return "ready";
    case ProviderHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string execution_plan_identity(const ProviderSdkRequestEnvelopePlan &plan,
                                                  ProviderHostExecutionStatus status) {
    return "durable-store-import-provider-host-execution::" + plan.session_id +
           "::" + execution_status_slug(status);
}

[[nodiscard]] bool policy_matches_sdk_envelope(const ProviderSdkRequestEnvelopePlan &plan,
                                               const ProviderHostExecutionPolicy &policy) {
    return policy.host_handoff_policy_ref == plan.envelope_policy.host_handoff_policy_ref &&
           policy.provider_namespace == plan.envelope_policy.provider_namespace;
}

[[nodiscard]] std::optional<std::string>
provider_host_execution_descriptor_identity(const ProviderSdkRequestEnvelopePlan &plan) {
    if (!plan.host_handoff_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-host-execution-descriptor::" + *plan.host_handoff_descriptor_identity;
}

[[nodiscard]] std::optional<std::string>
provider_host_receipt_placeholder_identity(const ProviderSdkRequestEnvelopePlan &plan) {
    if (!plan.host_handoff_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-host-receipt-placeholder::" + *plan.host_handoff_descriptor_identity;
}

[[nodiscard]] ProviderHostExecutionFailureAttribution
sdk_handoff_not_ready_failure(const ProviderSdkRequestEnvelopePlan &plan) {
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
first_missing_host_capability(const ProviderHostExecutionPolicy &policy) {
    if (!policy.supports_local_host_execution_descriptor_planning) {
        return ProviderHostExecutionCapabilityKind::PlanLocalHostExecutionDescriptor;
    }
    return ProviderHostExecutionCapabilityKind::PlanDryRunHostReceiptPlaceholder;
}

[[nodiscard]] std::string
missing_host_capability_message(ProviderHostExecutionCapabilityKind capability) {
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
next_action_for_host_execution_plan(const ProviderHostExecutionPlan &plan) {
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

[[nodiscard]] std::string boundary_summary(const ProviderHostExecutionPlan &plan) {
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

[[nodiscard]] std::string next_step_summary(const ProviderHostExecutionPlan &plan) {
    switch (next_action_for_host_execution_plan(plan)) {
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

void validate_failure_attribution(
    const std::optional<ProviderHostExecutionFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_side_effects(bool starts_host_process,
                              bool reads_host_environment,
                              bool opens_network_connection,
                              bool materializes_sdk_request_payload,
                              bool invokes_provider_sdk,
                              bool writes_host_filesystem,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (starts_host_process) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot start host process");
    }
    if (reads_host_environment) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (materializes_sdk_request_payload) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (writes_host_filesystem) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

} // namespace

ProviderHostExecutionPolicyValidationResult
validate_provider_host_execution_policy(const ProviderHostExecutionPolicy &policy) {
    ProviderHostExecutionPolicyValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (policy.format_version != kProviderHostExecutionPolicyFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider host execution policy format_version must be '" +
                std::string(kProviderHostExecutionPolicyFormatVersion) + "'");
    }
    if (policy.host_execution_policy_identity.empty() || policy.host_handoff_policy_ref.empty() ||
        policy.provider_namespace.empty() || policy.execution_profile_ref.empty() ||
        policy.sandbox_policy_ref.empty() || policy.timeout_policy_ref.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy identity "
                              "fields must not be empty");
    }
    if (!policy.credential_free) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy must be "
                              "credential_free");
    }
    if (!policy.network_free) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy must be "
                              "network_free");
    }
    if (policy.credential_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "credential_reference");
    }
    if (policy.secret_value.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "secret_value");
    }
    if (policy.host_command.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "host_command");
    }
    if (policy.host_environment_secret.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "host_environment_secret");
    }
    if (policy.provider_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "provider_endpoint_uri");
    }
    if (policy.network_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "network_endpoint_uri");
    }
    if (policy.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "object_path");
    }
    if (policy.database_table.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "database_table");
    }
    if (policy.sdk_request_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution policy cannot contain "
                              "sdk_request_payload");
    }
    if (policy.sdk_response_payload.has_value()) {
        emit_validation_error(diagnostics,
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
        emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_envelope_policy_format_version !=
        kProviderSdkEnvelopePolicyFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider host execution plan "
            "source_durable_store_import_provider_sdk_envelope_policy_format_version must be '" +
                std::string(kProviderSdkEnvelopePolicyFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        plan.durable_store_import_provider_host_execution_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution plan identity fields "
                              "must not be empty");
    }

    result.diagnostics.append(
        validate_provider_host_execution_policy(plan.host_execution_policy).diagnostics);
    validate_failure_attribution(
        plan.failure_attribution, diagnostics, "durable store import provider host execution plan");
    validate_no_side_effects(plan.starts_host_process,
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
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan ready status "
                                  "requires ready SDK envelope and host handoff descriptor "
                                  "identity");
        }
        if (!plan.host_execution_policy.supports_local_host_execution_descriptor_planning ||
            !plan.host_execution_policy.supports_dry_run_host_receipt_placeholder_planning) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan ready status "
                                  "requires host execution planning capabilities");
        }
        if (plan.operation_kind != ProviderHostExecutionOperationKind::PlanProviderHostExecution ||
            !plan.provider_host_execution_descriptor_identity.has_value() ||
            !plan.provider_host_receipt_placeholder_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan ready status "
                                  "requires host execution descriptor and receipt placeholder "
                                  "identities");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan ready status "
                                  "cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderHostExecutionOperationKind::PlanProviderHostExecution) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan blocked "
                                  "status cannot plan host execution");
        }
        if (plan.provider_host_execution_descriptor_identity.has_value() ||
            plan.provider_host_receipt_placeholder_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution plan blocked "
                                  "status cannot contain host execution descriptor or receipt "
                                  "placeholder identities");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
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
        emit_validation_error(diagnostics,
                              "durable store import provider host execution readiness review "
                              "format_version must be '" +
                                  std::string(kProviderHostExecutionReadinessReviewFormatVersion) +
                                  "'");
    }
    if (review.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider host execution readiness review "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        emit_validation_error(
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
        emit_validation_error(diagnostics,
                              "durable store import provider host execution readiness review "
                              "identity fields must not be empty");
    }
    if (review.host_execution_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider host execution readiness review "
                              "summaries must not be empty");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider host execution readiness review");
    validate_no_side_effects(review.starts_host_process,
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
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution readiness review "
                                  "ready status requires host execution descriptor and receipt "
                                  "placeholder identities");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution readiness review "
                                  "ready status cannot contain failure_attribution");
        }
    } else {
        if (review.provider_host_execution_descriptor_identity.has_value() ||
            review.provider_host_receipt_placeholder_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider host execution readiness review "
                                  "blocked status cannot contain host execution descriptor or "
                                  "receipt placeholder identities");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
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

    const auto policy_matches = policy_matches_sdk_envelope(plan, policy);
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
        failure = sdk_handoff_not_ready_failure(plan);
    } else if (!policy_matches) {
        operation_kind = ProviderHostExecutionOperationKind::NoopHostPolicyMismatch;
        failure = ProviderHostExecutionFailureAttribution{
            .kind = ProviderHostExecutionFailureKind::HostPolicyMismatch,
            .message = "provider host execution policy does not match SDK handoff",
            .missing_capability = std::nullopt,
        };
    } else {
        const auto missing_capability = first_missing_host_capability(policy);
        operation_kind = ProviderHostExecutionOperationKind::NoopUnsupportedHostCapability;
        failure = ProviderHostExecutionFailureAttribution{
            .kind = ProviderHostExecutionFailureKind::UnsupportedHostCapability,
            .message = missing_host_capability_message(missing_capability),
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
            execution_plan_identity(plan, status),
        .operation_kind = operation_kind,
        .execution_status = status,
        .provider_host_execution_descriptor_identity =
            can_plan ? provider_host_execution_descriptor_identity(plan) : std::nullopt,
        .provider_host_receipt_placeholder_identity =
            can_plan ? provider_host_receipt_placeholder_identity(plan) : std::nullopt,
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
        .host_execution_boundary_summary = boundary_summary(plan),
        .next_step_recommendation = next_step_summary(plan),
        .next_action = next_action_for_host_execution_plan(plan),
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
