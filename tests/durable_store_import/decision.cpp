#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/request.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_ready_descriptor() {
    using namespace ahfl;

    store_import::StoreImportDescriptor descriptor;
    descriptor.source_package_identity = handoff::PackageIdentity{
        .format_version = std::string(handoff::kFormatVersion),
        .name = "workflow-value-flow",
        .version = "0.2.0",
    };
    descriptor.workflow_canonical_name = "app::main::ValueFlowWorkflow";
    descriptor.session_id = "run-partial-001";
    descriptor.run_id = "run-partial-001";
    descriptor.input_fixture = "fixture.request.partial";
    descriptor.workflow_status = runtime_session::WorkflowSessionStatus::Partial;
    descriptor.checkpoint_status = checkpoint_record::CheckpointRecordStatus::ReadyToPersist;
    descriptor.persistence_status =
        persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport;
    descriptor.manifest_status = persistence_export::PersistenceExportManifestStatus::ReadyToImport;
    descriptor.export_package_identity = "export::workflow-value-flow::run-partial-001::prefix-1";
    descriptor.store_import_candidate_identity =
        "store-import::workflow-value-flow::run-partial-001::stage-3";
    descriptor.planned_durable_identity = "plan::workflow-value-flow::run-partial-001::prefix-1";
    descriptor.descriptor_boundary_kind = store_import::StoreImportBoundaryKind::AdapterConsumable;
    descriptor.staging_artifact_set.entry_count = 3;
    descriptor.staging_artifact_set.entries = {
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::ExportManifest,
            .logical_artifact_name = "export-manifest.json",
            .source_format_version =
                std::string(persistence_export::kPersistenceExportManifestFormatVersion),
            .required_for_import = true,
        },
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::PersistenceDescriptor,
            .logical_artifact_name = "persistence-descriptor.json",
            .source_format_version =
                std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion),
            .required_for_import = true,
        },
        store_import::StagingArtifactEntry{
            .artifact_kind = store_import::StagingArtifactKind::CheckpointRecord,
            .logical_artifact_name = "checkpoint-record.json",
            .source_format_version = std::string(checkpoint_record::kCheckpointRecordFormatVersion),
            .required_for_import = true,
        },
    };
    descriptor.import_ready = true;
    descriptor.descriptor_status = store_import::StoreImportDescriptorStatus::ReadyToImport;
    return descriptor;
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_blocked_descriptor() {
    auto descriptor = make_ready_descriptor();
    descriptor.import_ready = false;
    descriptor.descriptor_status = ahfl::store_import::StoreImportDescriptorStatus::Blocked;
    descriptor.manifest_status = ahfl::persistence_export::PersistenceExportManifestStatus::Blocked;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind =
        ahfl::store_import::StagingArtifactKind::PersistenceReview;
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::MissingStagingArtifactSet,
        .message = "waiting for readable persistence review staging artifact",
        .logical_artifact_name = "persistence-review.txt",
    };
    return descriptor;
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_completed_descriptor() {
    auto descriptor = make_ready_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed;
    descriptor.checkpoint_status =
        ahfl::checkpoint_record::CheckpointRecordStatus::TerminalCompleted;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalCompleted;
    descriptor.descriptor_status =
        ahfl::store_import::StoreImportDescriptorStatus::TerminalCompleted;
    return descriptor;
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_failed_descriptor() {
    auto descriptor = make_ready_descriptor();
    descriptor.workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    descriptor.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::TerminalFailed;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalFailed;
    descriptor.import_ready = false;
    descriptor.descriptor_status = ahfl::store_import::StoreImportDescriptorStatus::TerminalFailed;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind.reset();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WorkflowFailure,
        .message = "workflow failed before durable store import handoff could be finalized",
        .logical_artifact_name = std::nullopt,
    };
    descriptor.workflow_failure_summary = ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::NodeFailed,
        .node_name = "first",
        .message = "echo capability failed",
    };
    return descriptor;
}

