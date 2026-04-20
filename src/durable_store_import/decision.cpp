#include "ahfl/durable_store_import/decision.hpp"

#include <string>

namespace ahfl::durable_store_import {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    if (identity.format_version != handoff::kFormatVersion) {
        diagnostics.error(
            "durable store import decision source_package_identity format_version must be '" +
            std::string(handoff::kFormatVersion) + "'");
    }

    if (identity.name.empty()) {
        diagnostics.error(
            "durable store import decision source_package_identity name must not be empty");
    }

    if (identity.version.empty()) {
        diagnostics.error(
            "durable store import decision source_package_identity version must not be empty");
    }
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error(
            "durable store import decision workflow_failure_summary message must not be empty");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error(
            "durable store import decision workflow_failure_summary node_name must not be empty");
    }
}

[[nodiscard]] std::string decision_outcome_slug(DurableStoreImportDecisionOutcome outcome) {
    switch (outcome) {
    case DurableStoreImportDecisionOutcome::AcceptRequest:
        return "accepted";
    case DurableStoreImportDecisionOutcome::BlockRequest:
        return "blocked";
    case DurableStoreImportDecisionOutcome::DeferPartialRequest:
        return "deferred";
    case DurableStoreImportDecisionOutcome::RejectFailedRequest:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string build_durable_store_import_decision_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    DurableStoreImportDecisionOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import-decision::" + identity_anchor + "::" +
           std::string(session_id) + "::" + decision_outcome_slug(outcome);
}

[[nodiscard]] AdapterCapabilityKind
to_adapter_capability_kind(RequestedArtifactKind kind) {
    switch (kind) {
    case RequestedArtifactKind::StoreImportDescriptor:
        return AdapterCapabilityKind::ConsumeStoreImportDescriptor;
    case RequestedArtifactKind::ExportManifest:
        return AdapterCapabilityKind::ConsumeExportManifest;
    case RequestedArtifactKind::PersistenceDescriptor:
        return AdapterCapabilityKind::ConsumePersistenceDescriptor;
    case RequestedArtifactKind::PersistenceReview:
        return AdapterCapabilityKind::ConsumeHumanReviewContext;
    case RequestedArtifactKind::CheckpointRecord:
        return AdapterCapabilityKind::ConsumeCheckpointRecord;
    }

    return AdapterCapabilityKind::ConsumeStoreImportDescriptor;
}

} // namespace

