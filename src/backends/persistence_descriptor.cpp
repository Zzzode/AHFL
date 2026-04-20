#include "ahfl/backends/persistence_descriptor.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <vector>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class PersistenceDescriptorJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit PersistenceDescriptorJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const persistence_descriptor::CheckpointPersistenceDescriptor &descriptor) {
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
            field("source_package_identity", [&]() {
                if (descriptor.source_package_identity.has_value()) {
                    print_package_identity(*descriptor.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name", [&]() {
                write_string(descriptor.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(descriptor.session_id); });
            field("run_id", [&]() {
                if (descriptor.run_id.has_value()) {
                    write_string(*descriptor.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(descriptor.input_fixture); });
            field("workflow_status", [&]() {
                print_workflow_status(descriptor.workflow_status);
            });
            field("checkpoint_status", [&]() {
                print_checkpoint_status(descriptor.checkpoint_status);
            });
            field("persistence_status", [&]() {
                print_persistence_status(descriptor.persistence_status);
            });
            field("workflow_failure_summary", [&]() {
                if (descriptor.workflow_failure_summary.has_value()) {
                    print_failure_summary(*descriptor.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("execution_order", [&]() {
                print_string_array(descriptor.execution_order, 1);
            });
            field("planned_durable_identity", [&]() {
                write_string(descriptor.planned_durable_identity);
            });
            field("export_basis_kind", [&]() {
                print_basis_kind(descriptor.export_basis_kind);
            });
            field("cursor", [&]() { print_cursor(descriptor.cursor, 1); });
            field("persistence_blocker", [&]() {
                if (descriptor.persistence_blocker.has_value()) {
                    print_blocker(*descriptor.persistence_blocker, 1);
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

    void print_persistence_status(
        persistence_descriptor::PersistenceDescriptorStatus status) {
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

    void print_basis_kind(persistence_descriptor::PersistenceBasisKind kind) {
        switch (kind) {
        case persistence_descriptor::PersistenceBasisKind::LocalPlanningOnly:
            write_string("local_planning_only");
            return;
        case persistence_descriptor::PersistenceBasisKind::StoreAdjacent:
            write_string("store_adjacent");
            return;
        }
    }

    void print_blocker_kind(persistence_descriptor::PersistenceBlockerKind kind) {
        switch (kind) {
        case persistence_descriptor::PersistenceBlockerKind::WaitingOnCheckpointState:
            write_string("waiting_on_checkpoint_state");
            return;
        case persistence_descriptor::PersistenceBlockerKind::MissingPlannedDurableIdentity:
            write_string("missing_planned_durable_identity");
            return;
        case persistence_descriptor::PersistenceBlockerKind::WorkflowFailure:
            write_string("workflow_failure");
            return;
        case persistence_descriptor::PersistenceBlockerKind::WorkflowPartial:
            write_string("workflow_partial");
            return;
        }
    }

    void print_string_array(const std::vector<std::string> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &value : values) {
                item([&]() { write_string(value); });
            }
        });
    }

    void print_cursor(const persistence_descriptor::PersistenceCursor &cursor,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("exportable_prefix_size", [&]() { out_ << cursor.exportable_prefix_size; });
            field("exportable_prefix", [&]() {
                print_string_array(cursor.exportable_prefix, indent_level + 1);
            });
            field("next_export_candidate_node_name", [&]() {
                if (cursor.next_export_candidate_node_name.has_value()) {
                    write_string(*cursor.next_export_candidate_node_name);
                    return;
                }
                out_ << "null";
            });
            field("export_ready", [&]() { out_ << (cursor.export_ready ? "true" : "false"); });
        });
    }

    void print_blocker(const persistence_descriptor::PersistenceBlocker &blocker,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_blocker_kind(blocker.kind); });
            field("message", [&]() { write_string(blocker.message); });
            field("node_name", [&]() {
                if (blocker.node_name.has_value()) {
                    write_string(*blocker.node_name);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_persistence_descriptor_json(
    const persistence_descriptor::CheckpointPersistenceDescriptor &descriptor,
    std::ostream &out) {
    PersistenceDescriptorJsonPrinter{out}.print(descriptor);
}

} // namespace ahfl
