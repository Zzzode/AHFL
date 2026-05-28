#include "backends/pipeline/replay_view.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "backends/pipeline/json_helpers.hpp"

namespace ahfl {

namespace {

class ReplayViewJsonPrinter final : private PipelineJsonHelpers {
  public:
    explicit ReplayViewJsonPrinter(std::ostream &out) : PipelineJsonHelpers(out) {}

    void print(const replay_view::ReplayView &replay) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(replay.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(replay.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(replay.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(replay.source_execution_journal_format_version); });
            field("source_package_identity", [&]() {
                if (replay.source_package_identity.has_value()) {
                    print_package_identity(*replay.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(replay.workflow_canonical_name); });
            field("session_id", [&]() { write_string(replay.session_id); });
            field("run_id", [&]() {
                if (replay.run_id.has_value()) {
                    write_string(*replay.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(replay.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(replay.workflow_status); });
            field("replay_status", [&]() {
                switch (replay.replay_status) {
                case replay_view::ReplayStatus::Consistent:
                    write_string("consistent");
                    return;
                case replay_view::ReplayStatus::RuntimeFailed:
                    write_string("runtime_failed");
                    return;
                case replay_view::ReplayStatus::Partial:
                    write_string("partial");
                    return;
                }
            });
            field("workflow_failure_summary", [&]() {
                if (replay.workflow_failure_summary.has_value()) {
                    print_failure_summary(*replay.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("execution_order", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node_name : replay.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : replay.nodes) {
                        item([&]() { print_node(node, 2); });
                    }
                });
            });
            field("consistency", [&]() { print_consistency(replay.consistency, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_consistency(const replay_view::ReplayConsistencySummary &consistency,
                           int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("plan_matches_session",
                  [&]() { out_ << (consistency.plan_matches_session ? "true" : "false"); });
            field("session_matches_journal",
                  [&]() { out_ << (consistency.session_matches_journal ? "true" : "false"); });
            field("journal_matches_execution_order", [&]() {
                out_ << (consistency.journal_matches_execution_order ? "true" : "false");
            });
        });
    }

    void print_node(const replay_view::ReplayNodeProgression &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("planned_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.planned_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("saw_node_became_ready",
                  [&]() { out_ << (node.saw_node_became_ready ? "true" : "false"); });
            field("saw_node_started",
                  [&]() { out_ << (node.saw_node_started ? "true" : "false"); });
            field("saw_node_completed",
                  [&]() { out_ << (node.saw_node_completed ? "true" : "false"); });
            field("saw_node_failed", [&]() { out_ << (node.saw_node_failed ? "true" : "false"); });
            field("failure_summary", [&]() {
                if (node.failure_summary.has_value()) {
                    print_failure_summary(*node.failure_summary, indent_level + 1);
                    return;
                }
                out_ << "null";
            });
            field("used_mock_selectors", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &selector : node.used_mock_selectors) {
                        item([&]() { write_string(selector); });
                    }
                });
            });
            field("final_status", [&]() {
                switch (node.final_status) {
                case runtime_session::NodeSessionStatus::Blocked:
                    write_string("blocked");
                    return;
                case runtime_session::NodeSessionStatus::Ready:
                    write_string("ready");
                    return;
                case runtime_session::NodeSessionStatus::Completed:
                    write_string("completed");
                    return;
                case runtime_session::NodeSessionStatus::Failed:
                    write_string("failed");
                    return;
                case runtime_session::NodeSessionStatus::Skipped:
                    write_string("skipped");
                    return;
                }
            });
        });
    }
};

} // namespace

void print_replay_view_json(const replay_view::ReplayView &replay, std::ostream &out) {
    ReplayViewJsonPrinter{out}.print(replay);
}

} // namespace ahfl
