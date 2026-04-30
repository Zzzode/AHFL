#include "ahfl/backends/durable_store_import_request.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class DurableStoreImportRequestJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportRequestJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                if (request.source_package_identity.has_value()) {
                    print_package_identity(*request.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() {
                if (request.run_id.has_value()) {
                    write_string(*request.run_id);
                    return;
                }
                out_ << "null";
            });
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
                if (request.workflow_failure_summary.has_value()) {
                    print_failure_summary(*request.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
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
            field("adapter_ready", [&]() { out_ << (request.adapter_ready ? "true" : "false"); });
            field("next_required_adapter_artifact_kind", [&]() {
                if (request.next_required_adapter_artifact_kind.has_value()) {
                    print_requested_artifact_kind(*request.next_required_adapter_artifact_kind);
                    return;
                }
                out_ << "null";
            });
            field("request_status", [&]() { print_request_status(request.request_status); });
            field("adapter_blocker", [&]() {
                if (request.adapter_blocker.has_value()) {
                    print_request_blocker(*request.adapter_blocker, 1);
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

    void print_requested_artifact_kind(durable_store_import::RequestedArtifactKind kind) {
        switch (kind) {
        case durable_store_import::RequestedArtifactKind::StoreImportDescriptor:
            write_string("store_import_descriptor");
            return;
        case durable_store_import::RequestedArtifactKind::ExportManifest:
            write_string("export_manifest");
            return;
        case durable_store_import::RequestedArtifactKind::PersistenceDescriptor:
            write_string("persistence_descriptor");
            return;
        case durable_store_import::RequestedArtifactKind::PersistenceReview:
            write_string("persistence_review");
            return;
        case durable_store_import::RequestedArtifactKind::CheckpointRecord:
            write_string("checkpoint_record");
            return;
        }
    }

    void print_adapter_role(durable_store_import::RequestedArtifactAdapterRole role) {
        switch (role) {
        case durable_store_import::RequestedArtifactAdapterRole::RequestAnchor:
            write_string("request_anchor");
            return;
        case durable_store_import::RequestedArtifactAdapterRole::ImportManifest:
            write_string("import_manifest");
            return;
        case durable_store_import::RequestedArtifactAdapterRole::DurableStateDescriptor:
            write_string("durable_state_descriptor");
            return;
        case durable_store_import::RequestedArtifactAdapterRole::HumanReviewContext:
            write_string("human_review_context");
            return;
        case durable_store_import::RequestedArtifactAdapterRole::CheckpointPayload:
            write_string("checkpoint_payload");
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

    void print_request_blocker_kind(durable_store_import::RequestBlockerKind kind) {
        switch (kind) {
        case durable_store_import::RequestBlockerKind::WaitingOnRequestedArtifact:
            write_string("waiting_on_requested_artifact");
            return;
        case durable_store_import::RequestBlockerKind::MissingRequestIdentity:
            write_string("missing_durable_store_import_request_identity");
            return;
        case durable_store_import::RequestBlockerKind::MissingRequestedArtifactSet:
            write_string("missing_requested_artifact_set");
            return;
        case durable_store_import::RequestBlockerKind::WorkflowFailure:
            write_string("workflow_failure");
            return;
        case durable_store_import::RequestBlockerKind::WorkflowPartial:
            write_string("workflow_partial");
            return;
        }
    }

    void print_requested_artifact_entry(const durable_store_import::RequestedArtifactEntry &entry,
                                        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("artifact_kind", [&]() { print_requested_artifact_kind(entry.artifact_kind); });
            field("logical_artifact_name", [&]() { write_string(entry.logical_artifact_name); });
            field("source_format_version", [&]() { write_string(entry.source_format_version); });
            field("required_for_import",
                  [&]() { out_ << (entry.required_for_import ? "true" : "false"); });
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
            field("logical_artifact_name", [&]() {
                if (blocker.logical_artifact_name.has_value()) {
                    write_string(*blocker.logical_artifact_name);
                    return;
                }
                out_ << "null";
            });
            field("message", [&]() { write_string(blocker.message); });
        });
    }
};

} // namespace

void print_durable_store_import_request_json(const durable_store_import::Request &request,
                                             std::ostream &out) {
    DurableStoreImportRequestJsonPrinter(out).print(request);
}

} // namespace ahfl
