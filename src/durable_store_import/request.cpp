#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_REQUEST";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(identity, diagnostics, "durable store import request");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(
        summary, diagnostics, "durable store import request");
}

[[nodiscard]] std::string
build_request_identity(const std::optional<handoff::PackageIdentity> &source_package_identity,
                       std::string_view workflow_canonical_name,
                       std::string_view session_id,
                       std::size_t requested_entry_count) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import::" + identity_anchor + "::" + std::string(session_id) +
           "::request-" + std::to_string(requested_entry_count);
}

[[nodiscard]] RequestedArtifactKind
to_requested_artifact_kind(store_import::StagingArtifactKind kind) {
    switch (kind) {
    case store_import::StagingArtifactKind::ExportManifest:
        return RequestedArtifactKind::ExportManifest;
    case store_import::StagingArtifactKind::PersistenceDescriptor:
        return RequestedArtifactKind::PersistenceDescriptor;
    case store_import::StagingArtifactKind::PersistenceReview:
        return RequestedArtifactKind::PersistenceReview;
    case store_import::StagingArtifactKind::CheckpointRecord:
        return RequestedArtifactKind::CheckpointRecord;
    }

    return RequestedArtifactKind::StoreImportDescriptor;
}

[[nodiscard]] RequestedArtifactAdapterRole adapter_role_for_artifact(RequestedArtifactKind kind) {
    switch (kind) {
    case RequestedArtifactKind::StoreImportDescriptor:
        return RequestedArtifactAdapterRole::RequestAnchor;
    case RequestedArtifactKind::ExportManifest:
        return RequestedArtifactAdapterRole::ImportManifest;
    case RequestedArtifactKind::PersistenceDescriptor:
        return RequestedArtifactAdapterRole::DurableStateDescriptor;
    case RequestedArtifactKind::PersistenceReview:
        return RequestedArtifactAdapterRole::HumanReviewContext;
    case RequestedArtifactKind::CheckpointRecord:
        return RequestedArtifactAdapterRole::CheckpointPayload;
    }

    return RequestedArtifactAdapterRole::RequestAnchor;
}

[[nodiscard]] RequestBlockerKind to_request_blocker_kind(store_import::StagingBlockerKind kind) {
    switch (kind) {
    case store_import::StagingBlockerKind::WaitingOnExportManifest:
        return RequestBlockerKind::WaitingOnRequestedArtifact;
    case store_import::StagingBlockerKind::MissingStoreImportCandidateIdentity:
        return RequestBlockerKind::MissingRequestIdentity;
    case store_import::StagingBlockerKind::MissingStagingArtifactSet:
        return RequestBlockerKind::MissingRequestedArtifactSet;
    case store_import::StagingBlockerKind::WorkflowFailure:
        return RequestBlockerKind::WorkflowFailure;
    case store_import::StagingBlockerKind::WorkflowPartial:
        return RequestBlockerKind::WorkflowPartial;
    }

    return RequestBlockerKind::WaitingOnRequestedArtifact;
}

[[nodiscard]] RequestedArtifactSet
build_requested_artifact_set(const store_import::StoreImportDescriptor &descriptor) {
    RequestedArtifactSet artifact_set;
    artifact_set.entries.push_back(RequestedArtifactEntry{
        .artifact_kind = RequestedArtifactKind::StoreImportDescriptor,
        .logical_artifact_name = "store-import-descriptor.json",
        .source_format_version = descriptor.format_version,
        .required_for_import = true,
        .adapter_role = RequestedArtifactAdapterRole::RequestAnchor,
    });

    for (const auto &entry : descriptor.staging_artifact_set.entries) {
        const auto requested_kind = to_requested_artifact_kind(entry.artifact_kind);
        artifact_set.entries.push_back(RequestedArtifactEntry{
            .artifact_kind = requested_kind,
            .logical_artifact_name = entry.logical_artifact_name,
            .source_format_version = entry.source_format_version,
            .required_for_import = entry.required_for_import,
            .adapter_role = adapter_role_for_artifact(requested_kind),
        });
    }

    artifact_set.entry_count = artifact_set.entries.size();
    return artifact_set;
}

} // namespace

RequestValidationResult validate_request(const Request &request) {
    RequestValidationResult result;
    auto &diagnostics = result.diagnostics;

    // Format version must match stable contract
    if (request.format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import request format_version must be '" +
                                  std::string(kRequestFormatVersion) + "'");
    }

    if (request.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_store_import_descriptor_format_version must be '" +
                std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (request.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_execution_plan_format_version must be '" +
                std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (request.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_runtime_session_format_version must be '" +
                std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (request.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_execution_journal_format_version must be '" +
                std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (request.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_replay_view_format_version must be '" +
                std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (request.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_scheduler_snapshot_format_version must be '" +
                std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (request.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_checkpoint_record_format_version must be '" +
                std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (request.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_persistence_descriptor_format_version must be '" +
                std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (request.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import request source_export_manifest_format_version must be '" +
                std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (request.workflow_canonical_name.empty()) {
        emit_validation_error(
            diagnostics, "durable store import request workflow_canonical_name must not be empty");
    }

    if (request.session_id.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import request session_id must not be empty");
    }

    if (request.run_id.has_value() && request.run_id->empty()) {
        emit_validation_error(diagnostics,
                              "durable store import request run_id must not be empty when present");
    }

    if (request.input_fixture.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import request input_fixture must not be empty");
    }

    if (request.export_package_identity.empty()) {
        emit_validation_error(
            diagnostics, "durable store import request export_package_identity must not be empty");
    }

    if (request.store_import_candidate_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import request store_import_candidate_identity must not be empty");
    }

    // Note: using durable_store_import_request_identity field name for JSON compatibility
    if (request.durable_store_import_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import request durable_store_import_request_identity must not be empty");
    }

    if (request.planned_durable_identity.empty()) {
        emit_validation_error(
            diagnostics, "durable store import request planned_durable_identity must not be empty");
    }

    if (request.source_package_identity.has_value()) {
        validate_package_identity(*request.source_package_identity, diagnostics);
    }

    if (request.workflow_failure_summary.has_value()) {
        validate_failure_summary(*request.workflow_failure_summary, diagnostics);
    }

    if (request.requested_artifact_set.entry_count !=
        request.requested_artifact_set.entries.size()) {
        emit_validation_error(
            diagnostics,
            "durable store import request requested_artifact_set entry_count must "
            "match entries length");
    }

    if (request.adapter_ready && request.requested_artifact_set.entries.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import request adapter_ready requires non-empty requested_artifact_set");
    }

    std::unordered_set<std::string> logical_names;
    for (const auto &entry : request.requested_artifact_set.entries) {
        if (entry.logical_artifact_name.empty()) {
            emit_validation_error(diagnostics,
                                  "durable store import request requested_artifact_set entry "
                                  "logical_artifact_name must not be empty");
        } else if (!logical_names.insert(entry.logical_artifact_name).second) {
            emit_validation_error(diagnostics,
                                  "durable store import request requested_artifact_set contains "
                                  "duplicate logical_artifact_name '" +
                                      entry.logical_artifact_name + "'");
        }

        if (entry.source_format_version.empty()) {
            emit_validation_error(diagnostics,
                                  "durable store import request requested_artifact_set entry "
                                  "source_format_version must not be empty");
        }

        if (entry.artifact_kind == RequestedArtifactKind::StoreImportDescriptor &&
            entry.source_format_version != request.source_store_import_descriptor_format_version) {
            emit_validation_error(
                diagnostics,
                "durable store import request store import descriptor entry source_format_version "
                "must match source_store_import_descriptor_format_version");
        }
    }

    if (request.adapter_ready && request.adapter_blocker.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import request cannot contain adapter_blocker when "
                              "adapter_ready is true");
    }

    if (request.adapter_ready && request.next_required_adapter_artifact_kind.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import request cannot contain "
                              "next_required_adapter_artifact_kind when adapter_ready is true");
    }

    if (!request.adapter_ready && request.request_status != RequestStatus::TerminalCompleted &&
        !request.adapter_blocker.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import request must contain adapter_blocker when "
                              "adapter_ready is false");
    }

    if (request.adapter_blocker.has_value()) {
        if (request.adapter_blocker->message.empty()) {
            emit_validation_error(
                diagnostics,
                "durable store import request adapter_blocker message must not be empty");
        }

        if (request.adapter_blocker->logical_artifact_name.has_value() &&
            request.adapter_blocker->logical_artifact_name->empty()) {
            emit_validation_error(
                diagnostics,
                "durable store import request adapter_blocker logical_artifact_name "
                "must not be empty");
        }
    }

    if (request.request_status == RequestStatus::ReadyForAdapter && !request.adapter_ready) {
        emit_validation_error(
            diagnostics,
            "durable store import request ReadyForAdapter status requires adapter_ready");
    }

    if (request.request_status == RequestStatus::ReadyForAdapter &&
        request.descriptor_status != store_import::StoreImportDescriptorStatus::ReadyToImport) {
        emit_validation_error(diagnostics,
                              "durable store import request ReadyForAdapter status requires "
                              "ReadyToImport descriptor_status");
    }

    if (request.request_status == RequestStatus::Blocked && request.adapter_ready) {
        emit_validation_error(
            diagnostics, "durable store import request Blocked status cannot be adapter_ready");
    }

    if (request.request_status == RequestStatus::Blocked &&
        request.descriptor_status != store_import::StoreImportDescriptorStatus::Blocked) {
        emit_validation_error(
            diagnostics,
            "durable store import request Blocked status requires Blocked descriptor_status");
    }

    if (request.request_status == RequestStatus::TerminalCompleted && !request.adapter_ready) {
        emit_validation_error(
            diagnostics,
            "durable store import request TerminalCompleted status requires adapter_ready");
    }

    if (request.request_status == RequestStatus::TerminalCompleted &&
        request.descriptor_status != store_import::StoreImportDescriptorStatus::TerminalCompleted) {
        emit_validation_error(diagnostics,
                              "durable store import request TerminalCompleted status requires "
                              "TerminalCompleted descriptor_status");
    }

    if ((request.request_status == RequestStatus::TerminalFailed ||
         request.request_status == RequestStatus::TerminalPartial) &&
        !request.adapter_blocker.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import request terminal blocked status requires adapter_blocker");
    }

    if ((request.request_status == RequestStatus::TerminalFailed ||
         request.request_status == RequestStatus::TerminalPartial) &&
        request.adapter_ready) {
        emit_validation_error(
            diagnostics,
            "durable store import request terminal blocked status cannot be adapter_ready");
    }

    if ((request.request_status == RequestStatus::TerminalFailed ||
         request.request_status == RequestStatus::TerminalPartial) &&
        request.next_required_adapter_artifact_kind.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import request terminal blocked status cannot have "
                              "next_required_adapter_artifact_kind");
    }

    if (request.request_status == RequestStatus::TerminalFailed &&
        request.descriptor_status != store_import::StoreImportDescriptorStatus::TerminalFailed) {
        emit_validation_error(diagnostics,
                              "durable store import request TerminalFailed status requires "
                              "TerminalFailed descriptor_status");
    }

    if (request.request_status == RequestStatus::TerminalPartial &&
        request.descriptor_status != store_import::StoreImportDescriptorStatus::TerminalPartial) {
        emit_validation_error(diagnostics,
                              "durable store import request TerminalPartial status requires "
                              "TerminalPartial descriptor_status");
    }

    if (request.request_status == RequestStatus::TerminalFailed &&
        !request.workflow_failure_summary.has_value()) {
        emit_validation_error(
            diagnostics,
            "durable store import request TerminalFailed status requires workflow_failure_summary");
    }

    if (request.request_boundary_kind == RequestBoundaryKind::AdapterContractConsumable &&
        !request.adapter_ready && request.request_status != RequestStatus::TerminalCompleted) {
        emit_validation_error(diagnostics,
                              "durable store import request adapter-contract-consumable boundary "
                              "requires ready or completed request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        request.request_status != RequestStatus::TerminalCompleted) {
        emit_validation_error(diagnostics,
                              "durable store import request completed workflow_status requires "
                              "TerminalCompleted request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        request.request_status != RequestStatus::TerminalFailed) {
        emit_validation_error(diagnostics,
                              "durable store import request failed workflow_status requires "
                              "TerminalFailed request_status");
    }

    if (request.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        request.request_status != RequestStatus::ReadyForAdapter &&
        request.request_status != RequestStatus::Blocked &&
        request.request_status != RequestStatus::TerminalPartial) {
        emit_validation_error(diagnostics,
                              "durable store import request partial workflow_status must map to "
                              "ReadyForAdapter, Blocked, or TerminalPartial request_status");
    }

    return result;
}

RequestResult build_request(const store_import::StoreImportDescriptor &descriptor) {
    RequestResult result{
        .request = std::nullopt,
        .diagnostics = {},
    };

    const auto descriptor_validation = store_import::validate_store_import_descriptor(descriptor);
    result.diagnostics.append(descriptor_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    auto requested_artifact_set = build_requested_artifact_set(descriptor);
    Request request{
        .format_version = std::string(kRequestFormatVersion),
        .source_store_import_descriptor_format_version = descriptor.format_version,
        .source_execution_plan_format_version = descriptor.source_execution_plan_format_version,
        .source_runtime_session_format_version = descriptor.source_runtime_session_format_version,
        .source_execution_journal_format_version =
            descriptor.source_execution_journal_format_version,
        .source_replay_view_format_version = descriptor.source_replay_view_format_version,
        .source_scheduler_snapshot_format_version =
            descriptor.source_scheduler_snapshot_format_version,
        .source_checkpoint_record_format_version =
            descriptor.source_checkpoint_record_format_version,
        .source_persistence_descriptor_format_version =
            descriptor.source_persistence_descriptor_format_version,
        .source_export_manifest_format_version = descriptor.source_export_manifest_format_version,
        .source_package_identity = descriptor.source_package_identity,
        .workflow_canonical_name = descriptor.workflow_canonical_name,
        .session_id = descriptor.session_id,
        .run_id = descriptor.run_id,
        .input_fixture = descriptor.input_fixture,
        .workflow_status = descriptor.workflow_status,
        .checkpoint_status = descriptor.checkpoint_status,
        .persistence_status = descriptor.persistence_status,
        .manifest_status = descriptor.manifest_status,
        .descriptor_status = descriptor.descriptor_status,
        .workflow_failure_summary = descriptor.workflow_failure_summary,
        .export_package_identity = descriptor.export_package_identity,
        .store_import_candidate_identity = descriptor.store_import_candidate_identity,
        .durable_store_import_request_identity =
            build_request_identity(descriptor.source_package_identity,
                                   descriptor.workflow_canonical_name,
                                   descriptor.session_id,
                                   requested_artifact_set.entry_count),
        .planned_durable_identity = descriptor.planned_durable_identity,
        .request_boundary_kind = RequestBoundaryKind::LocalIntentOnly,
        .requested_artifact_set = std::move(requested_artifact_set),
        .adapter_ready = false,
        .next_required_adapter_artifact_kind = std::nullopt,
        .request_status = RequestStatus::Blocked,
        .adapter_blocker = std::nullopt,
    };

    switch (descriptor.descriptor_status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        request.request_status = RequestStatus::ReadyForAdapter;
        request.adapter_ready = true;
        request.request_boundary_kind = RequestBoundaryKind::AdapterContractConsumable;
        break;
    case store_import::StoreImportDescriptorStatus::Blocked:
        request.request_status = RequestStatus::Blocked;
        if (descriptor.next_required_staging_artifact_kind.has_value()) {
            request.next_required_adapter_artifact_kind =
                to_requested_artifact_kind(*descriptor.next_required_staging_artifact_kind);
        }
        if (descriptor.staging_blocker.has_value()) {
            request.adapter_blocker = RequestBlocker{
                .kind = to_request_blocker_kind(descriptor.staging_blocker->kind),
                .message = descriptor.staging_blocker->message,
                .logical_artifact_name = descriptor.staging_blocker->logical_artifact_name,
            };
        }
        break;
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        request.request_status = RequestStatus::TerminalCompleted;
        request.adapter_ready = true;
        request.request_boundary_kind = RequestBoundaryKind::AdapterContractConsumable;
        break;
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        request.request_status = RequestStatus::TerminalFailed;
        if (descriptor.staging_blocker.has_value()) {
            request.adapter_blocker = RequestBlocker{
                .kind = to_request_blocker_kind(descriptor.staging_blocker->kind),
                .message = descriptor.staging_blocker->message,
                .logical_artifact_name = descriptor.staging_blocker->logical_artifact_name,
            };
        }
        break;
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        request.request_status = RequestStatus::TerminalPartial;
        if (descriptor.staging_blocker.has_value()) {
            request.adapter_blocker = RequestBlocker{
                .kind = to_request_blocker_kind(descriptor.staging_blocker->kind),
                .message = descriptor.staging_blocker->message,
                .logical_artifact_name = descriptor.staging_blocker->logical_artifact_name,
            };
        }
        break;
    }

    const auto validation = validate_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.request = std::move(request);
    return result;
}

} // namespace ahfl::durable_store_import
