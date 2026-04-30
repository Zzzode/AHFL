#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_DECISION_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}


void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "durable store import decision review summary");
}

void validate_decision_blocker(const DecisionBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary decision_blocker message must not be empty");
    }
}

[[nodiscard]] std::string capability_name(AdapterCapabilityKind capability) {
    switch (capability) {
    case AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] DecisionReviewNextActionKind
next_action_for_decision(const Decision &decision) {
    switch (decision.decision_status) {
    case DecisionStatus::Accepted:
        return decision.request_status == RequestStatus::TerminalCompleted
                   ? DecisionReviewNextActionKind::ArchiveCompletedDurableStoreImportDecision
                   : DecisionReviewNextActionKind::HandoffDurableStoreImportDecision;
    case DecisionStatus::Blocked:
        return DecisionReviewNextActionKind::ResolveRequiredAdapterCapability;
    case DecisionStatus::Deferred:
        return DecisionReviewNextActionKind::PreservePartialDurableStoreImportDecision;
    case DecisionStatus::Rejected:
        return DecisionReviewNextActionKind::InvestigateDurableStoreImportDecisionRejection;
    }

    return DecisionReviewNextActionKind::ResolveRequiredAdapterCapability;
}

[[nodiscard]] std::string adapter_contract_summary_for_decision(
    const Decision &decision) {
    switch (decision.decision_boundary_kind) {
    case DecisionBoundaryKind::LocalContractOnly:
        return "local durable adapter contract reasoning only; real receipt and executor ABI not yet promised";
    case DecisionBoundaryKind::AdapterDecisionConsumable:
        return "adapter-decision-consumable contract shape available; real receipt and executor ABI still not promised";
    }

    return "invalid durable adapter decision boundary";
}

[[nodiscard]] std::string decision_preview_for_decision(
    const Decision &decision) {
    switch (decision.decision_status) {
    case DecisionStatus::Accepted:
        if (decision.request_status == RequestStatus::TerminalCompleted) {
            return "workflow already completed; durable adapter decision is retained for archival review";
        }
        return "durable adapter decision '" + decision.durable_store_import_decision_identity +
               "' accepts current request for future adapter execution";
    case DecisionStatus::Blocked:
        return "durable adapter decision remains blocked until required capability '" +
               (decision.next_required_adapter_capability.has_value()
                    ? capability_name(*decision.next_required_adapter_capability)
                    : std::string("unknown")) +
               "' is available";
    case DecisionStatus::Deferred:
        return "partial workflow durable adapter decision is deferred until partial state preservation is available";
    case DecisionStatus::Rejected:
        return decision.workflow_failure_summary.has_value() &&
                       decision.workflow_failure_summary->node_name.has_value()
                   ? "workflow failed at node '" +
                         *decision.workflow_failure_summary->node_name +
                         "'; durable adapter decision rejects future execution"
                   : "workflow failure rejects future durable adapter execution";
    }

    return "invalid durable adapter decision preview";
}

[[nodiscard]] std::string next_step_recommendation_for_decision(
    const Decision &decision) {
    switch (decision.decision_status) {
    case DecisionStatus::Accepted:
        if (decision.request_status == RequestStatus::TerminalCompleted) {
            return "archive completed durable adapter decision; no further adapter action";
        }
        return "handoff current durable adapter decision before real adapter implementation work";
    case DecisionStatus::Blocked:
        return "add required adapter capability before claiming durable adapter contract acceptance";
    case DecisionStatus::Deferred:
        return "preserve partial workflow state before future adapter execution planning";
    case DecisionStatus::Rejected:
        return "inspect workflow failure before planning future durable adapter execution";
    }

    return "invalid durable adapter next step";
}

} // namespace

DecisionReviewSummaryValidationResult
validate_decision_review_summary(const DecisionReviewSummary &summary) {
    DecisionReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kDecisionReviewFormatVersion) {
        emit_validation_error(diagnostics, "durable store import decision review summary format_version must be '" +
                          std::string(kDecisionReviewFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_format_version !=
        kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary source_durable_store_import_decision_format_version must be '" +
            std::string(kDecisionFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_request_format_version !=
        kRequestFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary source_durable_store_import_request_format_version must be '" +
            std::string(kRequestFormatVersion) + "'");
    }

    if (summary.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "durable store import decision review summary workflow_canonical_name "
                          "must not be empty");
    }

    if (summary.session_id.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        emit_validation_error(diagnostics, "durable store import decision review summary run_id must not be empty "
                          "when present");
    }

    if (summary.input_fixture.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary input_fixture must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import decision review summary export_package_identity "
                          "must not be empty");
    }

    if (summary.store_import_candidate_identity.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary store_import_candidate_identity must not be empty");
    }

    if (summary.durable_store_import_request_identity.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary durable_store_import_request_identity must not be empty");
    }

    if (summary.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary durable_store_import_decision_identity must not be empty");
    }

    if (summary.planned_durable_identity.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.accepted_for_future_execution && summary.decision_blocker.has_value()) {
        emit_validation_error(diagnostics, "durable store import decision review summary cannot contain "
                          "decision_blocker when accepted_for_future_execution is true");
    }

    if (summary.accepted_for_future_execution &&
        summary.next_required_adapter_capability.has_value()) {
        emit_validation_error(diagnostics, "durable store import decision review summary cannot contain "
                          "next_required_adapter_capability when "
                          "accepted_for_future_execution is true");
    }

    if (summary.decision_blocker.has_value()) {
        validate_decision_blocker(*summary.decision_blocker, diagnostics);
    }

    if (summary.adapter_contract_summary.empty()) {
        emit_validation_error(diagnostics, "durable store import decision review summary "
                          "adapter_contract_summary must not be empty");
    }

    if (summary.decision_preview.empty()) {
        emit_validation_error(diagnostics,
            "durable store import decision review summary decision_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics, "durable store import decision review summary "
                          "next_step_recommendation must not be empty");
    }

    switch (summary.decision_status) {
    case DecisionStatus::Accepted:
        if (summary.request_status == RequestStatus::TerminalCompleted) {
            if (summary.next_action !=
                DecisionReviewNextActionKind::ArchiveCompletedDurableStoreImportDecision) {
                emit_validation_error(diagnostics,
                    "durable store import decision review summary Accepted completed decision requires next_action archive_completed_durable_store_import_decision");
            }
        } else if (summary.next_action !=
                   DecisionReviewNextActionKind::HandoffDurableStoreImportDecision) {
            emit_validation_error(diagnostics,
                "durable store import decision review summary Accepted decision requires next_action handoff_durable_store_import_decision");
        }
        if (!summary.accepted_for_future_execution) {
            emit_validation_error(diagnostics, "durable store import decision review summary Accepted decision "
                              "requires accepted_for_future_execution");
        }
        break;
    case DecisionStatus::Blocked:
        if (summary.next_action !=
            DecisionReviewNextActionKind::ResolveRequiredAdapterCapability) {
            emit_validation_error(diagnostics,
                "durable store import decision review summary Blocked decision requires next_action resolve_required_adapter_capability");
        }
        if (summary.accepted_for_future_execution) {
            emit_validation_error(diagnostics, "durable store import decision review summary Blocked decision "
                              "cannot be accepted_for_future_execution");
        }
        if (!summary.decision_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import decision review summary Blocked decision "
                              "requires decision_blocker");
        }
        break;
    case DecisionStatus::Deferred:
        if (summary.next_action !=
            DecisionReviewNextActionKind::PreservePartialDurableStoreImportDecision) {
            emit_validation_error(diagnostics,
                "durable store import decision review summary Deferred decision requires next_action preserve_partial_durable_store_import_decision");
        }
        if (!summary.decision_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import decision review summary Deferred decision "
                              "requires decision_blocker");
        }
        break;
    case DecisionStatus::Rejected:
        if (summary.next_action !=
            DecisionReviewNextActionKind::InvestigateDurableStoreImportDecisionRejection) {
            emit_validation_error(diagnostics,
                "durable store import decision review summary Rejected decision requires next_action investigate_durable_store_import_decision_rejection");
        }
        if (!summary.decision_blocker.has_value()) {
            emit_validation_error(diagnostics, "durable store import decision review summary Rejected decision "
                              "requires decision_blocker");
        }
        break;
    }

    return result;
}

DecisionReviewSummaryResult
build_decision_review_summary(const Decision &decision) {
    DecisionReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_decision(decision);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    DecisionReviewSummary summary{
        .format_version = std::string(kDecisionReviewFormatVersion),
        .source_durable_store_import_decision_format_version = decision.format_version,
        .source_durable_store_import_request_format_version =
            decision.source_durable_store_import_request_format_version,
        .source_store_import_descriptor_format_version =
            decision.source_store_import_descriptor_format_version,
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
        .durable_store_import_request_identity =
            decision.durable_store_import_request_identity,
        .durable_store_import_decision_identity =
            decision.durable_store_import_decision_identity,
        .planned_durable_identity = decision.planned_durable_identity,
        .decision_boundary_kind = decision.decision_boundary_kind,
        .decision_outcome = decision.decision_outcome,
        .accepted_for_future_execution = decision.accepted_for_future_execution,
        .next_required_adapter_capability =
            decision.next_required_adapter_capability,
        .decision_blocker = decision.decision_blocker,
        .next_action = next_action_for_decision(decision),
        .adapter_contract_summary = adapter_contract_summary_for_decision(decision),
        .decision_preview = decision_preview_for_decision(decision),
        .next_step_recommendation = next_step_recommendation_for_decision(decision),
    };

    const auto summary_validation = validate_decision_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::durable_store_import