[[nodiscard]] ahfl::store_import::StoreImportDescriptor make_partial_terminal_descriptor() {
    auto descriptor = make_ready_descriptor();
    descriptor.checkpoint_status = ahfl::checkpoint_record::CheckpointRecordStatus::TerminalPartial;
    descriptor.persistence_status =
        ahfl::persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial;
    descriptor.manifest_status =
        ahfl::persistence_export::PersistenceExportManifestStatus::TerminalPartial;
    descriptor.import_ready = false;
    descriptor.descriptor_status = ahfl::store_import::StoreImportDescriptorStatus::TerminalPartial;
    descriptor.descriptor_boundary_kind =
        ahfl::store_import::StoreImportBoundaryKind::LocalStagingOnly;
    descriptor.next_required_staging_artifact_kind.reset();
    descriptor.staging_blocker = ahfl::store_import::StagingBlocker{
        .kind = ahfl::store_import::StagingBlockerKind::WorkflowPartial,
        .message = "partial workflow retained as local durable store import handoff",
        .logical_artifact_name = std::nullopt,
    };
    return descriptor;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportRequest>
make_request(const ahfl::store_import::StoreImportDescriptor &descriptor) {
    const auto request = ahfl::durable_store_import::build_durable_store_import_request(descriptor);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecision>
make_valid_decision() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecisionReceipt>
make_receipt_from_decision(
    const ahfl::durable_store_import::DurableStoreImportDecision &decision) {
    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecisionReceipt>
make_valid_receipt() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return std::nullopt;
    }

    return make_receipt_from_decision(*decision);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptReviewSummary>
make_valid_receipt_review_summary() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceRequest>
make_persistence_request_from_receipt(
    const ahfl::durable_store_import::DurableStoreImportDecisionReceipt &receipt) {
    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceRequest>
make_valid_receipt_persistence_request() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    return make_persistence_request_from_receipt(*receipt);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceReviewSummary>
make_valid_receipt_persistence_review_summary() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

int validate_durable_store_import_decision_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(*decision);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_durable_store_import_decision_blocked_ok() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Blocked ||
        value.accepted_for_future_execution || !value.decision_blocker.has_value() ||
        value.decision_blocker->kind !=
            ahfl::durable_store_import::DecisionBlockerKind::MissingRequiredAdapterCapability ||
        value.next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext) {
        std::cerr << "unexpected blocked durable store import decision\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_decision_deferred_ok() {
    const auto request = make_request(make_partial_terminal_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Deferred ||
        value.accepted_for_future_execution || !value.decision_blocker.has_value() ||
        value.decision_blocker->kind !=
            ahfl::durable_store_import::DecisionBlockerKind::PartialWorkflowState ||
        value.next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState) {
        std::cerr << "unexpected deferred durable store import decision\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_decision_rejected_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Rejected ||
        value.accepted_for_future_execution || !value.decision_blocker.has_value() ||
        value.decision_blocker->kind !=
            ahfl::durable_store_import::DecisionBlockerKind::WorkflowFailure) {
        std::cerr << "unexpected rejected durable store import decision\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_decision_rejects_missing_decision_identity() {
    auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    decision->durable_store_import_decision_identity.clear();
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import decision identity to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "durable_store_import_decision_identity must not be empty")
               ? 0
               : 1;
}

int validate_durable_store_import_decision_rejects_accepted_with_blocker() {
    auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    decision->decision_blocker = ahfl::durable_store_import::DecisionBlocker{
        .kind = ahfl::durable_store_import::DecisionBlockerKind::SourceRequestBlocked,
        .message = "unexpected decision blocker",
        .required_capability = std::nullopt,
    };
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected accepted decision with blocker to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "cannot contain decision_blocker when accepted_for_future_execution is true")
               ? 0
               : 1;
}

int validate_durable_store_import_decision_rejects_unsupported_source_request_format() {
    auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    decision->source_durable_store_import_request_format_version =
        "ahfl.durable-store-import-request.v999";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source request format to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_request_format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_decision_rejects_adapter_decision_consumable_blocked() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (built.has_errors() || !built.decision.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto decision = *built.decision;
    decision.decision_boundary_kind =
        ahfl::durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable;
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(decision);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked adapter-decision-consumable decision to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "adapter-decision-consumable boundary requires "
                               "accepted_for_future_execution")
               ? 0
               : 1;
}

int validate_durable_store_import_decision_rejects_rejected_without_failure_summary() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (built.has_errors() || !built.decision.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto decision = *built.decision;
    decision.workflow_failure_summary.reset();
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision(decision);
    if (!validation.has_errors()) {
        std::cerr << "expected rejected decision without failure summary to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "Rejected status requires workflow_failure_summary")
               ? 0
               : 1;
}

int build_durable_store_import_decision_ready_request() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Accepted ||
        !value.accepted_for_future_execution || value.decision_blocker.has_value() ||
        value.decision_boundary_kind !=
            ahfl::durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable ||
        value.durable_store_import_decision_identity !=
            "durable-store-import-decision::workflow-value-flow::run-partial-001::accepted") {
        std::cerr << "unexpected ready durable store import decision bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_decision_completed_request() {
    const auto request = make_request(make_completed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Accepted ||
        !value.accepted_for_future_execution || value.decision_blocker.has_value() ||
        value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::TerminalCompleted) {
        std::cerr << "unexpected completed durable store import decision bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_decision_rejects_invalid_request() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    auto invalid_request = *request;
    invalid_request.format_version = "ahfl.durable-store-import-request.v999";
    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(invalid_request);
    if (!decision.has_errors()) {
        std::cerr << "expected invalid durable store import request to fail\n";
        return 1;
    }

    return diagnostics_contain(decision.diagnostics,
                               "durable store import request format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_ok() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::ReadyForArchive ||
        !receipt->accepted_for_receipt_archive || receipt->receipt_blocker.has_value()) {
        std::cerr << "unexpected durable store import decision receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_blocked_ok() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Blocked ||
        receipt->accepted_for_receipt_archive || !receipt->receipt_blocker.has_value() ||
        receipt->receipt_blocker->kind !=
            ahfl::durable_store_import::ReceiptBlockerKind::MissingRequiredAdapterCapability ||
        receipt->next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext) {
        std::cerr << "unexpected blocked durable store import decision receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_deferred_ok() {
    const auto request = make_request(make_partial_terminal_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Deferred ||
        receipt->accepted_for_receipt_archive || !receipt->receipt_blocker.has_value() ||
        receipt->receipt_blocker->kind !=
            ahfl::durable_store_import::ReceiptBlockerKind::PartialWorkflowState ||
        receipt->next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState) {
        std::cerr << "unexpected deferred durable store import decision receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_rejected_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Rejected ||
        receipt->accepted_for_receipt_archive || !receipt->receipt_blocker.has_value() ||
        receipt->receipt_blocker->kind !=
            ahfl::durable_store_import::ReceiptBlockerKind::WorkflowFailure) {
        std::cerr << "unexpected rejected durable store import decision receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_rejects_missing_receipt_identity() {
    auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->durable_store_import_receipt_identity.clear();
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import receipt identity to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "durable_store_import_receipt_identity must not be empty")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_rejects_unsupported_source_decision_format() {
    auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->source_durable_store_import_decision_format_version =
        "ahfl.durable-store-import-decision.v999";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source decision format to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_decision_format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_rejects_ready_with_blocker() {
    auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->receipt_blocker = ahfl::durable_store_import::ReceiptBlocker{
        .kind = ahfl::durable_store_import::ReceiptBlockerKind::SourceDecisionBlocked,
        .message = "unexpected receipt blocker",
        .required_capability = std::nullopt,
    };
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected ready durable store import receipt with blocker to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "cannot contain receipt_blocker when accepted_for_receipt_archive is true")
               ? 0
               : 1;
}

int build_durable_store_import_receipt_ready_decision() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::ReadyForArchive ||
        !value.accepted_for_receipt_archive || value.receipt_blocker.has_value() ||
        value.receipt_boundary_kind !=
            ahfl::durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable ||
        value.durable_store_import_receipt_identity !=
            "durable-store-import-receipt::workflow-value-flow::run-partial-001::archive-ready") {
        std::cerr << "unexpected ready durable store import receipt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_blocked_decision() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Blocked ||
        value.accepted_for_receipt_archive || !value.receipt_blocker.has_value()) {
        std::cerr << "unexpected blocked durable store import receipt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_deferred_decision() {
    const auto request = make_request(make_partial_terminal_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Deferred ||
        value.accepted_for_receipt_archive || !value.receipt_blocker.has_value()) {
        std::cerr << "unexpected deferred durable store import receipt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_rejected_decision() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Rejected ||
        value.accepted_for_receipt_archive || !value.receipt_blocker.has_value()) {
        std::cerr << "unexpected rejected durable store import receipt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_rejects_invalid_decision() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    auto invalid_decision = *decision;
    invalid_decision.format_version = "ahfl.durable-store-import-decision.v999";
    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(invalid_decision);
    if (!receipt.has_errors()) {
        std::cerr << "expected invalid durable store import decision to fail\n";
        return 1;
    }

    return diagnostics_contain(receipt.diagnostics,
                               "durable store import decision format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_request_ok() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist ||
        !request->accepted_for_receipt_persistence ||
        request->receipt_persistence_blocker.has_value()) {
        std::cerr << "unexpected durable store import receipt persistence request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_request_blocked_ok() {
    const auto source_request = make_request(make_blocked_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = make_persistence_request_from_receipt(*receipt);
    if (!request.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            ReceiptPersistenceBlockerKind::MissingRequiredAdapterCapability ||
        request->next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext) {
        std::cerr << "unexpected blocked durable store import receipt persistence request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_request_deferred_ok() {
    const auto source_request = make_request(make_partial_terminal_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = make_persistence_request_from_receipt(*receipt);
    if (!request.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Deferred ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            ReceiptPersistenceBlockerKind::PartialWorkflowState ||
        request->next_required_adapter_capability !=
            ahfl::durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState) {
        std::cerr << "unexpected deferred durable store import receipt persistence request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_request_rejected_ok() {
    const auto source_request = make_request(make_failed_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = make_persistence_request_from_receipt(*receipt);
    if (!request.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            ReceiptPersistenceBlockerKind::WorkflowFailure) {
        std::cerr << "unexpected rejected durable store import receipt persistence request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_request_rejects_missing_request_identity() {
    auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    request->durable_store_import_receipt_persistence_request_identity.clear();
    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import receipt persistence request identity "
                     "to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "durable_store_import_receipt_persistence_request_identity must not be empty")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_request_rejects_unsupported_source_receipt_format() {
    auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    request->source_durable_store_import_decision_receipt_format_version =
        "ahfl.durable-store-import-decision-receipt.v999";
    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source receipt format to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "source_durable_store_import_decision_receipt_format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_request_rejects_ready_with_blocker() {
    auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    request->receipt_persistence_blocker =
        ahfl::durable_store_import::ReceiptPersistenceBlocker{
            .kind =
                ahfl::durable_store_import::ReceiptPersistenceBlockerKind::SourceReceiptBlocked,
            .message = "unexpected persistence blocker",
            .required_capability = std::nullopt,
        };
    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected ready durable store import receipt persistence request with blocker "
                     "to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "cannot contain receipt_persistence_blocker when accepted_for_receipt_persistence is true")
               ? 0
               : 1;
}

int build_durable_store_import_receipt_persistence_request_ready_receipt() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist ||
        !value.accepted_for_receipt_persistence ||
        value.receipt_persistence_blocker.has_value() ||
        value.receipt_persistence_boundary_kind != ahfl::durable_store_import::
            ReceiptPersistenceBoundaryKind::AdapterReceiptPersistenceConsumable ||
        value.durable_store_import_receipt_persistence_request_identity !=
            "durable-store-import-receipt-persistence-request::workflow-value-flow::run-partial-001::persistence-ready") {
        std::cerr << "unexpected ready durable store import receipt persistence request bootstrap "
                     "result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_request_blocked_receipt() {
    const auto source_request = make_request(make_blocked_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Blocked ||
        value.accepted_for_receipt_persistence ||
        !value.receipt_persistence_blocker.has_value()) {
        std::cerr << "unexpected blocked durable store import receipt persistence request bootstrap "
                     "result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_request_deferred_receipt() {
    const auto source_request = make_request(make_partial_terminal_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Deferred ||
        value.accepted_for_receipt_persistence ||
        !value.receipt_persistence_blocker.has_value()) {
        std::cerr << "unexpected deferred durable store import receipt persistence request bootstrap "
                     "result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_request_rejected_receipt() {
    const auto source_request = make_request(make_failed_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected ||
        value.accepted_for_receipt_persistence ||
        !value.receipt_persistence_blocker.has_value()) {
        std::cerr << "unexpected rejected durable store import receipt persistence request "
                     "bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_request_rejects_invalid_receipt() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    auto invalid_receipt = *receipt;
    invalid_receipt.format_version = "ahfl.durable-store-import-decision-receipt.v999";
    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(invalid_receipt);
    if (!request.has_errors()) {
        std::cerr << "expected invalid durable store import receipt to fail\n";
        return 1;
    }

    return diagnostics_contain(
               request.diagnostics,
               "durable store import decision receipt format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_review_ok() {
    const auto summary = make_valid_receipt_persistence_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_review_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (summary->receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist ||
        !summary->accepted_for_receipt_persistence ||
        summary->receipt_persistence_blocker.has_value()) {
        std::cerr << "unexpected durable store import receipt persistence review summary\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_review_rejects_unsupported_source_request_format() {
    auto summary = make_valid_receipt_persistence_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    summary->source_durable_store_import_decision_receipt_persistence_request_format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-request.v999";
    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_persistence_review_summary(*summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source persistence request format to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "source_durable_store_import_decision_receipt_persistence_request_format_version must be")
               ? 0
               : 1;
}

int build_durable_store_import_receipt_persistence_review_ready_request_ok() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::ReadyToPersist ||
        !value.accepted_for_receipt_persistence || value.receipt_persistence_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind::
                                     HandoffDurableStoreImportDecisionReceiptPersistenceRequest) {
        std::cerr << "unexpected ready durable store import receipt persistence review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_review_rejected_request_ok() {
    const auto source_request = make_request(make_failed_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_durable_store_import_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = make_persistence_request_from_receipt(*receipt);
    if (!request.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            DurableStoreImportDecisionReceiptPersistenceRequestStatus::Rejected ||
        value.accepted_for_receipt_persistence || !value.receipt_persistence_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 DurableStoreImportDecisionReceiptPersistenceReviewNextActionKind::
                                     InvestigateDurableStoreImportDecisionReceiptPersistenceRejection) {
        std::cerr << "unexpected rejected durable store import receipt persistence review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_review_rejects_invalid_request() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    auto invalid_request = *request;
    invalid_request.format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-request.v999";
    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_review_summary(invalid_request);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import receipt persistence request to fail\n";
        return 1;
    }

    return diagnostics_contain(
               summary.diagnostics,
               "durable store import receipt persistence request format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_review_ok() {
    const auto summary = make_valid_receipt_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_review_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_review_rejects_unsupported_source_receipt_format() {
    const auto summary = make_valid_receipt_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    auto invalid_summary = *summary;
    invalid_summary.source_durable_store_import_decision_receipt_format_version =
        "ahfl.durable-store-import-decision-receipt.v999";
    const auto validation = ahfl::durable_store_import::
        validate_durable_store_import_decision_receipt_review_summary(invalid_summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source receipt format to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "source_durable_store_import_decision_receipt_format_version must be")
               ? 0
               : 1;
}

int build_durable_store_import_receipt_review_ready_receipt_ok() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::ReadyForArchive ||
        !value.accepted_for_receipt_archive || value.receipt_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 DurableStoreImportDecisionReceiptReviewNextActionKind::
                                     HandoffDurableStoreImportDecisionReceipt) {
        std::cerr << "unexpected ready durable store import receipt review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_review_rejected_receipt_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionReceiptStatus::Rejected ||
        value.accepted_for_receipt_archive || !value.receipt_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 DurableStoreImportDecisionReceiptReviewNextActionKind::
                                     InvestigateDurableStoreImportDecisionReceiptRejection) {
        std::cerr << "unexpected rejected durable store import receipt review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_review_rejects_invalid_receipt() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    auto invalid_receipt = *receipt;
    invalid_receipt.format_version = "ahfl.durable-store-import-decision-receipt.v999";
    const auto summary = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_review_summary(invalid_receipt);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import receipt to fail\n";
        return 1;
    }

    return diagnostics_contain(summary.diagnostics,
                               "durable store import decision receipt format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_decision_review_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_review_summary(*decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_review_summary(
            *review.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_durable_store_import_decision_review_rejects_unsupported_source_decision_format() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_durable_store_import_decision_review_summary(*decision);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_durable_store_import_decision_format_version =
        "ahfl.durable-store-import-decision.v999";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_decision_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source decision format to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_decision_format_version must be")
               ? 0
               : 1;
}

int build_durable_store_import_decision_review_accepted_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_review_summary(*decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &summary = *review.summary;
    if (summary.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Accepted ||
        !summary.accepted_for_future_execution || summary.decision_blocker.has_value() ||
        summary.next_action != ahfl::durable_store_import::
                                   DurableStoreImportDecisionReviewNextActionKind::
                                       HandoffDurableStoreImportDecision) {
        std::cerr << "unexpected accepted durable store import decision review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_decision_review_rejected_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto review = ahfl::durable_store_import::build_durable_store_import_decision_review_summary(
        *decision.decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &summary = *review.summary;
    if (summary.decision_status !=
            ahfl::durable_store_import::DurableStoreImportDecisionStatus::Rejected ||
        summary.accepted_for_future_execution || !summary.decision_blocker.has_value() ||
        summary.next_action != ahfl::durable_store_import::
                                   DurableStoreImportDecisionReviewNextActionKind::
                                       InvestigateDurableStoreImportDecisionRejection) {
        std::cerr << "unexpected rejected durable store import decision review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_decision_review_rejects_invalid_decision() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    auto invalid_decision = *decision;
    invalid_decision.format_version = "ahfl.durable-store-import-decision.v999";
    const auto review = ahfl::durable_store_import::build_durable_store_import_decision_review_summary(
        invalid_decision);
    if (!review.has_errors()) {
        std::cerr << "expected invalid durable store import decision to fail\n";
        return 1;
    }

    return diagnostics_contain(review.diagnostics,
                               "durable store import decision format_version must be")
               ? 0
               : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "expected test command\n";
        return 1;
    }

    const std::string_view command = argv[1];

    if (command == "validate-durable-store-import-decision-ok") {
        return validate_durable_store_import_decision_ok();
    }

    if (command == "validate-durable-store-import-decision-blocked-ok") {
        return validate_durable_store_import_decision_blocked_ok();
    }

    if (command == "validate-durable-store-import-decision-deferred-ok") {
        return validate_durable_store_import_decision_deferred_ok();
    }

    if (command == "validate-durable-store-import-decision-rejected-ok") {
        return validate_durable_store_import_decision_rejected_ok();
    }

    if (command == "validate-durable-store-import-decision-rejects-missing-decision-identity") {
        return validate_durable_store_import_decision_rejects_missing_decision_identity();
    }

    if (command == "validate-durable-store-import-decision-rejects-accepted-with-blocker") {
        return validate_durable_store_import_decision_rejects_accepted_with_blocker();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-unsupported-source-request-format") {
        return validate_durable_store_import_decision_rejects_unsupported_source_request_format();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-adapter-decision-consumable-blocked") {
        return validate_durable_store_import_decision_rejects_adapter_decision_consumable_blocked();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-rejected-without-failure-summary") {
        return validate_durable_store_import_decision_rejects_rejected_without_failure_summary();
    }

    if (command == "build-durable-store-import-decision-ready-request") {
        return build_durable_store_import_decision_ready_request();
    }

    if (command == "build-durable-store-import-decision-completed-request") {
        return build_durable_store_import_decision_completed_request();
    }

    if (command == "build-durable-store-import-decision-rejects-invalid-request") {
        return build_durable_store_import_decision_rejects_invalid_request();
    }

    if (command == "validate-durable-store-import-receipt-ok") {
        return validate_durable_store_import_receipt_ok();
    }

    if (command == "validate-durable-store-import-receipt-blocked-ok") {
        return validate_durable_store_import_receipt_blocked_ok();
    }

    if (command == "validate-durable-store-import-receipt-deferred-ok") {
        return validate_durable_store_import_receipt_deferred_ok();
    }

    if (command == "validate-durable-store-import-receipt-rejected-ok") {
        return validate_durable_store_import_receipt_rejected_ok();
    }

    if (command ==
        "validate-durable-store-import-receipt-rejects-missing-receipt-identity") {
        return validate_durable_store_import_receipt_rejects_missing_receipt_identity();
    }

    if (command ==
        "validate-durable-store-import-receipt-rejects-unsupported-source-decision-format") {
        return validate_durable_store_import_receipt_rejects_unsupported_source_decision_format();
    }

    if (command == "validate-durable-store-import-receipt-rejects-ready-with-blocker") {
        return validate_durable_store_import_receipt_rejects_ready_with_blocker();
    }

    if (command == "build-durable-store-import-receipt-ready-decision") {
        return build_durable_store_import_receipt_ready_decision();
    }

    if (command == "build-durable-store-import-receipt-blocked-decision") {
        return build_durable_store_import_receipt_blocked_decision();
    }

    if (command == "build-durable-store-import-receipt-deferred-decision") {
        return build_durable_store_import_receipt_deferred_decision();
    }

    if (command == "build-durable-store-import-receipt-rejected-decision") {
        return build_durable_store_import_receipt_rejected_decision();
    }

    if (command == "build-durable-store-import-receipt-rejects-invalid-decision") {
        return build_durable_store_import_receipt_rejects_invalid_decision();
    }

    if (command == "validate-durable-store-import-receipt-persistence-request-ok") {
        return validate_durable_store_import_receipt_persistence_request_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-request-blocked-ok") {
        return validate_durable_store_import_receipt_persistence_request_blocked_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-request-deferred-ok") {
        return validate_durable_store_import_receipt_persistence_request_deferred_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-request-rejected-ok") {
        return validate_durable_store_import_receipt_persistence_request_rejected_ok();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-request-rejects-missing-request-identity") {
        return validate_durable_store_import_receipt_persistence_request_rejects_missing_request_identity();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-request-rejects-unsupported-source-receipt-format") {
        return validate_durable_store_import_receipt_persistence_request_rejects_unsupported_source_receipt_format();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-request-rejects-ready-with-blocker") {
        return validate_durable_store_import_receipt_persistence_request_rejects_ready_with_blocker();
    }

    if (command == "build-durable-store-import-receipt-persistence-request-ready-receipt") {
        return build_durable_store_import_receipt_persistence_request_ready_receipt();
    }

    if (command == "build-durable-store-import-receipt-persistence-request-blocked-receipt") {
        return build_durable_store_import_receipt_persistence_request_blocked_receipt();
    }

    if (command == "build-durable-store-import-receipt-persistence-request-deferred-receipt") {
        return build_durable_store_import_receipt_persistence_request_deferred_receipt();
    }

    if (command == "build-durable-store-import-receipt-persistence-request-rejected-receipt") {
        return build_durable_store_import_receipt_persistence_request_rejected_receipt();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-request-rejects-invalid-receipt") {
        return build_durable_store_import_receipt_persistence_request_rejects_invalid_receipt();
    }

    if (command == "validate-durable-store-import-receipt-persistence-review-ok") {
        return validate_durable_store_import_receipt_persistence_review_ok();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-review-rejects-unsupported-source-request-format") {
        return validate_durable_store_import_receipt_persistence_review_rejects_unsupported_source_request_format();
    }

    if (command == "build-durable-store-import-receipt-persistence-review-ready-request-ok") {
        return build_durable_store_import_receipt_persistence_review_ready_request_ok();
    }

    if (command == "build-durable-store-import-receipt-persistence-review-rejected-request-ok") {
        return build_durable_store_import_receipt_persistence_review_rejected_request_ok();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-review-rejects-invalid-request") {
        return build_durable_store_import_receipt_persistence_review_rejects_invalid_request();
    }

    if (command == "validate-durable-store-import-receipt-review-ok") {
        return validate_durable_store_import_receipt_review_ok();
    }

    if (command ==
        "validate-durable-store-import-receipt-review-rejects-unsupported-source-receipt-format") {
        return validate_durable_store_import_receipt_review_rejects_unsupported_source_receipt_format();
    }

    if (command == "build-durable-store-import-receipt-review-ready-receipt-ok") {
        return build_durable_store_import_receipt_review_ready_receipt_ok();
    }

    if (command == "build-durable-store-import-receipt-review-rejected-receipt-ok") {
        return build_durable_store_import_receipt_review_rejected_receipt_ok();
    }

    if (command == "build-durable-store-import-receipt-review-rejects-invalid-receipt") {
        return build_durable_store_import_receipt_review_rejects_invalid_receipt();
    }

    if (command == "validate-durable-store-import-decision-review-ok") {
        return validate_durable_store_import_decision_review_ok();
    }

    if (command ==
        "validate-durable-store-import-decision-review-rejects-unsupported-source-decision-format") {
        return validate_durable_store_import_decision_review_rejects_unsupported_source_decision_format();
    }

    if (command == "build-durable-store-import-decision-review-accepted-ok") {
        return build_durable_store_import_decision_review_accepted_ok();
    }

    if (command == "build-durable-store-import-decision-review-rejected-ok") {
        return build_durable_store_import_decision_review_rejected_ok();
    }

    if (command == "build-durable-store-import-decision-review-rejects-invalid-decision") {
        return build_durable_store_import_decision_review_rejects_invalid_decision();
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
