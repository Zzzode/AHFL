#include "ahfl/backends/durable_store_import_provider_host_execution_readiness.hpp"

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
execution_status_name(durable_store_import::ProviderHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderHostExecutionStatus::Ready:
        return "ready";
    case durable_store_import::ProviderHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
        return "plan_provider_host_execution";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
        return "noop_sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
        return "noop_host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopUnsupportedHostCapability:
        return "noop_unsupported_host_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderHostExecutionReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::
        ReadyForLocalHostExecutionHarness:
        return "ready_for_local_host_execution_harness";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::FixHostPolicy:
        return "fix_host_policy";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::WaitForHostCapability:
        return "wait_for_host_capability";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanLocalHostExecutionDescriptor:
        return "plan_local_host_execution_descriptor";
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanDryRunHostReceiptPlaceholder:
        return "plan_dry_run_host_receipt_placeholder";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady:
        return "sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch:
        return "host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability:
        return "unsupported_host_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderHostExecutionFailureAttribution> &failure) {
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

void print_durable_store_import_provider_host_execution_readiness(
    const durable_store_import::ProviderHostExecutionReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out,
         0,
         "durable_store_import_provider_host_execution_identity " +
             review.durable_store_import_provider_host_execution_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_host_execution_descriptor_identity " +
             (review.provider_host_execution_descriptor_identity.has_value()
                  ? *review.provider_host_execution_descriptor_identity
                  : std::string("none")));
    line(out,
         0,
         "provider_host_receipt_placeholder_identity " +
             (review.provider_host_receipt_placeholder_identity.has_value()
                  ? *review.provider_host_receipt_placeholder_identity
                  : std::string("none")));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "host_execution_boundary " + review.host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl
