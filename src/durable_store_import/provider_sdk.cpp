#include "ahfl/durable_store_import/provider_sdk.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_SDK";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string envelope_status_slug(ProviderSdkEnvelopeStatus status) {
    switch (status) {
    case ProviderSdkEnvelopeStatus::Ready:
        return "ready";
    case ProviderSdkEnvelopeStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string request_plan_identity(const ProviderRuntimePreflightPlan &plan,
                                                ProviderSdkEnvelopeStatus status) {
    return "durable-store-import-provider-sdk-request-envelope::" + plan.session_id +
           "::" + envelope_status_slug(status);
}

[[nodiscard]] bool policy_matches_runtime_preflight(const ProviderRuntimePreflightPlan &plan,
                                                    const ProviderSdkEnvelopePolicy &policy) {
    return policy.runtime_profile_ref == plan.runtime_profile.runtime_profile_identity &&
           policy.provider_namespace == plan.runtime_profile.provider_namespace;
}

[[nodiscard]] std::optional<std::string>
provider_sdk_request_envelope_identity(const ProviderRuntimePreflightPlan &plan) {
    if (!plan.sdk_invocation_envelope_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-request-envelope::" + *plan.sdk_invocation_envelope_identity;
}

[[nodiscard]] std::optional<std::string>
host_handoff_descriptor_identity(const ProviderRuntimePreflightPlan &plan) {
    if (!plan.sdk_invocation_envelope_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-host-handoff::" + *plan.sdk_invocation_envelope_identity;
}

[[nodiscard]] ProviderSdkEnvelopeFailureAttribution
runtime_preflight_not_ready_failure(const ProviderRuntimePreflightPlan &plan) {
    std::string message =
        "provider SDK request envelope cannot proceed because runtime preflight is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderSdkEnvelopeFailureAttribution{
        .kind = ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady,
        .message = std::move(message),
        .missing_capability = std::nullopt,
    };
}

[[nodiscard]] ProviderSdkEnvelopeCapabilityKind
first_missing_envelope_capability(const ProviderSdkEnvelopePolicy &policy) {
    if (!policy.supports_secret_free_request_envelope_planning) {
        return ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope;
    }
    return ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor;
}

[[nodiscard]] std::string
missing_envelope_capability_message(ProviderSdkEnvelopeCapabilityKind capability) {
    switch (capability) {
    case ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope:
        return "provider SDK envelope policy does not support secret-free request envelope "
               "planning";
    case ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor:
        return "provider SDK envelope policy does not support host handoff descriptor planning";
    }

    return "provider SDK envelope capability is unsupported";
}

[[nodiscard]] ProviderSdkHandoffReadinessNextActionKind
next_action_for_request_envelope_plan(const ProviderSdkRequestEnvelopePlan &plan) {
    if (plan.envelope_status == ProviderSdkEnvelopeStatus::Ready) {
        return ProviderSdkHandoffReadinessNextActionKind::ReadyForHostExecutionPrototype;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch:
            return ProviderSdkHandoffReadinessNextActionKind::FixEnvelopePolicy;
        case ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability:
            return ProviderSdkHandoffReadinessNextActionKind::WaitForEnvelopeCapability;
        case ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady:
            return ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderSdkRequestEnvelopePlan &plan) {
    if (plan.envelope_status == ProviderSdkEnvelopeStatus::Ready) {
        return "provider SDK request envelope is planned without materializing SDK payload, "
               "starting a host process, opening network, or invoking a provider SDK";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch) {
        return "provider SDK request envelope skipped because envelope policy does not match "
               "runtime preflight";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability) {
        return "provider SDK request envelope skipped because envelope capability is unsupported";
    }

    return "provider SDK request envelope skipped because runtime preflight is not ready";
}

[[nodiscard]] std::string next_step_summary(const ProviderSdkRequestEnvelopePlan &plan) {
    switch (next_action_for_request_envelope_plan(plan)) {
    case ProviderSdkHandoffReadinessNextActionKind::ReadyForHostExecutionPrototype:
        return "ready for future host execution prototype; no SDK payload, process, network, or "
               "SDK call was used";
    case ProviderSdkHandoffReadinessNextActionKind::FixEnvelopePolicy:
        return "fix envelope policy runtime reference and provider namespace before handoff";
    case ProviderSdkHandoffReadinessNextActionKind::WaitForEnvelopeCapability:
        return "wait for secret-free request envelope or host handoff descriptor capability";
    case ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider SDK handoff can proceed";
    }

    return "manual review required before provider SDK handoff can proceed";
}

void validate_failure_attribution(
    const std::optional<ProviderSdkEnvelopeFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_side_effects(bool materializes_sdk_request_payload,
                              bool starts_host_process,
                              bool opens_network_connection,
                              bool invokes_provider_sdk,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (materializes_sdk_request_payload) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize SDK request payload");
    }
    if (starts_host_process) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot start host process");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
}

} // namespace

ProviderSdkEnvelopePolicyValidationResult
validate_provider_sdk_envelope_policy(const ProviderSdkEnvelopePolicy &policy) {
    ProviderSdkEnvelopePolicyValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (policy.format_version != kProviderSdkEnvelopePolicyFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK envelope policy format_version must be '" +
                std::string(kProviderSdkEnvelopePolicyFormatVersion) + "'");
    }
    if (policy.sdk_envelope_policy_identity.empty() || policy.runtime_profile_ref.empty() ||
        policy.provider_namespace.empty() || policy.secret_free_request_schema_ref.empty() ||
        policy.host_handoff_policy_ref.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy identity fields "
                              "must not be empty");
    }
    if (!policy.credential_free) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy must be "
                              "credential_free");
    }
    if (policy.credential_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "credential_reference");
    }
    if (policy.secret_value.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "secret_value");
    }
    if (policy.provider_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "provider_endpoint_uri");
    }
    if (policy.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "object_path");
    }
    if (policy.database_table.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "database_table");
    }
    if (policy.sdk_request_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "sdk_request_payload");
    }
    if (policy.sdk_response_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "sdk_response_payload");
    }
    if (policy.host_command.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "host_command");
    }
    if (policy.network_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK envelope policy cannot contain "
                              "network_endpoint_uri");
    }

    return result;
}

