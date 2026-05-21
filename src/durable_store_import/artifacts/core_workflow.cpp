#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_adapter_execution {
namespace {

class DurableStoreImportAdapterExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportAdapterExecutionJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::AdapterExecutionReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field(
                "source_durable_store_import_decision_receipt_persistence_response_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_decision_receipt_persistence_response_format_version);
                });
            field(
                "source_durable_store_import_decision_receipt_persistence_request_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_decision_receipt_persistence_request_format_version);
                });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(receipt.source_durable_store_import_request_format_version);
            });
            field("source_package_identity", [&]() {
                print_optional(receipt.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(receipt.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(receipt.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(receipt.durable_store_import_receipt_persistence_request_identity);
            });
            field("durable_store_import_receipt_persistence_response_identity", [&]() {
                write_string(receipt.durable_store_import_receipt_persistence_response_identity);
            });
            field("durable_store_import_adapter_execution_identity",
                  [&]() { write_string(receipt.durable_store_import_adapter_execution_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(receipt.planned_durable_identity); });
            field("response_status", [&]() { print_response_status(receipt.response_status); });
            field("response_outcome", [&]() { print_response_outcome(receipt.response_outcome); });
            field("response_boundary_kind",
                  [&]() { print_response_boundary_kind(receipt.response_boundary_kind); });
            field("acknowledged_for_response",
                  [&]() { write_bool(receipt.acknowledged_for_response); });
            field("store_kind", [&]() { print_store_kind(receipt.store_kind); });
            field("local_fake_store_contract_version",
                  [&]() { write_string(receipt.local_fake_store_contract_version); });
            field("mutation_intent", [&]() { print_mutation_intent(receipt.mutation_intent, 1); });
            field("mutation_status", [&]() { print_mutation_status(receipt.mutation_status); });
            field("persistence_id", [&]() { print_optional_string(receipt.persistence_id); });
            field("failure_attribution", [&]() {
                print_optional(receipt.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
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

    void print_response_boundary_kind(durable_store_import::PersistenceResponseBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable,
                "adapter_response_consumable"));
    }

    void print_store_kind(durable_store_import::AdapterExecutionStoreKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionStoreKind::LocalFakeDurableStore,
                "local_fake_durable_store"));
    }

    void print_mutation_intent_kind(durable_store_import::StoreMutationIntentKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::PersistReceipt,
                                    "persist_receipt");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopBlocked,
                                    "noop_blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopDeferred,
                                    "noop_deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopRejected,
                                    "noop_rejected"));
    }

    void print_mutation_status(durable_store_import::StoreMutationStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::Persisted,
                                    "persisted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::NotMutated,
                                    "not_mutated"));
    }

    void print_capability(durable_store_import::AdapterCapabilityKind capability) {
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

    void print_failure_kind(durable_store_import::AdapterExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked,
                "source_response_blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred,
                "source_response_deferred");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected,
                "source_response_rejected"));
    }

    void print_mutation_intent(const durable_store_import::StoreMutationIntent &intent,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_mutation_intent_kind(intent.kind); });
            field("source_response_identity",
                  [&]() { write_string(intent.source_response_identity); });
            field("target_receipt_identity",
                  [&]() { write_string(intent.target_receipt_identity); });
            field("target_planned_durable_identity",
                  [&]() { write_string(intent.target_planned_durable_identity); });
            field("mutates_store", [&]() { write_bool(intent.mutates_store); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::AdapterExecutionFailureAttribution &failure, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("required_capability", [&]() {
                print_optional(failure.required_capability,
                               [&](const auto &value) { print_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_adapter_execution_json(
    const durable_store_import::AdapterExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportAdapterExecutionJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_adapter_execution

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision {

namespace {

class DurableStoreImportDecisionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Decision &decision) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(decision.format_version); });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(decision.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(decision.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(decision.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(decision.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(decision.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(decision.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(decision.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(decision.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(decision.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(decision.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(decision.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(decision.workflow_canonical_name); });
            field("session_id", [&]() { write_string(decision.session_id); });
            field("run_id", [&]() { print_optional_string(decision.run_id); });
            field("input_fixture", [&]() { write_string(decision.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(decision.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(decision.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(decision.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(decision.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(decision.descriptor_status); });
            field("request_status", [&]() { print_request_status(decision.request_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(decision.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(decision.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(decision.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(decision.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(decision.durable_store_import_decision_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(decision.planned_durable_identity); });
            field("request_boundary_kind",
                  [&]() { print_request_boundary_kind(decision.request_boundary_kind); });
            field("decision_boundary_kind",
                  [&]() { print_decision_boundary_kind(decision.decision_boundary_kind); });
            field("decision_status", [&]() { print_decision_status(decision.decision_status); });
            field("decision_outcome", [&]() { print_decision_outcome(decision.decision_outcome); });
            field("accepted_for_future_execution",
                  [&]() { write_bool(decision.accepted_for_future_execution); });
            field("next_required_adapter_capability", [&]() {
                print_optional(decision.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("decision_blocker", [&]() {
                print_optional(decision.decision_blocker,
                               [&](const auto &value) { print_decision_blocker(value, 1); });
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

    void print_request_boundary_kind(durable_store_import::RequestBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBoundaryKind::LocalIntentOnly,
                                    "local_intent_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBoundaryKind::AdapterContractConsumable,
                "adapter_contract_consumable"));
    }

    void print_decision_boundary_kind(durable_store_import::DecisionBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable,
                "adapter_decision_consumable"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_decision_outcome(durable_store_import::DecisionOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::AcceptRequest,
                                    "accept_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::BlockRequest,
                                    "block_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::DeferPartialRequest,
                                    "defer_partial_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::RejectFailedRequest,
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

    void print_decision_blocker(const durable_store_import::DecisionBlocker &blocker,
                                int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::DecisionBlockerKind::SourceRequestBlocked:
                    write_string("source_request_blocked");
                    return;
                case durable_store_import::DecisionBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::DecisionBlockerKind::WorkflowFailure:
                    write_string("workflow_failure");
                    return;
                case durable_store_import::DecisionBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
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

void print_durable_store_import_decision_json(const durable_store_import::Decision &decision,
                                              std::ostream &out) {
    DurableStoreImportDecisionJsonPrinter(out).print(decision);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision_review {

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

[[nodiscard]] std::string decision_outcome_name(durable_store_import::DecisionOutcome outcome) {
    switch (outcome) {
    case durable_store_import::DecisionOutcome::AcceptRequest:
        return "accept_request";
    case durable_store_import::DecisionOutcome::BlockRequest:
        return "block_request";
    case durable_store_import::DecisionOutcome::DeferPartialRequest:
        return "defer_partial_request";
    case durable_store_import::DecisionOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string
decision_boundary_kind_name(durable_store_import::DecisionBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::DecisionBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable:
        return "adapter_decision_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::DecisionReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::DecisionReviewNextActionKind::HandoffDurableStoreImportDecision:
        return "handoff_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::DecisionReviewNextActionKind::
        ArchiveCompletedDurableStoreImportDecision:
        return "archive_completed_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::
        PreservePartialDurableStoreImportDecision:
        return "preserve_partial_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::
        InvestigateDurableStoreImportDecisionRejection:
        return "investigate_durable_store_import_decision_rejection";
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

[[nodiscard]] std::string blocker_kind_name(durable_store_import::DecisionBlockerKind kind) {
    switch (kind) {
    case durable_store_import::DecisionBlockerKind::SourceRequestBlocked:
        return "source_request_blocked";
    case durable_store_import::DecisionBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::DecisionBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case durable_store_import::DecisionBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
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

void print_decision_blocker(std::ostream &out,
                            int indent_level,
                            const std::optional<durable_store_import::DecisionBlocker> &blocker) {
    line(out, indent_level, "decision_blocker {");
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

void print_durable_store_import_decision_review(
    const durable_store_import::DecisionReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
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
    line(out,
         0,
         "decision_boundary_kind " + decision_boundary_kind_name(summary.decision_boundary_kind));
    line(out, 0, "decision_outcome " + decision_outcome_name(summary.decision_outcome));
    line(out,
         0,
         std::string("accepted_for_future_execution ") +
             (summary.accepted_for_future_execution ? "true" : "false"));
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
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_contract " + summary.adapter_contract_summary);
    line(out, 0, "decision_preview " + summary.decision_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_decision_blocker(out, 0, summary.decision_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision_review

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt {

namespace {

class DurableStoreImportDecisionReceiptJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionReceiptJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Receipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(receipt.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(receipt.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(receipt.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(receipt.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(receipt.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(receipt.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(receipt.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(receipt.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(receipt.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(receipt.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(receipt.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(receipt.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(receipt.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(receipt.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(receipt.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(receipt.descriptor_status); });
            field("request_status", [&]() { print_request_status(receipt.request_status); });
            field("decision_status", [&]() { print_decision_status(receipt.decision_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(receipt.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(receipt.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(receipt.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(receipt.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(receipt.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(receipt.durable_store_import_receipt_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(receipt.planned_durable_identity); });
            field("decision_boundary_kind",
                  [&]() { print_decision_boundary_kind(receipt.decision_boundary_kind); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(receipt.receipt_boundary_kind); });
            field("receipt_status", [&]() { print_receipt_status(receipt.receipt_status); });
            field("receipt_outcome", [&]() { print_receipt_outcome(receipt.receipt_outcome); });
            field("accepted_for_receipt_archive",
                  [&]() { write_bool(receipt.accepted_for_receipt_archive); });
            field("next_required_adapter_capability", [&]() {
                print_optional(receipt.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("receipt_blocker", [&]() {
                print_optional(receipt.receipt_blocker,
                               [&](const auto &value) { print_receipt_blocker(value, 1); });
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

    void print_decision_boundary_kind(durable_store_import::DecisionBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable,
                "adapter_decision_consumable"));
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

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_receipt_outcome(durable_store_import::ReceiptOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::ArchiveAcceptedDecision,
                                    "archive_accepted_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::BlockBlockedDecision,
                                    "block_blocked_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::DeferPartialDecision,
                                    "defer_partial_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::RejectFailedDecision,
                                    "reject_failed_decision"));
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

    void print_receipt_blocker(const durable_store_import::ReceiptBlocker &blocker,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::ReceiptBlockerKind::SourceDecisionBlocked:
                    write_string("source_decision_blocked");
                    return;
                case durable_store_import::ReceiptBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::ReceiptBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
                    return;
                case durable_store_import::ReceiptBlockerKind::WorkflowFailure:
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

void print_durable_store_import_receipt_json(const durable_store_import::Receipt &receipt,
                                             std::ostream &out) {
    DurableStoreImportDecisionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_review {

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
receipt_boundary_kind_name(durable_store_import::ReceiptBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable:
        return "adapter_receipt_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_outcome_name(durable_store_import::ReceiptOutcome outcome) {
    switch (outcome) {
    case durable_store_import::ReceiptOutcome::ArchiveAcceptedDecision:
        return "archive_accepted_decision";
    case durable_store_import::ReceiptOutcome::BlockBlockedDecision:
        return "block_blocked_decision";
    case durable_store_import::ReceiptOutcome::DeferPartialDecision:
        return "defer_partial_decision";
    case durable_store_import::ReceiptOutcome::RejectFailedDecision:
        return "reject_failed_decision";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ReceiptReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ReceiptReviewNextActionKind::
        HandoffDurableStoreImportDecisionReceipt:
        return "handoff_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::ReceiptReviewNextActionKind::
        ArchiveCompletedDurableStoreImportDecisionReceipt:
        return "archive_completed_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::
        PreservePartialDurableStoreImportDecisionReceipt:
        return "preserve_partial_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::
        InvestigateDurableStoreImportDecisionReceiptRejection:
        return "investigate_durable_store_import_decision_receipt_rejection";
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

[[nodiscard]] std::string blocker_kind_name(durable_store_import::ReceiptBlockerKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBlockerKind::SourceDecisionBlocked:
        return "source_decision_blocked";
    case durable_store_import::ReceiptBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::ReceiptBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
    case durable_store_import::ReceiptBlockerKind::WorkflowFailure:
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

void print_receipt_blocker(std::ostream &out,
                           int indent_level,
                           const std::optional<durable_store_import::ReceiptBlocker> &blocker) {
    line(out, indent_level, "receipt_blocker {");
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

void print_durable_store_import_receipt_review(
    const durable_store_import::ReceiptReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
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
         "receipt_boundary_kind " + receipt_boundary_kind_name(summary.receipt_boundary_kind));
    line(out, 0, "receipt_outcome " + receipt_outcome_name(summary.receipt_outcome));
    line(out,
         0,
         std::string("accepted_for_receipt_archive ") +
             (summary.accepted_for_receipt_archive ? "true" : "false"));
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
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_receipt_contract " + summary.adapter_receipt_contract_summary);
    line(out, 0, "receipt_preview " + summary.receipt_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_receipt_blocker(out, 0, summary.receipt_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_review

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_recovery_preview {
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

[[nodiscard]] std::string mutation_status_name(durable_store_import::StoreMutationStatus status) {
    switch (status) {
    case durable_store_import::StoreMutationStatus::Persisted:
        return "persisted";
    case durable_store_import::StoreMutationStatus::NotMutated:
        return "not_mutated";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::RecoveryPreviewNextActionKind action) {
    switch (action) {
    case durable_store_import::RecoveryPreviewNextActionKind::NoRecoveryRequired:
        return "no_recovery_required";
    case durable_store_import::RecoveryPreviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::RecoveryPreviewNextActionKind::WaitForCapability:
        return "wait_for_capability";
    case durable_store_import::RecoveryPreviewNextActionKind::ReviewFailure:
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
failure_kind_name(durable_store_import::AdapterExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked:
        return "source_response_blocked";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred:
        return "source_response_deferred";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected:
        return "source_response_rejected";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::AdapterExecutionFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (failure->required_capability.has_value()
                                       ? capability_name(*failure->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_recovery_preview(
    const durable_store_import::RecoveryCommandPreview &preview, std::ostream &out) {
    out << preview.format_version << '\n';
    line(out,
         0,
         "source_adapter_execution_format " +
             preview.source_durable_store_import_adapter_execution_format_version);
    line(out,
         0,
         "source_response_format " +
             preview
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(out, 0, "workflow " + preview.workflow_canonical_name);
    line(out, 0, "session " + preview.session_id);
    line(out, 0, "run_id " + (preview.run_id.has_value() ? *preview.run_id : "none"));
    line(out, 0, "input_fixture " + preview.input_fixture);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             preview.durable_store_import_receipt_persistence_response_identity);
    line(out,
         0,
         "durable_store_import_adapter_execution_identity " +
             preview.durable_store_import_adapter_execution_identity);
    line(out, 0, "response_status " + response_status_name(preview.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(preview.response_outcome));
    line(out, 0, "mutation_status " + mutation_status_name(preview.mutation_status));
    line(out,
         0,
         "persistence_id " +
             (preview.persistence_id.has_value() ? *preview.persistence_id : std::string("none")));
    line(out, 0, "next_action " + next_action_name(preview.next_action));
    line(out, 0, "recovery_boundary " + preview.recovery_boundary_summary);
    line(out, 0, "next_step " + preview.next_step_recommendation);
    line(out, 0, "execution_summary {");
    line(out,
         1,
         "adapter_execution_identity " + preview.execution_summary.adapter_execution_identity);
    line(out,
         1,
         "mutation_status " + mutation_status_name(preview.execution_summary.mutation_status));
    line(out,
         1,
         "persistence_id " + (preview.execution_summary.persistence_id.has_value()
                                  ? *preview.execution_summary.persistence_id
                                  : std::string("none")));
    line(out, 0, "}");
    print_failure_attribution(out, 0, preview.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_recovery_preview

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_request {

namespace {

class DurableStoreImportRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Request &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
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
            field("planned_durable_identity",
                  [&]() { write_string(request.planned_durable_identity); });
            field("request_boundary_kind",
                  [&]() { print_request_boundary_kind(request.request_boundary_kind); });
            field("requested_artifact_set",
                  [&]() { print_requested_artifact_set(request.requested_artifact_set, 1); });
            field("adapter_ready", [&]() { write_bool(request.adapter_ready); });
            field("next_required_adapter_artifact_kind", [&]() {
                print_optional(request.next_required_adapter_artifact_kind,
                               [&](const auto &value) { print_requested_artifact_kind(value); });
            });
            field("request_status", [&]() { print_request_status(request.request_status); });
            field("adapter_blocker", [&]() {
                print_optional(request.adapter_blocker,
                               [&](const auto &value) { print_request_blocker(value, 1); });
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

    void print_request_boundary_kind(durable_store_import::RequestBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBoundaryKind::LocalIntentOnly,
                                    "local_intent_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBoundaryKind::AdapterContractConsumable,
                "adapter_contract_consumable"));
    }

    void print_requested_artifact_kind(durable_store_import::RequestedArtifactKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactKind::StoreImportDescriptor,
                "store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::ExportManifest,
                                    "export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactKind::PersistenceDescriptor,
                "persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::PersistenceReview,
                                    "persistence_review");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::CheckpointRecord,
                                    "checkpoint_record"));
    }

    void print_adapter_role(durable_store_import::RequestedArtifactAdapterRole role) {
        AHFL_ARTIFACT_PRINT_ENUM(
            role,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::RequestAnchor,
                "request_anchor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::ImportManifest,
                "import_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::DurableStateDescriptor,
                "durable_state_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::HumanReviewContext,
                "human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::CheckpointPayload,
                "checkpoint_payload"));
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

    void print_request_blocker_kind(durable_store_import::RequestBlockerKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::WaitingOnRequestedArtifact,
                "waiting_on_requested_artifact");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::MissingRequestIdentity,
                "missing_durable_store_import_request_identity");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::MissingRequestedArtifactSet,
                "missing_requested_artifact_set");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBlockerKind::WorkflowFailure,
                                    "workflow_failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBlockerKind::WorkflowPartial,
                                    "workflow_partial"));
    }

    void print_requested_artifact_entry(const durable_store_import::RequestedArtifactEntry &entry,
                                        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("artifact_kind", [&]() { print_requested_artifact_kind(entry.artifact_kind); });
            field("logical_artifact_name", [&]() { write_string(entry.logical_artifact_name); });
            field("source_format_version", [&]() { write_string(entry.source_format_version); });
            field("required_for_import", [&]() { write_bool(entry.required_for_import); });
            field("adapter_role", [&]() { print_adapter_role(entry.adapter_role); });
        });
    }

    void
    print_requested_artifact_set(const durable_store_import::RequestedArtifactSet &artifact_set,
                                 int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("entry_count", [&]() { out_ << artifact_set.entry_count; });
            field("entries", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &entry : artifact_set.entries) {
                        item([&]() { print_requested_artifact_entry(entry, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_request_blocker(const durable_store_import::RequestBlocker &blocker,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_request_blocker_kind(blocker.kind); });
            field("logical_artifact_name",
                  [&]() { print_optional_string(blocker.logical_artifact_name); });
            field("message", [&]() { write_string(blocker.message); });
        });
    }
};

} // namespace

void print_durable_store_import_request_json(const durable_store_import::Request &request,
                                             std::ostream &out) {
    DurableStoreImportRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_request

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_review {

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

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_review
