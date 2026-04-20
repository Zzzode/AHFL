#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/review.hpp"

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
make_valid_request() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_ready_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportReviewSummary>
make_valid_review_summary() {
    const auto request = make_valid_request();
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto summary =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *summary.summary;
}

int validate_durable_store_import_request_ok() {
    const auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_durable_store_import_request_blocked_ok() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_blocked_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::Blocked ||
        value.adapter_ready || !value.adapter_blocker.has_value() ||
        value.next_required_adapter_artifact_kind !=
            ahfl::durable_store_import::RequestedArtifactKind::PersistenceReview) {
        std::cerr << "unexpected blocked durable store import request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_request_terminal_failed_ok() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_failed_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::TerminalFailed ||
        value.adapter_ready || !value.adapter_blocker.has_value() ||
        value.adapter_blocker->kind !=
            ahfl::durable_store_import::AdapterBlockerKind::WorkflowFailure) {
        std::cerr << "unexpected failed durable store import request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_request_terminal_partial_ok() {
    const auto request = ahfl::durable_store_import::build_durable_store_import_request(
        make_partial_terminal_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::TerminalPartial ||
        value.adapter_ready || !value.adapter_blocker.has_value() ||
        value.adapter_blocker->kind !=
            ahfl::durable_store_import::AdapterBlockerKind::WorkflowPartial) {
        std::cerr << "unexpected partial durable store import request\n";
        return 1;
    }

    return 0;
}

int validate_durable_store_import_request_rejects_missing_request_identity() {
    auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    request->durable_store_import_request_identity.clear();
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected missing durable store import request identity to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "durable_store_import_request_identity must not be empty")
               ? 0
               : 1;
}

int validate_durable_store_import_request_rejects_duplicate_artifact_name() {
    auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    request->requested_artifact_set.entries[1].logical_artifact_name =
        "store-import-descriptor.json";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected duplicate requested artifact name to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics, "contains duplicate logical_artifact_name")
               ? 0
               : 1;
}

int validate_durable_store_import_request_rejects_ready_with_blocker() {
    auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    request->adapter_blocker = ahfl::durable_store_import::AdapterBlocker{
        .kind = ahfl::durable_store_import::AdapterBlockerKind::MissingRequestedArtifactSet,
        .message = "unexpected adapter blocker",
        .logical_artifact_name = std::nullopt,
    };
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected adapter_ready with blocker to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "cannot contain adapter_blocker when adapter_ready is true")
               ? 0
               : 1;
}

int validate_durable_store_import_request_rejects_unsupported_source_descriptor_format() {
    auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    request->source_store_import_descriptor_format_version = "ahfl.store-import-descriptor.v999";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source descriptor format to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "source_store_import_descriptor_format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_request_rejects_adapter_contract_blocked() {
    auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_blocked_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    request.request->request_boundary_kind =
        ahfl::durable_store_import::RequestBoundaryKind::AdapterContractConsumable;
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request.request);
    if (!validation.has_errors()) {
        std::cerr << "expected blocked adapter-contract-consumable request to fail\n";
        return 1;
    }

    return diagnostics_contain(
               validation.diagnostics,
               "adapter-contract-consumable boundary requires ready or completed request_status")
               ? 0
               : 1;
}

int validate_durable_store_import_request_rejects_terminal_failed_without_failure_summary() {
    auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_failed_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    request.request->workflow_failure_summary.reset();
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_request(*request.request);
    if (!validation.has_errors()) {
        std::cerr << "expected terminal failed without failure summary to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "TerminalFailed status requires workflow_failure_summary")
               ? 0
               : 1;
}

int build_durable_store_import_request_ready_descriptor() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_ready_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::ReadyForAdapter ||
        !value.adapter_ready || value.adapter_blocker.has_value() ||
        value.requested_artifact_set.entry_count != 4 ||
        value.requested_artifact_set.entries.front().artifact_kind !=
            ahfl::durable_store_import::RequestedArtifactKind::StoreImportDescriptor ||
        value.requested_artifact_set.entries.front().adapter_role !=
            ahfl::durable_store_import::RequestedArtifactAdapterRole::RequestAnchor ||
        value.durable_store_import_request_identity !=
            "durable-store-import::workflow-value-flow::run-partial-001::request-4") {
        std::cerr << "unexpected ready durable store import request bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_request_completed_descriptor() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_completed_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *request.request;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::TerminalCompleted ||
        !value.adapter_ready || value.adapter_blocker.has_value() ||
        value.request_boundary_kind !=
            ahfl::durable_store_import::RequestBoundaryKind::AdapterContractConsumable) {
        std::cerr << "unexpected completed durable store import request bootstrap result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_request_rejects_invalid_descriptor() {
    auto descriptor = make_ready_descriptor();
    descriptor.format_version = "ahfl.store-import-descriptor.v999";

    const auto request = ahfl::durable_store_import::build_durable_store_import_request(descriptor);
    if (!request.has_errors()) {
        std::cerr << "expected invalid store import descriptor to fail\n";
        return 1;
    }

    return diagnostics_contain(request.diagnostics,
                               "store import descriptor format_version must be")
               ? 0
               : 1;
}