DurableStoreImportDecisionValidationResult
validate_durable_store_import_decision(const DurableStoreImportDecision &decision) {
    DurableStoreImportDecisionValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (decision.format_version != kDurableStoreImportDecisionFormatVersion) {
        diagnostics.error("durable store import decision format_version must be '" +
                          std::string(kDurableStoreImportDecisionFormatVersion) + "'");
    }

    if (decision.source_durable_store_import_request_format_version !=
        kDurableStoreImportRequestFormatVersion) {
        diagnostics.error(
            "durable store import decision source_durable_store_import_request_format_version must be '" +
            std::string(kDurableStoreImportRequestFormatVersion) + "'");
    }

    if (decision.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        diagnostics.error(
            "durable store import decision source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (decision.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error(
            "durable store import decision source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (decision.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "durable store import decision source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (decision.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "durable store import decision source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (decision.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error(
            "durable store import decision source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (decision.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "durable store import decision source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (decision.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "durable store import decision source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (decision.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        diagnostics.error(
            "durable store import decision source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (decision.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        diagnostics.error(
            "durable store import decision source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (decision.workflow_canonical_name.empty()) {
        diagnostics.error(
            "durable store import decision workflow_canonical_name must not be empty");
    }

    if (decision.session_id.empty()) {
        diagnostics.error("durable store import decision session_id must not be empty");
    }

    if (decision.run_id.has_value() && decision.run_id->empty()) {
        diagnostics.error("durable store import decision run_id must not be empty when present");
    }

    if (decision.input_fixture.empty()) {
        diagnostics.error("durable store import decision input_fixture must not be empty");
    }

    if (decision.export_package_identity.empty()) {
        diagnostics.error(
            "durable store import decision export_package_identity must not be empty");
    }

    if (decision.store_import_candidate_identity.empty()) {
        diagnostics.error(
            "durable store import decision store_import_candidate_identity must not be empty");
    }

    if (decision.durable_store_import_request_identity.empty()) {
        diagnostics.error(
            "durable store import decision durable_store_import_request_identity must not be empty");
    }

    if (decision.durable_store_import_decision_identity.empty()) {
        diagnostics.error(
            "durable store import decision durable_store_import_decision_identity must not be empty");
    }

    if (decision.planned_durable_identity.empty()) {
        diagnostics.error(
            "durable store import decision planned_durable_identity must not be empty");
    }

    if (decision.source_package_identity.has_value()) {
        validate_package_identity(*decision.source_package_identity, diagnostics);
    }

    if (decision.workflow_failure_summary.has_value()) {
        validate_failure_summary(*decision.workflow_failure_summary, diagnostics);
    }

    if (decision.accepted_for_future_execution && decision.decision_blocker.has_value()) {
        diagnostics.error("durable store import decision cannot contain decision_blocker when "
                          "accepted_for_future_execution is true");
    }

    if (decision.accepted_for_future_execution &&
        decision.next_required_adapter_capability.has_value()) {
        diagnostics.error("durable store import decision cannot contain "
                          "next_required_adapter_capability when accepted_for_future_execution "
                          "is true");
    }

    if (!decision.accepted_for_future_execution && !decision.decision_blocker.has_value()) {
        diagnostics.error("durable store import decision must contain decision_blocker when "
                          "accepted_for_future_execution is false");
    }

    if (decision.decision_blocker.has_value()) {
        if (decision.decision_blocker->message.empty()) {
            diagnostics.error(
                "durable store import decision decision_blocker message must not be empty");
        }

        if (decision.decision_blocker->required_capability.has_value() &&
            decision.next_required_adapter_capability !=
                decision.decision_blocker->required_capability) {
            diagnostics.error("durable store import decision decision_blocker "
                              "required_capability must match "
                              "next_required_adapter_capability");
        }
    }

    if (decision.decision_boundary_kind == DecisionBoundaryKind::AdapterDecisionConsumable &&
        !decision.accepted_for_future_execution) {
        diagnostics.error("durable store import decision adapter-decision-consumable boundary "
                          "requires accepted_for_future_execution");
    }

    switch (decision.decision_status) {
    case DurableStoreImportDecisionStatus::Accepted:
        if (decision.decision_outcome != DurableStoreImportDecisionOutcome::AcceptRequest) {
            diagnostics.error(
                "durable store import decision Accepted status requires AcceptRequest outcome");
        }
        if (!decision.accepted_for_future_execution) {
            diagnostics.error("durable store import decision Accepted status requires "
                              "accepted_for_future_execution");
        }
        if (decision.request_status != DurableStoreImportRequestStatus::ReadyForAdapter &&
            decision.request_status !=
                DurableStoreImportRequestStatus::TerminalCompleted) {
            diagnostics.error("durable store import decision Accepted status requires "
                              "ReadyForAdapter or TerminalCompleted request_status");
        }
        if (decision.request_boundary_kind !=
            RequestBoundaryKind::AdapterContractConsumable) {
            diagnostics.error("durable store import decision Accepted status requires "
                              "AdapterContractConsumable request_boundary_kind");
        }
        break;
    case DurableStoreImportDecisionStatus::Blocked:
        if (decision.decision_outcome != DurableStoreImportDecisionOutcome::BlockRequest) {
            diagnostics.error(
                "durable store import decision Blocked status requires BlockRequest outcome");
        }
        if (decision.accepted_for_future_execution) {
            diagnostics.error("durable store import decision Blocked status cannot be "
                              "accepted_for_future_execution");
        }
        if (decision.request_status != DurableStoreImportRequestStatus::Blocked) {
            diagnostics.error(
                "durable store import decision Blocked status requires Blocked request_status");
        }
        if (!decision.decision_blocker.has_value()) {
            diagnostics.error(
                "durable store import decision Blocked status requires decision_blocker");
        }
        break;
    case DurableStoreImportDecisionStatus::Deferred:
        if (decision.decision_outcome !=
            DurableStoreImportDecisionOutcome::DeferPartialRequest) {
            diagnostics.error("durable store import decision Deferred status requires "
                              "DeferPartialRequest outcome");
        }
        if (decision.accepted_for_future_execution) {
            diagnostics.error("durable store import decision Deferred status cannot be "
                              "accepted_for_future_execution");
        }
        if (decision.request_status != DurableStoreImportRequestStatus::TerminalPartial) {
            diagnostics.error("durable store import decision Deferred status requires "
                              "TerminalPartial request_status");
        }
        if (!decision.next_required_adapter_capability.has_value() ||
            *decision.next_required_adapter_capability !=
                AdapterCapabilityKind::PreservePartialWorkflowState) {
            diagnostics.error("durable store import decision Deferred status requires "
                              "PreservePartialWorkflowState next_required_adapter_capability");
        }
        if (!decision.decision_blocker.has_value() ||
            decision.decision_blocker->kind != DecisionBlockerKind::PartialWorkflowState) {
            diagnostics.error("durable store import decision Deferred status requires "
                              "PartialWorkflowState decision_blocker");
        }
        break;
    case DurableStoreImportDecisionStatus::Rejected:
        if (decision.decision_outcome !=
            DurableStoreImportDecisionOutcome::RejectFailedRequest) {
            diagnostics.error("durable store import decision Rejected status requires "
                              "RejectFailedRequest outcome");
        }
        if (decision.accepted_for_future_execution) {
            diagnostics.error("durable store import decision Rejected status cannot be "
                              "accepted_for_future_execution");
        }
        if (decision.request_status != DurableStoreImportRequestStatus::TerminalFailed) {
            diagnostics.error("durable store import decision Rejected status requires "
                              "TerminalFailed request_status");
        }
        if (!decision.workflow_failure_summary.has_value()) {
            diagnostics.error("durable store import decision Rejected status requires "
                              "workflow_failure_summary");
        }
        if (!decision.decision_blocker.has_value() ||
            decision.decision_blocker->kind != DecisionBlockerKind::WorkflowFailure) {
            diagnostics.error("durable store import decision Rejected status requires "
                              "WorkflowFailure decision_blocker");
        }
        break;
    }

    if (decision.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        decision.decision_status != DurableStoreImportDecisionStatus::Accepted) {
        diagnostics.error("durable store import decision completed workflow_status requires "
                          "Accepted decision_status");
    }

    if (decision.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        decision.decision_status != DurableStoreImportDecisionStatus::Rejected) {
        diagnostics.error("durable store import decision failed workflow_status requires "
                          "Rejected decision_status");
    }

    if (decision.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        decision.decision_status != DurableStoreImportDecisionStatus::Accepted &&
        decision.decision_status != DurableStoreImportDecisionStatus::Blocked &&
        decision.decision_status != DurableStoreImportDecisionStatus::Deferred) {
        diagnostics.error("durable store import decision partial workflow_status must map to "
                          "Accepted, Blocked, or Deferred decision_status");
    }

    return result;
}

DurableStoreImportDecisionResult
build_durable_store_import_decision(const DurableStoreImportRequest &request) {
    DurableStoreImportDecisionResult result{
        .decision = std::nullopt,
        .diagnostics = {},
    };

    const auto request_validation = validate_durable_store_import_request(request);
    result.diagnostics.append(request_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    DurableStoreImportDecision decision{
        .format_version = std::string(kDurableStoreImportDecisionFormatVersion),
        .source_durable_store_import_request_format_version = request.format_version,
        .source_store_import_descriptor_format_version =
            request.source_store_import_descriptor_format_version,
        .source_execution_plan_format_version = request.source_execution_plan_format_version,
        .source_runtime_session_format_version = request.source_runtime_session_format_version,
        .source_execution_journal_format_version = request.source_execution_journal_format_version,
        .source_replay_view_format_version = request.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            request.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version =
            request.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            request.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version = request.source_export_manifest_format_version,
        .source_package_identity = request.source_package_identity,
        .workflow_canonical_name = request.workflow_canonical_name,
        .session_id = request.session_id,
        .run_id = request.run_id,
        .input_fixture = request.input_fixture,
        .workflow_status = request.workflow_status,
        .checkpoint_status = request.checkpoint_status,
        .persistence_status = request.persistence_status,
        .manifest_status = request.manifest_status,
        .descriptor_status = request.descriptor_status,
        .request_status = request.request_status,
        .workflow_failure_summary = request.workflow_failure_summary,
        .export_package_identity = request.export_package_identity,
        .store_import_candidate_identity = request.store_import_candidate_identity,
        .durable_store_import_request_identity =
            request.durable_store_import_request_identity,
        .durable_store_import_decision_identity = "",
        .planned_durable_identity = request.planned_durable_identity,
        .request_boundary_kind = request.request_boundary_kind,
        .decision_boundary_kind = DecisionBoundaryKind::LocalContractOnly,
        .decision_status = DurableStoreImportDecisionStatus::Blocked,
        .decision_outcome = DurableStoreImportDecisionOutcome::BlockRequest,
        .accepted_for_future_execution = false,
        .next_required_adapter_capability = std::nullopt,
        .decision_blocker = std::nullopt,
    };

    switch (request.request_status) {
    case DurableStoreImportRequestStatus::ReadyForAdapter:
    case DurableStoreImportRequestStatus::TerminalCompleted:
        decision.decision_status = DurableStoreImportDecisionStatus::Accepted;
        decision.decision_outcome = DurableStoreImportDecisionOutcome::AcceptRequest;
        decision.accepted_for_future_execution = true;
        decision.decision_boundary_kind = DecisionBoundaryKind::AdapterDecisionConsumable;
        break;
    case DurableStoreImportRequestStatus::Blocked:
        decision.decision_status = DurableStoreImportDecisionStatus::Blocked;
        decision.decision_outcome = DurableStoreImportDecisionOutcome::BlockRequest;
        if (request.next_required_adapter_artifact_kind.has_value()) {
            decision.next_required_adapter_capability =
                to_adapter_capability_kind(*request.next_required_adapter_artifact_kind);
        }
        decision.decision_blocker = DecisionBlocker{
            .kind = decision.next_required_adapter_capability.has_value()
                        ? DecisionBlockerKind::MissingRequiredAdapterCapability
                        : DecisionBlockerKind::SourceRequestBlocked,
            .message = request.adapter_blocker.has_value()
                           ? request.adapter_blocker->message
                           : "durable store import request is not yet adapter-consumable",
            .required_capability = decision.next_required_adapter_capability,
        };
        break;
    case DurableStoreImportRequestStatus::TerminalPartial:
        decision.decision_status = DurableStoreImportDecisionStatus::Deferred;
        decision.decision_outcome = DurableStoreImportDecisionOutcome::DeferPartialRequest;
        decision.next_required_adapter_capability =
            AdapterCapabilityKind::PreservePartialWorkflowState;
        decision.decision_blocker = DecisionBlocker{
            .kind = DecisionBlockerKind::PartialWorkflowState,
            .message = request.adapter_blocker.has_value()
                           ? request.adapter_blocker->message
                           : "partial durable store import state must be preserved before future adapter execution",
            .required_capability =
                AdapterCapabilityKind::PreservePartialWorkflowState,
        };
        break;
    case DurableStoreImportRequestStatus::TerminalFailed:
        decision.decision_status = DurableStoreImportDecisionStatus::Rejected;
        decision.decision_outcome = DurableStoreImportDecisionOutcome::RejectFailedRequest;
        decision.decision_blocker = DecisionBlocker{
            .kind = DecisionBlockerKind::WorkflowFailure,
            .message = request.adapter_blocker.has_value()
                           ? request.adapter_blocker->message
                           : "workflow failure rejects durable store adapter contract acceptance",
            .required_capability = std::nullopt,
        };
        break;
    }

    decision.durable_store_import_decision_identity =
        build_durable_store_import_decision_identity(decision.source_package_identity,
                                                     decision.workflow_canonical_name,
                                                     decision.session_id,
                                                     decision.decision_outcome);

    const auto validation = validate_durable_store_import_decision(decision);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.decision = std::move(decision);
    return result;
}

} // namespace ahfl::durable_store_import
