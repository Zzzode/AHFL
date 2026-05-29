#include "compiler/backends/pipeline/checkpoint_record.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>
#include <vector>

#include "compiler/backends/pipeline/json_helpers.hpp"

namespace ahfl {

namespace {

class CheckpointRecordJsonPrinter final : private PipelineJsonHelpers {
  public:
    explicit CheckpointRecordJsonPrinter(std::ostream &out) : PipelineJsonHelpers(out) {}

    void print(const checkpoint_record::CheckpointRecord &record) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(record.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(record.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(record.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(record.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(record.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(record.source_scheduler_snapshot_format_version); });
            field("source_package_identity", [&]() {
                if (record.source_package_identity.has_value()) {
                    print_package_identity(*record.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(record.workflow_canonical_name); });
            field("session_id", [&]() { write_string(record.session_id); });
            field("run_id", [&]() {
                if (record.run_id.has_value()) {
                    write_string(*record.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(record.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(record.workflow_status); });
            field("snapshot_status", [&]() { print_snapshot_status(record.snapshot_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(record.checkpoint_status); });
            field("workflow_failure_summary", [&]() {
                if (record.workflow_failure_summary.has_value()) {
                    print_failure_summary(*record.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("execution_order", [&]() { print_string_array(record.execution_order, 1); });
            field("basis_kind", [&]() { print_basis_kind(record.basis_kind); });
            field("checkpoint_friendly_source",
                  [&]() { out_ << (record.checkpoint_friendly_source ? "true" : "false"); });
            field("cursor", [&]() { print_cursor(record.cursor, 1); });
            field("resume_blocker", [&]() {
                if (record.resume_blocker.has_value()) {
                    print_resume_blocker(*record.resume_blocker, 1);
                    return;
                }
                out_ << "null";
            });
        });
        out_ << '\n';
    }

  private:
    void print_basis_kind(checkpoint_record::CheckpointBasisKind kind) {
        switch (kind) {
        case checkpoint_record::CheckpointBasisKind::LocalOnly:
            write_string("local_only");
            return;
        case checkpoint_record::CheckpointBasisKind::DurableAdjacent:
            write_string("durable_adjacent");
            return;
        }
    }

    void print_resume_blocker_kind(checkpoint_record::CheckpointResumeBlockerKind kind) {
        switch (kind) {
        case checkpoint_record::CheckpointResumeBlockerKind::NotCheckpointFriendly:
            write_string("not_checkpoint_friendly");
            return;
        case checkpoint_record::CheckpointResumeBlockerKind::WaitingOnSchedulerState:
            write_string("waiting_on_scheduler_state");
            return;
        case checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure:
            write_string("workflow_failure");
            return;
        case checkpoint_record::CheckpointResumeBlockerKind::WorkflowPartial:
            write_string("workflow_partial");
            return;
        }
    }

    void print_cursor(const checkpoint_record::CheckpointCursor &cursor, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("persistable_prefix_size", [&]() { out_ << cursor.persistable_prefix_size; });
            field("persistable_prefix",
                  [&]() { print_string_array(cursor.persistable_prefix, indent_level + 1); });
            field("resume_candidate_node_name", [&]() {
                if (cursor.resume_candidate_node_name.has_value()) {
                    write_string(*cursor.resume_candidate_node_name);
                    return;
                }
                out_ << "null";
            });
            field("resume_ready", [&]() { out_ << (cursor.resume_ready ? "true" : "false"); });
        });
    }

    void print_resume_blocker(const checkpoint_record::CheckpointResumeBlocker &blocker,
                              int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_resume_blocker_kind(blocker.kind); });
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

void print_checkpoint_record_json(const checkpoint_record::CheckpointRecord &record,
                                  std::ostream &out) {
    CheckpointRecordJsonPrinter{out}.print(record);
}

} // namespace ahfl
