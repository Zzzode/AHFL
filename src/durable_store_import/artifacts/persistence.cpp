#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_request {

namespace {

class PersistenceRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit PersistenceRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::PersistenceRequest &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(request.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(request.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(request.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(request.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(request.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(request.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(request.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(request.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(request.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(request.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(request.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(request.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(request.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(request.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(request.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(request.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(request.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(request.descriptor_status); });
            field("request_status", [&]() { print_request_status(request.request_status); });
            field("decision_status", [&]() { print_decision_status(request.decision_status); });
            field("receipt_status", [&]() { print_receipt_status(request.receipt_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(request.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(request.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(request.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(request.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(request.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(request.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(request.durable_store_import_receipt_persistence_request_identity);
            });
            field("planned_durable_identity",
                  [&]() { write_string(request.planned_durable_identity); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(request.receipt_boundary_kind); });
            field("receipt_persistence_boundary_kind", [&]() {
                print_receipt_persistence_boundary_kind(request.receipt_persistence_boundary_kind);
            });
            field("receipt_persistence_request_status", [&]() {
                print_receipt_persistence_request_status(
                    request.receipt_persistence_request_status);
            });
            field("receipt_persistence_request_outcome", [&]() {
                print_receipt_persistence_request_outcome(
                    request.receipt_persistence_request_outcome);
            });
            field("accepted_for_receipt_persistence",
                  [&]() { write_bool(request.accepted_for_receipt_persistence); });
            field("next_required_adapter_capability", [&]() {
                print_optional(request.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("receipt_persistence_blocker", [&]() {
                print_optional(request.receipt_persistence_blocker, [&](const auto &value) {
                    print_receipt_persistence_blocker(value, 1);
                });
            });
        });
        out_ << '\n';
    }

  private:
    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(identity.format_version); });
            field("name", [&]() { write_string(identity.name); });
            field("version", [&]() { write_string(identity.version); });
        });
    }

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable,
                "adapter_receipt_consumable"));
    }

    void
    print_receipt_persistence_boundary_kind(durable_store_import::PersistenceBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable,
                "adapter_receipt_persistence_consumable"));
    }

    void print_receipt_persistence_request_status(
        durable_store_import::PersistenceRequestStatus status) {
        switch (status) {
        case durable_store_import::PersistenceRequestStatus::ReadyToPersist:
            write_string("ready_to_persist");
            return;
        case durable_store_import::PersistenceRequestStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::PersistenceRequestStatus::Deferred:
            write_string("deferred");
            return;
        case durable_store_import::PersistenceRequestStatus::Rejected:
            write_string("rejected");
            return;
        }
    }

    void print_receipt_persistence_request_outcome(
        durable_store_import::PersistenceRequestOutcome outcome) {
        switch (outcome) {
        case durable_store_import::PersistenceRequestOutcome::PersistReadyReceipt:
            write_string("persist_ready_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::BlockBlockedReceipt:
            write_string("block_blocked_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::DeferPartialReceipt:
            write_string("defer_partial_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::RejectFailedReceipt:
            write_string("reject_failed_receipt");
            return;
        }
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_receipt_persistence_blocker(const durable_store_import::PersistenceBlocker &blocker,
                                           int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::PersistenceBlockerKind::SourceReceiptBlocked:
                    write_string("source_receipt_blocked");
                    return;
                case durable_store_import::PersistenceBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::PersistenceBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
                    return;
                case durable_store_import::PersistenceBlockerKind::WorkflowFailure:
                    write_string("workflow_failure");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_persistence_request_json(
    const durable_store_import::PersistenceRequest &request, std::ostream &out) {
    PersistenceRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_response {

namespace {

class PersistenceResponseJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit PersistenceResponseJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::PersistenceResponse &response) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(response.format_version); });
            field(
                "source_durable_store_import_decision_receipt_persistence_request_format_version",
                [&]() {
                    write_string(
                        response
                            .source_durable_store_import_decision_receipt_persistence_request_format_version);
                });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(response.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(response.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(response.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(response.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(response.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(response.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(response.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(response.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(response.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(response.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(response.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(response.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(response.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(response.workflow_canonical_name); });
            field("session_id", [&]() { write_string(response.session_id); });
            field("run_id", [&]() { print_optional_string(response.run_id); });
            field("input_fixture", [&]() { write_string(response.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(response.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(response.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(response.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(response.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(response.descriptor_status); });
            field("request_status", [&]() { print_request_status(response.request_status); });
            field("decision_status", [&]() { print_decision_status(response.decision_status); });
            field("receipt_status", [&]() { print_receipt_status(response.receipt_status); });
            field("persistence_request_status",
                  [&]() { print_persistence_request_status(response.persistence_request_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(response.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(response.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(response.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(response.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(response.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(response.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(response.durable_store_import_receipt_persistence_request_identity);
            });
            field("durable_store_import_receipt_persistence_response_identity", [&]() {
                write_string(response.durable_store_import_receipt_persistence_response_identity);
            });
            field("planned_durable_identity",
                  [&]() { write_string(response.planned_durable_identity); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(response.receipt_boundary_kind); });
            field("receipt_persistence_boundary_kind", [&]() {
                print_receipt_persistence_boundary_kind(response.receipt_persistence_boundary_kind);
            });
            field("receipt_persistence_response_boundary_kind", [&]() {
                print_receipt_persistence_response_boundary_kind(
                    response.receipt_persistence_response_boundary_kind);
            });
            field("response_status", [&]() { print_response_status(response.response_status); });
            field("response_outcome", [&]() { print_response_outcome(response.response_outcome); });
            field("acknowledged_for_response",
                  [&]() { write_bool(response.acknowledged_for_response); });
            field("next_required_adapter_capability", [&]() {
                print_optional(response.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("response_blocker", [&]() {
                print_optional(response.response_blocker,
                               [&](const auto &value) { print_response_blocker(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(identity.format_version); });
            field("name", [&]() { write_string(identity.name); });
            field("version", [&]() { write_string(identity.version); });
        });
    }

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_persistence_request_status(durable_store_import::PersistenceRequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Deferred,
                                    "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Rejected,
                                    "rejected"));
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable,
                "adapter_receipt_consumable"));
    }

    void
    print_receipt_persistence_boundary_kind(durable_store_import::PersistenceBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable,
                "adapter_receipt_persistence_consumable"));
    }

    void print_receipt_persistence_response_boundary_kind(
        durable_store_import::PersistenceResponseBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
            write_string("local_contract_only");
            return;
        case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
            write_string("adapter_response_consumable");
            return;
        }
    }

    void print_response_status(durable_store_import::PersistenceResponseStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Accepted,
                                    "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Deferred,
                                    "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Rejected,
                                    "rejected"));
    }

    void print_response_outcome(durable_store_import::PersistenceResponseOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest,
                "accept_persistence_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest,
                "block_blocked_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest,
                "defer_deferred_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::RejectFailedRequest,
                "reject_failed_request"));
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_response_blocker(const durable_store_import::PersistenceResponseBlocker &blocker,
                                int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::PersistenceResponseBlockerKind::
                    SourcePersistenceRequestBlocked:
                    write_string("source_persistence_request_blocked");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::
                    MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::PartialPersistenceState:
                    write_string("partial_persistence_state");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::
                    PersistenceWorkflowFailure:
                    write_string("persistence_workflow_failure");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_persistence_response_json(
    const durable_store_import::PersistenceResponse &response, std::ostream &out) {
    PersistenceResponseJsonPrinter(out).print(response);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_response

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_response_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
response_status_name(durable_store_import::PersistenceResponseStatus status) {
    switch (status) {
    case durable_store_import::PersistenceResponseStatus::Accepted:
        return "accepted";
    case durable_store_import::PersistenceResponseStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceResponseStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceResponseStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_outcome_name(durable_store_import::PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accept_persistence_request";
    case durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest:
        return "block_blocked_request";
    case durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest:
        return "defer_deferred_request";
    case durable_store_import::PersistenceResponseOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_boundary_kind_name(durable_store_import::PersistenceResponseBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
        return "adapter_response_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::PersistenceResponseReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::PersistenceResponseReviewNextActionKind::AcknowledgeResponse:
        return "acknowledge_response";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::PersistenceResponseReviewNextActionKind::WaitForCapability:
        return "wait_for_capability";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ReviewFailure:
        return "review_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string
blocker_kind_name(durable_store_import::PersistenceResponseBlockerKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked:
        return "source_persistence_request_blocked";
    case durable_store_import::PersistenceResponseBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::PersistenceResponseBlockerKind::PartialPersistenceState:
        return "partial_persistence_state";
    case durable_store_import::PersistenceResponseBlockerKind::PersistenceWorkflowFailure:
        return "persistence_workflow_failure";
    }

    return "invalid";
}

void print_response_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::PersistenceResponseBlocker> &blocker) {
    line(out, indent_level, "response_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_persistence_response_review(
    const durable_store_import::PersistenceResponseReviewSummary &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_response_format " +
             review
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(
        out,
        0,
        "source_persistence_request_format " +
            review.source_durable_store_import_decision_receipt_persistence_request_format_version);
    line(out,
         0,
         "source_receipt_format " +
             review.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + review.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + review.source_durable_store_import_request_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_decision_identity " + review.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + review.durable_store_import_receipt_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_request_identity " +
             review.durable_store_import_receipt_persistence_request_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             review.durable_store_import_receipt_persistence_response_identity);
    line(out, 0, "response_status " + response_status_name(review.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(review.response_outcome));
    line(out,
         0,
         "response_boundary_kind " + response_boundary_kind_name(review.response_boundary_kind));
    line(out,
         0,
         std::string("acknowledged_for_response ") +
             (review.acknowledged_for_response ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out,
         0,
         "next_required_adapter_capability " +
             (review.next_required_adapter_capability.has_value()
                  ? capability_name(*review.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_response_contract " + review.adapter_response_contract_summary);
    line(out, 0, "response_preview " + review.response_preview.response_identity);
    line(out, 0, "next_step " + review.next_step_recommendation);

    print_response_blocker(out, 0, review.response_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_response_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_review {

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

[[nodiscard]] std::string decision_status_name(durable_store_import::DecisionStatus status) {
    switch (status) {
    case durable_store_import::DecisionStatus::Accepted:
        return "accepted";
    case durable_store_import::DecisionStatus::Blocked:
        return "blocked";
    case durable_store_import::DecisionStatus::Deferred:
        return "deferred";
    case durable_store_import::DecisionStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_status_name(durable_store_import::ReceiptStatus status) {
    switch (status) {
    case durable_store_import::ReceiptStatus::ReadyForArchive:
        return "ready_for_archive";
    case durable_store_import::ReceiptStatus::Blocked:
        return "blocked";
    case durable_store_import::ReceiptStatus::Deferred:
        return "deferred";
    case durable_store_import::ReceiptStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_request_status_name(durable_store_import::PersistenceRequestStatus status) {
    switch (status) {
    case durable_store_import::PersistenceRequestStatus::ReadyToPersist:
        return "ready_to_persist";
    case durable_store_import::PersistenceRequestStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceRequestStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceRequestStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_boundary_kind_name(durable_store_import::ReceiptBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable:
        return "adapter_receipt_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_boundary_kind_name(durable_store_import::PersistenceBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable:
        return "adapter_receipt_persistence_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_request_outcome_name(durable_store_import::PersistenceRequestOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceRequestOutcome::PersistReadyReceipt:
        return "persist_ready_receipt";
    case durable_store_import::PersistenceRequestOutcome::BlockBlockedReceipt:
        return "block_blocked_receipt";
    case durable_store_import::PersistenceRequestOutcome::DeferPartialReceipt:
        return "defer_partial_receipt";
    case durable_store_import::PersistenceRequestOutcome::RejectFailedReceipt:
        return "reject_failed_receipt";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::PersistenceReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::PersistenceReviewNextActionKind::HandoffPersistenceRequest:
        return "handoff_durable_store_import_decision_receipt_persistence_request";
    case durable_store_import::PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::PersistenceReviewNextActionKind::PersistCompletedReceipt:
        return "persist_completed_durable_store_import_decision_receipt";
    case durable_store_import::PersistenceReviewNextActionKind::PreservePartialReceipt:
        return "preserve_partial_durable_store_import_decision_receipt";
    case durable_store_import::PersistenceReviewNextActionKind::InvestigatePersistenceRejection:
        return "investigate_durable_store_import_decision_receipt_persistence_rejection";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::PersistenceBlockerKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceBlockerKind::SourceReceiptBlocked:
        return "source_receipt_blocked";
    case durable_store_import::PersistenceBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::PersistenceBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
    case durable_store_import::PersistenceBlockerKind::WorkflowFailure:
        return "workflow_failure";
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

void print_receipt_persistence_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::PersistenceBlocker> &blocker) {
    line(out, indent_level, "receipt_persistence_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_persistence_review(
    const durable_store_import::PersistenceReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_persistence_request_format " +
             summary
                 .source_durable_store_import_decision_receipt_persistence_request_format_version);
    line(out,
         0,
         "source_receipt_format " +
             summary.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + summary.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
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
    line(out, 0, "decision_status " + decision_status_name(summary.decision_status));
    line(out, 0, "receipt_status " + receipt_status_name(summary.receipt_status));
    line(out,
         0,
         "receipt_persistence_request_status " +
             receipt_persistence_request_status_name(summary.receipt_persistence_request_status));
    line(out,
         0,
         "receipt_boundary_kind " + receipt_boundary_kind_name(summary.receipt_boundary_kind));
    line(out,
         0,
         "receipt_persistence_boundary_kind " +
             receipt_persistence_boundary_kind_name(summary.receipt_persistence_boundary_kind));
    line(out,
         0,
         "receipt_persistence_request_outcome " +
             receipt_persistence_request_outcome_name(summary.receipt_persistence_request_outcome));
    line(out,
         0,
         std::string("accepted_for_receipt_persistence ") +
             (summary.accepted_for_receipt_persistence ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out,
         0,
         "durable_store_import_decision_identity " +
             summary.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + summary.durable_store_import_receipt_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_request_identity " +
             summary.durable_store_import_receipt_persistence_request_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out,
         0,
         "adapter_receipt_persistence_contract " +
             summary.adapter_receipt_persistence_contract_summary);
    line(out, 0, "persistence_preview " + summary.persistence_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_receipt_persistence_blocker(out, 0, summary.receipt_persistence_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_review
