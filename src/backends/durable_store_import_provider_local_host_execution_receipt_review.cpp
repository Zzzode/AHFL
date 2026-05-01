#include "ahfl/backends/durable_store_import_provider_local_host_execution_receipt_review.hpp"

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
execution_status_name(durable_store_import::ProviderLocalHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady:
        return "simulated_ready";
    case durable_store_import::ProviderLocalHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionOperationKind::
        SimulateProviderLocalHostExecutionReceipt:
        return "simulate_provider_local_host_execution_receipt";
    case durable_store_import::ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady:
        return "noop_host_execution_not_ready";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(
    durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ReadyForProviderSdkAdapterPrototype:
        return "ready_for_provider_sdk_adapter_prototype";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        WaitForHostExecutionPlan:
        return "wait_for_host_execution_plan";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady:
        return "host_execution_not_ready";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderLocalHostExecutionFailureAttribution>
        &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_review(
    const durable_store_import::ProviderLocalHostExecutionReceiptReview &review,
    std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_local_host_execution_receipt_format " +
             review
                 .source_durable_store_import_provider_local_host_execution_receipt_format_version);
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
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
    line(out,
         0,
         "durable_store_import_provider_local_host_execution_receipt_identity " +
             review.durable_store_import_provider_local_host_execution_receipt_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_local_host_execution_receipt_identity " +
             (review.provider_local_host_execution_receipt_identity.has_value()
                  ? *review.provider_local_host_execution_receipt_identity
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
    line(out, 0, "local_host_execution_boundary " + review.local_host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl
