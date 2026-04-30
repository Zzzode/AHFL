#include "ahfl/backends/durable_store_import_receipt_persistence_response_review.hpp"

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

[[nodiscard]] std::string
response_boundary_kind_name(durable_store_import::PersistenceResponseBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
        return "adapter_response_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::PersistenceResponseReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::PersistenceResponseReviewNextActionKind::AcknowledgeResponse:
        return "acknowledge_response";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::PersistenceResponseReviewNextActionKind::WaitforCapability:
        return "wait_for_capability";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ReviewFailure:
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
blocker_kind_name(durable_store_import::PersistenceResponseBlockerKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked:
        return "source_persistence_request_blocked";
    case durable_store_import::PersistenceResponseBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::PersistenceResponseBlockerKind::PartialPersistenceState:
        return "partial_persistence_state";
    case durable_store_import::PersistenceResponseBlockerKind::PersistenceWorkflowFailure:
        return "persistence_workflow_failure";
    }

    return "invalid";
}

void print_response_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::PersistenceResponseBlocker> &blocker) {
    line(out, indent_level, "response_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_persistence_response_review(
    const durable_store_import::PersistenceResponseReviewSummary &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_response_format " +
             review
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(
        out,
        0,
        "source_persistence_request_format " +
            review.source_durable_store_import_decision_receipt_persistence_request_format_version);
    line(out,
         0,
         "source_receipt_format " +
             review.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + review.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + review.source_durable_store_import_request_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_decision_identity " + review.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + review.durable_store_import_receipt_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_request_identity " +
             review.durable_store_import_receipt_persistence_request_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             review.durable_store_import_receipt_persistence_response_identity);
    line(out, 0, "response_status " + response_status_name(review.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(review.response_outcome));
    line(out,
         0,
         "response_boundary_kind " + response_boundary_kind_name(review.response_boundary_kind));
    line(out,
         0,
         std::string("acknowledged_for_response ") +
             (review.acknowledged_for_response ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out,
         0,
         "next_required_adapter_capability " +
             (review.next_required_adapter_capability.has_value()
                  ? capability_name(*review.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_response_contract " + review.adapter_response_contract_summary);
    line(out, 0, "response_preview " + review.response_preview.response_identity);
    line(out, 0, "next_step " + review.next_step_recommendation);

    print_response_blocker(out, 0, review.response_blocker);
}

} // namespace ahfl