int validate_durable_store_import_review_ok() {
    const auto summary = make_valid_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_review_summary(*summary);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int validate_durable_store_import_review_rejects_unsupported_source_request_format() {
    auto summary = make_valid_review_summary();
    if (!summary.has_value()) {
        return 1;
    }

    summary->source_durable_store_import_request_format_version =
        "ahfl.durable-store-import-request.v999";
    const auto validation =
        ahfl::durable_store_import::validate_durable_store_import_review_summary(*summary);
    if (!validation.has_errors()) {
        std::cerr << "expected unsupported source request format to fail\n";
        return 1;
    }

    return diagnostics_contain(validation.diagnostics,
                               "source_durable_store_import_request_format_version must be")
               ? 0
               : 1;
}

int build_durable_store_import_review_ready_request() {
    const auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    const auto summary =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::ReadyForAdapter ||
        !value.adapter_ready || value.adapter_blocker.has_value() ||
        value.next_action !=
            ahfl::durable_store_import::DurableStoreImportReviewNextActionKind::
                HandoffDurableStoreImportRequest ||
        value.requested_artifact_entry_count != 4 ||
        value.durable_store_import_request_identity !=
            "durable-store-import::workflow-value-flow::run-partial-001::request-4") {
        std::cerr << "unexpected ready durable store import review result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_review_failed_request() {
    const auto request =
        ahfl::durable_store_import::build_durable_store_import_request(make_failed_descriptor());
    if (request.has_errors() || !request.request.has_value()) {
        request.diagnostics.render(std::cout);
        return 1;
    }

    const auto summary =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request.request);
    if (summary.has_errors() || !summary.summary.has_value()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *summary.summary;
    if (value.request_status !=
            ahfl::durable_store_import::DurableStoreImportRequestStatus::TerminalFailed ||
        value.adapter_ready || !value.adapter_blocker.has_value() ||
        value.adapter_blocker->kind !=
            ahfl::durable_store_import::AdapterBlockerKind::WorkflowFailure ||
        value.next_action !=
            ahfl::durable_store_import::DurableStoreImportReviewNextActionKind::
                InvestigateDurableStoreImportFailure) {
        std::cerr << "unexpected failed durable store import review result\n";
        return 1;
    }

    return 0;
}

int build_durable_store_import_review_rejects_invalid_request() {
    auto request = make_valid_request();
    if (!request.has_value()) {
        return 1;
    }

    request->format_version = "ahfl.durable-store-import-request.v999";
    const auto summary =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request);
    if (!summary.has_errors()) {
        std::cerr << "expected invalid durable store import request to fail\n";
        return 1;
    }

    return diagnostics_contain(summary.diagnostics,
                               "durable store import request format_version must be")
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

    if (command == "validate-durable-store-import-request-ok") {
        return validate_durable_store_import_request_ok();
    }

    if (command == "validate-durable-store-import-request-blocked-ok") {
        return validate_durable_store_import_request_blocked_ok();
    }

    if (command == "validate-durable-store-import-request-terminal-failed-ok") {
        return validate_durable_store_import_request_terminal_failed_ok();
    }

    if (command == "validate-durable-store-import-request-terminal-partial-ok") {
        return validate_durable_store_import_request_terminal_partial_ok();
    }

    if (command == "validate-durable-store-import-request-rejects-missing-request-identity") {
        return validate_durable_store_import_request_rejects_missing_request_identity();
    }

    if (command == "validate-durable-store-import-request-rejects-duplicate-artifact-name") {
        return validate_durable_store_import_request_rejects_duplicate_artifact_name();
    }

    if (command == "validate-durable-store-import-request-rejects-ready-with-blocker") {
        return validate_durable_store_import_request_rejects_ready_with_blocker();
    }

    if (command ==
        "validate-durable-store-import-request-rejects-unsupported-source-descriptor-format") {
        return validate_durable_store_import_request_rejects_unsupported_source_descriptor_format();
    }

    if (command == "validate-durable-store-import-request-rejects-adapter-contract-blocked") {
        return validate_durable_store_import_request_rejects_adapter_contract_blocked();
    }

    if (command ==
        "validate-durable-store-import-request-rejects-terminal-failed-without-failure-summary") {
        return validate_durable_store_import_request_rejects_terminal_failed_without_failure_summary();
    }

    if (command == "build-durable-store-import-request-ready-descriptor") {
        return build_durable_store_import_request_ready_descriptor();
    }

    if (command == "build-durable-store-import-request-completed-descriptor") {
        return build_durable_store_import_request_completed_descriptor();
    }

    if (command == "build-durable-store-import-request-rejects-invalid-descriptor") {
        return build_durable_store_import_request_rejects_invalid_descriptor();
    }

    if (command == "validate-durable-store-import-review-ok") {
        return validate_durable_store_import_review_ok();
    }

    if (command ==
        "validate-durable-store-import-review-rejects-unsupported-source-request-format") {
        return validate_durable_store_import_review_rejects_unsupported_source_request_format();
    }

    if (command == "build-durable-store-import-review-ready-request") {
        return build_durable_store_import_review_ready_request();
    }

    if (command == "build-durable-store-import-review-failed-request") {
        return build_durable_store_import_review_failed_request();
    }

    if (command == "build-durable-store-import-review-rejects-invalid-request") {
        return build_durable_store_import_review_rejects_invalid_request();
    }

    std::cerr << "unknown test command: " << command << '\n';
    return 1;
}
