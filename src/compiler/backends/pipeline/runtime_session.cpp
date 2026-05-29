#include "compiler/backends/pipeline/runtime_session.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "compiler/backends/pipeline/json_helpers.hpp"

namespace ahfl {

namespace {

class RuntimeSessionJsonPrinter final : private PipelineJsonHelpers {
  public:
    explicit RuntimeSessionJsonPrinter(std::ostream &out) : PipelineJsonHelpers(out) {}

    void print(const runtime_session::RuntimeSession &session) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(session.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(session.source_execution_plan_format_version); });
            field("source_package_identity", [&]() {
                if (session.source_package_identity.has_value()) {
                    print_package_identity(*session.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(session.workflow_canonical_name); });
            field("session_id", [&]() { write_string(session.session_id); });
            field("run_id", [&]() {
                if (session.run_id.has_value()) {
                    write_string(*session.run_id);
                    return;
                }
                out_ << "null";
            });
            field("workflow_status", [&]() { print_workflow_status(session.workflow_status); });
            field("failure_summary", [&]() {
                if (session.failure_summary.has_value()) {
                    print_failure_summary(*session.failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(session.input_fixture); });
            field("options", [&]() { print_options(session.options, 1); });
            field("execution_order", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node_name : session.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : session.nodes) {
                        item([&]() { print_node(node, 2); });
                    }
                });
            });
            field("return_summary",
                  [&]() { print_workflow_value_summary(session.return_summary, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_mock_usage(const runtime_session::RuntimeSessionMockUsage &usage, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("selector", [&]() { write_string(usage.selector); });
            field("capability_name", [&]() {
                if (usage.capability_name.has_value()) {
                    write_string(*usage.capability_name);
                    return;
                }
                out_ << "null";
            });
            field("binding_key", [&]() {
                if (usage.binding_key.has_value()) {
                    write_string(*usage.binding_key);
                    return;
                }
                out_ << "null";
            });
            field("result_fixture", [&]() { write_string(usage.result_fixture); });
            field("invocation_label", [&]() {
                if (usage.invocation_label.has_value()) {
                    write_string(*usage.invocation_label);
                    return;
                }
                out_ << "null";
            });
        });
    }

    void print_options(const runtime_session::RuntimeSessionOptions &options, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("sequential_mode",
                  [&]() { out_ << (options.sequential_mode ? "true" : "false"); });
        });
    }

    void print_node(const runtime_session::RuntimeSessionNode &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("status", [&]() {
                switch (node.status) {
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
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("failure_summary", [&]() {
                if (node.failure_summary.has_value()) {
                    print_failure_summary(*node.failure_summary, indent_level + 1);
                    return;
                }
                out_ << "null";
            });
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("lifecycle", [&]() { print_lifecycle(node.lifecycle, indent_level + 1); });
            field("input_summary",
                  [&]() { print_workflow_value_summary(node.input_summary, indent_level + 1); });
            field("capability_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &binding : node.capability_bindings) {
                        item([&]() { print_capability_binding_ref(binding, indent_level + 2); });
                    }
                });
            });
            field("used_mocks", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &usage : node.used_mocks) {
                        item([&]() { print_mock_usage(usage, indent_level + 2); });
                    }
                });
            });
        });
    }
};

} // namespace

void print_runtime_session_json(const runtime_session::RuntimeSession &session, std::ostream &out) {
    RuntimeSessionJsonPrinter{out}.print(session);
}

} // namespace ahfl
