#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_RECEIPT";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(
        identity, diagnostics, "durable store import decision receipt");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(
        summary, diagnostics, "durable store import decision receipt");
}

void validate_receipt_blocker(const Receipt &receipt, DiagnosticBag &diagnostics) {
    if (!receipt.receipt_blocker.has_value()) {
        return;
    }

    const auto &blocker = *receipt.receipt_blocker;
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt receipt_blocker message must not be empty");
    }

    if (blocker.required_capability.has_value() &&
        receipt.next_required_adapter_capability != blocker.required_capability) {
        emit_validation_error(diagnostics, "durable store import decision receipt receipt_blocker "
                          "required_capability must match "
                          "next_required_adapter_capability");
    }
}

[[nodiscard]] ReceiptBlockerKind to_receipt_blocker_kind(DecisionBlockerKind kind) {
    switch (kind) {
    case DecisionBlockerKind::SourceRequestBlocked:
        return ReceiptBlockerKind::SourceDecisionBlocked;
    case DecisionBlockerKind::MissingRequiredAdapterCapability:
        return ReceiptBlockerKind::MissingRequiredAdapterCapability;
    case DecisionBlockerKind::WorkflowFailure:
        return ReceiptBlockerKind::WorkflowFailure;
    case DecisionBlockerKind::PartialWorkflowState:
        return ReceiptBlockerKind::PartialWorkflowState;
    }

    return ReceiptBlockerKind::SourceDecisionBlocked;
}

[[nodiscard]] std::string receipt_outcome_slug(ReceiptOutcome outcome) {
    switch (outcome) {
    case ReceiptOutcome::ArchiveAcceptedDecision:
        return "archive-ready";
    case ReceiptOutcome::BlockBlockedDecision:
        return "blocked";
    case ReceiptOutcome::DeferPartialDecision:
        return "deferred";
    case ReceiptOutcome::RejectFailedDecision:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string build_receipt_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    ReceiptOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import-receipt::" + identity_anchor + "::" +
           std::string(session_id) + "::" + receipt_outcome_slug(outcome);
}

} // namespace

ReceiptValidationResult validate_receipt(const Receipt &receipt) {
    ReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kReceiptFormatVersion) {
        emit_validation_error(diagnostics, "durable store import decision receipt format_version must be '" +
                          std::string(kReceiptFormatVersion) + "'");
    }

    if (receipt.source_durable_store_import_decision_format_version != kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_durable_store_import_decision_format_version must be '" +
            std::string(kDecisionFormatVersion) + "'");
    }

    if (receipt.source_durable_store_import_request_format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_durable_store_import_request_format_version must be '" +
            std::string(kRequestFormatVersion) + "'");
    }

    if (receipt.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (receipt.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (receipt.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (receipt.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (receipt.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (receipt.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (receipt.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (receipt.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (receipt.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (receipt.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt workflow_canonical_name must not "
                          "be empty");
    }

    if (receipt.session_id.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt session_id must not be empty");
    }

    if (receipt.run_id.has_value() && receipt.run_id->empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt run_id must not be empty when present");
    }

    if (receipt.input_fixture.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision receipt input_fixture must not be empty");
    }

    if (receipt.export_package_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt export_package_identity must not "
                          "be empty");
    }

    if (receipt.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt "
                          "store_import_candidate_identity must not be empty");
    }

    if (receipt.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt "
                          "durable_store_import_request_identity must not be empty");
    }

    if (receipt.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt "
                          "durable_store_import_decision_identity must not be empty");
    }

    if (receipt.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt "
                          "durable_store_import_receipt_identity must not be empty");
    }

    if (receipt.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision receipt planned_durable_identity must "
                          "not be empty");
    }

    if (receipt.source_package_identity.has_value()) {
        validate_package_identity(*receipt.source_package_identity, diagnostics);
    }

    if (receipt.workflow_failure_summary.has_value()) {
        validate_failure_summary(*receipt.workflow_failure_summary, diagnostics);
    }

    if (receipt.accepted_for_receipt_archive && receipt.receipt_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import decision receipt cannot contain receipt_blocker "
                          "when accepted_for_receipt_archive is true");
    }

    if (receipt.accepted_for_receipt_archive &&
        receipt.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics, "durable store import decision receipt cannot contain "
                          "next_required_adapter_capability when "
                          "accepted_for_receipt_archive is true");
    }

    if (!receipt.accepted_for_receipt_archive && !receipt.receipt_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import decision receipt must contain receipt_blocker "
                          "when accepted_for_receipt_archive is false");
    }

    validate_receipt_blocker(receipt, diagnostics);

    if (receipt.accepted_for_receipt_archive &&
        receipt.receipt_boundary_kind != ReceiptBoundaryKind::AdapterReceiptConsumable) {
        emit_validation_error(diagnostics, "durable store import decision receipt accepted_for_receipt_archive "
                          "requires AdapterReceiptConsumable receipt_boundary_kind");
    }

    if (receipt.receipt_boundary_kind == ReceiptBoundaryKind::AdapterReceiptConsumable &&
        !receipt.accepted_for_receipt_archive) {
        emit_validation_error(diagnostics, "durable store import decision receipt adapter-receipt-consumable "
                          "boundary requires accepted_for_receipt_archive");
    }

    switch (receipt.receipt_status) {
    case ReceiptStatus::ReadyForArchive:
        if (receipt.receipt_outcome != ReceiptOutcome::ArchiveAcceptedDecision) {
            emit_validation_error(diagnostics, "durable store import decision receipt ReadyForArchive status "
                              "requires ArchiveAcceptedDecision outcome");
        }
        if (!receipt.accepted_for_receipt_archive) {
            emit_validation_error(diagnostics, "durable store import decision receipt ReadyForArchive status "
                              "requires accepted_for_receipt_archive");
        }
        if (receipt.decision_status != DecisionStatus::Accepted) {
            emit_validation_error(diagnostics, "durable store import decision receipt ReadyForArchive status "
                              "requires Accepted decision_status");
        }
        if (receipt.request_status != RequestStatus::ReadyForAdapter &&
            receipt.request_status != RequestStatus::TerminalCompleted) {
            emit_validation_error(diagnostics, "durable store import decision receipt ReadyForArchive status "
                              "requires ReadyForAdapter or TerminalCompleted request_status");
        }
        break;
    case ReceiptStatus::Blocked:
        if (receipt.receipt_outcome != ReceiptOutcome::BlockBlockedDecision) {
            emit_validation_error(diagnostics, "durable store import decision receipt Blocked status requires "
                              "BlockBlockedDecision outcome");
        }
        if (receipt.accepted_for_receipt_archive) {
            emit_validation_error(diagnostics, "durable store import decision receipt Blocked status cannot be "
                              "accepted_for_receipt_archive");
        }
        if (receipt.decision_status != DecisionStatus::Blocked) {
            emit_validation_error(diagnostics, "durable store import decision receipt Blocked status requires "
                              "Blocked decision_status");
        }
        if (receipt.request_status != RequestStatus::Blocked) {
            emit_validation_error(diagnostics, "durable store import decision receipt Blocked status requires "
                              "Blocked request_status");
        }
        if (!receipt.receipt_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import decision receipt Blocked status requires "
                              "receipt_blocker");
        }
        break;
    case ReceiptStatus::Deferred:
        if (receipt.receipt_outcome != ReceiptOutcome::DeferPartialDecision) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status requires "
                              "DeferPartialDecision outcome");
        }
        if (receipt.accepted_for_receipt_archive) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status cannot be "
                              "accepted_for_receipt_archive");
        }
        if (receipt.decision_status != DecisionStatus::Deferred) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status requires "
                              "Deferred decision_status");
        }
        if (receipt.request_status != RequestStatus::TerminalPartial) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status requires "
                              "TerminalPartial request_status");
        }
        if (!receipt.next_required_adapter_capability.has_value() ||
            *receipt.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status requires "
                              "PreservePartialWorkflowState next_required_adapter_capability");
        }
        if (!receipt.receipt_blocker.has_value() ||
            receipt.receipt_blocker->kind != ReceiptBlockerKind::PartialWorkflowState) {
            emit_validation_error(diagnostics, "durable store import decision receipt Deferred status requires "
                              "PartialWorkflowState receipt_blocker");
        }
        break;
    case ReceiptStatus::Rejected:
        if (receipt.receipt_outcome != ReceiptOutcome::RejectFailedDecision) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status requires "
                              "RejectFailedDecision outcome");
        }
        if (receipt.accepted_for_receipt_archive) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status cannot be "
                              "accepted_for_receipt_archive");
        }
        if (receipt.decision_status != DecisionStatus::Rejected) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status requires "
                              "Rejected decision_status");
        }
        if (receipt.request_status != RequestStatus::TerminalFailed) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status requires "
                              "TerminalFailed request_status");
        }
        if (!receipt.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status requires "
                              "workflow_failure_summary");
        }
        if (!receipt.receipt_blocker.has_value() ||
            receipt.receipt_blocker->kind != ReceiptBlockerKind::WorkflowFailure) {
            emit_validation_error(diagnostics, "durable store import decision receipt Rejected status requires "
                              "WorkflowFailure receipt_blocker");
        }
        break;
    }

    if (receipt.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        receipt.receipt_status != ReceiptStatus::ReadyForArchive) {
        emit_validation_error(diagnostics, "durable store import decision receipt completed workflow_status "
                          "requires ReadyForArchive receipt_status");
    }

    if (receipt.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        receipt.receipt_status != ReceiptStatus::Rejected) {
        emit_validation_error(diagnostics, "durable store import decision receipt failed workflow_status requires "
                          "Rejected receipt_status");
    }

    if (receipt.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        receipt.receipt_status != ReceiptStatus::ReadyForArchive &&
        receipt.receipt_status != ReceiptStatus::Blocked &&
        receipt.receipt_status != ReceiptStatus::Deferred) {
        emit_validation_error(diagnostics, "durable store import decision receipt partial workflow_status must map "
                          "to ReadyForArchive, Blocked, or Deferred receipt_status");
    }

    return result;
}

ReceiptResult build_receipt(const Decision &decision) {
    ReceiptResult result{
        .receipt = std::nullopt,
        .diagnostics = {},
    };

    const auto decision_validation = validate_decision(decision);
    result.diagnostics.append(decision_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    Receipt receipt{
        .format_version = std::string(kReceiptFormatVersion),
        .source_durable_store_import_decision_format_version = decision.format_version,
        .source_durable_store_import_request_format_version =
            decision.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            decision.source_store_import_descriptor_format_version,
        .source_execution_plan_format_version = decision.source_execution_plan_format_version,
        .source_runtime_session_format_version = decision.source_runtime_session_format_version,
        .source_execution_journal_format_version = decision.source_execution_journal_format_version,
        .source_replay_view_format_version = decision.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            decision.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version =
            decision.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            decision.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version =
            decision.source_export_manifest_format_version,
        .source_package_identity = decision.source_package_identity,
        .workflow_canonical_name = decision.workflow_canonical_name,
        .session_id = decision.session_id,
        .run_id = decision.run_id,
        .input_fixture = decision.input_fixture,
        .workflow_status = decision.workflow_status,
        .checkpoint_status = decision.checkpoint_status,
        .persistence_status = decision.persistence_status,
        .manifest_status = decision.manifest_status,
        .descriptor_status = decision.descriptor_status,
        .request_status = decision.request_status,
        .decision_status = decision.decision_status,
        .workflow_failure_summary = decision.workflow_failure_summary,
        .export_package_identity = decision.export_package_identity,
        .store_import_candidate_identity = decision.store_import_candidate_identity,
        .durable_store_import_request_identity = decision.durable_store_import_request_identity,
        .durable_store_import_decision_identity = decision.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity = "",
        .planned_durable_identity = decision.planned_durable_identity,
        .decision_boundary_kind = decision.decision_boundary_kind,
        .receipt_boundary_kind = ReceiptBoundaryKind::LocalContractOnly,
        .receipt_status = ReceiptStatus::Blocked,
        .receipt_outcome = ReceiptOutcome::BlockBlockedDecision,
        .accepted_for_receipt_archive = false,
        .next_required_adapter_capability = decision.next_required_adapter_capability,
        .receipt_blocker = std::nullopt,
    };

    if (decision.decision_blocker.has_value()) {
        receipt.receipt_blocker = ReceiptBlocker{
            .kind = to_receipt_blocker_kind(decision.decision_blocker->kind),
            .message = decision.decision_blocker->message,
            .required_capability = decision.decision_blocker->required_capability,
        };
    }

    switch (decision.decision_status) {
    case DecisionStatus::Accepted:
        receipt.receipt_status = ReceiptStatus::ReadyForArchive;
        receipt.receipt_outcome = ReceiptOutcome::ArchiveAcceptedDecision;
        receipt.receipt_boundary_kind = ReceiptBoundaryKind::AdapterReceiptConsumable;
        receipt.accepted_for_receipt_archive = true;
        receipt.next_required_adapter_capability.reset();
        receipt.receipt_blocker.reset();
        break;
    case DecisionStatus::Blocked:
        receipt.receipt_status = ReceiptStatus::Blocked;
        receipt.receipt_outcome = ReceiptOutcome::BlockBlockedDecision;
        receipt.receipt_boundary_kind = ReceiptBoundaryKind::LocalContractOnly;
        receipt.accepted_for_receipt_archive = false;
        break;
    case DecisionStatus::Deferred:
        receipt.receipt_status = ReceiptStatus::Deferred;
        receipt.receipt_outcome = ReceiptOutcome::DeferPartialDecision;
        receipt.receipt_boundary_kind = ReceiptBoundaryKind::LocalContractOnly;
        receipt.accepted_for_receipt_archive = false;
        break;
    case DecisionStatus::Rejected:
        receipt.receipt_status = ReceiptStatus::Rejected;
        receipt.receipt_outcome = ReceiptOutcome::RejectFailedDecision;
        receipt.receipt_boundary_kind = ReceiptBoundaryKind::LocalContractOnly;
        receipt.accepted_for_receipt_archive = false;
        break;
    }

    receipt.durable_store_import_receipt_identity = build_receipt_identity(
        receipt.source_package_identity,
        receipt.workflow_canonical_name,
        receipt.session_id,
        receipt.receipt_outcome);

    const auto validation = validate_receipt(receipt);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

} // namespace ahfl::durable_store_import
