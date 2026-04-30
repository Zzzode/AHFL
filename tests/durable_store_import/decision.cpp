#include "ahfl/durable_store_import/adapter_execution.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/provider_adapter.hpp"
#include "ahfl/durable_store_import/provider_driver.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response_review.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/recovery_preview.hpp"
#include "ahfl/durable_store_import/request.hpp"

#include "../common/test_support.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

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

[[nodiscard]] std::optional<ahfl::durable_store_import::Request>
make_request(const ahfl::store_import::StoreImportDescriptor &descriptor) {
    const auto request = ahfl::durable_store_import::build_request(descriptor);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::Decision>
make_valid_decision() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::Receipt>
make_receipt_from_decision(
    const ahfl::durable_store_import::Decision &decision) {
    const auto receipt =
        ahfl::durable_store_import::build_receipt(decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::Receipt>
make_valid_receipt() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return std::nullopt;
    }

    return make_receipt_from_decision(*decision);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ReceiptReviewSummary>
make_valid_receipt_review_summary() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::
        build_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceRequest>
make_persistence_request_from_receipt(
    const ahfl::durable_store_import::Receipt &receipt) {
    const auto request = ahfl::durable_store_import::
        build_persistence_request(receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceRequest>
make_valid_receipt_persistence_request() {
    const auto receipt = make_valid_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    return make_persistence_request_from_receipt(*receipt);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceReviewSummary>
make_valid_receipt_persistence_review_summary() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::
        build_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

int validate_decision_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_decision(*decision);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_decision_blocked_ok() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Blocked ||
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

int validate_decision_deferred_ok() {
    const auto request = make_request(make_partial_terminal_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Deferred ||
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

int validate_decision_rejected_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Rejected ||
        value.accepted_for_future_execution || !value.decision_blocker.has_value() ||
        value.decision_blocker->kind !=
            ahfl::durable_store_import::DecisionBlockerKind::WorkflowFailure) {
        std::cerr << "unexpected rejected durable store import decision\n";
        return 1;
    }

    return 0;
}

int validate_decision_rejects_missing_decision_identity() {
    auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    decision->durable_store_import_decision_identity.clear();
    const auto validation =
        ahfl::durable_store_import::validate_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import decision identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "durable_store_import_decision_identity must not be empty")
               ? 0
               : 1;
}

int validate_decision_rejects_accepted_with_blocker() {
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
        ahfl::durable_store_import::validate_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected accepted decision with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "cannot contain decision_blocker when accepted_for_future_execution is true")
               ? 0
               : 1;
}

int validate_decision_rejects_unsupported_source_request_format() {
    auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    decision->source_durable_store_import_request_format_version =
        "ahfl.durable-store-import-request.v999";
    const auto validation =
        ahfl::durable_store_import::validate_decision(*decision);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source request format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_request_format_version must be")
               ? 0
               : 1;
}

int validate_decision_rejects_adapter_decision_consumable_blocked() {
    const auto request = make_request(make_blocked_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_decision(*request);
    if (built.has_errors() || !built.decision.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto decision = *built.decision;
    decision.decision_boundary_kind =
        ahfl::durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable;
    const auto validation =
        ahfl::durable_store_import::validate_decision(decision);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked adapter-decision-consumable decision to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "adapter-decision-consumable boundary requires "
                               "accepted_for_future_execution")
               ? 0
               : 1;
}

int validate_decision_rejects_rejected_without_failure_summary() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_decision(*request);
    if (built.has_errors() || !built.decision.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto decision = *built.decision;
    decision.workflow_failure_summary.reset();
    const auto validation =
        ahfl::durable_store_import::validate_decision(decision);
    if (!validation.has_errors()) {
        std::cerr << "expected rejected decision without failure summary to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "Rejected status requires workflow_failure_summary")
               ? 0
               : 1;
}

int build_decision_ready_request() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Accepted ||
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

int build_decision_completed_request() {
    const auto request = make_request(make_completed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *decision.decision;
    if (value.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Accepted ||
        !value.accepted_for_future_execution || value.decision_blocker.has_value() ||
        value.request_status !=
            ahfl::durable_store_import::RequestStatus::TerminalCompleted) {
        std::cerr << "unexpected completed durable store import decision bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_decision_rejects_invalid_request() {
    const auto request = make_request(make_ready_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    auto invalid_request = *request;
    invalid_request.format_version = "ahfl.durable-store-import-request.v999";
    const auto decision =
        ahfl::durable_store_import::build_decision(invalid_request);
    if (!decision.has_errors()) {
        std::cerr << "expected invalid durable store import request to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(decision.diagnostics,
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
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::ReadyForArchive ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Blocked ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Deferred ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Rejected ||
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
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import receipt identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
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
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source decision format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
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
        ahfl::durable_store_import::validate_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected ready durable store import receipt with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        ahfl::durable_store_import::build_receipt(*decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::ReadyForArchive ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Blocked ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Deferred ||
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_receipt(*decision.decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *receipt.receipt;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Rejected ||
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
        ahfl::durable_store_import::build_receipt(invalid_decision);
    if (!receipt.has_errors()) {
        std::cerr << "expected invalid durable store import decision to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(receipt.diagnostics,
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
        validate_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::ReadyToPersist ||
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
        ahfl::durable_store_import::build_decision(*source_request);
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
        validate_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Blocked ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            PersistenceBlockerKind::MissingRequiredAdapterCapability ||
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
        ahfl::durable_store_import::build_decision(*source_request);
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
        validate_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Deferred ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            PersistenceBlockerKind::PartialWorkflowState ||
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
        ahfl::durable_store_import::build_decision(*source_request);
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
        validate_persistence_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Rejected ||
        request->accepted_for_receipt_persistence ||
        !request->receipt_persistence_blocker.has_value() ||
        request->receipt_persistence_blocker->kind != ahfl::durable_store_import::
            PersistenceBlockerKind::WorkflowFailure) {
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
        validate_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import receipt persistence request identity "
                     "to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
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
        validate_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source receipt format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        ahfl::durable_store_import::PersistenceBlocker{
            .kind =
                ahfl::durable_store_import::PersistenceBlockerKind::SourceReceiptBlocked,
            .message = "unexpected persistence blocker",
            .required_capability = std::nullopt,
        };
    const auto validation = ahfl::durable_store_import::
        validate_persistence_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected ready durable store import receipt persistence request with blocker "
                     "to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        build_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::ReadyToPersist ||
        !value.accepted_for_receipt_persistence ||
        value.receipt_persistence_blocker.has_value() ||
        value.receipt_persistence_boundary_kind != ahfl::durable_store_import::
            PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable ||
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
        ahfl::durable_store_import::build_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Blocked ||
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
        ahfl::durable_store_import::build_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Deferred ||
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
        ahfl::durable_store_import::build_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto request = ahfl::durable_store_import::
        build_persistence_request(*receipt);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Rejected ||
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
        build_persistence_request(invalid_receipt);
    if (!request.has_errors()) {
        std::cerr << "expected invalid durable store import receipt to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        validate_persistence_review_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (summary->receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::ReadyToPersist ||
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
        validate_persistence_review_summary(*summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source persistence request format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        build_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::ReadyToPersist ||
        !value.accepted_for_receipt_persistence || value.receipt_persistence_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 PersistenceReviewNextActionKind::
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
        ahfl::durable_store_import::build_decision(*source_request);
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
        build_persistence_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_persistence_request_status != ahfl::durable_store_import::
            PersistenceRequestStatus::Rejected ||
        value.accepted_for_receipt_persistence || !value.receipt_persistence_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 PersistenceReviewNextActionKind::
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
        build_persistence_review_summary(invalid_request);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import receipt persistence request to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        validate_receipt_review_summary(*summary);
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
        validate_receipt_review_summary(invalid_summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source receipt format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
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
        build_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::ReadyForArchive ||
        !value.accepted_for_receipt_archive || value.receipt_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 ReceiptReviewNextActionKind::
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

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_receipt_review_summary(*receipt);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.receipt_status !=
            ahfl::durable_store_import::ReceiptStatus::Rejected ||
        value.accepted_for_receipt_archive || !value.receipt_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 ReceiptReviewNextActionKind::
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
        build_receipt_review_summary(invalid_receipt);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import receipt to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(summary.diagnostics,
                               "durable store import decision receipt format_version must be")
               ? 0
               : 1;
}

int validate_decision_review_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_decision_review_summary(*decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_decision_review_summary(
            *review.summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_decision_review_rejects_unsupported_source_decision_format() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto built =
        ahfl::durable_store_import::build_decision_review_summary(*decision);
    if (built.has_errors() || !built.summary.has_value()) {
        built.diagnostics.render(std::cout);
        return 1;
    }

    auto summary = *built.summary;
    summary.source_durable_store_import_decision_format_version =
        "ahfl.durable-store-import-decision.v999";
    const auto validation =
        ahfl::durable_store_import::validate_decision_review_summary(summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source decision format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_decision_format_version must be")
               ? 0
               : 1;
}

int build_decision_review_accepted_ok() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_decision_review_summary(*decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &summary = *review.summary;
    if (summary.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Accepted ||
        !summary.accepted_for_future_execution || summary.decision_blocker.has_value() ||
        summary.next_action != ahfl::durable_store_import::
                                   DecisionReviewNextActionKind::
                                       HandoffDurableStoreImportDecision) {
        std::cerr << "unexpected accepted durable store import decision review\n";
        return 1;
    }

    return 0;
}

int build_decision_review_rejected_ok() {
    const auto request = make_request(make_failed_descriptor());
    if (!request.has_value()) {
        return 1;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return 1;
    }

    const auto review = ahfl::durable_store_import::build_decision_review_summary(
        *decision.decision);
    if (review.has_errors() || !review.summary.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    const auto &summary = *review.summary;
    if (summary.decision_status !=
            ahfl::durable_store_import::DecisionStatus::Rejected ||
        summary.accepted_for_future_execution || !summary.decision_blocker.has_value() ||
        summary.next_action != ahfl::durable_store_import::
                                   DecisionReviewNextActionKind::
                                       InvestigateDurableStoreImportDecisionRejection) {
        std::cerr << "unexpected rejected durable store import decision review\n";
        return 1;
    }

    return 0;
}

int build_decision_review_rejects_invalid_decision() {
    const auto decision = make_valid_decision();
    if (!decision.has_value()) {
        return 1;
    }

    auto invalid_decision = *decision;
    invalid_decision.format_version = "ahfl.durable-store-import-decision.v999";
    const auto review = ahfl::durable_store_import::build_decision_review_summary(
        invalid_decision);
    if (!review.has_errors()) {
        std::cerr << "expected invalid durable store import decision to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(review.diagnostics,
                               "durable store import decision format_version must be")
               ? 0
               : 1;
}

// V0.19: Receipt Persistence Response tests

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceResponse>
make_response_from_persistence_request(
    const ahfl::durable_store_import::PersistenceRequest &request) {
    const auto response = ahfl::durable_store_import::
        build_persistence_response(request);
    if (response.has_errors() || !response.response.has_value()) {
        response.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *response.response;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceResponse>
make_valid_receipt_persistence_response() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return std::nullopt;
    }

    return make_response_from_persistence_request(*request);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceResponseReviewSummary>
make_valid_receipt_persistence_response_review_summary() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::
        build_persistence_response_review(*response);
    if (summary.has_errors() || !summary.review.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.review;
}

int validate_durable_store_import_receipt_persistence_response_ok() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (response->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Accepted ||
        !response->acknowledged_for_response ||
        response->response_blocker.has_value()) {
        std::cerr << "unexpected durable store import receipt persistence response\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_response_blocked_ok() {
    const auto source_request = make_request(make_blocked_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_decision(*source_request);
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

    const auto response = make_response_from_persistence_request(*request);
    if (!response.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (response->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Blocked ||
        response->acknowledged_for_response ||
        !response->response_blocker.has_value() ||
        response->response_blocker->kind != ahfl::durable_store_import::
            PersistenceResponseBlockerKind::MissingRequiredAdapterCapability) {
        std::cerr << "unexpected blocked durable store import receipt persistence response\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_response_deferred_ok() {
    const auto source_request = make_request(make_partial_terminal_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_decision(*source_request);
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

    const auto response = make_response_from_persistence_request(*request);
    if (!response.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (response->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Deferred ||
        response->acknowledged_for_response ||
        !response->response_blocker.has_value() ||
        response->response_blocker->kind != ahfl::durable_store_import::
            PersistenceResponseBlockerKind::PartialPersistenceState) {
        std::cerr << "unexpected deferred durable store import receipt persistence response\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_response_rejected_ok() {
    const auto source_request = make_request(make_failed_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_decision(*source_request);
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

    const auto response = make_response_from_persistence_request(*request);
    if (!response.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (response->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Rejected ||
        response->acknowledged_for_response ||
        !response->response_blocker.has_value() ||
        response->response_blocker->kind != ahfl::durable_store_import::
            PersistenceResponseBlockerKind::PersistenceWorkflowFailure) {
        std::cerr << "unexpected rejected durable store import receipt persistence response\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_receipt_persistence_response_rejects_missing_response_identity() {
    auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    response->durable_store_import_receipt_persistence_response_identity.clear();
    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import receipt persistence response identity "
                     "to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                               "durable_store_import_receipt_persistence_response_identity must not be empty")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_response_rejects_unsupported_source_request_format() {
    auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    response->source_durable_store_import_decision_receipt_persistence_request_format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-request.v999";
    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source persistence request format to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "source_durable_store_import_decision_receipt_persistence_request_format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_response_rejects_accepted_with_blocker() {
    auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    response->response_blocker = ahfl::durable_store_import::PersistenceResponseBlocker{
        .kind = ahfl::durable_store_import::PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked,
        .message = "unexpected response blocker",
        .required_capability = std::nullopt,
    };
    const auto validation = ahfl::durable_store_import::
        validate_persistence_response(*response);
    if (!validation.has_errors()) {
        std::cerr << "expected accepted response with blocker to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "cannot contain response_blocker when acknowledged_for_response is true")
               ? 0
               : 1;
}

int build_durable_store_import_receipt_persistence_response_ready_request() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto response = ahfl::durable_store_import::
        build_persistence_response(*request);
    if (response.has_errors() || !response.response.has_value()) {
        response.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *response.response;
    if (value.response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Accepted ||
        !value.acknowledged_for_response ||
        value.response_blocker.has_value() ||
        value.receipt_persistence_response_boundary_kind != ahfl::durable_store_import::
            PersistenceResponseBoundaryKind::AdapterResponseConsumable ||
        value.durable_store_import_receipt_persistence_response_identity !=
            "durable-store-import-receipt-persistence-response::workflow-value-flow::run-partial-001::accepted") {
        std::cerr << "unexpected ready durable store import receipt persistence response bootstrap "
                     "result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_response_rejects_invalid_request() {
    const auto request = make_valid_receipt_persistence_request();
    if (!request.has_value()) {
        return 1;
    }

    auto invalid_request = *request;
    invalid_request.format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-request.v999";
    const auto response = ahfl::durable_store_import::
        build_persistence_response(invalid_request);
    if (!response.has_errors()) {
        std::cerr << "expected invalid durable store import receipt persistence request to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               response.diagnostics,
               "durable store import receipt persistence request format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_receipt_persistence_response_review_ok() {
    const auto summary = make_valid_receipt_persistence_response_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::
        validate_persistence_response_review(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (summary->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Accepted ||
        !summary->acknowledged_for_response ||
        summary->response_blocker.has_value()) {
        std::cerr << "unexpected durable store import receipt persistence response review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_response_review_ready_response_ok() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_persistence_response_review(*response);
    if (summary.has_errors() || !summary.review.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.review;
    if (value.response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Accepted ||
        !value.acknowledged_for_response || value.response_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 PersistenceResponseReviewNextActionKind::AcknowledgeResponse) {
        std::cerr << "unexpected ready durable store import receipt persistence response review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_response_review_rejected_response_ok() {
    const auto source_request = make_request(make_failed_descriptor());
    if (!source_request.has_value()) {
        return 1;
    }

    const auto decision =
        ahfl::durable_store_import::build_decision(*source_request);
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

    const auto response = make_response_from_persistence_request(*request);
    if (!response.has_value()) {
        return 1;
    }

    const auto summary = ahfl::durable_store_import::
        build_persistence_response_review(*response);
    if (summary.has_errors() || !summary.review.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.review;
    if (value.response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Rejected ||
        value.acknowledged_for_response || !value.response_blocker.has_value() ||
        value.next_action != ahfl::durable_store_import::
                                 PersistenceResponseReviewNextActionKind::ReviewFailure) {
        std::cerr << "unexpected rejected durable store import receipt persistence response review summary\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_receipt_persistence_response_review_rejects_invalid_response() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    auto invalid_response = *response;
    invalid_response.format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-response.v999";
    const auto summary = ahfl::durable_store_import::
        build_persistence_response_review(invalid_response);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import receipt persistence response to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               summary.diagnostics,
               "durable store import receipt persistence response format_version must be")
               ? 0
               : 1;
}

// V0.20: Adapter Execution Receipt and Recovery Preview tests

[[nodiscard]] std::optional<
    ahfl::durable_store_import::PersistenceResponse>
make_response_from_descriptor(
    const ahfl::store_import::StoreImportDescriptor &descriptor) {
    const auto source_request = make_request(descriptor);
    if (!source_request.has_value()) {
        return std::nullopt;
    }

    const auto decision =
        ahfl::durable_store_import::build_decision(*source_request);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto receipt = make_receipt_from_decision(*decision.decision);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto request = make_persistence_request_from_receipt(*receipt);
    if (!request.has_value()) {
        return std::nullopt;
    }

    return make_response_from_persistence_request(*request);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::AdapterExecutionReceipt>
make_adapter_execution_from_response(
    const ahfl::durable_store_import::PersistenceResponse &response) {
    const auto execution =
        ahfl::durable_store_import::build_adapter_execution_receipt(response);
    if (execution.has_errors() || !execution.receipt.has_value()) {
        execution.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *execution.receipt;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::AdapterExecutionReceipt>
make_valid_adapter_execution_receipt() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return std::nullopt;
    }

    return make_adapter_execution_from_response(*response);
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::RecoveryCommandPreview>
make_valid_recovery_command_preview() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return std::nullopt;
    }

    const auto preview =
        ahfl::durable_store_import::build_recovery_command_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *preview.preview;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderWriteAttemptPreview>
make_valid_provider_write_attempt_preview() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return std::nullopt;
    }

    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *preview.preview;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderRecoveryHandoffPreview>
make_valid_provider_recovery_handoff_preview() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return std::nullopt;
    }

    const auto handoff =
        ahfl::durable_store_import::build_provider_recovery_handoff_preview(*write_attempt);
    if (handoff.has_errors() || !handoff.preview.has_value()) {
        handoff.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *handoff.preview;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderDriverBindingPlan>
make_valid_provider_driver_binding_plan() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderDriverReadinessReview>
make_valid_provider_driver_readiness_review() {
    const auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_driver_readiness_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

int validate_durable_store_import_adapter_execution_ok() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (execution->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Accepted ||
        execution->mutation_status != ahfl::durable_store_import::StoreMutationStatus::Persisted ||
        !execution->persistence_id.has_value() ||
        execution->failure_attribution.has_value()) {
        std::cerr << "unexpected durable store import adapter execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_adapter_execution_blocked_ok() {
    const auto response = make_response_from_descriptor(make_blocked_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (execution->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Blocked ||
        execution->mutation_status != ahfl::durable_store_import::StoreMutationStatus::NotMutated ||
        execution->persistence_id.has_value() ||
        !execution->failure_attribution.has_value() ||
        execution->failure_attribution->kind != ahfl::durable_store_import::
                                                AdapterExecutionFailureKind::SourceResponseBlocked) {
        std::cerr << "unexpected blocked durable store import adapter execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_adapter_execution_deferred_ok() {
    const auto response = make_response_from_descriptor(make_partial_terminal_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (execution->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Deferred ||
        execution->mutation_intent.kind !=
            ahfl::durable_store_import::StoreMutationIntentKind::NoopDeferred ||
        execution->persistence_id.has_value() ||
        !execution->failure_attribution.has_value() ||
        execution->failure_attribution->kind != ahfl::durable_store_import::
                                                AdapterExecutionFailureKind::SourceResponseDeferred) {
        std::cerr << "unexpected deferred durable store import adapter execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_adapter_execution_rejected_ok() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (execution->response_status != ahfl::durable_store_import::
            PersistenceResponseStatus::Rejected ||
        execution->mutation_intent.kind !=
            ahfl::durable_store_import::StoreMutationIntentKind::NoopRejected ||
        execution->persistence_id.has_value() ||
        !execution->failure_attribution.has_value() ||
        execution->failure_attribution->kind != ahfl::durable_store_import::
                                                AdapterExecutionFailureKind::SourceResponseRejected) {
        std::cerr << "unexpected rejected durable store import adapter execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_adapter_execution_rejects_missing_persistence_id() {
    auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    execution->persistence_id.reset();
    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (!validation.has_errors()) {
        std::cerr << "expected accepted adapter execution without persistence id to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "Accepted status requires persistence_id")
               ? 0
               : 1;
}

int validate_durable_store_import_adapter_execution_rejects_non_mutating_accepted() {
    auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    execution->mutation_intent.mutates_store = false;
    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (!validation.has_errors()) {
        std::cerr << "expected non-mutating accepted adapter execution to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "Accepted status requires mutating PersistReceipt")
               ? 0
               : 1;
}

int validate_durable_store_import_adapter_execution_rejects_mutated_non_accepted() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    execution->mutation_intent.mutates_store = true;
    const auto validation =
        ahfl::durable_store_import::validate_adapter_execution_receipt(*execution);
    if (!validation.has_errors()) {
        std::cerr << "expected mutated rejected adapter execution to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "non-accepted status requires non-mutating mutation_intent")
               ? 0
               : 1;
}

int build_durable_store_import_adapter_execution_ready_response() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    const auto execution =
        ahfl::durable_store_import::build_adapter_execution_receipt(*response);
    if (execution.has_errors() || !execution.receipt.has_value()) {
        execution.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *execution.receipt;
    if (value.durable_store_import_adapter_execution_identity !=
            "durable-store-import-adapter-execution::workflow-value-flow::run-partial-001::accepted" ||
        value.persistence_id != std::optional<std::string>(
                                    "fake-persistence::workflow-value-flow::run-partial-001::accepted") ||
        value.mutation_status != ahfl::durable_store_import::StoreMutationStatus::Persisted) {
        std::cerr << "unexpected ready adapter execution bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_adapter_execution_rejects_invalid_response() {
    const auto response = make_valid_receipt_persistence_response();
    if (!response.has_value()) {
        return 1;
    }

    auto invalid_response = *response;
    invalid_response.format_version =
        "ahfl.durable-store-import-decision-receipt-persistence-response.v999";
    const auto execution =
        ahfl::durable_store_import::build_adapter_execution_receipt(invalid_response);
    if (!execution.has_errors()) {
        std::cerr << "expected invalid response to fail adapter execution bootstrap\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               execution.diagnostics,
               "durable store import receipt persistence response format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_recovery_preview_ok() {
    const auto preview = make_valid_recovery_command_preview();
    if (!preview.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_recovery_command_preview(*preview);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (preview->next_action != ahfl::durable_store_import::
                                    RecoveryPreviewNextActionKind::NoRecoveryRequired ||
        !preview->persistence_id.has_value() ||
        preview->failure_attribution.has_value()) {
        std::cerr << "unexpected durable store import recovery preview\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_recovery_preview_ready_execution_ok() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    const auto preview =
        ahfl::durable_store_import::build_recovery_command_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    if (preview.preview->next_action != ahfl::durable_store_import::
                                           RecoveryPreviewNextActionKind::NoRecoveryRequired ||
        preview.preview->execution_summary.persistence_id != execution->persistence_id) {
        std::cerr << "unexpected accepted recovery preview bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_recovery_preview_rejected_execution_ok() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto preview =
        ahfl::durable_store_import::build_recovery_command_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    if (preview.preview->next_action !=
            ahfl::durable_store_import::RecoveryPreviewNextActionKind::ReviewFailure ||
        preview.preview->persistence_id.has_value() ||
        !preview.preview->failure_attribution.has_value()) {
        std::cerr << "unexpected rejected recovery preview bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_recovery_preview_rejects_invalid_execution() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    auto invalid_execution = *execution;
    invalid_execution.format_version = "ahfl.durable-store-import-adapter-execution.v999";
    const auto preview =
        ahfl::durable_store_import::build_recovery_command_preview(invalid_execution);
    if (!preview.has_errors()) {
        std::cerr << "expected invalid adapter execution to fail recovery preview bootstrap\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               preview.diagnostics,
               "durable store import adapter execution format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_write_attempt_ok() {
    const auto preview = make_valid_provider_write_attempt_preview();
    if (!preview.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_attempt_preview(*preview);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (preview->planning_status !=
            ahfl::durable_store_import::ProviderWritePlanningStatus::Planned ||
        preview->write_intent.kind !=
            ahfl::durable_store_import::ProviderWriteIntentKind::ProviderPersistReceipt ||
        !preview->write_intent.mutates_provider ||
        !preview->write_intent.provider_persistence_id.has_value() ||
        preview->failure_attribution.has_value()) {
        std::cerr << "unexpected provider write attempt preview\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_write_attempt_rejected_ok() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    if (preview.preview->planning_status !=
            ahfl::durable_store_import::ProviderWritePlanningStatus::NotPlanned ||
        preview.preview->write_intent.kind !=
            ahfl::durable_store_import::ProviderWriteIntentKind::NoopRejected ||
        preview.preview->write_intent.mutates_provider ||
        preview.preview->write_intent.provider_persistence_id.has_value() ||
        !preview.preview->failure_attribution.has_value()) {
        std::cerr << "unexpected rejected provider write attempt preview\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_write_attempt_unsupported_capability_ok() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    const auto config =
        ahfl::durable_store_import::build_default_provider_adapter_config(*execution);
    auto matrix = ahfl::durable_store_import::build_default_provider_capability_matrix(config);
    matrix.supports_provider_write = false;

    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution, config, matrix);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    if (preview.preview->planning_status !=
            ahfl::durable_store_import::ProviderWritePlanningStatus::NotPlanned ||
        preview.preview->write_intent.kind !=
            ahfl::durable_store_import::ProviderWriteIntentKind::NoopUnsupportedCapability ||
        !preview.preview->failure_attribution.has_value() ||
        preview.preview->failure_attribution->kind !=
            ahfl::durable_store_import::ProviderPlanningFailureKind::UnsupportedProviderCapability ||
        preview.preview->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderCapabilityKind>(
                ahfl::durable_store_import::ProviderCapabilityKind::PlanProviderWrite)) {
        std::cerr << "unexpected unsupported-capability provider write attempt preview\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_adapter_config_rejects_secret_material() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    auto config =
        ahfl::durable_store_import::build_default_provider_adapter_config(*execution);
    config.secret_material_reference = "secret://provider-token";
    const auto validation = ahfl::durable_store_import::validate_provider_adapter_config(config);
    if (!validation.has_errors()) {
        std::cerr << "expected provider config with secret material to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "cannot contain secret_material_reference")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_adapter_config_rejects_provider_coordinates() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    auto config =
        ahfl::durable_store_import::build_default_provider_adapter_config(*execution);
    config.credential_free = false;
    config.object_path = "s3://real-bucket/receipt.json";
    config.database_key = "real-table-key";
    const auto validation = ahfl::durable_store_import::validate_provider_adapter_config(config);
    if (!validation.has_errors()) {
        std::cerr << "expected provider config with real provider coordinates to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "must be credential_free") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider database_key")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_write_attempt_rejects_planned_without_provider_id() {
    auto preview = make_valid_provider_write_attempt_preview();
    if (!preview.has_value()) {
        return 1;
    }

    preview->write_intent.provider_persistence_id.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_write_attempt_preview(*preview);
    if (!validation.has_errors()) {
        std::cerr << "expected planned provider write attempt without provider id to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "planned status requires persistence ids")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_write_attempt_rejects_not_planned_mutating_intent() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    preview.preview->write_intent.kind =
        ahfl::durable_store_import::ProviderWriteIntentKind::ProviderPersistReceipt;
    preview.preview->write_intent.mutates_provider = true;
    preview.preview->write_intent.provider_persistence_id = "provider-persistence::unexpected";
    const auto validation =
        ahfl::durable_store_import::validate_provider_write_attempt_preview(*preview.preview);
    if (!validation.has_errors()) {
        std::cerr << "expected not-planned provider write attempt with mutating intent to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "not-planned status requires non-mutating")
               ? 0
               : 1;
}

int build_durable_store_import_provider_write_attempt_ready_execution() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    if (preview.has_errors() || !preview.preview.has_value()) {
        preview.diagnostics.render(std::cout);
        return 1;
    }

    if (preview.preview->durable_store_import_provider_write_attempt_identity !=
            "durable-store-import-provider-write-attempt::workflow-value-flow::run-partial-001::accepted" ||
        preview.preview->write_intent.provider_persistence_id !=
            std::optional<std::string>(
                "provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        !preview.preview->retry_resume_placeholder.retry_placeholder_available ||
        !preview.preview->retry_resume_placeholder.resume_placeholder_available) {
        std::cerr << "unexpected ready provider write attempt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_write_attempt_rejects_invalid_execution() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    auto invalid_execution = *execution;
    invalid_execution.format_version = "ahfl.durable-store-import-adapter-execution.v999";
    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(invalid_execution);
    if (!preview.has_errors()) {
        std::cerr << "expected invalid adapter execution to fail provider write attempt\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               preview.diagnostics,
               "durable store import adapter execution format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_recovery_handoff_ok() {
    const auto handoff = make_valid_provider_recovery_handoff_preview();
    if (!handoff.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_recovery_handoff_preview(*handoff);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (handoff->next_action !=
            ahfl::durable_store_import::ProviderRecoveryHandoffNextActionKind::
                NoRecoveryRequired ||
        !handoff->provider_persistence_id.has_value() ||
        handoff->failure_attribution.has_value()) {
        std::cerr << "unexpected provider recovery handoff preview\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_recovery_handoff_unsupported_capability_ok() {
    const auto execution = make_valid_adapter_execution_receipt();
    if (!execution.has_value()) {
        return 1;
    }

    const auto config =
        ahfl::durable_store_import::build_default_provider_adapter_config(*execution);
    auto matrix = ahfl::durable_store_import::build_default_provider_capability_matrix(config);
    matrix.supports_provider_write = false;
    const auto write_attempt =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution, config, matrix);
    if (write_attempt.has_errors() || !write_attempt.preview.has_value()) {
        write_attempt.diagnostics.render(std::cout);
        return 1;
    }

    const auto handoff = ahfl::durable_store_import::build_provider_recovery_handoff_preview(
        *write_attempt.preview);
    if (handoff.has_errors() || !handoff.preview.has_value()) {
        handoff.diagnostics.render(std::cout);
        return 1;
    }

    if (handoff.preview->next_action !=
            ahfl::durable_store_import::ProviderRecoveryHandoffNextActionKind::
                RetryUnavailable ||
        handoff.preview->provider_persistence_id.has_value() ||
        !handoff.preview->failure_attribution.has_value()) {
        std::cerr << "unexpected unsupported provider recovery handoff preview\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_recovery_handoff_rejects_invalid_write_attempt() {
    auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    write_attempt->format_version =
        "ahfl.durable-store-import-provider-write-attempt-preview.v999";
    const auto handoff =
        ahfl::durable_store_import::build_provider_recovery_handoff_preview(*write_attempt);
    if (!handoff.has_errors()) {
        std::cerr << "expected invalid provider write attempt to fail recovery handoff\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               handoff.diagnostics,
               "durable store import provider write attempt preview format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_binding_ok() {
    const auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_binding_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->binding_status !=
            ahfl::durable_store_import::ProviderDriverBindingStatus::Bound ||
        plan->operation_kind != ahfl::durable_store_import::
                                    ProviderDriverOperationKind::TranslateProviderPersistReceipt ||
        !plan->operation_descriptor_identity.has_value() || plan->invokes_provider_sdk ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider driver binding plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_driver_binding_rejected_ok() {
    const auto response = make_response_from_descriptor(make_failed_descriptor());
    if (!response.has_value()) {
        return 1;
    }

    const auto execution = make_adapter_execution_from_response(*response);
    if (!execution.has_value()) {
        return 1;
    }

    const auto write_attempt =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    if (write_attempt.has_errors() || !write_attempt.preview.has_value()) {
        write_attempt.diagnostics.render(std::cout);
        return 1;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt.preview);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->binding_status !=
            ahfl::durable_store_import::ProviderDriverBindingStatus::NotBound ||
        plan.plan->operation_kind != ahfl::durable_store_import::
                                      ProviderDriverOperationKind::NoopSourceNotPlanned ||
        plan.plan->operation_descriptor_identity.has_value() ||
        !plan.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected rejected provider driver binding plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_binding_unsupported_capability_ok() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.supports_write_intent_translation = false;
    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt, profile);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->binding_status !=
            ahfl::durable_store_import::ProviderDriverBindingStatus::NotBound ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderDriverOperationKind::
                NoopUnsupportedDriverCapability ||
        !plan.plan->failure_attribution.has_value() ||
        plan.plan->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderDriverCapabilityKind>(
                ahfl::durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent)) {
        std::cerr << "unexpected unsupported-capability provider driver binding plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_binding_missing_profile_load_capability_ok() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.supports_secret_free_profile_load = false;
    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt, profile);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->binding_status !=
            ahfl::durable_store_import::ProviderDriverBindingStatus::NotBound ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderDriverOperationKind::
                NoopUnsupportedDriverCapability ||
        !plan.plan->failure_attribution.has_value() ||
        plan.plan->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderDriverCapabilityKind>(
                ahfl::durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile)) {
        std::cerr
            << "unexpected missing-profile-load provider driver binding plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_binding_profile_mismatch_ok() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.provider_profile_ref = "provider-profile::wrong";
    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt, profile);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->binding_status !=
            ahfl::durable_store_import::ProviderDriverBindingStatus::NotBound ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch ||
        !plan.plan->failure_attribution.has_value() ||
        plan.plan->failure_attribution->kind !=
            ahfl::durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch) {
        std::cerr << "unexpected provider driver profile mismatch binding plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_driver_profile_rejects_secret_material() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.credential_reference = "secret://provider-credential";
    profile.endpoint_secret_reference = "secret://endpoint";
    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_profile(profile);
    if (!validation.has_errors()) {
        std::cerr << "expected provider driver profile with secrets to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain credential_reference") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot contain endpoint_secret_reference")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_profile_rejects_provider_coordinates() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.credential_free = false;
    profile.endpoint_uri = "https://provider.example.invalid";
    profile.object_path = "bucket/receipt.json";
    profile.database_table = "receipt-table";
    profile.sdk_payload_schema = "provider-sdk-payload-v1";
    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_profile(profile);
    if (!validation.has_errors()) {
        std::cerr << "expected provider driver profile with provider coordinates to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "must be credential_free") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain database_table") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_payload_schema")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_binding_rejects_sdk_invocation() {
    auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->invokes_provider_sdk = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_binding_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider driver binding plan invoking SDK to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot invoke provider SDK")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_binding_rejects_bound_without_descriptor() {
    auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->operation_descriptor_identity.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_binding_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected bound provider driver binding without descriptor to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "bound status requires translation operation descriptor")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_binding_rejects_bound_without_profile_load() {
    auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->driver_profile.supports_secret_free_profile_load = false;
    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_binding_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected bound provider driver binding without profile load capability to "
                     "fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "requires secret-free profile load capability")
               ? 0
               : 1;
}

int build_durable_store_import_provider_driver_binding_ready_write_attempt() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->durable_store_import_provider_driver_binding_identity !=
            "durable-store-import-provider-driver-binding::run-partial-001::planned" ||
        plan.plan->operation_descriptor_identity !=
            std::optional<std::string>(
                "provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        plan.plan->driver_profile.secret_free_config_ref.empty()) {
        std::cerr << "unexpected ready provider driver binding bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_binding_rejects_invalid_write_attempt() {
    auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    write_attempt->format_version =
        "ahfl.durable-store-import-provider-write-attempt-preview.v999";
    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt);
    if (!plan.has_errors()) {
        std::cerr << "expected invalid provider write attempt to fail driver binding\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               plan.diagnostics,
               "durable store import provider write attempt preview format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_driver_readiness_ok() {
    const auto review = make_valid_provider_driver_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_driver_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderDriverReadinessNextActionKind::
                ReadyForDriverImplementation ||
        !review->operation_descriptor_identity.has_value() || review->invokes_provider_sdk ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider driver readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_readiness_unsupported_capability_ok() {
    const auto write_attempt = make_valid_provider_write_attempt_preview();
    if (!write_attempt.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_driver_profile(*write_attempt);
    profile.supports_write_intent_translation = false;
    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt, profile);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_driver_readiness_review(*plan.plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    if (review.review->next_action !=
            ahfl::durable_store_import::ProviderDriverReadinessNextActionKind::
                WaitForDriverCapability ||
        review.review->operation_descriptor_identity.has_value() ||
        !review.review->failure_attribution.has_value()) {
        std::cerr << "unexpected unsupported provider driver readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_driver_readiness_rejects_invalid_binding_plan() {
    auto plan = make_valid_provider_driver_binding_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->format_version = "ahfl.durable-store-import-provider-driver-binding-plan.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_driver_readiness_review(*plan);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider driver binding plan to fail readiness review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider driver binding plan format_version must be")
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
        return validate_decision_ok();
    }

    if (command == "validate-durable-store-import-decision-blocked-ok") {
        return validate_decision_blocked_ok();
    }

    if (command == "validate-durable-store-import-decision-deferred-ok") {
        return validate_decision_deferred_ok();
    }

    if (command == "validate-durable-store-import-decision-rejected-ok") {
        return validate_decision_rejected_ok();
    }

    if (command == "validate-durable-store-import-decision-rejects-missing-decision-identity") {
        return validate_decision_rejects_missing_decision_identity();
    }

    if (command == "validate-durable-store-import-decision-rejects-accepted-with-blocker") {
        return validate_decision_rejects_accepted_with_blocker();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-unsupported-source-request-format") {
        return validate_decision_rejects_unsupported_source_request_format();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-adapter-decision-consumable-blocked") {
        return validate_decision_rejects_adapter_decision_consumable_blocked();
    }

    if (command ==
        "validate-durable-store-import-decision-rejects-rejected-without-failure-summary") {
        return validate_decision_rejects_rejected_without_failure_summary();
    }

    if (command == "build-durable-store-import-decision-ready-request") {
        return build_decision_ready_request();
    }

    if (command == "build-durable-store-import-decision-completed-request") {
        return build_decision_completed_request();
    }

    if (command == "build-durable-store-import-decision-rejects-invalid-request") {
        return build_decision_rejects_invalid_request();
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
        return validate_decision_review_ok();
    }

    if (command ==
        "validate-durable-store-import-decision-review-rejects-unsupported-source-decision-format") {
        return validate_decision_review_rejects_unsupported_source_decision_format();
    }

    if (command == "build-durable-store-import-decision-review-accepted-ok") {
        return build_decision_review_accepted_ok();
    }

    if (command == "build-durable-store-import-decision-review-rejected-ok") {
        return build_decision_review_rejected_ok();
    }

    if (command == "build-durable-store-import-decision-review-rejects-invalid-decision") {
        return build_decision_review_rejects_invalid_decision();
    }

    // V0.19: Receipt Persistence Response tests
    if (command == "validate-durable-store-import-receipt-persistence-response-ok") {
        return validate_durable_store_import_receipt_persistence_response_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-response-blocked-ok") {
        return validate_durable_store_import_receipt_persistence_response_blocked_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-response-deferred-ok") {
        return validate_durable_store_import_receipt_persistence_response_deferred_ok();
    }

    if (command == "validate-durable-store-import-receipt-persistence-response-rejected-ok") {
        return validate_durable_store_import_receipt_persistence_response_rejected_ok();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-response-rejects-missing-response-identity") {
        return validate_durable_store_import_receipt_persistence_response_rejects_missing_response_identity();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-response-rejects-unsupported-source-request-format") {
        return validate_durable_store_import_receipt_persistence_response_rejects_unsupported_source_request_format();
    }

    if (command ==
        "validate-durable-store-import-receipt-persistence-response-rejects-accepted-with-blocker") {
        return validate_durable_store_import_receipt_persistence_response_rejects_accepted_with_blocker();
    }

    if (command == "build-durable-store-import-receipt-persistence-response-ready-request") {
        return build_durable_store_import_receipt_persistence_response_ready_request();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-response-rejects-invalid-request") {
        return build_durable_store_import_receipt_persistence_response_rejects_invalid_request();
    }

    if (command == "validate-durable-store-import-receipt-persistence-response-review-ok") {
        return validate_durable_store_import_receipt_persistence_response_review_ok();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-response-review-ready-response-ok") {
        return build_durable_store_import_receipt_persistence_response_review_ready_response_ok();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-response-review-rejected-response-ok") {
        return build_durable_store_import_receipt_persistence_response_review_rejected_response_ok();
    }

    if (command ==
        "build-durable-store-import-receipt-persistence-response-review-rejects-invalid-response") {
        return build_durable_store_import_receipt_persistence_response_review_rejects_invalid_response();
    }

    // V0.20: Adapter Execution Receipt and Recovery Preview tests
    if (command == "validate-durable-store-import-adapter-execution-ok") {
        return validate_durable_store_import_adapter_execution_ok();
    }

    if (command == "validate-durable-store-import-adapter-execution-blocked-ok") {
        return validate_durable_store_import_adapter_execution_blocked_ok();
    }

    if (command == "validate-durable-store-import-adapter-execution-deferred-ok") {
        return validate_durable_store_import_adapter_execution_deferred_ok();
    }

    if (command == "validate-durable-store-import-adapter-execution-rejected-ok") {
        return validate_durable_store_import_adapter_execution_rejected_ok();
    }

    if (command ==
        "validate-durable-store-import-adapter-execution-rejects-missing-persistence-id") {
        return validate_durable_store_import_adapter_execution_rejects_missing_persistence_id();
    }

    if (command ==
        "validate-durable-store-import-adapter-execution-rejects-non-mutating-accepted") {
        return validate_durable_store_import_adapter_execution_rejects_non_mutating_accepted();
    }

    if (command ==
        "validate-durable-store-import-adapter-execution-rejects-mutated-non-accepted") {
        return validate_durable_store_import_adapter_execution_rejects_mutated_non_accepted();
    }

    if (command == "build-durable-store-import-adapter-execution-ready-response") {
        return build_durable_store_import_adapter_execution_ready_response();
    }

    if (command == "build-durable-store-import-adapter-execution-rejects-invalid-response") {
        return build_durable_store_import_adapter_execution_rejects_invalid_response();
    }

    if (command == "validate-durable-store-import-recovery-preview-ok") {
        return validate_durable_store_import_recovery_preview_ok();
    }

    if (command == "build-durable-store-import-recovery-preview-ready-execution-ok") {
        return build_durable_store_import_recovery_preview_ready_execution_ok();
    }

    if (command == "build-durable-store-import-recovery-preview-rejected-execution-ok") {
        return build_durable_store_import_recovery_preview_rejected_execution_ok();
    }

    if (command == "build-durable-store-import-recovery-preview-rejects-invalid-execution") {
        return build_durable_store_import_recovery_preview_rejects_invalid_execution();
    }

    // V0.21: Provider Write Attempt and Recovery Handoff tests
    if (command == "validate-durable-store-import-provider-write-attempt-ok") {
        return validate_durable_store_import_provider_write_attempt_ok();
    }

    if (command == "validate-durable-store-import-provider-write-attempt-rejected-ok") {
        return validate_durable_store_import_provider_write_attempt_rejected_ok();
    }

    if (command ==
        "build-durable-store-import-provider-write-attempt-unsupported-capability-ok") {
        return build_durable_store_import_provider_write_attempt_unsupported_capability_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-adapter-config-rejects-secret-material") {
        return validate_durable_store_import_provider_adapter_config_rejects_secret_material();
    }

    if (command ==
        "validate-durable-store-import-provider-adapter-config-rejects-provider-coordinates") {
        return validate_durable_store_import_provider_adapter_config_rejects_provider_coordinates();
    }

    if (command ==
        "validate-durable-store-import-provider-write-attempt-rejects-planned-without-provider-id") {
        return validate_durable_store_import_provider_write_attempt_rejects_planned_without_provider_id();
    }

    if (command ==
        "validate-durable-store-import-provider-write-attempt-rejects-not-planned-mutating-intent") {
        return validate_durable_store_import_provider_write_attempt_rejects_not_planned_mutating_intent();
    }

    if (command == "build-durable-store-import-provider-write-attempt-ready-execution") {
        return build_durable_store_import_provider_write_attempt_ready_execution();
    }

    if (command == "build-durable-store-import-provider-write-attempt-rejects-invalid-execution") {
        return build_durable_store_import_provider_write_attempt_rejects_invalid_execution();
    }

    if (command == "validate-durable-store-import-provider-recovery-handoff-ok") {
        return validate_durable_store_import_provider_recovery_handoff_ok();
    }

    if (command ==
        "build-durable-store-import-provider-recovery-handoff-unsupported-capability-ok") {
        return build_durable_store_import_provider_recovery_handoff_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-recovery-handoff-rejects-invalid-write-attempt") {
        return build_durable_store_import_provider_recovery_handoff_rejects_invalid_write_attempt();
    }

    // V0.22: Provider Driver Binding and Readiness tests
    if (command == "validate-durable-store-import-provider-driver-binding-ok") {
        return validate_durable_store_import_provider_driver_binding_ok();
    }

    if (command == "validate-durable-store-import-provider-driver-binding-rejected-ok") {
        return validate_durable_store_import_provider_driver_binding_rejected_ok();
    }

    if (command ==
        "build-durable-store-import-provider-driver-binding-unsupported-capability-ok") {
        return build_durable_store_import_provider_driver_binding_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-driver-binding-missing-profile-load-capability-ok") {
        return build_durable_store_import_provider_driver_binding_missing_profile_load_capability_ok();
    }

    if (command == "build-durable-store-import-provider-driver-binding-profile-mismatch-ok") {
        return build_durable_store_import_provider_driver_binding_profile_mismatch_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-driver-profile-rejects-secret-material") {
        return validate_durable_store_import_provider_driver_profile_rejects_secret_material();
    }

    if (command ==
        "validate-durable-store-import-provider-driver-profile-rejects-provider-coordinates") {
        return validate_durable_store_import_provider_driver_profile_rejects_provider_coordinates();
    }

    if (command ==
        "validate-durable-store-import-provider-driver-binding-rejects-sdk-invocation") {
        return validate_durable_store_import_provider_driver_binding_rejects_sdk_invocation();
    }

    if (command ==
        "validate-durable-store-import-provider-driver-binding-rejects-bound-without-descriptor") {
        return validate_durable_store_import_provider_driver_binding_rejects_bound_without_descriptor();
    }

    if (command ==
        "validate-durable-store-import-provider-driver-binding-rejects-bound-without-profile-load") {
        return validate_durable_store_import_provider_driver_binding_rejects_bound_without_profile_load();
    }

    if (command == "build-durable-store-import-provider-driver-binding-ready-write-attempt") {
        return build_durable_store_import_provider_driver_binding_ready_write_attempt();
    }

    if (command ==
        "build-durable-store-import-provider-driver-binding-rejects-invalid-write-attempt") {
        return build_durable_store_import_provider_driver_binding_rejects_invalid_write_attempt();
    }

    if (command == "validate-durable-store-import-provider-driver-readiness-ok") {
        return validate_durable_store_import_provider_driver_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-driver-readiness-unsupported-capability-ok") {
        return build_durable_store_import_provider_driver_readiness_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-driver-readiness-rejects-invalid-binding-plan") {
        return build_durable_store_import_provider_driver_readiness_rejects_invalid_binding_plan();
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
