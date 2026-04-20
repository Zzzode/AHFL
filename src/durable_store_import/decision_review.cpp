#include "ahfl/durable_store_import/decision_review.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error("durable store import decision review summary " +
                          std::string(owner_name) +
                          " contains failure summary with empty message");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error("durable store import decision review summary " +
                          std::string(owner_name) +
                          " contains failure summary with empty node_name");
    }
}

void validate_decision_blocker(const DecisionBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        diagnostics.error(
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

[[nodiscard]] DurableStoreImportDecisionReviewNextActionKind
next_action_for_decision(const DurableStoreImportDecision &decision) {
    switch (decision.decision_status) {
    case DurableStoreImportDecisionStatus::Accepted:
        return decision.request_status == DurableStoreImportRequestStatus::TerminalCompleted
                   ? DurableStoreImportDecisionReviewNextActionKind::
                         ArchiveCompletedDurableStoreImportDecision
                   : DurableStoreImportDecisionReviewNextActionKind::
                         HandoffDurableStoreImportDecision;
    case DurableStoreImportDecisionStatus::Blocked:
        return DurableStoreImportDecisionReviewNextActionKind::
            ResolveRequiredAdapterCapability;
    case DurableStoreImportDecisionStatus::Deferred:
        return DurableStoreImportDecisionReviewNextActionKind::
            PreservePartialDurableStoreImportDecision;
    case DurableStoreImportDecisionStatus::Rejected:
        return DurableStoreImportDecisionReviewNextActionKind::
            InvestigateDurableStoreImportDecisionRejection;
    }

    return DurableStoreImportDecisionReviewNextActionKind::
        ResolveRequiredAdapterCapability;
}

[[nodiscard]] std::string adapter_contract_summary_for_decision(
    const DurableStoreImportDecision &decision) {
    switch (decision.decision_boundary_kind) {
    case DecisionBoundaryKind::LocalContractOnly:
        return "local durable adapter contract reasoning only; real receipt and executor ABI not yet promised";
    case DecisionBoundaryKind::AdapterDecisionConsumable:
        return "adapter-decision-consumable contract shape available; real receipt and executor ABI still not promised";
    }

    return "invalid durable adapter decision boundary";
}

[[nodiscard]] std::string decision_preview_for_decision(
    const DurableStoreImportDecision &decision) {
    switch (decision.decision_status) {
    case DurableStoreImportDecisionStatus::Accepted:
        if (decision.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            return "workflow already completed; durable adapter decision is retained for archival review";
        }
        return "durable adapter decision '" + decision.durable_store_import_decision_identity +
               "' accepts current request for future adapter execution";
    case DurableStoreImportDecisionStatus::Blocked:
        return "durable adapter decision remains blocked until required capability '" +
               (decision.next_required_adapter_capability.has_value()
                    ? capability_name(*decision.next_required_adapter_capability)
                    : std::string("unknown")) +
               "' is available";
    case DurableStoreImportDecisionStatus::Deferred:
        return "partial workflow durable adapter decision is deferred until partial state preservation is available";
    case DurableStoreImportDecisionStatus::Rejected:
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
    const DurableStoreImportDecision &decision) {
    switch (decision.decision_status) {
    case DurableStoreImportDecisionStatus::Accepted:
        if (decision.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            return "archive completed durable adapter decision; no further adapter action";
        }
        return "handoff current durable adapter decision before real adapter implementation work";
    case DurableStoreImportDecisionStatus::Blocked:
        return "add required adapter capability before claiming durable adapter contract acceptance";
    case DurableStoreImportDecisionStatus::Deferred:
        return "preserve partial workflow state before future adapter execution planning";
    case DurableStoreImportDecisionStatus::Rejected:
        return "inspect workflow failure before planning future durable adapter execution";
    }

    return "invalid durable adapter next step";
}

} // namespace

DurableStoreImportDecisionReviewSummaryValidationResult
validate_durable_store_import_decision_review_summary(
    const DurableStoreImportDecisionReviewSummary &summary) {
    DurableStoreImportDecisionReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kDurableStoreImportDecisionReviewSummaryFormatVersion) {
        diagnostics.error("durable store import decision review summary format_version must be '" +
                          std::string(kDurableStoreImportDecisionReviewSummaryFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_decision_format_version !=
        kDurableStoreImportDecisionFormatVersion) {
        diagnostics.error(
            "durable store import decision review summary source_durable_store_import_decision_format_version must be '" +
            std::string(kDurableStoreImportDecisionFormatVersion) + "'");
    }

    if (summary.source_durable_store_import_request_format_version !=
        kDurableStoreImportRequestFormatVersion) {
        diagnostics.error(
            "durable store import decision review summary source_durable_store_import_request_format_version must be '" +
            std::string(kDurableStoreImportRequestFormatVersion) + "'");
    }

    if (summary.source_store_import_descriptor_format_version !=
        store_import::kStoreImportDescriptorFormatVersion) {
        diagnostics.error(
            "durable store import decision review summary source_store_import_descriptor_format_version must be '" +
            std::string(store_import::kStoreImportDescriptorFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        diagnostics.error("durable store import decision review summary workflow_canonical_name "
                          "must not be empty");
    }

    if (summary.session_id.empty()) {
        diagnostics.error(
            "durable store import decision review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        diagnostics.error("durable store import decision review summary run_id must not be empty "
                          "when present");
    }

    if (summary.input_fixture.empty()) {
        diagnostics.error(
            "durable store import decision review summary input_fixture must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        diagnostics.error("durable store import decision review summary export_package_identity "
                          "must not be empty");
    }

    if (summary.store_import_candidate_identity.empty()) {
        diagnostics.error(
            "durable store import decision review summary store_import_candidate_identity must not be empty");
    }

    if (summary.durable_store_import_request_identity.empty()) {
        diagnostics.error(
            "durable store import decision review summary durable_store_import_request_identity must not be empty");
    }

    if (summary.durable_store_import_decision_identity.empty()) {
        diagnostics.error(
            "durable store import decision review summary durable_store_import_decision_identity must not be empty");
    }

    if (summary.planned_durable_identity.empty()) {
        diagnostics.error(
            "durable store import decision review summary planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.accepted_for_future_execution && summary.decision_blocker.has_value()) {
        diagnostics.error("durable store import decision review summary cannot contain "
                          "decision_blocker when accepted_for_future_execution is true");
    }

    if (summary.accepted_for_future_execution &&
        summary.next_required_adapter_capability.has_value()) {
        diagnostics.error("durable store import decision review summary cannot contain "
                          "next_required_adapter_capability when "
                          "accepted_for_future_execution is true");
    }

    if (summary.decision_blocker.has_value()) {
        validate_decision_blocker(*summary.decision_blocker, diagnostics);
    }

    if (summary.adapter_contract_summary.empty()) {
        diagnostics.error("durable store import decision review summary "
                          "adapter_contract_summary must not be empty");
    }

    if (summary.decision_preview.empty()) {
        diagnostics.error(
            "durable store import decision review summary decision_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        diagnostics.error("durable store import decision review summary "
                          "next_step_recommendation must not be empty");
    }

    switch (summary.decision_status) {
    case DurableStoreImportDecisionStatus::Accepted:
        if (summary.request_status == DurableStoreImportRequestStatus::TerminalCompleted) {
            if (summary.next_action !=
                DurableStoreImportDecisionReviewNextActionKind::
                    ArchiveCompletedDurableStoreImportDecision) {
                diagnostics.error(
                    "durable store import decision review summary Accepted completed decision requires next_action archive_completed_durable_store_import_decision");
            }
        } else if (summary.next_action !=
                   DurableStoreImportDecisionReviewNextActionKind::
                       HandoffDurableStoreImportDecision) {
            diagnostics.error(
                "durable store import decision review summary Accepted decision requires next_action handoff_durable_store_import_decision");
        }
        if (!summary.accepted_for_future_execution) {
            diagnostics.error("durable store import decision review summary Accepted decision "
                              "requires accepted_for_future_execution");
        }
        break;
    case DurableStoreImportDecisionStatus::Blocked:
        if (summary.next_action !=
            DurableStoreImportDecisionReviewNextActionKind::
                ResolveRequiredAdapterCapability) {
            diagnostics.error(
                "durable store import decision review summary Blocked decision requires next_action resolve_required_adapter_capability");
        }
        if (summary.accepted_for_future_execution) {
            diagnostics.error("durable store import decision review summary Blocked decision "
                              "cannot be accepted_for_future_execution");
        }
        if (!summary.decision_blocker.has_value()) {
            diagnostics.error("durable store import decision review summary Blocked decision "
                              "requires decision_blocker");
        }
        break;
    case DurableStoreImportDecisionStatus::Deferred:
        if (summary.next_action !=
            DurableStoreImportDecisionReviewNextActionKind::
                PreservePartialDurableStoreImportDecision) {
            diagnostics.error(
                "durable store import decision review summary Deferred decision requires next_action preserve_partial_durable_store_import_decision");
        }
        if (!summary.decision_blocker.has_value()) {
            diagnostics.error("durable store import decision review summary Deferred decision "
                              "requires decision_blocker");
        }
        break;
    case DurableStoreImportDecisionStatus::Rejected:
        if (summary.next_action !=
            DurableStoreImportDecisionReviewNextActionKind::
                InvestigateDurableStoreImportDecisionRejection) {
            diagnostics.error(
                "durable store import decision review summary Rejected decision requires next_action investigate_durable_store_import_decision_rejection");
        }
        if (!summary.decision_blocker.has_value()) {
            diagnostics.error("durable store import decision review summary Rejected decision "
                              "requires decision_blocker");
        }
        break;
    }

    return result;
}

DurableStoreImportDecisionReviewSummaryResult
build_durable_store_import_decision_review_summary(
    const DurableStoreImportDecision &decision) {
    DurableStoreImportDecisionReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_durable_store_import_decision(decision);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    DurableStoreImportDecisionReviewSummary summary{
        .format_version =
            std::string(kDurableStoreImportDecisionReviewSummaryFormatVersion),
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

    const auto summary_validation =
        validate_durable_store_import_decision_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::durable_store_import
