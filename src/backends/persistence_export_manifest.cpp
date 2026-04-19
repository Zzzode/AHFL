#include "ahfl/backends/persistence_export_manifest.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class PersistenceExportManifestJsonPrinter final {
  public:
    explicit PersistenceExportManifestJsonPrinter(std::ostream &out) : out_(out) {}

    void print(const persistence_export::PersistenceExportManifest &manifest) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(manifest.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(manifest.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(manifest.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(manifest.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(manifest.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(manifest.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(manifest.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(manifest.source_persistence_descriptor_format_version); });
            field("source_package_identity", [&]() {
                if (manifest.source_package_identity.has_value()) {
                    print_package_identity(*manifest.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(manifest.workflow_canonical_name); });
            field("session_id", [&]() { write_string(manifest.session_id); });
            field("run_id", [&]() {
                if (manifest.run_id.has_value()) {
                    write_string(*manifest.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(manifest.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(manifest.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(manifest.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(manifest.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(manifest.manifest_status); });
            field("workflow_failure_summary", [&]() {
                if (manifest.workflow_failure_summary.has_value()) {
                    print_failure_summary(*manifest.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("export_package_identity",
                  [&]() { write_string(manifest.export_package_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(manifest.planned_durable_identity); });
            field("manifest_boundary_kind",
                  [&]() { print_manifest_boundary_kind(manifest.manifest_boundary_kind); });
            field("artifact_bundle",
                  [&]() { print_artifact_bundle(manifest.artifact_bundle, 1); });
            field("manifest_ready", [&]() { out_ << (manifest.manifest_ready ? "true" : "false"); });
            field("next_required_artifact_kind", [&]() {
                if (manifest.next_required_artifact_kind.has_value()) {
                    print_artifact_kind(*manifest.next_required_artifact_kind);
                    return;
                }
                out_ << "null";
            });
            field("store_import_blocker", [&]() {
                if (manifest.store_import_blocker.has_value()) {
                    print_store_import_blocker(*manifest.store_import_blocker, 1);
                    return;
                }
                out_ << "null";
            });
        });
        out_ << '\n';
    }

  private:
    std::ostream &out_;

    void write_indent(int indent_level) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ');
    }

    void newline_and_indent(int indent_level) {
        out_ << '\n';
        write_indent(indent_level);
    }

    void write_string(std::string_view value) { write_escaped_json_string(out_, value); }

    template <typename WriteFields> void print_object(int indent_level, WriteFields write_fields) {
        out_ << '{';
        bool wrote_any_field = false;

        const auto field = [&](std::string_view name, const auto &write_value) {
            if (wrote_any_field) {
                out_ << ',';
            }
            wrote_any_field = true;
            newline_and_indent(indent_level + 1);
            write_string(name);
            out_ << ": ";
            write_value();
        };

        write_fields(field);

        if (wrote_any_field) {
            newline_and_indent(indent_level);
        }
        out_ << '}';
    }

    template <typename WriteItems> void print_array(int indent_level, WriteItems write_items) {
        out_ << '[';
        bool wrote_any_item = false;

        const auto item = [&](const auto &write_value) {
            if (wrote_any_item) {
                out_ << ',';
            }
            wrote_any_item = true;
            newline_and_indent(indent_level + 1);
            write_value();
        };

        write_items(item);

        if (wrote_any_item) {
            newline_and_indent(indent_level);
        }
        out_ << ']';
    }

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

    void print_manifest_boundary_kind(persistence_export::ManifestBoundaryKind kind) {
        switch (kind) {
        case persistence_export::ManifestBoundaryKind::LocalBundleOnly:
            write_string("local_bundle_only");
            return;
        case persistence_export::ManifestBoundaryKind::StoreImportAdjacent:
            write_string("store_import_adjacent");
            return;
        }
    }

    void print_artifact_kind(persistence_export::ExportArtifactKind kind) {
        switch (kind) {
        case persistence_export::ExportArtifactKind::PersistenceDescriptor:
            write_string("persistence_descriptor");
            return;
        case persistence_export::ExportArtifactKind::PersistenceReview:
            write_string("persistence_review");
            return;
        case persistence_export::ExportArtifactKind::CheckpointRecord:
            write_string("checkpoint_record");
            return;
        }
    }

    void print_store_import_blocker_kind(persistence_export::StoreImportBlockerKind kind) {
        switch (kind) {
        case persistence_export::StoreImportBlockerKind::WaitingOnPersistenceState:
            write_string("waiting_on_persistence_state");
            return;
        case persistence_export::StoreImportBlockerKind::MissingExportPackageIdentity:
            write_string("missing_export_package_identity");
            return;
        case persistence_export::StoreImportBlockerKind::MissingArtifactBundle:
            write_string("missing_artifact_bundle");
            return;
        case persistence_export::StoreImportBlockerKind::WorkflowFailure:
            write_string("workflow_failure");
            return;
        case persistence_export::StoreImportBlockerKind::WorkflowPartial:
            write_string("workflow_partial");
            return;
        }
    }

    void print_artifact_entry(const persistence_export::ExportArtifactEntry &entry,
                              int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("artifact_kind", [&]() { print_artifact_kind(entry.artifact_kind); });
            field("logical_artifact_name",
                  [&]() { write_string(entry.logical_artifact_name); });
            field("source_format_version",
                  [&]() { write_string(entry.source_format_version); });
        });
    }

    void print_artifact_bundle(const persistence_export::ExportArtifactBundle &bundle,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("entry_count", [&]() { out_ << bundle.entry_count; });
            field("entries", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &entry : bundle.entries) {
                        item([&]() { print_artifact_entry(entry, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_store_import_blocker(const persistence_export::StoreImportBlocker &blocker,
                                    int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_store_import_blocker_kind(blocker.kind); });
            field("message", [&]() { write_string(blocker.message); });
            field("logical_artifact_name", [&]() {
                if (blocker.logical_artifact_name.has_value()) {
                    write_string(*blocker.logical_artifact_name);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_persistence_export_manifest_json(
    const persistence_export::PersistenceExportManifest &manifest,
    std::ostream &out) {
    PersistenceExportManifestJsonPrinter printer(out);
    printer.print(manifest);
}

} // namespace ahfl
