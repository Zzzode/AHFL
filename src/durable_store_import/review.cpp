#include "ahfl/durable_store_import/review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "durable store import review summary");
}

void validate_adapter_blocker(const RequestBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary adapter_blocker message must not be empty");
    }

    if (blocker.logical_artifact_name.has_value() && blocker.logical_artifact_name->empty()) {
        emit_validation_error(diagnostics, "durable store import review summary adapter_blocker "
                          "logical_artifact_name must not be empty");
    }
}

[[nodiscard]] std::string artifact_kind_name(RequestedArtifactKind kind) {
    switch (kind) {
    case RequestedArtifactKind::StoreImportDescriptor:
        return "store_import_descriptor";
    case RequestedArtifactKind::ExportManifest:
        return "export_manifest";
    case RequestedArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case RequestedArtifactKind::PersistenceReview:
        return "persistence_review";
    case RequestedArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] std::string adapter_role_name(RequestedArtifactAdapterRole role) {
    switch (role) {
    case RequestedArtifactAdapterRole::RequestAnchor:
        return "request_anchor";
    case RequestedArtifactAdapterRole::ImportManifest:
        return "import_manifest";
    case RequestedArtifactAdapterRole::DurableStateDescriptor:
        return "durable_state_descriptor";
    case RequestedArtifactAdapterRole::HumanReviewContext:
        return "human_review_context";
    case RequestedArtifactAdapterRole::CheckpointPayload:
        return "checkpoint_payload";
    }

    return "invalid";
}

[[nodiscard]] ReviewNextActionKind
next_action_for_request(const Request &request) {
    switch (request.request_status) {
    case RequestStatus::ReadyForAdapter:
        return ReviewNextActionKind::HandoffDurableStoreImportRequest;
    case RequestStatus::Blocked:
        return ReviewNextActionKind::AwaitAdapterReadiness;
    case RequestStatus::TerminalCompleted:
        return ReviewNextActionKind::ArchiveCompletedDurableStoreImportState;
    case RequestStatus::TerminalFailed:
        return ReviewNextActionKind::InvestigateDurableStoreImportFailure;
    case RequestStatus::TerminalPartial:
        return ReviewNextActionKind::PreservePartialDurableStoreImportState;
    }

    return ReviewNextActionKind::AwaitAdapterReadiness;
}

[[nodiscard]] std::string adapter_boundary_summary_for_request(
    const Request &request) {
    switch (request.request_boundary_kind) {
    case RequestBoundaryKind::LocalIntentOnly:
        return "local durable store import intent only; real adapter ABI not yet promised";
    case RequestBoundaryKind::AdapterContractConsumable:
        return "adapter-contract-consumable request shape available; durable mutation ABI still not promised";
    }

    return "local durable store import intent only; real adapter ABI not yet promised";
}

[[nodiscard]] std::string request_preview_for_request(
    const Request &request) {
    switch (request.request_status) {
    case RequestStatus::ReadyForAdapter:
        return "durable store import request '" +
               request.durable_store_import_request_identity +
               "' can be handed off with current requested artifact set";
    case RequestStatus::Blocked:
        if (request.adapter_blocker.has_value() &&
            request.adapter_blocker->logical_artifact_name.has_value()) {
            return "durable store import request is waiting on adapter artifact '" +
                   *request.adapter_blocker->logical_artifact_name + "'";
        }
        if (request.next_required_adapter_artifact_kind.has_value()) {
            return "durable store import request is waiting on adapter artifact kind '" +
                   artifact_kind_name(*request.next_required_adapter_artifact_kind) + "'";
        }
        return "durable store import request is waiting on adapter blockers";
    case RequestStatus::TerminalCompleted:
        return "workflow already completed; durable store import request is retained for archival review";
    case RequestStatus::TerminalFailed:
        if (request.workflow_failure_summary.has_value() &&
            request.workflow_failure_summary->node_name.has_value()) {
            return "workflow failed at node '" +
                   *request.workflow_failure_summary->node_name +
                   "'; durable store import request handoff is closed";
        }
        return "workflow failed; durable store import request handoff is closed";
    case RequestStatus::TerminalPartial:
        return "partial workflow is retained as local durable store import request without adapter handoff";
    }

    return "durable store import request state unavailable";
}

[[nodiscard]] std::string next_step_recommendation_for_request(
    const Request &request) {
    switch (request.request_status) {
    case RequestStatus::ReadyForAdapter:
        return "handoff current durable store import request before real adapter implementation work";
    case RequestStatus::Blocked:
        if (request.next_required_adapter_artifact_kind.has_value()) {
            return "materialize required adapter artifact kind '" +
                   artifact_kind_name(*request.next_required_adapter_artifact_kind) +
                   "' before advertising durable store adapter handoff";
        }
        return "stabilize adapter blockers before advertising durable store adapter handoff";
    case RequestStatus::TerminalCompleted:
        return "archive completed durable store import request; no further import action";
    case RequestStatus::TerminalFailed:
        return "inspect workflow failure before planning durable store adapter handoff";
    case RequestStatus::TerminalPartial:
        return "preserve partial durable store import request for inspection; do not advertise adapter handoff";
    }

    return "no durable store import action";
}

[[nodiscard]] std::vector<std::string> requested_artifact_preview_for_request(
    const Request &request) {
    std::vector<std::string> preview;
    preview.reserve(request.requested_artifact_set.entries.size());
    for (const auto &entry : request.requested_artifact_set.entries) {
        preview.push_back(std::string(entry.required_for_import ? "required " : "optional ") +
                          artifact_kind_name(entry.artifact_kind) + " " +
                          entry.logical_artifact_name + " role " +
                          adapter_role_name(entry.adapter_role));
    }
    return preview;
}

} // namespace

ReviewSummaryValidationResult
validate_review_summary(
    const ReviewSummary &summary) {
    ReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kReviewSummaryFormatVersion) {
        emit_validation_error(diagnostics, "durable store import review summary format_version must be '" +
                          std::string(kReviewSummaryFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_request_format_version !=
        kRequestFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import review summary source_durable_store_import_request_format_version must be '" +
            std::string(kRequestFormatVersion) + "'");
    }

    if (summary.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import review summary source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (summary.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        emit_validation_error(diagnostics, 
            "durable store import review summary source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary workflow_canonical_name must not be empty");
    }

    if (summary.session_id.empty()) {
        emit_validation_error(diagnostics, "durable store import review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary run_id must not be empty when present");
    }

    if (summary.input_fixture.empty()) {
        emit_validation_error(diagnostics, "durable store import review summary input_fixture must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary export_package_identity must not be empty");
    }

    if (summary.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary store_import_candidate_identity must not be empty");
    }

    if (summary.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary durable_store_import_request_identity must not be empty");
    }

    if (summary.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.requested_artifact_entry_count !=
        summary.requested_artifact_preview.size()) {
        emit_validation_error(diagnostics, "durable store import review summary requested_artifact_entry_count must "
                          "match requested_artifact_preview length");
    }

    for (const auto &entry : summary.requested_artifact_preview) {
        if (entry.empty()) {
            emit_validation_error(diagnostics, "durable store import review summary requested_artifact_preview must "
                              "not contain empty entries");
            break;
        }
    }

    if (summary.next_required_adapter_artifact_kind.has_value() && summary.adapter_ready) {
        emit_validation_error(diagnostics, "durable store import review summary cannot contain "
                          "next_required_adapter_artifact_kind when adapter_ready is true");
    }

    if (summary.adapter_blocker.has_value()) {
        validate_adapter_blocker(*summary.adapter_blocker, diagnostics);
    }

    if (summary.adapter_ready && summary.adapter_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import review summary cannot contain adapter_blocker when "
                          "adapter_ready is true");
    }

    if (summary.adapter_boundary_summary.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary adapter_boundary_summary must not be empty");
    }

    if (summary.request_preview.empty()) {
        emit_validation_error(diagnostics, "durable store import review summary request_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics, 
            "durable store import review summary next_step_recommendation must not be empty");
    }

    switch (summary.request_status) {
    case RequestStatus::ReadyForAdapter:
        if (summary.next_action !=
            ReviewNextActionKind::HandoffDurableStoreImportRequest) {
            emit_validation_error(diagnostics, "durable store import review summary ReadyForAdapter request_status "
                              "requires next_action handoff_durable_store_import_request");
        }
        if (!summary.adapter_ready) {
            emit_validation_error(diagnostics, "durable store import review summary ReadyForAdapter request_status "
                              "requires adapter_ready");
        }
        break;
    case RequestStatus::Blocked:
        if (summary.next_action !=
            ReviewNextActionKind::AwaitAdapterReadiness) {
            emit_validation_error(diagnostics, "durable store import review summary Blocked request_status requires "
                              "next_action await_adapter_readiness");
        }
        if (summary.adapter_ready) {
            emit_validation_error(diagnostics, 
                "durable store import review summary Blocked request_status cannot be adapter_ready");
        }
        if (!summary.adapter_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import review summary Blocked request_status requires "
                              "adapter_blocker");
        }
        break;
    case RequestStatus::TerminalCompleted:
        if (summary.next_action !=
            ReviewNextActionKind::ArchiveCompletedDurableStoreImportState) {
            emit_validation_error(diagnostics, 
                "durable store import review summary TerminalCompleted request_status requires next_action archive_completed_durable_store_import_state");
        }
        if (!summary.adapter_ready) {
            emit_validation_error(diagnostics, "durable store import review summary TerminalCompleted "
                              "request_status requires adapter_ready");
        }
        if (summary.adapter_blocker.has_value()) {
            emit_validation_error(diagnostics, 
                "durable store import review summary TerminalCompleted request_status cannot contain adapter_blocker");
        }
        if (summary.next_required_adapter_artifact_kind.has_value()) {
            emit_validation_error(diagnostics, 
                "durable store import review summary TerminalCompleted request_status cannot contain next_required_adapter_artifact_kind");
        }
        break;
    case RequestStatus::TerminalFailed:
        if (summary.next_action !=
            ReviewNextActionKind::InvestigateDurableStoreImportFailure) {
            emit_validation_error(diagnostics, 
                "durable store import review summary TerminalFailed request_status requires next_action investigate_durable_store_import_failure");
        }
        if (!summary.adapter_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import review summary TerminalFailed request_status "
                              "requires adapter_blocker");
        }
        break;
    case RequestStatus::TerminalPartial:
        if (summary.next_action !=
            ReviewNextActionKind::PreservePartialDurableStoreImportState) {
            emit_validation_error(diagnostics, 
                "durable store import review summary TerminalPartial request_status requires next_action preserve_partial_durable_store_import_state");
        }
        if (!summary.adapter_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import review summary TerminalPartial request_status "
                              "requires adapter_blocker");
        }
        break;
    }

    return result;
}

ReviewSummaryResult
build_review_summary(const Request &request) {
    ReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_request(request);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ReviewSummary summary{
        .format_version = std::string(kReviewSummaryFormatVersion),
        .source_durable_store_import_request_format_version = request.format_version,
        .source_store_import_descriptor_format_version =
            request.source_store_import_descriptor_format_version,
        .source_export_manifest_format_version = request.source_export_manifest_format_version,
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
        .planned_durable_identity = request.planned_durable_identity,
        .request_boundary_kind = request.request_boundary_kind,
        .requested_artifact_entry_count = request.requested_artifact_set.entry_count,
        .requested_artifact_preview = requested_artifact_preview_for_request(request),
        .adapter_ready = request.adapter_ready,
        .next_required_adapter_artifact_kind =
            request.next_required_adapter_artifact_kind,
        .adapter_blocker = request.adapter_blocker,
        .next_action = next_action_for_request(request),
        .adapter_boundary_summary = adapter_boundary_summary_for_request(request),
        .request_preview = request_preview_for_request(request),
        .next_step_recommendation = next_step_recommendation_for_request(request),
    };

    const auto summary_validation = validate_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::durable_store_import
