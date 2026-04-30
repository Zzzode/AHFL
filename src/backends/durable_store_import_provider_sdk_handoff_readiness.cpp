#include "ahfl/backends/durable_store_import_provider_sdk_handoff_readiness.hpp"

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
envelope_status_name(durable_store_import::ProviderSdkEnvelopeStatus status) {
    switch (status) {
    case durable_store_import::ProviderSdkEnvelopeStatus::Ready:
        return "ready";
    case durable_store_import::ProviderSdkEnvelopeStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
        return "plan_provider_sdk_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
        return "noop_runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
        return "noop_envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopUnsupportedEnvelopeCapability:
        return "noop_unsupported_envelope_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderSdkHandoffReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::
        ReadyForHostExecutionPrototype:
        return "ready_for_host_execution_prototype";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::FixEnvelopePolicy:
        return "fix_envelope_policy";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::WaitForEnvelopeCapability:
        return "wait_for_envelope_capability";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope:
        return "plan_secret_free_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor:
        return "plan_host_handoff_descriptor";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady:
        return "runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch:
        return "envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability:
        return "unsupported_envelope_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderSdkEnvelopeFailureAttribution> &failure) {
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

void print_durable_store_import_provider_sdk_handoff_readiness(
    const durable_store_import::ProviderSdkHandoffReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out,
         0,
         "source_provider_runtime_preflight_plan_format " +
             review.source_durable_store_import_provider_runtime_preflight_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_runtime_preflight_identity " +
             review.durable_store_import_provider_runtime_preflight_identity);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out, 0, "envelope_status " + envelope_status_name(review.envelope_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_sdk_request_envelope_identity " +
             (review.provider_sdk_request_envelope_identity.has_value()
                  ? *review.provider_sdk_request_envelope_identity
                  : std::string("none")));
    line(out,
         0,
         "host_handoff_descriptor_identity " + (review.host_handoff_descriptor_identity.has_value()
                                                    ? *review.host_handoff_descriptor_identity
                                                    : std::string("none")));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "sdk_boundary " + review.sdk_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl
