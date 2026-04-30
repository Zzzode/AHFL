#include "ahfl/backends/durable_store_import_recovery_preview.hpp"

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
response_status_name(durable_store_import::PersistenceResponseStatus status) {
    switch (status) {
    case durable_store_import::PersistenceResponseStatus::Accepted:
        return "accepted";
    case durable_store_import::PersistenceResponseStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceResponseStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceResponseStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_outcome_name(durable_store_import::PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accept_persistence_request";
    case durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest:
        return "block_blocked_request";
    case durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest:
        return "defer_deferred_request";
    case durable_store_import::PersistenceResponseOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string mutation_status_name(durable_store_import::StoreMutationStatus status) {
    switch (status) {
    case durable_store_import::StoreMutationStatus::Persisted:
        return "persisted";
    case durable_store_import::StoreMutationStatus::NotMutated:
        return "not_mutated";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::RecoveryPreviewNextActionKind action) {
    switch (action) {
    case durable_store_import::RecoveryPreviewNextActionKind::NoRecoveryRequired:
        return "no_recovery_required";
    case durable_store_import::RecoveryPreviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::RecoveryPreviewNextActionKind::WaitForCapability:
        return "wait_for_capability";
    case durable_store_import::RecoveryPreviewNextActionKind::ReviewFailure:
        return "review_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::AdapterExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked:
        return "source_response_blocked";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred:
        return "source_response_deferred";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected:
        return "source_response_rejected";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::AdapterExecutionFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (failure->required_capability.has_value()
                                       ? capability_name(*failure->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_recovery_preview(
    const durable_store_import::RecoveryCommandPreview &preview, std::ostream &out) {
    out << preview.format_version << '\n';
    line(out,
         0,
         "source_adapter_execution_format " +
             preview.source_durable_store_import_adapter_execution_format_version);
    line(out,
         0,
         "source_response_format " +
             preview
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(out, 0, "workflow " + preview.workflow_canonical_name);
    line(out, 0, "session " + preview.session_id);
    line(out, 0, "run_id " + (preview.run_id.has_value() ? *preview.run_id : "none"));
    line(out, 0, "input_fixture " + preview.input_fixture);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             preview.durable_store_import_receipt_persistence_response_identity);
    line(out,
         0,
         "durable_store_import_adapter_execution_identity " +
             preview.durable_store_import_adapter_execution_identity);
    line(out, 0, "response_status " + response_status_name(preview.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(preview.response_outcome));
    line(out, 0, "mutation_status " + mutation_status_name(preview.mutation_status));
    line(out,
         0,
         "persistence_id " +
             (preview.persistence_id.has_value() ? *preview.persistence_id : std::string("none")));
    line(out, 0, "next_action " + next_action_name(preview.next_action));
    line(out, 0, "recovery_boundary " + preview.recovery_boundary_summary);
    line(out, 0, "next_step " + preview.next_step_recommendation);
    line(out, 0, "execution_summary {");
    line(out,
         1,
         "adapter_execution_identity " + preview.execution_summary.adapter_execution_identity);
    line(out,
         1,
         "mutation_status " + mutation_status_name(preview.execution_summary.mutation_status));
    line(out,
         1,
         "persistence_id " + (preview.execution_summary.persistence_id.has_value()
                                  ? *preview.execution_summary.persistence_id
                                  : std::string("none")));
    line(out, 0, "}");
    print_failure_attribution(out, 0, preview.failure_attribution);
}

} // namespace ahfl
