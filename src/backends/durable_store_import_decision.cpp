#include "ahfl/backends/durable_store_import_decision.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class DurableStoreImportDecisionJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportDecisionJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                if (decision.source_package_identity.has_value()) {
                    print_package_identity(*decision.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(decision.workflow_canonical_name); });
            field("session_id", [&]() { write_string(decision.session_id); });
            field("run_id", [&]() {
                if (decision.run_id.has_value()) {
                    write_string(*decision.run_id);
                    return;
                }
                out_ << "null";
            });
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
                if (decision.workflow_failure_summary.has_value()) {
                    print_failure_summary(*decision.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
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
                  [&]() { out_ << (decision.accepted_for_future_execution ? "true" : "false"); });
            field("next_required_adapter_capability", [&]() {
                if (decision.next_required_adapter_capability.has_value()) {
                    print_adapter_capability(*decision.next_required_adapter_capability);
                    return;
                }
                out_ << "null";
            });
            field("decision_blocker", [&]() {
                if (decision.decision_blocker.has_value()) {
                    print_decision_blocker(*decision.decision_blocker, 1);
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

    void print_request_boundary_kind(durable_store_import::RequestBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::RequestBoundaryKind::LocalIntentOnly:
            write_string("local_intent_only");
            return;
        case durable_store_import::RequestBoundaryKind::AdapterContractConsumable:
            write_string("adapter_contract_consumable");
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

    void print_decision_outcome(durable_store_import::DecisionOutcome outcome) {
        switch (outcome) {
        case durable_store_import::DecisionOutcome::AcceptRequest:
            write_string("accept_request");
            return;
        case durable_store_import::DecisionOutcome::BlockRequest:
            write_string("block_request");
            return;
        case durable_store_import::DecisionOutcome::DeferPartialRequest:
            write_string("defer_partial_request");
            return;
        case durable_store_import::DecisionOutcome::RejectFailedRequest:
            write_string("reject_failed_request");
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

void print_durable_store_import_decision_json(const durable_store_import::Decision &decision,
                                              std::ostream &out) {
    DurableStoreImportDecisionJsonPrinter(out).print(decision);
}

} // namespace ahfl