ProviderSdkRequestEnvelopePlanValidationResult
validate_provider_sdk_request_envelope_plan(const ProviderSdkRequestEnvelopePlan &plan) {
    ProviderSdkRequestEnvelopePlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderSdkRequestEnvelopePlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK request envelope plan format_version must be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_runtime_preflight_plan_format_version !=
        kProviderRuntimePreflightPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK request envelope plan "
            "source_durable_store_import_provider_runtime_preflight_plan_format_version must be '" +
                std::string(kProviderRuntimePreflightPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_runtime_profile_format_version !=
        kProviderRuntimeProfileFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK request envelope plan "
            "source_durable_store_import_provider_runtime_profile_format_version must be '" +
                std::string(kProviderRuntimeProfileFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_runtime_preflight_identity.empty() ||
        plan.durable_store_import_provider_sdk_request_envelope_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK request envelope plan identity "
                              "fields must not be empty");
    }

    result.diagnostics.append(
        validate_provider_sdk_envelope_policy(plan.envelope_policy).diagnostics);
    validate_failure_attribution(plan.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK request envelope plan");
    validate_no_side_effects(plan.materializes_sdk_request_payload,
                             plan.starts_host_process,
                             plan.opens_network_connection,
                             plan.invokes_provider_sdk,
                             diagnostics,
                             "durable store import provider SDK request envelope plan");

    if (plan.envelope_status == ProviderSdkEnvelopeStatus::Ready) {
        if (plan.source_preflight_status != ProviderRuntimePreflightStatus::Ready ||
            !plan.source_sdk_invocation_envelope_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan ready "
                                  "status requires ready runtime preflight and SDK invocation "
                                  "envelope identity");
        }
        if (!plan.envelope_policy.supports_secret_free_request_envelope_planning ||
            !plan.envelope_policy.supports_host_handoff_descriptor_planning) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan ready "
                                  "status requires envelope planning capabilities");
        }
        if (plan.operation_kind !=
                ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope ||
            !plan.provider_sdk_request_envelope_identity.has_value() ||
            !plan.host_handoff_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan ready "
                                  "status requires request envelope and host handoff descriptor "
                                  "identities");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan ready "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind ==
            ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan blocked "
                                  "status cannot plan request envelope");
        }
        if (plan.provider_sdk_request_envelope_identity.has_value() ||
            plan.host_handoff_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan blocked "
                                  "status cannot contain request envelope or host handoff "
                                  "descriptor identities");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK request envelope plan blocked "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

ProviderSdkHandoffReadinessReviewValidationResult
validate_provider_sdk_handoff_readiness_review(const ProviderSdkHandoffReadinessReview &review) {
    ProviderSdkHandoffReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderSdkHandoffReadinessReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK handoff readiness review format_version must be '" +
                std::string(kProviderSdkHandoffReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK handoff readiness review "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_runtime_preflight_plan_format_version !=
        kProviderRuntimePreflightPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK handoff readiness review "
            "source_durable_store_import_provider_runtime_preflight_plan_format_version must be '" +
                std::string(kProviderRuntimePreflightPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_runtime_preflight_identity.empty() ||
        review.durable_store_import_provider_sdk_request_envelope_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK handoff readiness review "
                              "identity fields must not be empty");
    }
    if (review.sdk_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK handoff readiness review "
                              "summaries must not be empty");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK handoff readiness review");
    validate_no_side_effects(review.materializes_sdk_request_payload,
                             review.starts_host_process,
                             review.opens_network_connection,
                             review.invokes_provider_sdk,
                             diagnostics,
                             "durable store import provider SDK handoff readiness review");

    if (review.envelope_status == ProviderSdkEnvelopeStatus::Ready) {
        if (review.operation_kind !=
                ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope ||
            !review.provider_sdk_request_envelope_identity.has_value() ||
            !review.host_handoff_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK handoff readiness review "
                                  "ready status requires request envelope and host handoff "
                                  "descriptor identities");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK handoff readiness review "
                                  "ready status cannot contain failure_attribution");
        }
    } else {
        if (review.provider_sdk_request_envelope_identity.has_value() ||
            review.host_handoff_descriptor_identity.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK handoff readiness review "
                                  "blocked status cannot contain request envelope or host handoff "
                                  "descriptor identities");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider SDK handoff readiness review "
                                  "blocked status requires failure_attribution");
        }
    }

    return result;
}

ProviderSdkEnvelopePolicy
build_default_provider_sdk_envelope_policy(const ProviderRuntimePreflightPlan &plan) {
    return ProviderSdkEnvelopePolicy{
        .format_version = std::string(kProviderSdkEnvelopePolicyFormatVersion),
        .sdk_envelope_policy_identity =
            "provider-sdk-envelope-policy::" + plan.runtime_profile.runtime_profile_identity,
        .runtime_profile_ref = plan.runtime_profile.runtime_profile_identity,
        .provider_namespace = plan.runtime_profile.provider_namespace,
        .secret_free_request_schema_ref =
            "secret-free-sdk-request-schema::" + plan.runtime_profile.runtime_profile_identity,
        .host_handoff_policy_ref =
            "host-handoff-policy::" + plan.runtime_profile.runtime_profile_identity,
        .credential_free = true,
        .supports_secret_free_request_envelope_planning = true,
        .supports_host_handoff_descriptor_planning = true,
        .credential_reference = std::nullopt,
        .secret_value = std::nullopt,
        .provider_endpoint_uri = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_request_payload = std::nullopt,
        .sdk_response_payload = std::nullopt,
        .host_command = std::nullopt,
        .network_endpoint_uri = std::nullopt,
    };
}

ProviderSdkRequestEnvelopePlanResult
build_provider_sdk_request_envelope_plan(const ProviderRuntimePreflightPlan &plan) {
    auto policy = build_default_provider_sdk_envelope_policy(plan);
    return build_provider_sdk_request_envelope_plan(plan, policy);
}

ProviderSdkRequestEnvelopePlanResult
build_provider_sdk_request_envelope_plan(const ProviderRuntimePreflightPlan &plan,
                                         const ProviderSdkEnvelopePolicy &policy) {
    ProviderSdkRequestEnvelopePlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_runtime_preflight_plan(plan).diagnostics);
    result.diagnostics.append(validate_provider_sdk_envelope_policy(policy).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto policy_matches = policy_matches_runtime_preflight(plan, policy);
    const auto has_capabilities = policy.supports_secret_free_request_envelope_planning &&
                                  policy.supports_host_handoff_descriptor_planning;
    const auto can_plan = plan.preflight_status == ProviderRuntimePreflightStatus::Ready &&
                          plan.sdk_invocation_envelope_identity.has_value() && policy_matches &&
                          has_capabilities;

    ProviderSdkEnvelopeOperationKind operation_kind{
        ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady};
    std::optional<ProviderSdkEnvelopeFailureAttribution> failure = std::nullopt;
    if (can_plan) {
        operation_kind = ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope;
    } else if (plan.preflight_status != ProviderRuntimePreflightStatus::Ready ||
               !plan.sdk_invocation_envelope_identity.has_value()) {
        operation_kind = ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady;
        failure = runtime_preflight_not_ready_failure(plan);
    } else if (!policy_matches) {
        operation_kind = ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch;
        failure = ProviderSdkEnvelopeFailureAttribution{
            .kind = ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch,
            .message = "provider SDK envelope policy does not match runtime preflight",
            .missing_capability = std::nullopt,
        };
    } else {
        const auto missing_capability = first_missing_envelope_capability(policy);
        operation_kind = ProviderSdkEnvelopeOperationKind::NoopUnsupportedEnvelopeCapability;
        failure = ProviderSdkEnvelopeFailureAttribution{
            .kind = ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability,
            .message = missing_envelope_capability_message(missing_capability),
            .missing_capability = missing_capability,
        };
    }

    const auto status =
        can_plan ? ProviderSdkEnvelopeStatus::Ready : ProviderSdkEnvelopeStatus::Blocked;
    ProviderSdkRequestEnvelopePlan request_plan{
        .format_version = std::string(kProviderSdkRequestEnvelopePlanFormatVersion),
        .source_durable_store_import_provider_runtime_preflight_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_runtime_profile_format_version =
            plan.runtime_profile.format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_runtime_preflight_identity =
            plan.durable_store_import_provider_runtime_preflight_identity,
        .source_sdk_invocation_envelope_identity = plan.sdk_invocation_envelope_identity,
        .source_preflight_status = plan.preflight_status,
        .envelope_policy = policy,
        .durable_store_import_provider_sdk_request_envelope_identity =
            request_plan_identity(plan, status),
        .operation_kind = operation_kind,
        .envelope_status = status,
        .provider_sdk_request_envelope_identity =
            can_plan ? provider_sdk_request_envelope_identity(plan) : std::nullopt,
        .host_handoff_descriptor_identity =
            can_plan ? host_handoff_descriptor_identity(plan) : std::nullopt,
        .materializes_sdk_request_payload = false,
        .starts_host_process = false,
        .opens_network_connection = false,
        .invokes_provider_sdk = false,
        .failure_attribution = failure,
    };

    result.diagnostics.append(
        validate_provider_sdk_request_envelope_plan(request_plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(request_plan);
    return result;
}

ProviderSdkHandoffReadinessReviewResult
build_provider_sdk_handoff_readiness_review(const ProviderSdkRequestEnvelopePlan &plan) {
    ProviderSdkHandoffReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_sdk_request_envelope_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderSdkHandoffReadinessReview review{
        .format_version = std::string(kProviderSdkHandoffReadinessReviewFormatVersion),
        .source_durable_store_import_provider_sdk_request_envelope_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_runtime_preflight_plan_format_version =
            plan.source_durable_store_import_provider_runtime_preflight_plan_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_runtime_preflight_identity =
            plan.durable_store_import_provider_runtime_preflight_identity,
        .durable_store_import_provider_sdk_request_envelope_identity =
            plan.durable_store_import_provider_sdk_request_envelope_identity,
        .envelope_status = plan.envelope_status,
        .operation_kind = plan.operation_kind,
        .provider_sdk_request_envelope_identity = plan.provider_sdk_request_envelope_identity,
        .host_handoff_descriptor_identity = plan.host_handoff_descriptor_identity,
        .materializes_sdk_request_payload = plan.materializes_sdk_request_payload,
        .starts_host_process = plan.starts_host_process,
        .opens_network_connection = plan.opens_network_connection,
        .invokes_provider_sdk = plan.invokes_provider_sdk,
        .failure_attribution = plan.failure_attribution,
        .sdk_boundary_summary = boundary_summary(plan),
        .next_step_recommendation = next_step_summary(plan),
        .next_action = next_action_for_request_envelope_plan(plan),
    };

    result.diagnostics.append(validate_provider_sdk_handoff_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
