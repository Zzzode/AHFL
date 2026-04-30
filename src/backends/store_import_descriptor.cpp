#include "ahfl/backends/store_import_descriptor.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class StoreImportDescriptorJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit StoreImportDescriptorJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const store_import::StoreImportDescriptor &descriptor) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(descriptor.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(descriptor.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(descriptor.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(descriptor.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(descriptor.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(descriptor.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(descriptor.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(descriptor.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(descriptor.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                if (descriptor.source_package_identity.has_value()) {
                    print_package_identity(*descriptor.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(descriptor.workflow_canonical_name); });
            field("session_id", [&]() { write_string(descriptor.session_id); });
            field("run_id", [&]() {
                if (descriptor.run_id.has_value()) {
                    write_string(*descriptor.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(descriptor.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(descriptor.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(descriptor.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(descriptor.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(descriptor.manifest_status); });
            field("workflow_failure_summary", [&]() {
                if (descriptor.workflow_failure_summary.has_value()) {
                    print_failure_summary(*descriptor.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("export_package_identity",
                  [&]() { write_string(descriptor.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(descriptor.store_import_candidate_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(descriptor.planned_durable_identity); });
            field("descriptor_boundary_kind",
                  [&]() { print_descriptor_boundary_kind(descriptor.descriptor_boundary_kind); });
            field("staging_artifact_set",
                  [&]() { print_staging_artifact_set(descriptor.staging_artifact_set, 1); });
            field("import_ready", [&]() { out_ << (descriptor.import_ready ? "true" : "false"); });
            field("next_required_staging_artifact_kind", [&]() {
                if (descriptor.next_required_staging_artifact_kind.has_value()) {
                    print_staging_artifact_kind(*descriptor.next_required_staging_artifact_kind);
                    return;
                }
                out_ << "null";
            });
            field("descriptor_status",
                  [&]() { print_descriptor_status(descriptor.descriptor_status); });
            field("staging_blocker", [&]() {
                if (descriptor.staging_blocker.has_value()) {
                    print_staging_blocker(*descriptor.staging_blocker, 1);
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

    void print_descriptor_boundary_kind(store_import::StoreImportBoundaryKind kind) {
        switch (kind) {
        case store_import::StoreImportBoundaryKind::LocalStagingOnly:
            write_string("local_staging_only");
            return;
        case store_import::StoreImportBoundaryKind::AdapterConsumable:
            write_string("adapter_consumable");
            return;
        }
    }

    void print_staging_artifact_kind(store_import::StagingArtifactKind kind) {
        switch (kind) {
        case store_import::StagingArtifactKind::ExportManifest:
            write_string("export_manifest");
            return;
        case store_import::StagingArtifactKind::PersistenceDescriptor:
            write_string("persistence_descriptor");
            return;
        case store_import::StagingArtifactKind::PersistenceReview:
            write_string("persistence_review");
            return;
        case store_import::StagingArtifactKind::CheckpointRecord:
            write_string("checkpoint_record");
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

    void print_staging_blocker_kind(store_import::StagingBlockerKind kind) {
        switch (kind) {
        case store_import::StagingBlockerKind::WaitingOnExportManifest:
            write_string("waiting_on_export_manifest");
            return;
        case store_import::StagingBlockerKind::MissingStoreImportCandidateIdentity:
            write_string("missing_store_import_candidate_identity");
            return;
        case store_import::StagingBlockerKind::MissingStagingArtifactSet:
            write_string("missing_staging_artifact_set");
            return;
        case store_import::StagingBlockerKind::WorkflowFailure:
            write_string("workflow_failure");
            return;
        case store_import::StagingBlockerKind::WorkflowPartial:
            write_string("workflow_partial");
            return;
        }
    }

    void print_staging_artifact_entry(const store_import::StagingArtifactEntry &entry,
                                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("artifact_kind", [&]() { print_staging_artifact_kind(entry.artifact_kind); });
            field("logical_artifact_name", [&]() { write_string(entry.logical_artifact_name); });
            field("source_format_version", [&]() { write_string(entry.source_format_version); });
            field("required_for_import",
                  [&]() { out_ << (entry.required_for_import ? "true" : "false"); });
        });
    }

    void print_staging_artifact_set(const store_import::StagingArtifactSet &artifact_set,
                                    int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("entry_count", [&]() { out_ << artifact_set.entry_count; });
            field("entries", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &entry : artifact_set.entries) {
                        item([&]() { print_staging_artifact_entry(entry, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_staging_blocker(const store_import::StagingBlocker &blocker, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_staging_blocker_kind(blocker.kind); });
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

void print_store_import_descriptor_json(const store_import::StoreImportDescriptor &descriptor,
                                        std::ostream &out) {
    StoreImportDescriptorJsonPrinter(out).print(descriptor);
}

} // namespace ahfl
