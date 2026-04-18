#include "ahfl/backends/replay_view.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class ReplayViewJsonPrinter final {
  public:
    explicit ReplayViewJsonPrinter(std::ostream &out) : out_(out) {}

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
            field("workflow_canonical_name", [&]() {
                write_string(replay.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(replay.session_id); });
            field("run_id", [&]() {
                if (replay.run_id.has_value()) {
                    write_string(*replay.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(replay.input_fixture); });
            field("workflow_status", [&]() {
                switch (replay.workflow_status) {
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
            });
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
    std::ostream &out_;

    void write_indent(int indent_level) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ');
    }

    void newline_and_indent(int indent_level) {
        out_ << '\n';
        write_indent(indent_level);
    }

    void write_string(std::string_view value) {
        write_escaped_json_string(out_, value);
    }

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

    void print_consistency(const replay_view::ReplayConsistencySummary &consistency,
                           int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("plan_matches_session", [&]() {
                out_ << (consistency.plan_matches_session ? "true" : "false");
            });
            field("session_matches_journal", [&]() {
                out_ << (consistency.session_matches_journal ? "true" : "false");
            });
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
            field("saw_node_became_ready", [&]() {
                out_ << (node.saw_node_became_ready ? "true" : "false");
            });
            field("saw_node_started", [&]() {
                out_ << (node.saw_node_started ? "true" : "false");
            });
            field("saw_node_completed", [&]() {
                out_ << (node.saw_node_completed ? "true" : "false");
            });
            field("saw_node_failed", [&]() {
                out_ << (node.saw_node_failed ? "true" : "false");
            });
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
};

} // namespace

void print_replay_view_json(const replay_view::ReplayView &replay, std::ostream &out) {
    ReplayViewJsonPrinter{out}.print(replay);
}

} // namespace ahfl
