#include "ahfl/backends/durable_store_import_receipt.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class DurableStoreImportDecisionReceiptJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportDecisionReceiptJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::Receipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_decision_format_version",
                  [&]() {
                      write_string(
                          receipt.source_durable_store_import_decision_format_version);
                  });
            field("source_durable_store_import_request_format_version",
                  [&]() {
                      write_string(
                          receipt.source_durable_store_import_request_format_version);
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
                if (receipt.source_package_identity.has_value()) {
                    print_package_identity(*receipt.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() {
                if (receipt.run_id.has_value()) {
                    write_string(*receipt.run_id);
                    return;
                }
                out_ << "null";
            });
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
                if (receipt.workflow_failure_summary.has_value()) {
                    print_failure_summary(*receipt.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
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
            field("receipt_status",
                  [&]() { print_receipt_status(receipt.receipt_status); });
            field("receipt_outcome",
                  [&]() { print_receipt_outcome(receipt.receipt_outcome); });
            field("accepted_for_receipt_archive",
                  [&]() { out_ << (receipt.accepted_for_receipt_archive ? "true" : "false"); });
            field("next_required_adapter_capability", [&]() {
                if (receipt.next_required_adapter_capability.has_value()) {
                    print_adapter_capability(*receipt.next_required_adapter_capability);
                    return;
                }
                out_ << "null";
            });
            field("receipt_blocker", [&]() {
                if (receipt.receipt_blocker.has_value()) {
                    print_receipt_blocker(*receipt.receipt_blocker, 1);
                    return;
                }
                out_ << "null";
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
            field("node_name", [&]() {
                if (summary.node_name.has_value()) {
                    write_string(*summary.node_name);
                    return;
                }
                out_ << "null";
            });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        switch (status) {
        case runtime_session::WorkflowSessionStatus::Completed:
            write_string("completed");
            return;
        case runtime_session::WorkflowSessionStatus::Failed:
            write_string("failed");
            return;
        case runtime_session::WorkflowSessionStatus::Partial:
            write_string("partial");
            return;
        }
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        switch (status) {
        case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
            write_string("ready_to_persist");
            return;
        case checkpoint_record::CheckpointRecordStatus::Blocked:
            write_string("blocked");
            return;
        case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        switch (status) {
        case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
            write_string("ready_to_export");
            return;
        case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
            write_string("blocked");
            return;
        case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        switch (status) {
        case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
            write_string("ready_to_import");
            return;
        case persistence_export::PersistenceExportManifestStatus::Blocked:
            write_string("blocked");
            return;
        case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        switch (status) {
        case store_import::StoreImportDescriptorStatus::ReadyToImport:
            write_string("ready_to_import");
            return;
        case store_import::StoreImportDescriptorStatus::Blocked:
            write_string("blocked");
            return;
        case store_import::StoreImportDescriptorStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case store_import::StoreImportDescriptorStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case store_import::StoreImportDescriptorStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        switch (status) {
        case durable_store_import::RequestStatus::ReadyForAdapter:
            write_string("ready_for_adapter");
            return;
        case durable_store_import::RequestStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::RequestStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case durable_store_import::RequestStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case durable_store_import::RequestStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        switch (status) {
        case durable_store_import::DecisionStatus::Accepted:
            write_string("accepted");
            return;
        case durable_store_import::DecisionStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::DecisionStatus::Deferred:
            write_string("deferred");
            return;
        case durable_store_import::DecisionStatus::Rejected:
            write_string("rejected");
            return;
        }
    }

    void print_decision_boundary_kind(durable_store_import::DecisionBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::DecisionBoundaryKind::LocalContractOnly:
            write_string("local_contract_only");
            return;
        case durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable:
            write_string("adapter_decision_consumable");
            return;
        }
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::ReceiptBoundaryKind::LocalContractOnly:
            write_string("local_contract_only");
            return;
        case durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable:
            write_string("adapter_receipt_consumable");
            return;
        }
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        switch (status) {
        case durable_store_import::ReceiptStatus::ReadyForArchive:
            write_string("ready_for_archive");
            return;
        case durable_store_import::ReceiptStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::ReceiptStatus::Deferred:
            write_string("deferred");
            return;
        case durable_store_import::ReceiptStatus::Rejected:
            write_string("rejected");
            return;
        }
    }

    void print_receipt_outcome(
        durable_store_import::ReceiptOutcome outcome) {
        switch (outcome) {
        case durable_store_import::ReceiptOutcome::ArchiveAcceptedDecision:
            write_string("archive_accepted_decision");
            return;
        case durable_store_import::ReceiptOutcome::BlockBlockedDecision:
            write_string("block_blocked_decision");
            return;
        case durable_store_import::ReceiptOutcome::DeferPartialDecision:
            write_string("defer_partial_decision");
            return;
        case durable_store_import::ReceiptOutcome::RejectFailedDecision:
            write_string("reject_failed_decision");
            return;
        }
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
            write_string("consume_store_import_descriptor");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
            write_string("consume_export_manifest");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
            write_string("consume_persistence_descriptor");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
            write_string("consume_human_review_context");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
            write_string("consume_checkpoint_record");
            return;
        case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
            write_string("preserve_partial_workflow_state");
            return;
        }
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
                if (blocker.required_capability.has_value()) {
                    print_adapter_capability(*blocker.required_capability);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_json(
    const durable_store_import::Receipt &receipt,
    std::ostream &out) {
    DurableStoreImportDecisionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl
