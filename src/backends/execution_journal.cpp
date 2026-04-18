#include "ahfl/backends/execution_journal.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class ExecutionJournalJsonPrinter final {
  public:
    explicit ExecutionJournalJsonPrinter(std::ostream &out) : out_(out) {}

    void print(const execution_journal::ExecutionJournal &journal) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(journal.format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(journal.source_runtime_session_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(journal.source_execution_plan_format_version); });
            field("source_package_identity", [&]() {
                if (journal.source_package_identity.has_value()) {
                    print_package_identity(*journal.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name", [&]() {
                write_string(journal.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(journal.session_id); });
            field("run_id", [&]() {
                if (journal.run_id.has_value()) {
                    write_string(*journal.run_id);
                    return;
                }
                out_ << "null";
            });
            field("events", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &event : journal.events) {
                        item([&]() { print_event(event, 2); });
                    }
                });
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

    void print_event(const execution_journal::ExecutionJournalEvent &event, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (event.kind) {
                case execution_journal::ExecutionJournalEventKind::SessionStarted:
                    write_string("session_started");
                    return;
                case execution_journal::ExecutionJournalEventKind::NodeBecameReady:
                    write_string("node_became_ready");
                    return;
                case execution_journal::ExecutionJournalEventKind::NodeStarted:
                    write_string("node_started");
                    return;
                case execution_journal::ExecutionJournalEventKind::NodeCompleted:
                    write_string("node_completed");
                    return;
                case execution_journal::ExecutionJournalEventKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case execution_journal::ExecutionJournalEventKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case execution_journal::ExecutionJournalEventKind::WorkflowCompleted:
                    write_string("workflow_completed");
                    return;
                case execution_journal::ExecutionJournalEventKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("workflow_canonical_name", [&]() {
                write_string(event.workflow_canonical_name);
            });
            field("node_name", [&]() {
                if (event.node_name.has_value()) {
                    write_string(*event.node_name);
                    return;
                }
                out_ << "null";
            });
            field("execution_index", [&]() {
                if (event.execution_index.has_value()) {
                    out_ << *event.execution_index;
                    return;
                }
                out_ << "null";
            });
            field("failure_summary", [&]() {
                if (event.failure_summary.has_value()) {
                    print_failure_summary(*event.failure_summary, indent_level + 1);
                    return;
                }
                out_ << "null";
            });
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : event.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("used_mock_selectors", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &selector : event.used_mock_selectors) {
                        item([&]() { write_string(selector); });
                    }
                });
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

void print_execution_journal_json(const execution_journal::ExecutionJournal &journal,
                                  std::ostream &out) {
    ExecutionJournalJsonPrinter{out}.print(journal);
}

} // namespace ahfl
