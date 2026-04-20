#include "ahfl/durable_store_import/receipt_persistence.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    if (identity.format_version != handoff::kFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_package_identity format_version must be '" +
            std::string(handoff::kFormatVersion) + "'");
    }

    if (identity.name.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "source_package_identity name must not be empty");
    }

    if (identity.version.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "source_package_identity version must not be empty");
    }
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "workflow_failure_summary message must not be empty");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "workflow_failure_summary node_name must not be empty");
    }
}

void validate_receipt_persistence_blocker(
    const DurableStoreImportDecisionReceiptPersistenceRequest &request,
    DiagnosticBag &diagnostics) {
    if (!request.receipt_persistence_blocker.has_value()) {
        return;
    }

    const auto &blocker = *request.receipt_persistence_blocker;
    if (blocker.message.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "receipt_persistence_blocker message must not be empty");
    }

    if (blocker.required_capability.has_value() &&
        request.next_required_adapter_capability != blocker.required_capability) {
        diagnostics.error("durable store import receipt persistence request "
                          "receipt_persistence_blocker required_capability must match "
                          "next_required_adapter_capability");
    }
}

[[nodiscard]] ReceiptPersistenceBlockerKind
to_receipt_persistence_blocker_kind(ReceiptBlockerKind kind) {
    switch (kind) {
    case ReceiptBlockerKind::SourceDecisionBlocked:
        return ReceiptPersistenceBlockerKind::SourceReceiptBlocked;
    case ReceiptBlockerKind::MissingRequiredAdapterCapability:
        return ReceiptPersistenceBlockerKind::MissingRequiredAdapterCapability;
    case ReceiptBlockerKind::WorkflowFailure:
        return ReceiptPersistenceBlockerKind::WorkflowFailure;
    case ReceiptBlockerKind::PartialWorkflowState:
        return ReceiptPersistenceBlockerKind::PartialWorkflowState;
    }

    return ReceiptPersistenceBlockerKind::SourceReceiptBlocked;
}

[[nodiscard]] std::string
receipt_persistence_outcome_slug(DurableStoreImportDecisionReceiptPersistenceRequestOutcome outcome) {
    switch (outcome) {
    case DurableStoreImportDecisionReceiptPersistenceRequestOutcome::PersistReadyReceipt:
        return "persistence-ready";
    case DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt:
        return "blocked";
    case DurableStoreImportDecisionReceiptPersistenceRequestOutcome::DeferPartialReceipt:
        return "deferred";
    case DurableStoreImportDecisionReceiptPersistenceRequestOutcome::RejectFailedReceipt:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string build_durable_store_import_receipt_persistence_request_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    DurableStoreImportDecisionReceiptPersistenceRequestOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import-receipt-persistence-request::" + identity_anchor + "::" +
           std::string(session_id) + "::" + receipt_persistence_outcome_slug(outcome);
}

} // namespace

DurableStoreImportDecisionReceiptPersistenceRequestValidationResult
validate_durable_store_import_decision_receipt_persistence_request(
    const DurableStoreImportDecisionReceiptPersistenceRequest &request) {
    DurableStoreImportDecisionReceiptPersistenceRequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (request.format_version != kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion) {
        diagnostics.error("durable store import receipt persistence request format_version must be '" +
                          std::string(kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_receipt_format_version !=
        kDurableStoreImportDecisionReceiptFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_durable_store_import_decision_receipt_format_version must be '" +
            std::string(kDurableStoreImportDecisionReceiptFormatVersion) + "'");
    }

    if (request.source_durable_store_import_decision_format_version !=
        kDurableStoreImportDecisionFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_durable_store_import_decision_format_version must be '" +
            std::string(kDurableStoreImportDecisionFormatVersion) + "'");
    }

    if (request.source_durable_store_import_request_format_version !=
        kDurableStoreImportRequestFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_durable_store_import_request_format_version must be '" +
            std::string(kDurableStoreImportRequestFormatVersion) + "'");
    }

    if (request.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (request.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (request.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (request.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (request.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (request.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (request.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (request.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (request.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        diagnostics.error(
            "durable store import receipt persistence request source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty()) {
        diagnostics.error("durable store import receipt persistence request workflow_canonical_name "
                          "must not be empty");
    }

    if (request.session_id.empty()) {
        diagnostics.error(
            "durable store import receipt persistence request session_id must not be empty");
    }

    if (request.run_id.has_value() && request.run_id->empty()) {
        diagnostics.error(
            "durable store import receipt persistence request run_id must not be empty when present");
    }

    if (request.input_fixture.empty()) {
        diagnostics.error(
            "durable store import receipt persistence request input_fixture must not be empty");
    }

    if (request.export_package_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request export_package_identity "
                          "must not be empty");
    }

    if (request.store_import_candidate_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "store_import_candidate_identity must not be empty");
    }

    if (request.durable_store_import_request_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "durable_store_import_request_identity must not be empty");
    }

    if (request.durable_store_import_decision_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "durable_store_import_decision_identity must not be empty");
    }

    if (request.durable_store_import_receipt_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "durable_store_import_receipt_identity must not be empty");
    }

    if (request.durable_store_import_receipt_persistence_request_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request "
                          "durable_store_import_receipt_persistence_request_identity must not be empty");
    }

    if (request.planned_durable_identity.empty()) {
        diagnostics.error("durable store import receipt persistence request planned_durable_identity "
                          "must not be empty");
    }

    if (request.source_package_identity.has_value()) {
        validate_package_identity(*request.source_package_identity, diagnostics);
    }

    if (request.workflow_failure_summary.has_value()) {
        validate_failure_summary(*request.workflow_failure_summary, diagnostics);
    }

    if (request.accepted_for_receipt_persistence &&
        request.receipt_persistence_blocker.has_value()) {
        diagnostics.error("durable store import receipt persistence request cannot contain "
                          "receipt_persistence_blocker when accepted_for_receipt_persistence is true");
    }

    if (request.accepted_for_receipt_persistence &&
        request.next_required_adapter_capability.has_value()) {
        diagnostics.error("durable store import receipt persistence request cannot contain "
                          "next_required_adapter_capability when "
                          "accepted_for_receipt_persistence is true");
    }

    if (!request.accepted_for_receipt_persistence &&
        !request.receipt_persistence_blocker.has_value()) {
        diagnostics.error("durable store import receipt persistence request must contain "
                          "receipt_persistence_blocker when accepted_for_receipt_persistence is false");
    }

    validate_receipt_persistence_blocker(request, diagnostics);

    if (request.accepted_for_receipt_persistence &&
        request.receipt_persistence_boundary_kind !=
            ReceiptPersistenceBoundaryKind::AdapterReceiptPersistenceConsumable) {
        diagnostics.error("durable store import receipt persistence request "
                          "accepted_for_receipt_persistence requires "
                          "AdapterReceiptPersistenceConsumable receipt_persistence_boundary_kind");
    }

    if (request.receipt_persistence_boundary_kind ==
            ReceiptPersistenceBoundaryKind::AdapterReceiptPersistenceConsumable &&
        !request.accepted_for_receipt_persistence) {
        diagnostics.error("durable store import receipt persistence request "
                          "adapter-receipt-persistence-consumable boundary requires "
                          "accepted_for_receipt_persistence");
    }

    if (request.accepted_for_receipt_persistence &&
        request.receipt_boundary_kind != ReceiptBoundaryKind::AdapterReceiptConsumable) {
        diagnostics.error("durable store import receipt persistence request accepted persistence "
                          "requires AdapterReceiptConsumable receipt_boundary_kind");
    }

    switch (request.receipt_persistence_request_status) {
    case DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist:
        if (request.receipt_persistence_request_outcome !=
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::PersistReadyReceipt) {
            diagnostics.error("durable store import receipt persistence request ReadyToPersist "
                              "status requires PersistReadyReceipt outcome");
        }
        if (!request.accepted_for_receipt_persistence) {
            diagnostics.error("durable store import receipt persistence request ReadyToPersist "
                              "status requires accepted_for_receipt_persistence");
        }
        if (request.receipt_status !=
            DurableStoreImportDecisionReceiptStatus::ReadyForArchive) {
            diagnostics.error("durable store import receipt persistence request ReadyToPersist "
                              "status requires ReadyForArchive receipt_status");
        }
        if (request.decision_status != DurableStoreImportDecisionStatus::Accepted) {
            diagnostics.error("durable store import receipt persistence request ReadyToPersist "
                              "status requires Accepted decision_status");
        }
        if (request.request_status != DurableStoreImportRequestStatus::ReadyForAdapter &&
            request.request_status !=
                DurableStoreImportRequestStatus::TerminalCompleted) {
            diagnostics.error("durable store import receipt persistence request ReadyToPersist "
                              "status requires ReadyForAdapter or TerminalCompleted request_status");
        }
        break;
    case DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked:
        if (request.receipt_persistence_request_outcome !=
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "requires BlockBlockedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != DurableStoreImportDecisionReceiptStatus::Blocked) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "requires Blocked receipt_status");
        }
        if (request.decision_status != DurableStoreImportDecisionStatus::Blocked) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "requires Blocked decision_status");
        }
        if (request.request_status != DurableStoreImportRequestStatus::Blocked) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "requires Blocked request_status");
        }
        if (!request.receipt_persistence_blocker.has_value()) {
            diagnostics.error("durable store import receipt persistence request Blocked status "
                              "requires receipt_persistence_blocker");
        }
        break;
    case DurableStoreImportDecisionReceiptPersistenceRequestStatus::Deferred:
        if (request.receipt_persistence_request_outcome !=
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::DeferPartialReceipt) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires DeferPartialReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != DurableStoreImportDecisionReceiptStatus::Deferred) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires Deferred receipt_status");
        }
        if (request.decision_status != DurableStoreImportDecisionStatus::Deferred) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires Deferred decision_status");
        }
        if (request.request_status != DurableStoreImportRequestStatus::TerminalPartial) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires TerminalPartial request_status");
        }
        if (!request.next_required_adapter_capability.has_value() ||
            *request.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires PreservePartialWorkflowState "
                              "next_required_adapter_capability");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind !=
                ReceiptPersistenceBlockerKind::PartialWorkflowState) {
            diagnostics.error("durable store import receipt persistence request Deferred status "
                              "requires PartialWorkflowState receipt_persistence_blocker");
        }
        break;
    case DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected:
        if (request.receipt_persistence_request_outcome !=
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::RejectFailedReceipt) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires RejectFailedReceipt outcome");
        }
        if (request.accepted_for_receipt_persistence) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "cannot be accepted_for_receipt_persistence");
        }
        if (request.receipt_status != DurableStoreImportDecisionReceiptStatus::Rejected) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires Rejected receipt_status");
        }
        if (request.decision_status != DurableStoreImportDecisionStatus::Rejected) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires Rejected decision_status");
        }
        if (request.request_status != DurableStoreImportRequestStatus::TerminalFailed) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires TerminalFailed request_status");
        }
        if (!request.workflow_failure_summary.has_value()) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires workflow_failure_summary");
        }
        if (!request.receipt_persistence_blocker.has_value() ||
            request.receipt_persistence_blocker->kind !=
                ReceiptPersistenceBlockerKind::WorkflowFailure) {
            diagnostics.error("durable store import receipt persistence request Rejected status "
                              "requires WorkflowFailure receipt_persistence_blocker");
        }
        break;
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        request.receipt_persistence_request_status !=
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist) {
        diagnostics.error("durable store import receipt persistence request completed "
                          "workflow_status requires ReadyToPersist "
                          "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        request.receipt_persistence_request_status !=
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected) {
        diagnostics.error("durable store import receipt persistence request failed "
                          "workflow_status requires Rejected "
                          "receipt_persistence_request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        request.receipt_persistence_request_status !=
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist &&
        request.receipt_persistence_request_status !=
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked &&
        request.receipt_persistence_request_status !=
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Deferred) {
        diagnostics.error("durable store import receipt persistence request partial "
                          "workflow_status must map to ReadyToPersist, Blocked, or Deferred "
                          "receipt_persistence_request_status");
    }

    return result;
}

DurableStoreImportDecisionReceiptPersistenceRequestResult
build_durable_store_import_decision_receipt_persistence_request(
    const DurableStoreImportDecisionReceipt &receipt) {
    DurableStoreImportDecisionReceiptPersistenceRequestResult result{
        .request = std::nullopt,
        .diagnostics = {},
    };

    const auto receipt_validation = validate_durable_store_import_decision_receipt(receipt);
    result.diagnostics.append(receipt_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    DurableStoreImportDecisionReceiptPersistenceRequest request{
        .format_version =
            std::string(kDurableStoreImportDecisionReceiptPersistenceRequestFormatVersion),
        .source_durable_store_import_decision_receipt_format_version = receipt.format_version,
        .source_durable_store_import_decision_format_version =
            receipt.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            receipt.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            receipt.source_store_import_descriptor_format_version,
        .source_execution_plan_format_version = receipt.source_execution_plan_format_version,
        .source_runtime_session_format_version =
            receipt.source_runtime_session_format_version,
        .source_execution_journal_format_version =
            receipt.source_execution_journal_format_version,
        .source_replay_view_format_version = receipt.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            receipt.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version =
            receipt.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            receipt.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version =
            receipt.source_export_manifest_format_version,
        .source_package_identity = receipt.source_package_identity,
        .workflow_canonical_name = receipt.workflow_canonical_name,
        .session_id = receipt.session_id,
        .run_id = receipt.run_id,
        .input_fixture = receipt.input_fixture,
        .workflow_status = receipt.workflow_status,
        .checkpoint_status = receipt.checkpoint_status,
        .persistence_status = receipt.persistence_status,
        .manifest_status = receipt.manifest_status,
        .descriptor_status = receipt.descriptor_status,
        .request_status = receipt.request_status,
        .decision_status = receipt.decision_status,
        .receipt_status = receipt.receipt_status,
        .workflow_failure_summary = receipt.workflow_failure_summary,
        .export_package_identity = receipt.export_package_identity,
        .store_import_candidate_identity = receipt.store_import_candidate_identity,
        .durable_store_import_request_identity =
            receipt.durable_store_import_request_identity,
        .durable_store_import_decision_identity =
            receipt.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity =
            receipt.durable_store_import_receipt_identity,
        .durable_store_import_receipt_persistence_request_identity = "",
        .planned_durable_identity = receipt.planned_durable_identity,
        .receipt_boundary_kind = receipt.receipt_boundary_kind,
        .receipt_persistence_boundary_kind = ReceiptPersistenceBoundaryKind::LocalContractOnly,
        .receipt_persistence_request_status =
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked,
        .receipt_persistence_request_outcome =
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt,
        .accepted_for_receipt_persistence = false,
        .next_required_adapter_capability = receipt.next_required_adapter_capability,
        .receipt_persistence_blocker = std::nullopt,
    };

    if (receipt.receipt_blocker.has_value()) {
        request.receipt_persistence_blocker = ReceiptPersistenceBlocker{
            .kind =
                to_receipt_persistence_blocker_kind(receipt.receipt_blocker->kind),
            .message = receipt.receipt_blocker->message,
            .required_capability = receipt.receipt_blocker->required_capability,
        };
    }

    switch (receipt.receipt_status) {
    case DurableStoreImportDecisionReceiptStatus::ReadyForArchive:
        request.receipt_persistence_request_status =
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist;
        request.receipt_persistence_request_outcome =
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::PersistReadyReceipt;
        request.receipt_persistence_boundary_kind =
            ReceiptPersistenceBoundaryKind::AdapterReceiptPersistenceConsumable;
        request.accepted_for_receipt_persistence = true;
        request.next_required_adapter_capability.reset();
        request.receipt_persistence_blocker.reset();
        break;
    case DurableStoreImportDecisionReceiptStatus::Blocked:
        request.receipt_persistence_request_status =
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked;
        request.receipt_persistence_request_outcome =
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::BlockBlockedReceipt;
        request.receipt_persistence_boundary_kind =
            ReceiptPersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case DurableStoreImportDecisionReceiptStatus::Deferred:
        request.receipt_persistence_request_status =
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Deferred;
        request.receipt_persistence_request_outcome =
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::DeferPartialReceipt;
        request.receipt_persistence_boundary_kind =
            ReceiptPersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    case DurableStoreImportDecisionReceiptStatus::Rejected:
        request.receipt_persistence_request_status =
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected;
        request.receipt_persistence_request_outcome =
            DurableStoreImportDecisionReceiptPersistenceRequestOutcome::RejectFailedReceipt;
        request.receipt_persistence_boundary_kind =
            ReceiptPersistenceBoundaryKind::LocalContractOnly;
        request.accepted_for_receipt_persistence = false;
        break;
    }

    request.durable_store_import_receipt_persistence_request_identity =
        build_durable_store_import_receipt_persistence_request_identity(
            request.source_package_identity,
            request.workflow_canonical_name,
            request.session_id,
            request.receipt_persistence_request_outcome);

    const auto validation =
        validate_durable_store_import_decision_receipt_persistence_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

} // namespace ahfl::durable_store_import
