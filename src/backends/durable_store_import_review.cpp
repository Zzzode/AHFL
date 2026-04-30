#include "ahfl/backends/durable_store_import_review.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status) {
    switch (status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        return "ready_to_import";
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        return "blocked";
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string request_status_name(durable_store_import::RequestStatus status) {
    switch (status) {
    case durable_store_import::RequestStatus::ReadyForAdapter:
        return "ready_for_adapter";
    case durable_store_import::RequestStatus::Blocked:
        return "blocked";
    case durable_store_import::RequestStatus::TerminalCompleted:
        return "terminal_completed";
    case durable_store_import::RequestStatus::TerminalFailed:
        return "terminal_failed";
    case durable_store_import::RequestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
request_boundary_kind_name(durable_store_import::RequestBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::RequestBoundaryKind::LocalIntentOnly:
        return "local_intent_only";
    case durable_store_import::RequestBoundaryKind::AdapterContractConsumable:
        return "adapter_contract_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string artifact_kind_name(durable_store_import::RequestedArtifactKind kind) {
    switch (kind) {
    case durable_store_import::RequestedArtifactKind::StoreImportDescriptor:
        return "store_import_descriptor";
    case durable_store_import::RequestedArtifactKind::ExportManifest:
        return "export_manifest";
    case durable_store_import::RequestedArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case durable_store_import::RequestedArtifactKind::PersistenceReview:
        return "persistence_review";
    case durable_store_import::RequestedArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::RequestBlockerKind kind) {
    switch (kind) {
    case durable_store_import::RequestBlockerKind::WaitingOnRequestedArtifact:
        return "waiting_on_requested_artifact";
    case durable_store_import::RequestBlockerKind::MissingRequestIdentity:
        return "missing_durable_store_import_request_identity";
    case durable_store_import::RequestBlockerKind::MissingRequestedArtifactSet:
        return "missing_requested_artifact_set";
    case durable_store_import::RequestBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case durable_store_import::RequestBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(durable_store_import::ReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ReviewNextActionKind::HandoffDurableStoreImportRequest:
        return "handoff_durable_store_import_request";
    case durable_store_import::ReviewNextActionKind::AwaitAdapterReadiness:
        return "await_adapter_readiness";
    case durable_store_import::ReviewNextActionKind::ArchiveCompletedDurableStoreImportState:
        return "archive_completed_durable_store_import_state";
    case durable_store_import::ReviewNextActionKind::InvestigateDurableStoreImportFailure:
        return "investigate_durable_store_import_failure";
    case durable_store_import::ReviewNextActionKind::PreservePartialDurableStoreImportState:
        return "preserve_partial_durable_store_import_state";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_adapter_blocker(std::ostream &out,
                           int indent_level,
                           const std::optional<durable_store_import::RequestBlocker> &blocker) {
    line(out, indent_level, "adapter_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "logical_artifact_name " + (blocker->logical_artifact_name.has_value()
                                         ? *blocker->logical_artifact_name
                                         : "none"));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

void print_string_list(std::ostream &out,
                       int indent_level,
                       std::string_view label,
                       const std::vector<std::string> &values) {
    line(out, indent_level, std::string(label) + " {");
    line(out, indent_level + 1, "count " + std::to_string(values.size()));
    if (values.empty()) {
        line(out, indent_level + 1, "- none");
    } else {
        for (const auto &value : values) {
            line(out, indent_level + 1, "- " + value);
        }
    }
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_review(const durable_store_import::ReviewSummary &summary,
                                       std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "source_manifest_format " + summary.source_export_manifest_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "manifest_status " + manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "request_status " + request_status_name(summary.request_status));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "request_boundary_kind " + request_boundary_kind_name(summary.request_boundary_kind));
    line(out, 0, std::string("adapter_ready ") + (summary.adapter_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "adapter_boundary " + summary.adapter_boundary_summary);
    line(out, 0, "request_preview " + summary.request_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_required_adapter_artifact " +
             (summary.next_required_adapter_artifact_kind.has_value()
                  ? artifact_kind_name(*summary.next_required_adapter_artifact_kind)
                  : "none"));

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_adapter_blocker(out, 0, summary.adapter_blocker);
    print_string_list(out, 0, "requested_artifact_preview", summary.requested_artifact_preview);
}

} // namespace ahfl
