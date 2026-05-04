#include "ahfl/durable_store_import/adapter_execution.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/provider_adapter.hpp"
#include "ahfl/durable_store_import/provider_audit.hpp"
#include "ahfl/durable_store_import/provider_compatibility.hpp"
#include "ahfl/durable_store_import/provider_config.hpp"
#include "ahfl/durable_store_import/provider_commit.hpp"
#include "ahfl/durable_store_import/provider_driver.hpp"
#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"
#include "ahfl/durable_store_import/provider_host_execution.hpp"
#include "ahfl/durable_store_import/provider_local_filesystem_alpha.hpp"
#include "ahfl/durable_store_import/provider_local_host_execution.hpp"
#include "ahfl/durable_store_import/provider_local_host_harness.hpp"
#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_runtime.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/durable_store_import/provider_sdk.hpp"
#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"
#include "ahfl/durable_store_import/provider_sdk_interface.hpp"
#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"
#include "ahfl/durable_store_import/provider_sdk_payload.hpp"
#include "ahfl/durable_store_import/provider_secret.hpp"
#include "ahfl/durable_store_import/provider_retry.hpp"
#include "ahfl/durable_store_import/provider_recovery.hpp"
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
#include <filesystem>
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

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderRuntimePreflightPlan>
make_valid_provider_runtime_preflight_plan() {
    const auto driver_binding = make_valid_provider_driver_binding_plan();
    if (!driver_binding.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*driver_binding);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderRuntimeReadinessReview>
make_valid_provider_runtime_readiness_review() {
    const auto plan = make_valid_provider_runtime_preflight_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_runtime_readiness_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderSdkRequestEnvelopePlan>
make_valid_provider_sdk_request_envelope_plan() {
    const auto runtime_preflight = make_valid_provider_runtime_preflight_plan();
    if (!runtime_preflight.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*runtime_preflight);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderSdkHandoffReadinessReview>
make_valid_provider_sdk_handoff_readiness_review() {
    const auto plan = make_valid_provider_sdk_request_envelope_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_handoff_readiness_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderHostExecutionPlan>
make_valid_provider_host_execution_plan() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_host_execution_plan(*envelope);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderHostExecutionReadinessReview>
make_valid_provider_host_execution_readiness_review() {
    const auto plan = make_valid_provider_host_execution_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_host_execution_readiness_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostExecutionReceipt>
make_valid_provider_local_host_execution_receipt() {
    const auto plan = make_valid_provider_host_execution_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt(*plan);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderLocalHostExecutionReceiptReview>
make_valid_provider_local_host_execution_receipt_review() {
    const auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt_review(*receipt);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterRequestPlan>
make_valid_provider_sdk_adapter_request_plan() {
    const auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_sdk_adapter_request_plan(*receipt);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterResponsePlaceholder>
make_valid_provider_sdk_adapter_response_placeholder() {
    const auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_sdk_adapter_response_placeholder(*plan);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        placeholder.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *placeholder.placeholder;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterReadinessReview>
make_valid_provider_sdk_adapter_readiness_review() {
    const auto placeholder = make_valid_provider_sdk_adapter_response_placeholder();
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_readiness_review(*placeholder);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfacePlan>
make_valid_provider_sdk_adapter_interface_plan() {
    const auto request_plan = make_valid_provider_sdk_adapter_request_plan();
    if (!request_plan.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_plan(*request_plan);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfaceReview>
make_valid_provider_sdk_adapter_interface_review() {
    const auto plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigLoadPlan>
make_valid_provider_config_load_plan() {
    const auto interface_plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!interface_plan.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_config_load_plan(*interface_plan);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigSnapshotPlaceholder>
make_valid_provider_config_snapshot_placeholder() {
    const auto plan = make_valid_provider_config_load_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_config_snapshot_placeholder(*plan);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        placeholder.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *placeholder.placeholder;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigReadinessReview>
make_valid_provider_config_readiness_review() {
    const auto placeholder = make_valid_provider_config_snapshot_placeholder();
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_config_readiness_review(*placeholder);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverRequestPlan>
make_valid_provider_secret_resolver_request_plan() {
    const auto snapshot = make_valid_provider_config_snapshot_placeholder();
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_secret_resolver_request_plan(*snapshot);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderSecretResolverResponsePlaceholder>
make_valid_provider_secret_resolver_response_placeholder() {
    const auto plan = make_valid_provider_secret_resolver_request_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_secret_resolver_response_placeholder(*plan);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        placeholder.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *placeholder.placeholder;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretPolicyReview>
make_valid_provider_secret_policy_review() {
    const auto placeholder = make_valid_provider_secret_resolver_response_placeholder();
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_secret_policy_review(*placeholder);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRequest>
make_valid_provider_local_host_harness_request() {
    const auto secret_review = make_valid_provider_secret_policy_review();
    if (!secret_review.has_value()) {
        return std::nullopt;
    }

    const auto request =
        ahfl::durable_store_import::build_provider_local_host_harness_execution_request(
            *secret_review);
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRecord>
make_valid_provider_local_host_harness_record() {
    const auto request = make_valid_provider_local_host_harness_request();
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto record = ahfl::durable_store_import::run_provider_local_host_test_harness(*request);
    if (record.has_errors() || !record.record.has_value()) {
        record.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *record.record;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessReview>
make_valid_provider_local_host_harness_review() {
    const auto record = make_valid_provider_local_host_harness_record();
    if (!record.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_local_host_harness_review(*record);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderSdkPayloadMaterializationPlan>
make_valid_provider_sdk_payload_plan() {
    const auto review = make_valid_provider_local_host_harness_review();
    if (!review.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_sdk_payload_materialization_plan(*review);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadAuditSummary>
make_valid_provider_sdk_payload_audit() {
    const auto plan = make_valid_provider_sdk_payload_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto audit = ahfl::durable_store_import::build_provider_sdk_payload_audit_summary(*plan);
    if (audit.has_errors() || !audit.audit.has_value()) {
        audit.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *audit.audit;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterContract>
make_valid_provider_sdk_mock_adapter_contract() {
    const auto audit = make_valid_provider_sdk_payload_audit();
    if (!audit.has_value()) {
        return std::nullopt;
    }

    const auto contract =
        ahfl::durable_store_import::build_provider_sdk_mock_adapter_contract(*audit);
    if (contract.has_errors() || !contract.contract.has_value()) {
        contract.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *contract.contract;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::ProviderSdkMockAdapterExecutionResult>
make_valid_provider_sdk_mock_adapter_execution() {
    const auto contract = make_valid_provider_sdk_mock_adapter_contract();
    if (!contract.has_value()) {
        return std::nullopt;
    }

    const auto result = ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
    if (result.has_errors() || !result.result.has_value()) {
        result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *result.result;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterReadiness>
make_valid_provider_sdk_mock_adapter_readiness() {
    const auto result = make_valid_provider_sdk_mock_adapter_execution();
    if (!result.has_value()) {
        return std::nullopt;
    }

    const auto readiness =
        ahfl::durable_store_import::build_provider_sdk_mock_adapter_readiness(*result);
    if (readiness.has_errors() || !readiness.readiness.has_value()) {
        readiness.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *readiness.readiness;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaPlan>
make_valid_provider_local_filesystem_alpha_plan() {
    const auto readiness = make_valid_provider_sdk_mock_adapter_readiness();
    if (!readiness.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_local_filesystem_alpha_plan(*readiness);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaResult>
make_valid_provider_local_filesystem_alpha_result() {
    const auto plan = make_valid_provider_local_filesystem_alpha_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto result = ahfl::durable_store_import::run_provider_local_filesystem_alpha(*plan);
    if (result.has_errors() || !result.result.has_value()) {
        result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *result.result;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaReadiness>
make_valid_provider_local_filesystem_alpha_readiness() {
    const auto result = make_valid_provider_local_filesystem_alpha_result();
    if (!result.has_value()) {
        return std::nullopt;
    }

    const auto readiness =
        ahfl::durable_store_import::build_provider_local_filesystem_alpha_readiness(*result);
    if (readiness.has_errors() || !readiness.readiness.has_value()) {
        readiness.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *readiness.readiness;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRetryDecision>
make_valid_provider_write_retry_decision() {
    const auto result = make_valid_provider_sdk_mock_adapter_execution();
    if (!result.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_provider_write_retry_decision(*result);
    if (decision.has_errors() || !decision.decision.has_value()) {
        decision.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReceipt>
make_valid_provider_write_commit_receipt() {
    const auto decision = make_valid_provider_write_retry_decision();
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt = ahfl::durable_store_import::build_provider_write_commit_receipt(*decision);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReview>
make_valid_provider_write_commit_review() {
    const auto receipt = make_valid_provider_write_commit_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_write_commit_review(*receipt);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryCheckpoint>
make_valid_provider_write_recovery_checkpoint() {
    const auto receipt = make_valid_provider_write_commit_receipt();
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto checkpoint =
        ahfl::durable_store_import::build_provider_write_recovery_checkpoint(*receipt);
    if (checkpoint.has_errors() || !checkpoint.checkpoint.has_value()) {
        checkpoint.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *checkpoint.checkpoint;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryPlan>
make_valid_provider_write_recovery_plan() {
    const auto checkpoint = make_valid_provider_write_recovery_checkpoint();
    if (!checkpoint.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_write_recovery_plan(*checkpoint);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryReview>
make_valid_provider_write_recovery_review() {
    const auto plan = make_valid_provider_write_recovery_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_write_recovery_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReport>
make_valid_provider_failure_taxonomy_report() {
    const auto adapter_result = make_valid_provider_sdk_mock_adapter_execution();
    const auto recovery_plan = make_valid_provider_write_recovery_plan();
    if (!adapter_result.has_value() || !recovery_plan.has_value()) {
        return std::nullopt;
    }

    const auto report = ahfl::durable_store_import::build_provider_failure_taxonomy_report(
        *adapter_result, *recovery_plan);
    if (report.has_errors() || !report.report.has_value()) {
        report.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *report.report;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReview>
make_valid_provider_failure_taxonomy_review() {
    const auto report = make_valid_provider_failure_taxonomy_report();
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_failure_taxonomy_review(*report);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderExecutionAuditEvent>
make_valid_provider_execution_audit_event() {
    const auto report = make_valid_provider_failure_taxonomy_report();
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto event = ahfl::durable_store_import::build_provider_execution_audit_event(*report);
    if (event.has_errors() || !event.event.has_value()) {
        event.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *event.event;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderTelemetrySummary>
make_valid_provider_telemetry_summary() {
    const auto event = make_valid_provider_execution_audit_event();
    if (!event.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::build_provider_telemetry_summary(*event);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderOperatorReviewEvent>
make_valid_provider_operator_review_event() {
    const auto summary = make_valid_provider_telemetry_summary();
    if (!summary.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_operator_review_event(*summary);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityTestManifest>
make_valid_provider_compatibility_test_manifest() {
    const auto review = make_valid_provider_operator_review_event();
    if (!review.has_value()) {
        return std::nullopt;
    }

    const auto manifest =
        ahfl::durable_store_import::build_provider_compatibility_test_manifest(*review);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        manifest.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *manifest.manifest;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFixtureMatrix>
make_valid_provider_fixture_matrix() {
    const auto manifest = make_valid_provider_compatibility_test_manifest();
    if (!manifest.has_value()) {
        return std::nullopt;
    }

    const auto matrix = ahfl::durable_store_import::build_provider_fixture_matrix(*manifest);
    if (matrix.has_errors() || !matrix.matrix.has_value()) {
        matrix.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *matrix.matrix;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityReport>
make_valid_provider_compatibility_report() {
    const auto matrix = make_valid_provider_fixture_matrix();
    const auto telemetry = make_valid_provider_telemetry_summary();
    if (!matrix.has_value() || !telemetry.has_value()) {
        return std::nullopt;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_compatibility_report(*matrix, *telemetry);
    if (report.has_errors() || !report.report.has_value()) {
        report.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *report.report;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRegistry>
make_valid_provider_registry() {
    const auto report = make_valid_provider_compatibility_report();
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto registry = ahfl::durable_store_import::build_provider_registry(*report);
    if (registry.has_errors() || !registry.registry.has_value()) {
        registry.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *registry.registry;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSelectionPlan>
make_valid_provider_selection_plan() {
    const auto registry = make_valid_provider_registry();
    if (!registry.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_selection_plan(*registry);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCapabilityNegotiationReview>
make_valid_provider_capability_negotiation_review() {
    const auto plan = make_valid_provider_selection_plan();
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_capability_negotiation_review(*plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessEvidence>
make_valid_provider_production_readiness_evidence() {
    const auto negotiation = make_valid_provider_capability_negotiation_review();
    const auto compatibility = make_valid_provider_compatibility_report();
    const auto audit = make_valid_provider_execution_audit_event();
    const auto recovery = make_valid_provider_write_recovery_plan();
    const auto taxonomy = make_valid_provider_failure_taxonomy_report();
    if (!negotiation.has_value() || !compatibility.has_value() || !audit.has_value() ||
        !recovery.has_value() || !taxonomy.has_value()) {
        return std::nullopt;
    }

    const auto evidence = ahfl::durable_store_import::build_provider_production_readiness_evidence(
        *negotiation, *compatibility, *audit, *recovery, *taxonomy);
    if (evidence.has_errors() || !evidence.evidence.has_value()) {
        evidence.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *evidence.evidence;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReview>
make_valid_provider_production_readiness_review() {
    const auto evidence = make_valid_provider_production_readiness_evidence();
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_production_readiness_review(*evidence);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReport>
make_valid_provider_production_readiness_report() {
    const auto review = make_valid_provider_production_readiness_review();
    if (!review.has_value()) {
        return std::nullopt;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_production_readiness_report(*review);
    if (report.has_errors() || !report.report.has_value()) {
        report.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *report.report;
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

int validate_durable_store_import_provider_runtime_preflight_ok() {
    const auto plan = make_valid_provider_runtime_preflight_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_preflight_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->preflight_status !=
            ahfl::durable_store_import::ProviderRuntimePreflightStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderRuntimeOperationKind::
                PlanProviderSdkInvocationEnvelope ||
        !plan->sdk_invocation_envelope_identity.has_value() || plan->loads_runtime_config ||
        plan->resolves_secret_handles || plan->invokes_provider_sdk ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider runtime preflight plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_runtime_preflight_blocked_ok() {
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

    const auto binding =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt.preview);
    if (binding.has_errors() || !binding.plan.has_value()) {
        binding.diagnostics.render(std::cout);
        return 1;
    }

    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding.plan);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    if (preflight.plan->preflight_status !=
            ahfl::durable_store_import::ProviderRuntimePreflightStatus::Blocked ||
        preflight.plan->operation_kind !=
            ahfl::durable_store_import::ProviderRuntimeOperationKind::
                NoopDriverBindingNotReady ||
        preflight.plan->sdk_invocation_envelope_identity.has_value() ||
        !preflight.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider runtime preflight plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_runtime_preflight_unsupported_capability_ok() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_runtime_profile(*binding);
    profile.supports_sdk_invocation_envelope_planning = false;
    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding, profile);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    if (preflight.plan->preflight_status !=
            ahfl::durable_store_import::ProviderRuntimePreflightStatus::Blocked ||
        preflight.plan->operation_kind !=
            ahfl::durable_store_import::ProviderRuntimeOperationKind::
                NoopUnsupportedRuntimeCapability ||
        !preflight.plan->failure_attribution.has_value() ||
        preflight.plan->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderRuntimeCapabilityKind>(
                ahfl::durable_store_import::ProviderRuntimeCapabilityKind::
                    PlanSdkInvocationEnvelope)) {
        std::cerr << "unexpected unsupported-capability provider runtime preflight plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_runtime_preflight_profile_mismatch_ok() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_runtime_profile(*binding);
    profile.driver_profile_ref = "provider-driver-profile::wrong";
    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding, profile);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    if (preflight.plan->preflight_status !=
            ahfl::durable_store_import::ProviderRuntimePreflightStatus::Blocked ||
        preflight.plan->operation_kind !=
            ahfl::durable_store_import::ProviderRuntimeOperationKind::
                NoopRuntimeProfileMismatch ||
        !preflight.plan->failure_attribution.has_value() ||
        preflight.plan->failure_attribution->kind !=
            ahfl::durable_store_import::ProviderRuntimePreflightFailureKind::
                RuntimeProfileMismatch) {
        std::cerr << "unexpected provider runtime profile mismatch preflight plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_runtime_profile_rejects_secret_material() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_runtime_profile(*binding);
    profile.credential_reference = "secret://provider-credential";
    profile.secret_value = "plaintext-secret";
    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_profile(profile);
    if (!validation.has_errors()) {
        std::cerr << "expected provider runtime profile with secrets to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain credential_reference") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain secret_value")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_runtime_profile_rejects_provider_coordinates() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_runtime_profile(*binding);
    profile.credential_free = false;
    profile.secret_manager_endpoint_uri = "https://secret-manager.example.invalid";
    profile.provider_endpoint_uri = "https://provider.example.invalid";
    profile.object_path = "bucket/receipt.json";
    profile.database_table = "receipt-table";
    profile.sdk_payload_schema = "provider-sdk-payload-v1";
    profile.sdk_request_payload = "{\"unsafe\":true}";
    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_profile(profile);
    if (!validation.has_errors()) {
        std::cerr << "expected provider runtime profile with provider coordinates to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "must be credential_free") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot contain secret_manager_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain database_table") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_payload_schema") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_runtime_preflight_rejects_side_effects() {
    auto plan = make_valid_provider_runtime_preflight_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->loads_runtime_config = true;
    plan->resolves_secret_handles = true;
    plan->invokes_provider_sdk = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_preflight_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider runtime preflight with side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot load runtime config") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot resolve secret handles") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke provider SDK")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_runtime_preflight_rejects_ready_without_envelope() {
    auto plan = make_valid_provider_runtime_preflight_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->sdk_invocation_envelope_identity.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_preflight_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected ready provider runtime preflight without envelope to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "ready status requires SDK invocation envelope identity")
               ? 0
               : 1;
}

int build_durable_store_import_provider_runtime_preflight_ready_binding() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    if (preflight.plan->durable_store_import_provider_runtime_preflight_identity !=
            "durable-store-import-provider-runtime-preflight::run-partial-001::ready" ||
        preflight.plan->sdk_invocation_envelope_identity !=
            std::optional<std::string>(
                "provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        preflight.plan->runtime_profile.secret_free_runtime_config_ref.empty()) {
        std::cerr << "unexpected ready provider runtime preflight bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_runtime_preflight_rejects_invalid_driver_binding() {
    auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    binding->format_version = "ahfl.durable-store-import-provider-driver-binding-plan.v999";
    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding);
    if (!preflight.has_errors()) {
        std::cerr << "expected invalid driver binding to fail runtime preflight\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               preflight.diagnostics,
               "durable store import provider driver binding plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_runtime_readiness_ok() {
    const auto review = make_valid_provider_runtime_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_runtime_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderRuntimeReadinessNextActionKind::
                ReadyForSdkAdapterImplementation ||
        !review->sdk_invocation_envelope_identity.has_value() || review->loads_runtime_config ||
        review->resolves_secret_handles || review->invokes_provider_sdk ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider runtime readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_runtime_readiness_unsupported_capability_ok() {
    const auto binding = make_valid_provider_driver_binding_plan();
    if (!binding.has_value()) {
        return 1;
    }

    auto profile =
        ahfl::durable_store_import::build_default_provider_runtime_profile(*binding);
    profile.supports_config_snapshot_placeholder_load = false;
    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding, profile);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_runtime_readiness_review(*preflight.plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    if (review.review->next_action !=
            ahfl::durable_store_import::ProviderRuntimeReadinessNextActionKind::
                WaitForRuntimeCapability ||
        review.review->sdk_invocation_envelope_identity.has_value() ||
        !review.review->failure_attribution.has_value()) {
        std::cerr << "unexpected unsupported provider runtime readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_runtime_readiness_rejects_invalid_preflight() {
    auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    preflight->format_version =
        "ahfl.durable-store-import-provider-runtime-preflight-plan.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_runtime_readiness_review(*preflight);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider runtime preflight to fail readiness review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider runtime preflight plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_envelope_ok() {
    const auto plan = make_valid_provider_sdk_request_envelope_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_request_envelope_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->envelope_status != ahfl::durable_store_import::ProviderSdkEnvelopeStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkEnvelopeOperationKind::
                PlanProviderSdkRequestEnvelope ||
        !plan->provider_sdk_request_envelope_identity.has_value() ||
        !plan->host_handoff_descriptor_identity.has_value() ||
        plan->materializes_sdk_request_payload || plan->starts_host_process ||
        plan->opens_network_connection || plan->invokes_provider_sdk ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK request envelope plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_envelope_blocked_ok() {
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

    const auto binding =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt.preview);
    if (binding.has_errors() || !binding.plan.has_value()) {
        binding.diagnostics.render(std::cout);
        return 1;
    }

    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding.plan);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight.plan);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    if (envelope.plan->envelope_status !=
            ahfl::durable_store_import::ProviderSdkEnvelopeStatus::Blocked ||
        envelope.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkEnvelopeOperationKind::
                NoopRuntimePreflightNotReady ||
        envelope.plan->provider_sdk_request_envelope_identity.has_value() ||
        envelope.plan->host_handoff_descriptor_identity.has_value() ||
        !envelope.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider SDK request envelope plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_envelope_unsupported_capability_ok() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_sdk_envelope_policy(*preflight);
    policy.supports_host_handoff_descriptor_planning = false;
    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight, policy);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    if (envelope.plan->envelope_status !=
            ahfl::durable_store_import::ProviderSdkEnvelopeStatus::Blocked ||
        envelope.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkEnvelopeOperationKind::
                NoopUnsupportedEnvelopeCapability ||
        !envelope.plan->failure_attribution.has_value() ||
        envelope.plan->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderSdkEnvelopeCapabilityKind>(
                ahfl::durable_store_import::ProviderSdkEnvelopeCapabilityKind::
                    PlanHostHandoffDescriptor)) {
        std::cerr << "unexpected unsupported-capability provider SDK envelope plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_envelope_policy_mismatch_ok() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_sdk_envelope_policy(*preflight);
    policy.runtime_profile_ref = "provider-runtime-profile::wrong";
    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight, policy);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    if (envelope.plan->envelope_status !=
            ahfl::durable_store_import::ProviderSdkEnvelopeStatus::Blocked ||
        envelope.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkEnvelopeOperationKind::
                NoopEnvelopePolicyMismatch ||
        !envelope.plan->failure_attribution.has_value() ||
        envelope.plan->failure_attribution->kind !=
            ahfl::durable_store_import::ProviderSdkEnvelopeFailureKind::
                EnvelopePolicyMismatch) {
        std::cerr << "unexpected provider SDK envelope policy mismatch plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_policy_rejects_secret_material() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_sdk_envelope_policy(*preflight);
    policy.credential_reference = "secret://provider-credential";
    policy.secret_value = "plaintext-secret";
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_envelope_policy(policy);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK envelope policy with secrets to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain credential_reference") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain secret_value")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_policy_rejects_provider_coordinates() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_sdk_envelope_policy(*preflight);
    policy.credential_free = false;
    policy.provider_endpoint_uri = "https://provider.example.invalid";
    policy.object_path = "bucket/receipt.json";
    policy.database_table = "receipt-table";
    policy.sdk_request_payload = "{\"unsafe\":true}";
    policy.sdk_response_payload = "{\"unsafe\":true}";
    policy.host_command = "/usr/bin/provider-sdk";
    policy.network_endpoint_uri = "https://network.example.invalid";
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_envelope_policy(policy);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK envelope policy with provider coordinates to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "must be credential_free") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain database_table") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_response_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain host_command") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain network_endpoint_uri")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_envelope_rejects_side_effects() {
    auto plan = make_valid_provider_sdk_request_envelope_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->materializes_sdk_request_payload = true;
    plan->starts_host_process = true;
    plan->opens_network_connection = true;
    plan->invokes_provider_sdk = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_request_envelope_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK request envelope with side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot materialize SDK request payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot start host process") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke provider SDK")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_envelope_rejects_ready_without_handoff() {
    auto plan = make_valid_provider_sdk_request_envelope_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->host_handoff_descriptor_identity.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_request_envelope_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected ready provider SDK request envelope without handoff to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "ready status requires request envelope and host handoff descriptor identities")
               ? 0
               : 1;
}

int build_durable_store_import_provider_sdk_envelope_ready_preflight() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    if (envelope.plan->durable_store_import_provider_sdk_request_envelope_identity !=
            "durable-store-import-provider-sdk-request-envelope::run-partial-001::ready" ||
        envelope.plan->provider_sdk_request_envelope_identity !=
            std::optional<std::string>(
                "provider-sdk-request-envelope::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        envelope.plan->host_handoff_descriptor_identity !=
            std::optional<std::string>(
                "provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted")) {
        std::cerr << "unexpected ready provider SDK request envelope bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_envelope_rejects_invalid_preflight() {
    auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    preflight->format_version =
        "ahfl.durable-store-import-provider-runtime-preflight-plan.v999";
    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight);
    if (!envelope.has_errors()) {
        std::cerr << "expected invalid runtime preflight to fail SDK envelope\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               envelope.diagnostics,
               "durable store import provider runtime preflight plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_handoff_readiness_ok() {
    const auto review = make_valid_provider_sdk_handoff_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_handoff_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderSdkHandoffReadinessNextActionKind::
                ReadyForHostExecutionPrototype ||
        !review->provider_sdk_request_envelope_identity.has_value() ||
        !review->host_handoff_descriptor_identity.has_value() ||
        review->materializes_sdk_request_payload || review->starts_host_process ||
        review->opens_network_connection || review->invokes_provider_sdk ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK handoff readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_handoff_readiness_unsupported_capability_ok() {
    const auto preflight = make_valid_provider_runtime_preflight_plan();
    if (!preflight.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_sdk_envelope_policy(*preflight);
    policy.supports_secret_free_request_envelope_planning = false;
    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight, policy);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_handoff_readiness_review(*envelope.plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    if (review.review->next_action !=
            ahfl::durable_store_import::ProviderSdkHandoffReadinessNextActionKind::
                WaitForEnvelopeCapability ||
        review.review->provider_sdk_request_envelope_identity.has_value() ||
        !review.review->failure_attribution.has_value()) {
        std::cerr << "unexpected unsupported provider SDK handoff readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_handoff_readiness_rejects_invalid_envelope() {
    auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    envelope->format_version =
        "ahfl.durable-store-import-provider-sdk-request-envelope-plan.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_sdk_handoff_readiness_review(*envelope);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider SDK envelope to fail handoff readiness\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider SDK request envelope plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_host_execution_ok() {
    const auto plan = make_valid_provider_host_execution_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->execution_status !=
            ahfl::durable_store_import::ProviderHostExecutionStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderHostExecutionOperationKind::
                PlanProviderHostExecution ||
        !plan->provider_host_execution_descriptor_identity.has_value() ||
        !plan->provider_host_receipt_placeholder_identity.has_value() ||
        plan->starts_host_process || plan->reads_host_environment ||
        plan->opens_network_connection || plan->materializes_sdk_request_payload ||
        plan->invokes_provider_sdk || plan->writes_host_filesystem ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider host execution plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_host_execution_blocked_ok() {
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

    const auto binding =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt.preview);
    if (binding.has_errors() || !binding.plan.has_value()) {
        binding.diagnostics.render(std::cout);
        return 1;
    }

    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding.plan);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight.plan);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope.plan);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    if (host_execution.plan->execution_status !=
            ahfl::durable_store_import::ProviderHostExecutionStatus::Blocked ||
        host_execution.plan->operation_kind !=
            ahfl::durable_store_import::ProviderHostExecutionOperationKind::
                NoopSdkHandoffNotReady ||
        host_execution.plan->provider_host_execution_descriptor_identity.has_value() ||
        host_execution.plan->provider_host_receipt_placeholder_identity.has_value() ||
        !host_execution.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider host execution plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_host_execution_unsupported_capability_ok() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_host_execution_policy(*envelope);
    policy.supports_dry_run_host_receipt_placeholder_planning = false;
    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope, policy);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    if (host_execution.plan->execution_status !=
            ahfl::durable_store_import::ProviderHostExecutionStatus::Blocked ||
        host_execution.plan->operation_kind !=
            ahfl::durable_store_import::ProviderHostExecutionOperationKind::
                NoopUnsupportedHostCapability ||
        !host_execution.plan->failure_attribution.has_value() ||
        host_execution.plan->failure_attribution->missing_capability !=
            std::optional<ahfl::durable_store_import::ProviderHostExecutionCapabilityKind>(
                ahfl::durable_store_import::ProviderHostExecutionCapabilityKind::
                    PlanDryRunHostReceiptPlaceholder)) {
        std::cerr << "unexpected unsupported-capability provider host execution plan\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_host_execution_policy_mismatch_ok() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_host_execution_policy(*envelope);
    policy.host_handoff_policy_ref = "host-handoff-policy::wrong";
    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope, policy);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    if (host_execution.plan->execution_status !=
            ahfl::durable_store_import::ProviderHostExecutionStatus::Blocked ||
        host_execution.plan->operation_kind !=
            ahfl::durable_store_import::ProviderHostExecutionOperationKind::
                NoopHostPolicyMismatch ||
        !host_execution.plan->failure_attribution.has_value() ||
        host_execution.plan->failure_attribution->kind !=
            ahfl::durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch) {
        std::cerr << "unexpected provider host execution policy mismatch plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_host_policy_rejects_secret_material() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_host_execution_policy(*envelope);
    policy.credential_reference = "secret://provider-credential";
    policy.secret_value = "plaintext-secret";
    policy.host_environment_secret = "AHFL_PROVIDER_SECRET";
    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_policy(policy);
    if (!validation.has_errors()) {
        std::cerr << "expected provider host execution policy with secrets to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain credential_reference") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain secret_value") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot contain host_environment_secret")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_host_policy_rejects_host_side_effects() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_host_execution_policy(*envelope);
    policy.credential_free = false;
    policy.network_free = false;
    policy.host_command = "/usr/bin/provider-host";
    policy.provider_endpoint_uri = "https://provider.example.invalid";
    policy.network_endpoint_uri = "https://network.example.invalid";
    policy.object_path = "bucket/receipt.json";
    policy.database_table = "receipt-table";
    policy.sdk_request_payload = "{\"unsafe\":true}";
    policy.sdk_response_payload = "{\"unsafe\":true}";
    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_policy(policy);
    if (!validation.has_errors()) {
        std::cerr << "expected provider host execution policy with host side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "must be credential_free") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "must be network_free") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain host_command") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain network_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain database_table") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_response_payload")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_host_execution_rejects_side_effects() {
    auto plan = make_valid_provider_host_execution_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->starts_host_process = true;
    plan->reads_host_environment = true;
    plan->opens_network_connection = true;
    plan->materializes_sdk_request_payload = true;
    plan->invokes_provider_sdk = true;
    plan->writes_host_filesystem = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider host execution with side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot start host process") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot read host environment") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot materialize SDK request payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke provider SDK") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot write host filesystem")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_host_execution_rejects_ready_without_descriptor() {
    auto plan = make_valid_provider_host_execution_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->provider_host_execution_descriptor_identity.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected ready provider host execution without descriptor to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "ready status requires host execution descriptor and receipt placeholder identities")
               ? 0
               : 1;
}

int build_durable_store_import_provider_host_execution_ready_envelope() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    if (host_execution.plan->durable_store_import_provider_host_execution_identity !=
            "durable-store-import-provider-host-execution::run-partial-001::ready" ||
        host_execution.plan->provider_host_execution_descriptor_identity !=
            std::optional<std::string>(
                "provider-host-execution-descriptor::provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        host_execution.plan->provider_host_receipt_placeholder_identity !=
            std::optional<std::string>(
                "provider-host-receipt-placeholder::provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted")) {
        std::cerr << "unexpected ready provider host execution bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_host_execution_rejects_invalid_envelope() {
    auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    envelope->format_version =
        "ahfl.durable-store-import-provider-sdk-request-envelope-plan.v999";
    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope);
    if (!host_execution.has_errors()) {
        std::cerr << "expected invalid provider SDK envelope to fail host execution\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               host_execution.diagnostics,
               "durable store import provider SDK request envelope plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_host_execution_readiness_ok() {
    const auto review = make_valid_provider_host_execution_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_host_execution_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderHostExecutionReadinessNextActionKind::
                ReadyForLocalHostExecutionHarness ||
        !review->provider_host_execution_descriptor_identity.has_value() ||
        !review->provider_host_receipt_placeholder_identity.has_value() ||
        review->starts_host_process || review->reads_host_environment ||
        review->opens_network_connection || review->materializes_sdk_request_payload ||
        review->invokes_provider_sdk || review->writes_host_filesystem ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider host execution readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_host_execution_readiness_unsupported_capability_ok() {
    const auto envelope = make_valid_provider_sdk_request_envelope_plan();
    if (!envelope.has_value()) {
        return 1;
    }

    auto policy =
        ahfl::durable_store_import::build_default_provider_host_execution_policy(*envelope);
    policy.supports_local_host_execution_descriptor_planning = false;
    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope, policy);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_host_execution_readiness_review(
            *host_execution.plan);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    if (review.review->next_action !=
            ahfl::durable_store_import::ProviderHostExecutionReadinessNextActionKind::
                WaitForHostCapability ||
        review.review->provider_host_execution_descriptor_identity.has_value() ||
        !review.review->failure_attribution.has_value()) {
        std::cerr << "unexpected unsupported provider host execution readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_host_execution_readiness_rejects_invalid_host_execution() {
    auto host_execution = make_valid_provider_host_execution_plan();
    if (!host_execution.has_value()) {
        return 1;
    }

    host_execution->format_version =
        "ahfl.durable-store-import-provider-host-execution-plan.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_host_execution_readiness_review(
            *host_execution);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider host execution to fail readiness review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider host execution plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_host_execution_receipt_ok() {
    const auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_execution_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt->execution_status !=
            ahfl::durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady ||
        receipt->operation_kind !=
            ahfl::durable_store_import::ProviderLocalHostExecutionOperationKind::
                SimulateProviderLocalHostExecutionReceipt ||
        !receipt->provider_local_host_execution_receipt_identity.has_value() ||
        receipt->starts_host_process || receipt->reads_host_environment ||
        receipt->opens_network_connection || receipt->materializes_sdk_request_payload ||
        receipt->invokes_provider_sdk || receipt->writes_host_filesystem ||
        receipt->failure_attribution.has_value()) {
        std::cerr << "unexpected provider local host execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_local_host_execution_receipt_blocked_ok() {
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

    const auto binding =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt.preview);
    if (binding.has_errors() || !binding.plan.has_value()) {
        binding.diagnostics.render(std::cout);
        return 1;
    }

    const auto preflight =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*binding.plan);
    if (preflight.has_errors() || !preflight.plan.has_value()) {
        preflight.diagnostics.render(std::cout);
        return 1;
    }

    const auto envelope =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight.plan);
    if (envelope.has_errors() || !envelope.plan.has_value()) {
        envelope.diagnostics.render(std::cout);
        return 1;
    }

    const auto host_execution =
        ahfl::durable_store_import::build_provider_host_execution_plan(*envelope.plan);
    if (host_execution.has_errors() || !host_execution.plan.has_value()) {
        host_execution.diagnostics.render(std::cout);
        return 1;
    }

    const auto receipt =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt(
            *host_execution.plan);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        receipt.diagnostics.render(std::cout);
        return 1;
    }

    if (receipt.receipt->execution_status !=
            ahfl::durable_store_import::ProviderLocalHostExecutionStatus::Blocked ||
        receipt.receipt->operation_kind !=
            ahfl::durable_store_import::ProviderLocalHostExecutionOperationKind::
                NoopHostExecutionNotReady ||
        receipt.receipt->provider_local_host_execution_receipt_identity.has_value() ||
        !receipt.receipt->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider local host execution receipt\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_local_host_execution_receipt_rejects_side_effects() {
    auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->starts_host_process = true;
    receipt->reads_host_environment = true;
    receipt->opens_network_connection = true;
    receipt->materializes_sdk_request_payload = true;
    receipt->invokes_provider_sdk = true;
    receipt->writes_host_filesystem = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_execution_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected provider local host execution receipt with side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot start host process") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot read host environment") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot materialize SDK request payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke provider SDK") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot write host filesystem")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_host_execution_receipt_rejects_ready_without_identity() {
    auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->provider_local_host_execution_receipt_identity.reset();
    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_execution_receipt(*receipt);
    if (!validation.has_errors()) {
        std::cerr << "expected simulated provider local host execution without identity to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics,
               "simulated-ready status requires local host receipt identity")
               ? 0
               : 1;
}

int build_durable_store_import_provider_local_host_execution_receipt_ready_host_execution() {
    const auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    if (receipt->durable_store_import_provider_local_host_execution_receipt_identity !=
            "durable-store-import-provider-local-host-execution-receipt::run-partial-001::simulated-ready" ||
        receipt->provider_local_host_execution_receipt_identity !=
            std::optional<std::string>(
                "provider-local-host-execution-receipt::provider-host-execution-descriptor::provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted")) {
        std::cerr << "unexpected ready provider local host execution receipt bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_local_host_execution_receipt_rejects_invalid_host_execution() {
    auto host_execution = make_valid_provider_host_execution_plan();
    if (!host_execution.has_value()) {
        return 1;
    }

    host_execution->format_version =
        "ahfl.durable-store-import-provider-host-execution-plan.v999";
    const auto receipt =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt(*host_execution);
    if (!receipt.has_errors()) {
        std::cerr << "expected invalid provider host execution to fail local host receipt\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               receipt.diagnostics,
               "durable store import provider host execution plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_host_execution_receipt_review_ok() {
    const auto review = make_valid_provider_local_host_execution_receipt_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_execution_receipt_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::
                ProviderLocalHostExecutionReceiptReviewNextActionKind::
                    ReadyForProviderSdkAdapterPrototype ||
        !review->provider_local_host_execution_receipt_identity.has_value() ||
        review->starts_host_process || review->reads_host_environment ||
        review->opens_network_connection || review->materializes_sdk_request_payload ||
        review->invokes_provider_sdk || review->writes_host_filesystem ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider local host execution receipt review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_local_host_execution_receipt_review_blocked_ok() {
    auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->execution_status =
        ahfl::durable_store_import::ProviderLocalHostExecutionStatus::Blocked;
    receipt->operation_kind =
        ahfl::durable_store_import::ProviderLocalHostExecutionOperationKind::
            NoopHostExecutionNotReady;
    receipt->provider_local_host_execution_receipt_identity.reset();
    receipt->failure_attribution =
        ahfl::durable_store_import::ProviderLocalHostExecutionFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderLocalHostExecutionFailureKind::
                HostExecutionNotReady,
            .message = "test blocked local host receipt",
        };

    const auto review =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt_review(*receipt);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    if (review.review->next_action !=
            ahfl::durable_store_import::
                ProviderLocalHostExecutionReceiptReviewNextActionKind::WaitForHostExecutionPlan ||
        review.review->provider_local_host_execution_receipt_identity.has_value() ||
        !review.review->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider local host execution receipt review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_local_host_execution_receipt_review_rejects_invalid_receipt() {
    auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->format_version =
        "ahfl.durable-store-import-provider-local-host-execution-receipt.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt_review(*receipt);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider local host receipt to fail review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider local host execution receipt format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_adapter_request_ok() {
    const auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_request_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->request_status != ahfl::durable_store_import::ProviderSdkAdapterStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
                PlanProviderSdkAdapterRequest ||
        !plan->provider_sdk_adapter_request_identity.has_value() ||
        !plan->provider_sdk_adapter_response_placeholder_identity.has_value() ||
        plan->materializes_sdk_request_payload || plan->invokes_provider_sdk ||
        plan->opens_network_connection || plan->reads_secret_material ||
        plan->reads_host_environment || plan->writes_host_filesystem ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK adapter request plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_request_blocked_ok() {
    auto receipt = make_valid_provider_local_host_execution_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    receipt->execution_status =
        ahfl::durable_store_import::ProviderLocalHostExecutionStatus::Blocked;
    receipt->operation_kind =
        ahfl::durable_store_import::ProviderLocalHostExecutionOperationKind::
            NoopHostExecutionNotReady;
    receipt->provider_local_host_execution_receipt_identity.reset();
    receipt->failure_attribution =
        ahfl::durable_store_import::ProviderLocalHostExecutionFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderLocalHostExecutionFailureKind::
                HostExecutionNotReady,
            .message = "blocked local host receipt",
        };

    const auto plan = ahfl::durable_store_import::build_provider_sdk_adapter_request_plan(*receipt);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->request_status !=
            ahfl::durable_store_import::ProviderSdkAdapterStatus::Blocked ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
                NoopLocalHostExecutionNotReady ||
        plan.plan->provider_sdk_adapter_request_identity.has_value() ||
        plan.plan->provider_sdk_adapter_response_placeholder_identity.has_value() ||
        !plan.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider SDK adapter request plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_request_rejects_side_effects() {
    auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->materializes_sdk_request_payload = true;
    plan->invokes_provider_sdk = true;
    plan->opens_network_connection = true;
    plan->reads_secret_material = true;
    plan->reads_host_environment = true;
    plan->writes_host_filesystem = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_request_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK adapter request side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               validation.diagnostics, "cannot materialize SDK request payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke provider SDK") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot read secret material") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot read host environment") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot write host filesystem")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_adapter_request_rejects_forbidden_material() {
    auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->provider_endpoint_uri = "https://provider.example.invalid";
    plan->object_path = "bucket/receipt.json";
    plan->database_table = "receipt-table";
    plan->sdk_request_payload = "{\"unsafe\":true}";
    plan->sdk_response_payload = "{\"unsafe\":true}";
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_request_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK adapter request forbidden material to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain object_path") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain database_table") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_response_payload")
               ? 0
               : 1;
}

int build_durable_store_import_provider_sdk_adapter_request_ready_receipt() {
    const auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    if (plan->durable_store_import_provider_sdk_adapter_request_identity !=
            "durable-store-import-provider-sdk-adapter-request::run-partial-001::ready" ||
        plan->provider_sdk_adapter_request_identity !=
            std::optional<std::string>(
                "provider-sdk-adapter-request::provider-local-host-execution-receipt::provider-host-execution-descriptor::provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted") ||
        plan->provider_sdk_adapter_response_placeholder_identity !=
            std::optional<std::string>(
                "provider-sdk-adapter-response-placeholder::provider-sdk-adapter-request::provider-local-host-execution-receipt::provider-host-execution-descriptor::provider-sdk-host-handoff::provider-sdk-invocation-envelope::provider-driver-operation::provider-persistence::workflow-value-flow::run-partial-001::accepted")) {
        std::cerr << "unexpected ready provider SDK adapter request bootstrap result\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_response_placeholder_ok() {
    const auto placeholder = make_valid_provider_sdk_adapter_response_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_response_placeholder(
            *placeholder);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (placeholder->response_status !=
            ahfl::durable_store_import::ProviderSdkAdapterStatus::Ready ||
        placeholder->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
                PlanProviderSdkAdapterResponsePlaceholder ||
        !placeholder->provider_sdk_adapter_response_placeholder_identity.has_value() ||
        placeholder->materializes_sdk_request_payload || placeholder->invokes_provider_sdk ||
        placeholder->opens_network_connection || placeholder->reads_secret_material ||
        placeholder->reads_host_environment || placeholder->writes_host_filesystem ||
        placeholder->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK adapter response placeholder\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_adapter_response_placeholder_blocked_ok() {
    auto plan = make_valid_provider_sdk_adapter_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->request_status = ahfl::durable_store_import::ProviderSdkAdapterStatus::Blocked;
    plan->operation_kind =
        ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
            NoopLocalHostExecutionNotReady;
    plan->provider_sdk_adapter_request_identity.reset();
    plan->provider_sdk_adapter_response_placeholder_identity.reset();
    plan->failure_attribution =
        ahfl::durable_store_import::ProviderSdkAdapterFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderSdkAdapterFailureKind::
                LocalHostExecutionNotReady,
            .message = "blocked SDK adapter request",
        };

    const auto placeholder =
        ahfl::durable_store_import::build_provider_sdk_adapter_response_placeholder(*plan);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        placeholder.diagnostics.render(std::cout);
        return 1;
    }

    if (placeholder.placeholder->response_status !=
            ahfl::durable_store_import::ProviderSdkAdapterStatus::Blocked ||
        placeholder.placeholder->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
                NoopSdkAdapterRequestNotReady ||
        placeholder.placeholder->provider_sdk_adapter_response_placeholder_identity.has_value() ||
        !placeholder.placeholder->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider SDK adapter response placeholder\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_readiness_ok() {
    const auto review = make_valid_provider_sdk_adapter_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderSdkAdapterReadinessNextActionKind::
                ReadyForProviderSdkAdapterInterface ||
        !review->provider_sdk_adapter_response_placeholder_identity.has_value() ||
        review->materializes_sdk_request_payload || review->invokes_provider_sdk ||
        review->opens_network_connection || review->reads_secret_material ||
        review->reads_host_environment || review->writes_host_filesystem ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK adapter readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_adapter_readiness_rejects_invalid_placeholder() {
    auto placeholder = make_valid_provider_sdk_adapter_response_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    placeholder->format_version =
        "ahfl.durable-store-import-provider-sdk-adapter-response-placeholder.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_readiness_review(*placeholder);
    if (!review.has_errors()) {
        std::cerr << "expected invalid SDK adapter placeholder to fail readiness review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider SDK adapter response placeholder format_version "
               "must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_adapter_interface_ok() {
    const auto plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_interface_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->interface_status !=
            ahfl::durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
                PlanProviderSdkAdapterInterface ||
        plan->capability_descriptor.support_status !=
            ahfl::durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported ||
        plan->normalized_error_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None ||
        !plan->returns_placeholder_result || plan->materializes_sdk_request_payload ||
        plan->invokes_provider_sdk || plan->opens_network_connection ||
        plan->reads_secret_material || plan->reads_host_environment ||
        plan->writes_host_filesystem || plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK adapter interface plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_interface_rejects_forbidden_material() {
    auto plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->provider_endpoint_uri = "https://provider.example.invalid";
    plan->credential_reference = "secret://provider";
    plan->sdk_request_payload = "{\"unsafe\":true}";
    plan->sdk_response_payload = "{\"unsafe\":true}";
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_interface_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider SDK adapter interface forbidden material to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain credential_reference") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_response_payload")
               ? 0
               : 1;
}

int build_durable_store_import_provider_sdk_adapter_interface_blocked_ok() {
    auto request_plan = make_valid_provider_sdk_adapter_request_plan();
    if (!request_plan.has_value()) {
        return 1;
    }

    request_plan->request_status = ahfl::durable_store_import::ProviderSdkAdapterStatus::Blocked;
    request_plan->operation_kind =
        ahfl::durable_store_import::ProviderSdkAdapterOperationKind::
            NoopLocalHostExecutionNotReady;
    request_plan->provider_sdk_adapter_request_identity.reset();
    request_plan->provider_sdk_adapter_response_placeholder_identity.reset();
    request_plan->failure_attribution =
        ahfl::durable_store_import::ProviderSdkAdapterFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderSdkAdapterFailureKind::
                LocalHostExecutionNotReady,
            .message = "blocked SDK adapter request",
        };

    const auto plan =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_plan(*request_plan);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->interface_status !=
            ahfl::durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
                NoopSdkAdapterRequestNotReady ||
        plan.plan->capability_descriptor.support_status !=
            ahfl::durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported ||
        plan.plan->normalized_error_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterNormalizedErrorKind::
                SdkAdapterRequestNotReady ||
        plan.plan->returns_placeholder_result || !plan.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider SDK adapter interface plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_adapter_interface_review_ok() {
    const auto review = make_valid_provider_sdk_adapter_interface_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_adapter_interface_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
                ReadyForMockAdapterImplementation ||
        review->capability_support_status !=
            ahfl::durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported ||
        review->normalized_error_kind !=
            ahfl::durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None ||
        !review->returns_placeholder_result || review->materializes_sdk_request_payload ||
        review->invokes_provider_sdk || review->opens_network_connection ||
        review->reads_secret_material || review->reads_host_environment ||
        review->writes_host_filesystem || review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider SDK adapter interface review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_sdk_adapter_interface_review_rejects_invalid_plan() {
    auto plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->format_version =
        "ahfl.durable-store-import-provider-sdk-adapter-interface-plan.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_review(*plan);
    if (!review.has_errors()) {
        std::cerr << "expected invalid SDK adapter interface plan to fail review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider SDK adapter interface plan format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_config_load_ok() {
    const auto plan = make_valid_provider_config_load_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::validate_provider_config_load_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->config_load_status != ahfl::durable_store_import::ProviderConfigStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad ||
        plan->reads_secret_material || plan->materializes_secret_value ||
        plan->materializes_credential_material || plan->opens_network_connection ||
        plan->reads_host_environment || plan->writes_host_filesystem ||
        plan->materializes_sdk_request_payload || plan->invokes_provider_sdk ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider config load plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_config_load_rejects_forbidden_material() {
    auto plan = make_valid_provider_config_load_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->secret_value = "unsafe-secret";
    plan->credential_material = "unsafe-credential";
    plan->provider_endpoint_uri = "https://provider.example.invalid";
    plan->remote_config_uri = "https://config.example.invalid";
    plan->sdk_request_payload = "{\"unsafe\":true}";
    const auto validation = ahfl::durable_store_import::validate_provider_config_load_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider config load forbidden material to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain secret_value") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain credential_material") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain provider_endpoint_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain remote_config_uri") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain sdk_request_payload")
               ? 0
               : 1;
}

int build_durable_store_import_provider_config_load_blocked_ok() {
    auto interface_plan = make_valid_provider_sdk_adapter_interface_plan();
    if (!interface_plan.has_value()) {
        return 1;
    }

    interface_plan->interface_status =
        ahfl::durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked;
    interface_plan->operation_kind =
        ahfl::durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopSdkAdapterRequestNotReady;
    interface_plan->capability_descriptor.support_status =
        ahfl::durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported;
    interface_plan->normalized_error_kind =
        ahfl::durable_store_import::ProviderSdkAdapterNormalizedErrorKind::
            SdkAdapterRequestNotReady;
    interface_plan->returns_placeholder_result = false;
    interface_plan->failure_attribution =
        ahfl::durable_store_import::ProviderSdkAdapterInterfaceFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderSdkAdapterInterfaceFailureKind::
                SdkAdapterRequestNotReady,
            .message = "blocked adapter interface",
        };

    const auto plan = ahfl::durable_store_import::build_provider_config_load_plan(*interface_plan);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->config_load_status !=
            ahfl::durable_store_import::ProviderConfigStatus::Blocked ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderConfigOperationKind::
                NoopAdapterInterfaceNotReady ||
        !plan.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider config load plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_config_snapshot_ok() {
    const auto placeholder = make_valid_provider_config_snapshot_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_config_snapshot_placeholder(*placeholder);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (placeholder->snapshot_status !=
            ahfl::durable_store_import::ProviderConfigStatus::Ready ||
        placeholder->operation_kind !=
            ahfl::durable_store_import::ProviderConfigOperationKind::
                PlanProviderConfigSnapshotPlaceholder ||
        placeholder->reads_secret_material || placeholder->materializes_secret_value ||
        placeholder->materializes_credential_material || placeholder->opens_network_connection ||
        placeholder->reads_host_environment || placeholder->writes_host_filesystem ||
        placeholder->materializes_sdk_request_payload || placeholder->invokes_provider_sdk ||
        placeholder->failure_attribution.has_value()) {
        std::cerr << "unexpected provider config snapshot placeholder\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_config_readiness_ok() {
    const auto review = make_valid_provider_config_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_config_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderConfigReadinessNextActionKind::
                ReadyForSecretHandleResolver ||
        review->reads_secret_material || review->materializes_secret_value ||
        review->materializes_credential_material || review->opens_network_connection ||
        review->reads_host_environment || review->writes_host_filesystem ||
        review->materializes_sdk_request_payload || review->invokes_provider_sdk ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider config readiness review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_config_readiness_rejects_invalid_snapshot() {
    auto placeholder = make_valid_provider_config_snapshot_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    placeholder->format_version =
        "ahfl.durable-store-import-provider-config-snapshot-placeholder.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_config_readiness_review(*placeholder);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider config snapshot to fail readiness review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider config snapshot placeholder format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_secret_resolver_request_ok() {
    const auto plan = make_valid_provider_secret_resolver_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_secret_resolver_request_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->request_status != ahfl::durable_store_import::ProviderSecretStatus::Ready ||
        plan->operation_kind !=
            ahfl::durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest ||
        plan->reads_secret_material || plan->materializes_secret_value ||
        plan->materializes_credential_material || plan->materializes_token_value ||
        plan->opens_network_connection || plan->reads_host_environment ||
        plan->writes_host_filesystem || plan->invokes_secret_manager ||
        plan->secret_value.has_value() || plan->credential_material.has_value() ||
        plan->token_value.has_value() || plan->secret_manager_response.has_value() ||
        plan->failure_attribution.has_value()) {
        std::cerr << "unexpected provider secret resolver request plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_secret_resolver_request_rejects_forbidden_material() {
    auto plan = make_valid_provider_secret_resolver_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->secret_value = "unsafe-secret";
    plan->credential_material = "unsafe-credential";
    plan->token_value = "unsafe-token";
    plan->secret_manager_response = "{\"unsafe\":true}";
    const auto validation =
        ahfl::durable_store_import::validate_provider_secret_resolver_request_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider secret forbidden material to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot contain secret_value") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain credential_material") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain token_value") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot contain secret_manager_response")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_secret_resolver_request_rejects_side_effects() {
    auto plan = make_valid_provider_secret_resolver_request_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->reads_secret_material = true;
    plan->materializes_secret_value = true;
    plan->materializes_credential_material = true;
    plan->materializes_token_value = true;
    plan->opens_network_connection = true;
    plan->reads_host_environment = true;
    plan->writes_host_filesystem = true;
    plan->invokes_secret_manager = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_secret_resolver_request_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected provider secret side effects to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot read secret material") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot invoke secret manager")
               ? 0
               : 1;
}

int build_durable_store_import_provider_secret_resolver_request_blocked_ok() {
    auto snapshot = make_valid_provider_config_snapshot_placeholder();
    if (!snapshot.has_value()) {
        return 1;
    }

    snapshot->snapshot_status = ahfl::durable_store_import::ProviderConfigStatus::Blocked;
    snapshot->operation_kind =
        ahfl::durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady;
    snapshot->failure_attribution =
        ahfl::durable_store_import::ProviderConfigFailureAttribution{
            .kind = ahfl::durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady,
            .message = "blocked config snapshot",
        };

    const auto plan =
        ahfl::durable_store_import::build_provider_secret_resolver_request_plan(*snapshot);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->request_status != ahfl::durable_store_import::ProviderSecretStatus::Blocked ||
        plan.plan->operation_kind !=
            ahfl::durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady ||
        !plan.plan->failure_attribution.has_value()) {
        std::cerr << "unexpected blocked provider secret resolver request plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_secret_resolver_response_ok() {
    const auto placeholder = make_valid_provider_secret_resolver_response_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_secret_resolver_response_placeholder(
            *placeholder);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (placeholder->response_status != ahfl::durable_store_import::ProviderSecretStatus::Ready ||
        placeholder->credential_lifecycle_state !=
            ahfl::durable_store_import::ProviderCredentialLifecycleState::
                PlaceholderPendingResolution ||
        placeholder->reads_secret_material || placeholder->materializes_secret_value ||
        placeholder->materializes_credential_material || placeholder->materializes_token_value ||
        placeholder->opens_network_connection || placeholder->reads_host_environment ||
        placeholder->writes_host_filesystem || placeholder->invokes_secret_manager ||
        placeholder->failure_attribution.has_value()) {
        std::cerr << "unexpected provider secret resolver response placeholder\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_secret_policy_review_ok() {
    const auto review = make_valid_provider_secret_policy_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_secret_policy_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (review->next_action !=
            ahfl::durable_store_import::ProviderSecretPolicyNextActionKind::
                ReadyForLocalHostHarness ||
        review->reads_secret_material || review->materializes_secret_value ||
        review->materializes_credential_material || review->materializes_token_value ||
        review->opens_network_connection || review->reads_host_environment ||
        review->writes_host_filesystem || review->invokes_secret_manager ||
        review->failure_attribution.has_value()) {
        std::cerr << "unexpected provider secret policy review\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_provider_secret_policy_review_rejects_invalid_placeholder() {
    auto placeholder = make_valid_provider_secret_resolver_response_placeholder();
    if (!placeholder.has_value()) {
        return 1;
    }

    placeholder->format_version =
        "ahfl.durable-store-import-provider-secret-resolver-response-placeholder.v999";
    const auto review =
        ahfl::durable_store_import::build_provider_secret_policy_review(*placeholder);
    if (!review.has_errors()) {
        std::cerr << "expected invalid provider secret placeholder to fail policy review\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(
               review.diagnostics,
               "durable store import provider secret resolver response placeholder format_version "
               "must be")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_host_harness_request_ok() {
    const auto request = make_valid_provider_local_host_harness_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_harness_execution_request(
            *request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (request->request_status !=
            ahfl::durable_store_import::ProviderLocalHostHarnessStatus::Ready ||
        request->opens_network_connection || request->reads_secret_material ||
        request->writes_host_filesystem || request->reads_host_environment ||
        request->injects_secret || request->invokes_provider_sdk ||
        !request->sandbox_policy.test_only || request->sandbox_policy.allow_network ||
        request->sandbox_policy.allow_secret || request->sandbox_policy.allow_filesystem_write ||
        request->sandbox_policy.allow_host_environment) {
        std::cerr << "unexpected local host harness request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_local_host_harness_request_rejects_sandbox_escape() {
    auto request = make_valid_provider_local_host_harness_request();
    if (!request.has_value()) {
        return 1;
    }

    request->sandbox_policy.allow_network = true;
    request->opens_network_connection = true;
    request->reads_secret_material = true;
    request->writes_host_filesystem = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_harness_execution_request(
            *request);
    if (!validation.has_errors()) {
        std::cerr << "expected sandbox escape to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "sandbox policy must be test-only") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot open network connection")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_host_harness_record_ok() {
    const auto record = make_valid_provider_local_host_harness_record();
    if (!record.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_harness_execution_record(
            *record);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (record->outcome_kind !=
            ahfl::durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded ||
        record->exit_code != 0 || record->timed_out || record->sandbox_denied ||
        record->failure_attribution.has_value()) {
        std::cerr << "unexpected local host harness record\n";
        return 1;
    }

    return 0;
}

int run_durable_store_import_provider_local_host_harness_matrix_ok() {
    auto request = make_valid_provider_local_host_harness_request();
    if (!request.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderLocalHostHarnessFixtureKind fixtures[] = {
        ahfl::durable_store_import::ProviderLocalHostHarnessFixtureKind::NonzeroExit,
        ahfl::durable_store_import::ProviderLocalHostHarnessFixtureKind::Timeout,
        ahfl::durable_store_import::ProviderLocalHostHarnessFixtureKind::SandboxDenied,
    };
    for (const auto fixture : fixtures) {
        request->fixture_kind = fixture;
        const auto record = ahfl::durable_store_import::run_provider_local_host_test_harness(
            *request);
        if (record.has_errors() || !record.record.has_value() ||
            !record.record->failure_attribution.has_value()) {
            record.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_local_host_harness_review_ok() {
    const auto review = make_valid_provider_local_host_harness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_host_harness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->next_action ==
                   ahfl::durable_store_import::ProviderLocalHostHarnessNextActionKind::
                       ReadyForSdkPayloadMaterialization
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_payload_plan_ok() {
    const auto plan = make_valid_provider_sdk_payload_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_payload_materialization_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (plan->payload_status != ahfl::durable_store_import::ProviderSdkPayloadStatus::Ready ||
        !plan->fake_provider_only || plan->payload_digest.empty() ||
        plan->persists_materialized_payload || plan->opens_network_connection ||
        plan->reads_secret_material || plan->materializes_secret_value ||
        plan->materializes_credential_material || plan->materializes_token_value ||
        plan->invokes_provider_sdk || plan->raw_payload.has_value()) {
        std::cerr << "unexpected SDK payload plan\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_payload_plan_rejects_forbidden_fields() {
    auto plan = make_valid_provider_sdk_payload_plan();
    if (!plan.has_value()) {
        return 1;
    }

    plan->raw_payload = "{\"unsafe\":true}";
    plan->secret_value = "unsafe-secret";
    plan->credential_material = "unsafe-credential";
    plan->token_value = "unsafe-token";
    plan->persists_materialized_payload = true;
    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_payload_materialization_plan(*plan);
    if (!validation.has_errors()) {
        std::cerr << "expected SDK payload forbidden fields to fail\n";
        return 1;
    }

    return ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                   "cannot persist raw_payload") &&
                   ahfl::test_support::diagnostics_contain(validation.diagnostics,
                                                           "cannot contain secret_value") &&
                   ahfl::test_support::diagnostics_contain(
                       validation.diagnostics, "cannot persist materialized payload")
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_payload_audit_ok() {
    const auto audit = make_valid_provider_sdk_payload_audit();
    if (!audit.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_payload_audit_summary(*audit);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return audit->next_action ==
                       ahfl::durable_store_import::ProviderSdkPayloadNextActionKind::
                           ReadyForMockAdapter &&
                   !audit->raw_payload_persisted && audit->redaction_summary.secret_free
               ? 0
               : 1;
}

int validate_durable_store_import_provider_sdk_mock_adapter_contract_ok() {
    const auto contract = make_valid_provider_sdk_mock_adapter_contract();
    if (!contract.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_mock_adapter_contract(*contract);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return contract->contract_status ==
                       ahfl::durable_store_import::ProviderSdkMockAdapterStatus::Ready &&
                   !contract->opens_network_connection && !contract->reads_secret_material &&
                   !contract->invokes_real_provider_sdk
               ? 0
               : 1;
}

int run_durable_store_import_provider_sdk_mock_adapter_matrix_ok() {
    auto contract = make_valid_provider_sdk_mock_adapter_contract();
    if (!contract.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind scenarios[] = {
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
    };
    for (const auto scenario : scenarios) {
        contract->scenario_kind = scenario;
        const auto result = ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
        if (result.has_errors() || !result.result.has_value()) {
            result.diagnostics.render(std::cout);
            return 1;
        }
        if (scenario == ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success) {
            if (result.result->normalized_result !=
                    ahfl::durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::
                        Accepted ||
                result.result->failure_attribution.has_value()) {
                return 1;
            }
        } else if (!result.result->failure_attribution.has_value()) {
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_sdk_mock_adapter_readiness_ok() {
    const auto readiness = make_valid_provider_sdk_mock_adapter_readiness();
    if (!readiness.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_sdk_mock_adapter_readiness(*readiness);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return readiness->next_action ==
                   ahfl::durable_store_import::ProviderSdkMockAdapterNextActionKind::
                       ReadyForRealAdapterParity
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_filesystem_alpha_plan_ok() {
    const auto plan = make_valid_provider_local_filesystem_alpha_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_filesystem_alpha_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return plan->plan_status ==
                       ahfl::durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready &&
                   plan->provider_key == "local-filesystem-alpha" &&
                   plan->real_provider_alpha && plan->fake_adapter_default_path_preserved &&
                   plan->opt_in_required && !plan->opt_in_enabled &&
                   !plan->opens_network_connection && !plan->reads_secret_material &&
                   !plan->invokes_cloud_provider_sdk
               ? 0
               : 1;
}

int validate_durable_store_import_provider_local_filesystem_alpha_result_dry_run_ok() {
    const auto result = make_valid_provider_local_filesystem_alpha_result();
    if (!result.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_filesystem_alpha_result(*result);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return result->normalized_result ==
                       ahfl::durable_store_import::ProviderLocalFilesystemAlphaResultKind::
                           DryRunOnly &&
                   !result->wrote_local_file && !result->opt_in_used &&
                   result->failure_attribution.has_value()
               ? 0
               : 1;
}

int run_durable_store_import_provider_local_filesystem_alpha_opt_in_write_ok() {
    auto plan = make_valid_provider_local_filesystem_alpha_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto target_dir =
        std::filesystem::temp_directory_path() / "ahfl-provider-alpha-test-run";
    std::filesystem::remove_all(target_dir);
    plan->opt_in_enabled = true;
    plan->target_directory = target_dir.string();
    const auto result = ahfl::durable_store_import::run_provider_local_filesystem_alpha(*plan);
    if (result.has_errors() || !result.result.has_value()) {
        result.diagnostics.render(std::cout);
        return 1;
    }

    const auto target_path = target_dir / plan->target_object_name;
    const auto ok = result.result->normalized_result ==
                        ahfl::durable_store_import::ProviderLocalFilesystemAlphaResultKind::
                            Accepted &&
                    result.result->wrote_local_file && result.result->opt_in_used &&
                    std::filesystem::exists(target_path);
    std::filesystem::remove_all(target_dir);
    return ok ? 0 : 1;
}

int validate_durable_store_import_provider_local_filesystem_alpha_readiness_ok() {
    const auto readiness = make_valid_provider_local_filesystem_alpha_readiness();
    if (!readiness.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_local_filesystem_alpha_readiness(
            *readiness);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return readiness->next_action ==
                   ahfl::durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::
                       ReadyForIdempotencyContract
               ? 0
               : 1;
}

int validate_durable_store_import_provider_write_retry_decision_ok() {
    const auto decision = make_valid_provider_write_retry_decision();
    if (!decision.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_retry_decision(*decision);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return decision->retry_eligibility ==
                       ahfl::durable_store_import::ProviderWriteRetryEligibility::
                           NotApplicable &&
                   !decision->retry_allowed && !decision->duplicate_write_possible
               ? 0
               : 1;
}

int build_durable_store_import_provider_write_retry_decision_matrix_ok() {
    auto contract = make_valid_provider_sdk_mock_adapter_contract();
    if (!contract.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind scenarios[] = {
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
    };
    for (const auto scenario : scenarios) {
        contract->scenario_kind = scenario;
        const auto adapter_result =
            ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
        if (adapter_result.has_errors() || !adapter_result.result.has_value()) {
            adapter_result.diagnostics.render(std::cout);
            return 1;
        }
        const auto decision =
            ahfl::durable_store_import::build_provider_write_retry_decision(
                *adapter_result.result);
        if (decision.has_errors() || !decision.decision.has_value()) {
            decision.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_write_commit_receipt_ok() {
    const auto receipt = make_valid_provider_write_commit_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_commit_receipt(*receipt);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return receipt->commit_status ==
                       ahfl::durable_store_import::ProviderWriteCommitStatus::Committed &&
                   receipt->secret_free && !receipt->raw_provider_payload_persisted &&
                   !receipt->failure_attribution.has_value()
               ? 0
               : 1;
}

int build_durable_store_import_provider_write_commit_receipt_matrix_ok() {
    auto contract = make_valid_provider_sdk_mock_adapter_contract();
    if (!contract.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind scenarios[] = {
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure,
    };
    for (const auto scenario : scenarios) {
        contract->scenario_kind = scenario;
        const auto adapter_result =
            ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
        if (adapter_result.has_errors() || !adapter_result.result.has_value()) {
            adapter_result.diagnostics.render(std::cout);
            return 1;
        }
        const auto decision =
            ahfl::durable_store_import::build_provider_write_retry_decision(
                *adapter_result.result);
        if (decision.has_errors() || !decision.decision.has_value()) {
            decision.diagnostics.render(std::cout);
            return 1;
        }
        const auto receipt =
            ahfl::durable_store_import::build_provider_write_commit_receipt(
                *decision.decision);
        if (receipt.has_errors() || !receipt.receipt.has_value()) {
            receipt.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_write_commit_review_ok() {
    const auto review = make_valid_provider_write_commit_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_commit_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->next_action ==
                   ahfl::durable_store_import::ProviderWriteCommitNextActionKind::
                       ReadyForRecoveryAudit
               ? 0
               : 1;
}

int validate_durable_store_import_provider_write_recovery_checkpoint_ok() {
    const auto checkpoint = make_valid_provider_write_recovery_checkpoint();
    if (!checkpoint.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_recovery_checkpoint(*checkpoint);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return checkpoint->recovery_eligibility ==
                       ahfl::durable_store_import::ProviderWriteRecoveryEligibility::
                           NotRequired &&
                   !checkpoint->failure_attribution.has_value()
               ? 0
               : 1;
}

int build_durable_store_import_provider_write_recovery_matrix_ok() {
    auto receipt = make_valid_provider_write_commit_receipt();
    if (!receipt.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderWriteCommitStatus statuses[] = {
        ahfl::durable_store_import::ProviderWriteCommitStatus::Committed,
        ahfl::durable_store_import::ProviderWriteCommitStatus::Duplicate,
        ahfl::durable_store_import::ProviderWriteCommitStatus::Partial,
        ahfl::durable_store_import::ProviderWriteCommitStatus::Failed,
        ahfl::durable_store_import::ProviderWriteCommitStatus::Blocked,
    };
    for (const auto status : statuses) {
        auto candidate = *receipt;
        candidate.commit_status = status;
        if (status != ahfl::durable_store_import::ProviderWriteCommitStatus::Committed &&
            !candidate.failure_attribution.has_value()) {
            candidate.failure_attribution =
                ahfl::durable_store_import::ProviderWriteCommitFailureAttribution{
                    .kind = ahfl::durable_store_import::ProviderWriteCommitFailureKind::
                        ProviderFailure,
                    .message = "synthetic recovery matrix failure",
                };
        }
        const auto checkpoint =
            ahfl::durable_store_import::build_provider_write_recovery_checkpoint(candidate);
        if (checkpoint.has_errors() || !checkpoint.checkpoint.has_value()) {
            checkpoint.diagnostics.render(std::cout);
            return 1;
        }
        const auto plan =
            ahfl::durable_store_import::build_provider_write_recovery_plan(*checkpoint.checkpoint);
        if (plan.has_errors() || !plan.plan.has_value()) {
            plan.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_write_recovery_review_ok() {
    const auto review = make_valid_provider_write_recovery_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_write_recovery_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->next_action ==
                   ahfl::durable_store_import::ProviderWriteRecoveryNextActionKind::
                       ReadyForAuditEvent
               ? 0
               : 1;
}

int validate_durable_store_import_provider_failure_taxonomy_report_ok() {
    const auto report = make_valid_provider_failure_taxonomy_report();
    if (!report.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_failure_taxonomy_report(*report);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return report->failure_kind == ahfl::durable_store_import::ProviderFailureKindV1::None &&
                   report->retryability ==
                       ahfl::durable_store_import::ProviderFailureRetryabilityV1::NotApplicable &&
                   !report->secret_bearing_error_persisted &&
                   !report->raw_provider_error_persisted
               ? 0
               : 1;
}

int build_durable_store_import_provider_failure_taxonomy_matrix_ok() {
    auto contract = make_valid_provider_sdk_mock_adapter_contract();
    auto recovery_plan = make_valid_provider_write_recovery_plan();
    if (!contract.has_value() || !recovery_plan.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind scenarios[] = {
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
    };
    for (const auto scenario : scenarios) {
        contract->scenario_kind = scenario;
        const auto adapter_result =
            ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
        if (adapter_result.has_errors() || !adapter_result.result.has_value()) {
            adapter_result.diagnostics.render(std::cout);
            return 1;
        }
        const auto report = ahfl::durable_store_import::build_provider_failure_taxonomy_report(
            *adapter_result.result, *recovery_plan);
        if (report.has_errors() || !report.report.has_value()) {
            report.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_failure_taxonomy_review_ok() {
    const auto review = make_valid_provider_failure_taxonomy_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_failure_taxonomy_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->operator_action ==
                   ahfl::durable_store_import::ProviderFailureOperatorActionV1::None
               ? 0
               : 1;
}

int validate_durable_store_import_provider_execution_audit_event_ok() {
    const auto event = make_valid_provider_execution_audit_event();
    if (!event.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_execution_audit_event(*event);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return event->outcome == ahfl::durable_store_import::ProviderAuditOutcome::Success &&
                   event->secret_free && !event->raw_telemetry_persisted
               ? 0
               : 1;
}

int build_durable_store_import_provider_execution_audit_event_matrix_ok() {
    auto contract = make_valid_provider_sdk_mock_adapter_contract();
    auto recovery_plan = make_valid_provider_write_recovery_plan();
    if (!contract.has_value() || !recovery_plan.has_value()) {
        return 1;
    }

    const ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind scenarios[] = {
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Success,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict,
        ahfl::durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure,
    };
    for (const auto scenario : scenarios) {
        contract->scenario_kind = scenario;
        const auto adapter_result =
            ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
        if (adapter_result.has_errors() || !adapter_result.result.has_value()) {
            adapter_result.diagnostics.render(std::cout);
            return 1;
        }
        const auto report = ahfl::durable_store_import::build_provider_failure_taxonomy_report(
            *adapter_result.result, *recovery_plan);
        if (report.has_errors() || !report.report.has_value()) {
            report.diagnostics.render(std::cout);
            return 1;
        }
        const auto event =
            ahfl::durable_store_import::build_provider_execution_audit_event(*report.report);
        if (event.has_errors() || !event.event.has_value()) {
            event.diagnostics.render(std::cout);
            return 1;
        }
    }

    return 0;
}

int validate_durable_store_import_provider_telemetry_summary_ok() {
    const auto summary = make_valid_provider_telemetry_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_telemetry_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return summary->provider_write_committed && summary->secret_free &&
                   !summary->raw_telemetry_persisted
               ? 0
               : 1;
}

int validate_durable_store_import_provider_operator_review_event_ok() {
    const auto review = make_valid_provider_operator_review_event();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_operator_review_event(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->next_action == ahfl::durable_store_import::ProviderAuditNextActionKind::Archive
               ? 0
               : 1;
}

int validate_durable_store_import_provider_compatibility_test_manifest_ok() {
    const auto manifest = make_valid_provider_compatibility_test_manifest();
    if (!manifest.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_compatibility_test_manifest(*manifest);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return manifest->includes_mock_adapter && manifest->includes_local_filesystem_alpha &&
                   !manifest->external_service_required
               ? 0
               : 1;
}

int validate_durable_store_import_provider_fixture_matrix_ok() {
    const auto matrix = make_valid_provider_fixture_matrix();
    if (!matrix.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::validate_provider_fixture_matrix(*matrix);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return matrix->covers_mock_adapter && matrix->covers_local_filesystem_alpha &&
                   matrix->covers_success_fixture && matrix->covers_timeout_fixture
               ? 0
               : 1;
}

int validate_durable_store_import_provider_compatibility_report_ok() {
    const auto report = make_valid_provider_compatibility_report();
    if (!report.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_compatibility_report(*report);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return report->status == ahfl::durable_store_import::ProviderCompatibilityStatus::Passed &&
                   report->capability_matrix_complete && !report->external_service_required
               ? 0
               : 1;
}

int build_durable_store_import_provider_compatibility_report_blocked_ok() {
    auto report = make_valid_provider_compatibility_report();
    auto telemetry = make_valid_provider_telemetry_summary();
    auto matrix = make_valid_provider_fixture_matrix();
    if (!report.has_value() || !telemetry.has_value() || !matrix.has_value()) {
        return 1;
    }

    telemetry->outcome = ahfl::durable_store_import::ProviderAuditOutcome::Blocked;
    const auto blocked =
        ahfl::durable_store_import::build_provider_compatibility_report(*matrix, *telemetry);
    if (blocked.has_errors() || !blocked.report.has_value()) {
        blocked.diagnostics.render(std::cout);
        return 1;
    }

    return blocked.report->status ==
                   ahfl::durable_store_import::ProviderCompatibilityStatus::Blocked
               ? 0
               : 1;
}

int validate_durable_store_import_provider_registry_ok() {
    const auto registry = make_valid_provider_registry();
    if (!registry.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::validate_provider_registry(*registry);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return registry->registered_provider_count == 2 && !registry->unaudited_fallback_allowed
               ? 0
               : 1;
}

int validate_durable_store_import_provider_selection_plan_ok() {
    const auto plan = make_valid_provider_selection_plan();
    if (!plan.has_value()) {
        return 1;
    }

    const auto validation = ahfl::durable_store_import::validate_provider_selection_plan(*plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return plan->selection_status == ahfl::durable_store_import::ProviderSelectionStatus::Selected &&
                   !plan->requires_operator_review
               ? 0
               : 1;
}

int validate_durable_store_import_provider_capability_negotiation_review_ok() {
    const auto review = make_valid_provider_capability_negotiation_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_capability_negotiation_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->negotiation_status ==
                   ahfl::durable_store_import::ProviderCapabilityNegotiationStatus::Compatible
               ? 0
               : 1;
}

int build_durable_store_import_provider_selection_fallback_ok() {
    auto registry = make_valid_provider_registry();
    if (!registry.has_value()) {
        return 1;
    }

    registry->compatibility_status =
        ahfl::durable_store_import::ProviderCompatibilityStatus::Failed;
    const auto plan = ahfl::durable_store_import::build_provider_selection_plan(*registry);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    return plan.plan->selection_status ==
                       ahfl::durable_store_import::ProviderSelectionStatus::FallbackSelected &&
                   plan.plan->degradation_allowed
               ? 0
               : 1;
}

int validate_durable_store_import_provider_production_readiness_evidence_ok() {
    const auto evidence = make_valid_provider_production_readiness_evidence();
    if (!evidence.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_production_readiness_evidence(*evidence);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return evidence->blocking_issue_count == 0 && evidence->security_evidence_passed &&
                   evidence->registry_evidence_passed
               ? 0
               : 1;
}

int validate_durable_store_import_provider_production_readiness_review_ok() {
    const auto review = make_valid_provider_production_readiness_review();
    if (!review.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_production_readiness_review(*review);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return review->release_gate ==
                   ahfl::durable_store_import::ProviderProductionReleaseGate::
                       ReadyForProductionReview
               ? 0
               : 1;
}

int validate_durable_store_import_provider_production_readiness_report_ok() {
    const auto report = make_valid_provider_production_readiness_report();
    if (!report.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_provider_production_readiness_report(*report);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return report->release_gate ==
                   ahfl::durable_store_import::ProviderProductionReleaseGate::
                       ReadyForProductionReview
               ? 0
               : 1;
}

int build_durable_store_import_provider_production_readiness_blocked_ok() {
    auto evidence = make_valid_provider_production_readiness_evidence();
    if (!evidence.has_value()) {
        return 1;
    }

    evidence->compatibility_evidence_passed = false;
    evidence->blocking_issue_count = 1;
    const auto review =
        ahfl::durable_store_import::build_provider_production_readiness_review(*evidence);
    if (review.has_errors() || !review.review.has_value()) {
        review.diagnostics.render(std::cout);
        return 1;
    }

    return review.review->release_gate ==
                   ahfl::durable_store_import::ProviderProductionReleaseGate::Blocked
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

    // V0.23: Provider Runtime Preflight and Readiness tests
    if (command == "validate-durable-store-import-provider-runtime-preflight-ok") {
        return validate_durable_store_import_provider_runtime_preflight_ok();
    }

    if (command == "validate-durable-store-import-provider-runtime-preflight-blocked-ok") {
        return validate_durable_store_import_provider_runtime_preflight_blocked_ok();
    }

    if (command ==
        "build-durable-store-import-provider-runtime-preflight-unsupported-capability-ok") {
        return build_durable_store_import_provider_runtime_preflight_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-runtime-preflight-profile-mismatch-ok") {
        return build_durable_store_import_provider_runtime_preflight_profile_mismatch_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-runtime-profile-rejects-secret-material") {
        return validate_durable_store_import_provider_runtime_profile_rejects_secret_material();
    }

    if (command ==
        "validate-durable-store-import-provider-runtime-profile-rejects-provider-coordinates") {
        return validate_durable_store_import_provider_runtime_profile_rejects_provider_coordinates();
    }

    if (command ==
        "validate-durable-store-import-provider-runtime-preflight-rejects-side-effects") {
        return validate_durable_store_import_provider_runtime_preflight_rejects_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-runtime-preflight-rejects-ready-without-envelope") {
        return validate_durable_store_import_provider_runtime_preflight_rejects_ready_without_envelope();
    }

    if (command == "build-durable-store-import-provider-runtime-preflight-ready-binding") {
        return build_durable_store_import_provider_runtime_preflight_ready_binding();
    }

    if (command ==
        "build-durable-store-import-provider-runtime-preflight-rejects-invalid-driver-binding") {
        return build_durable_store_import_provider_runtime_preflight_rejects_invalid_driver_binding();
    }

    if (command == "validate-durable-store-import-provider-runtime-readiness-ok") {
        return validate_durable_store_import_provider_runtime_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-runtime-readiness-unsupported-capability-ok") {
        return build_durable_store_import_provider_runtime_readiness_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-runtime-readiness-rejects-invalid-preflight") {
        return build_durable_store_import_provider_runtime_readiness_rejects_invalid_preflight();
    }

    // V0.24: Provider SDK Envelope and Handoff Readiness tests
    if (command == "validate-durable-store-import-provider-sdk-envelope-ok") {
        return validate_durable_store_import_provider_sdk_envelope_ok();
    }

    if (command == "validate-durable-store-import-provider-sdk-envelope-blocked-ok") {
        return validate_durable_store_import_provider_sdk_envelope_blocked_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-envelope-unsupported-capability-ok") {
        return build_durable_store_import_provider_sdk_envelope_unsupported_capability_ok();
    }

    if (command == "build-durable-store-import-provider-sdk-envelope-policy-mismatch-ok") {
        return build_durable_store_import_provider_sdk_envelope_policy_mismatch_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-policy-rejects-secret-material") {
        return validate_durable_store_import_provider_sdk_policy_rejects_secret_material();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-policy-rejects-provider-coordinates") {
        return validate_durable_store_import_provider_sdk_policy_rejects_provider_coordinates();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-envelope-rejects-side-effects") {
        return validate_durable_store_import_provider_sdk_envelope_rejects_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-envelope-rejects-ready-without-handoff") {
        return validate_durable_store_import_provider_sdk_envelope_rejects_ready_without_handoff();
    }

    if (command == "build-durable-store-import-provider-sdk-envelope-ready-preflight") {
        return build_durable_store_import_provider_sdk_envelope_ready_preflight();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-envelope-rejects-invalid-preflight") {
        return build_durable_store_import_provider_sdk_envelope_rejects_invalid_preflight();
    }

    if (command == "validate-durable-store-import-provider-sdk-handoff-readiness-ok") {
        return validate_durable_store_import_provider_sdk_handoff_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-handoff-readiness-unsupported-capability-ok") {
        return build_durable_store_import_provider_sdk_handoff_readiness_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-handoff-readiness-rejects-invalid-envelope") {
        return build_durable_store_import_provider_sdk_handoff_readiness_rejects_invalid_envelope();
    }

    // V0.25: Provider Host Execution and Readiness tests
    if (command == "validate-durable-store-import-provider-host-execution-ok") {
        return validate_durable_store_import_provider_host_execution_ok();
    }

    if (command == "validate-durable-store-import-provider-host-execution-blocked-ok") {
        return validate_durable_store_import_provider_host_execution_blocked_ok();
    }

    if (command ==
        "build-durable-store-import-provider-host-execution-unsupported-capability-ok") {
        return build_durable_store_import_provider_host_execution_unsupported_capability_ok();
    }

    if (command == "build-durable-store-import-provider-host-execution-policy-mismatch-ok") {
        return build_durable_store_import_provider_host_execution_policy_mismatch_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-host-policy-rejects-secret-material") {
        return validate_durable_store_import_provider_host_policy_rejects_secret_material();
    }

    if (command ==
        "validate-durable-store-import-provider-host-policy-rejects-host-side-effects") {
        return validate_durable_store_import_provider_host_policy_rejects_host_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-host-execution-rejects-side-effects") {
        return validate_durable_store_import_provider_host_execution_rejects_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-host-execution-rejects-ready-without-descriptor") {
        return validate_durable_store_import_provider_host_execution_rejects_ready_without_descriptor();
    }

    if (command == "build-durable-store-import-provider-host-execution-ready-envelope") {
        return build_durable_store_import_provider_host_execution_ready_envelope();
    }

    if (command ==
        "build-durable-store-import-provider-host-execution-rejects-invalid-envelope") {
        return build_durable_store_import_provider_host_execution_rejects_invalid_envelope();
    }

    if (command == "validate-durable-store-import-provider-host-execution-readiness-ok") {
        return validate_durable_store_import_provider_host_execution_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-host-execution-readiness-unsupported-capability-ok") {
        return build_durable_store_import_provider_host_execution_readiness_unsupported_capability_ok();
    }

    if (command ==
        "build-durable-store-import-provider-host-execution-readiness-rejects-invalid-host-execution") {
        return build_durable_store_import_provider_host_execution_readiness_rejects_invalid_host_execution();
    }

    // V0.26: Provider Local Host Execution Receipt and Review tests
    if (command == "validate-durable-store-import-provider-local-host-execution-receipt-ok") {
        return validate_durable_store_import_provider_local_host_execution_receipt_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-local-host-execution-receipt-blocked-ok") {
        return validate_durable_store_import_provider_local_host_execution_receipt_blocked_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-local-host-execution-receipt-rejects-side-effects") {
        return validate_durable_store_import_provider_local_host_execution_receipt_rejects_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-local-host-execution-receipt-rejects-ready-without-identity") {
        return validate_durable_store_import_provider_local_host_execution_receipt_rejects_ready_without_identity();
    }

    if (command ==
        "build-durable-store-import-provider-local-host-execution-receipt-ready-host-execution") {
        return build_durable_store_import_provider_local_host_execution_receipt_ready_host_execution();
    }

    if (command ==
        "build-durable-store-import-provider-local-host-execution-receipt-rejects-invalid-host-execution") {
        return build_durable_store_import_provider_local_host_execution_receipt_rejects_invalid_host_execution();
    }

    if (command ==
        "validate-durable-store-import-provider-local-host-execution-receipt-review-ok") {
        return validate_durable_store_import_provider_local_host_execution_receipt_review_ok();
    }

    if (command ==
        "build-durable-store-import-provider-local-host-execution-receipt-review-blocked-ok") {
        return build_durable_store_import_provider_local_host_execution_receipt_review_blocked_ok();
    }

    if (command ==
        "build-durable-store-import-provider-local-host-execution-receipt-review-rejects-invalid-receipt") {
        return build_durable_store_import_provider_local_host_execution_receipt_review_rejects_invalid_receipt();
    }

    // V0.27: Provider SDK Adapter request/response/readiness tests
    if (command == "validate-durable-store-import-provider-sdk-adapter-request-ok") {
        return validate_durable_store_import_provider_sdk_adapter_request_ok();
    }

    if (command == "validate-durable-store-import-provider-sdk-adapter-request-blocked-ok") {
        return validate_durable_store_import_provider_sdk_adapter_request_blocked_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-adapter-request-rejects-side-effects") {
        return validate_durable_store_import_provider_sdk_adapter_request_rejects_side_effects();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-adapter-request-rejects-forbidden-material") {
        return validate_durable_store_import_provider_sdk_adapter_request_rejects_forbidden_material();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-adapter-request-ready-receipt") {
        return build_durable_store_import_provider_sdk_adapter_request_ready_receipt();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-adapter-response-placeholder-ok") {
        return validate_durable_store_import_provider_sdk_adapter_response_placeholder_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-adapter-response-placeholder-blocked-ok") {
        return build_durable_store_import_provider_sdk_adapter_response_placeholder_blocked_ok();
    }

    if (command == "validate-durable-store-import-provider-sdk-adapter-readiness-ok") {
        return validate_durable_store_import_provider_sdk_adapter_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-adapter-readiness-rejects-invalid-placeholder") {
        return build_durable_store_import_provider_sdk_adapter_readiness_rejects_invalid_placeholder();
    }

    // V0.28: Provider SDK Adapter interface tests
    if (command == "validate-durable-store-import-provider-sdk-adapter-interface-ok") {
        return validate_durable_store_import_provider_sdk_adapter_interface_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-adapter-interface-rejects-forbidden-material") {
        return validate_durable_store_import_provider_sdk_adapter_interface_rejects_forbidden_material();
    }

    if (command == "build-durable-store-import-provider-sdk-adapter-interface-blocked-ok") {
        return build_durable_store_import_provider_sdk_adapter_interface_blocked_ok();
    }

    if (command == "validate-durable-store-import-provider-sdk-adapter-interface-review-ok") {
        return validate_durable_store_import_provider_sdk_adapter_interface_review_ok();
    }

    if (command ==
        "build-durable-store-import-provider-sdk-adapter-interface-review-rejects-invalid-plan") {
        return build_durable_store_import_provider_sdk_adapter_interface_review_rejects_invalid_plan();
    }

    // V0.29: Provider Config load/snapshot/readiness tests
    if (command == "validate-durable-store-import-provider-config-load-ok") {
        return validate_durable_store_import_provider_config_load_ok();
    }

    if (command == "validate-durable-store-import-provider-config-load-rejects-forbidden-material") {
        return validate_durable_store_import_provider_config_load_rejects_forbidden_material();
    }

    if (command == "build-durable-store-import-provider-config-load-blocked-ok") {
        return build_durable_store_import_provider_config_load_blocked_ok();
    }

    if (command == "validate-durable-store-import-provider-config-snapshot-ok") {
        return validate_durable_store_import_provider_config_snapshot_ok();
    }

    if (command == "validate-durable-store-import-provider-config-readiness-ok") {
        return validate_durable_store_import_provider_config_readiness_ok();
    }

    if (command ==
        "build-durable-store-import-provider-config-readiness-rejects-invalid-snapshot") {
        return build_durable_store_import_provider_config_readiness_rejects_invalid_snapshot();
    }

    // V0.30: Provider secret resolver boundary tests
    if (command == "validate-durable-store-import-provider-secret-resolver-request-ok") {
        return validate_durable_store_import_provider_secret_resolver_request_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-secret-resolver-request-rejects-forbidden-material") {
        return validate_durable_store_import_provider_secret_resolver_request_rejects_forbidden_material();
    }

    if (command ==
        "validate-durable-store-import-provider-secret-resolver-request-rejects-side-effects") {
        return validate_durable_store_import_provider_secret_resolver_request_rejects_side_effects();
    }

    if (command == "build-durable-store-import-provider-secret-resolver-request-blocked-ok") {
        return build_durable_store_import_provider_secret_resolver_request_blocked_ok();
    }

    if (command == "validate-durable-store-import-provider-secret-resolver-response-ok") {
        return validate_durable_store_import_provider_secret_resolver_response_ok();
    }

    if (command == "validate-durable-store-import-provider-secret-policy-review-ok") {
        return validate_durable_store_import_provider_secret_policy_review_ok();
    }

    if (command ==
        "build-durable-store-import-provider-secret-policy-review-rejects-invalid-placeholder") {
        return build_durable_store_import_provider_secret_policy_review_rejects_invalid_placeholder();
    }

    // V0.31: Provider local host harness tests
    if (command == "validate-durable-store-import-provider-local-host-harness-request-ok") {
        return validate_durable_store_import_provider_local_host_harness_request_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-local-host-harness-request-rejects-sandbox-escape") {
        return validate_durable_store_import_provider_local_host_harness_request_rejects_sandbox_escape();
    }

    if (command == "validate-durable-store-import-provider-local-host-harness-record-ok") {
        return validate_durable_store_import_provider_local_host_harness_record_ok();
    }

    if (command == "run-durable-store-import-provider-local-host-harness-matrix-ok") {
        return run_durable_store_import_provider_local_host_harness_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-local-host-harness-review-ok") {
        return validate_durable_store_import_provider_local_host_harness_review_ok();
    }

    // V0.32: Provider SDK payload materialization tests
    if (command == "validate-durable-store-import-provider-sdk-payload-plan-ok") {
        return validate_durable_store_import_provider_sdk_payload_plan_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-sdk-payload-plan-rejects-forbidden-fields") {
        return validate_durable_store_import_provider_sdk_payload_plan_rejects_forbidden_fields();
    }

    if (command == "validate-durable-store-import-provider-sdk-payload-audit-ok") {
        return validate_durable_store_import_provider_sdk_payload_audit_ok();
    }

    // V0.33: Provider SDK mock adapter tests
    if (command == "validate-durable-store-import-provider-sdk-mock-adapter-contract-ok") {
        return validate_durable_store_import_provider_sdk_mock_adapter_contract_ok();
    }

    if (command == "run-durable-store-import-provider-sdk-mock-adapter-matrix-ok") {
        return run_durable_store_import_provider_sdk_mock_adapter_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-sdk-mock-adapter-readiness-ok") {
        return validate_durable_store_import_provider_sdk_mock_adapter_readiness_ok();
    }

    // V0.34: Provider local filesystem alpha tests
    if (command == "validate-durable-store-import-provider-local-filesystem-alpha-plan-ok") {
        return validate_durable_store_import_provider_local_filesystem_alpha_plan_ok();
    }

    if (command ==
        "validate-durable-store-import-provider-local-filesystem-alpha-result-dry-run-ok") {
        return validate_durable_store_import_provider_local_filesystem_alpha_result_dry_run_ok();
    }

    if (command ==
        "run-durable-store-import-provider-local-filesystem-alpha-opt-in-write-ok") {
        return run_durable_store_import_provider_local_filesystem_alpha_opt_in_write_ok();
    }

    if (command == "validate-durable-store-import-provider-local-filesystem-alpha-readiness-ok") {
        return validate_durable_store_import_provider_local_filesystem_alpha_readiness_ok();
    }

    // V0.35: Provider write retry/idempotency tests
    if (command == "validate-durable-store-import-provider-write-retry-decision-ok") {
        return validate_durable_store_import_provider_write_retry_decision_ok();
    }

    if (command == "build-durable-store-import-provider-write-retry-decision-matrix-ok") {
        return build_durable_store_import_provider_write_retry_decision_matrix_ok();
    }

    // V0.36: Provider write commit receipt tests
    if (command == "validate-durable-store-import-provider-write-commit-receipt-ok") {
        return validate_durable_store_import_provider_write_commit_receipt_ok();
    }

    if (command == "build-durable-store-import-provider-write-commit-receipt-matrix-ok") {
        return build_durable_store_import_provider_write_commit_receipt_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-write-commit-review-ok") {
        return validate_durable_store_import_provider_write_commit_review_ok();
    }

    // V0.37: Provider write recovery/resume tests
    if (command == "validate-durable-store-import-provider-write-recovery-checkpoint-ok") {
        return validate_durable_store_import_provider_write_recovery_checkpoint_ok();
    }

    if (command == "build-durable-store-import-provider-write-recovery-matrix-ok") {
        return build_durable_store_import_provider_write_recovery_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-write-recovery-review-ok") {
        return validate_durable_store_import_provider_write_recovery_review_ok();
    }

    // V0.38: Provider failure taxonomy tests
    if (command == "validate-durable-store-import-provider-failure-taxonomy-report-ok") {
        return validate_durable_store_import_provider_failure_taxonomy_report_ok();
    }

    if (command == "build-durable-store-import-provider-failure-taxonomy-matrix-ok") {
        return build_durable_store_import_provider_failure_taxonomy_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-failure-taxonomy-review-ok") {
        return validate_durable_store_import_provider_failure_taxonomy_review_ok();
    }

    // V0.39: Provider audit / telemetry tests
    if (command == "validate-durable-store-import-provider-execution-audit-event-ok") {
        return validate_durable_store_import_provider_execution_audit_event_ok();
    }

    if (command == "build-durable-store-import-provider-execution-audit-event-matrix-ok") {
        return build_durable_store_import_provider_execution_audit_event_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-telemetry-summary-ok") {
        return validate_durable_store_import_provider_telemetry_summary_ok();
    }

    if (command == "validate-durable-store-import-provider-operator-review-event-ok") {
        return validate_durable_store_import_provider_operator_review_event_ok();
    }

    // V0.40: Provider compatibility suite tests
    if (command == "validate-durable-store-import-provider-compatibility-test-manifest-ok") {
        return validate_durable_store_import_provider_compatibility_test_manifest_ok();
    }

    if (command == "validate-durable-store-import-provider-fixture-matrix-ok") {
        return validate_durable_store_import_provider_fixture_matrix_ok();
    }

    if (command == "validate-durable-store-import-provider-compatibility-report-ok") {
        return validate_durable_store_import_provider_compatibility_report_ok();
    }

    if (command == "build-durable-store-import-provider-compatibility-report-blocked-ok") {
        return build_durable_store_import_provider_compatibility_report_blocked_ok();
    }

    // V0.41: Provider registry / selection tests
    if (command == "validate-durable-store-import-provider-registry-ok") {
        return validate_durable_store_import_provider_registry_ok();
    }

    if (command == "validate-durable-store-import-provider-selection-plan-ok") {
        return validate_durable_store_import_provider_selection_plan_ok();
    }

    if (command == "validate-durable-store-import-provider-capability-negotiation-review-ok") {
        return validate_durable_store_import_provider_capability_negotiation_review_ok();
    }

    if (command == "build-durable-store-import-provider-selection-fallback-ok") {
        return build_durable_store_import_provider_selection_fallback_ok();
    }

    // V0.42: Provider production readiness tests
    if (command == "validate-durable-store-import-provider-production-readiness-evidence-ok") {
        return validate_durable_store_import_provider_production_readiness_evidence_ok();
    }

    if (command == "validate-durable-store-import-provider-production-readiness-review-ok") {
        return validate_durable_store_import_provider_production_readiness_review_ok();
    }

    if (command == "validate-durable-store-import-provider-production-readiness-report-ok") {
        return validate_durable_store_import_provider_production_readiness_report_ok();
    }

    if (command == "build-durable-store-import-provider-production-readiness-blocked-ok") {
        return build_durable_store_import_provider_production_readiness_blocked_ok();
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
