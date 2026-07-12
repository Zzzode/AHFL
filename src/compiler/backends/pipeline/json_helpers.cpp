#include "compiler/backends/pipeline/json_helpers.hpp"

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

void PipelineJsonHelpers::print_package_identity(const handoff::PackageIdentity &identity,
                                                 int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("format_version", [&]() { write_string(identity.format_version); });
        field("name", [&]() { write_string(identity.name); });
        field("version", [&]() { write_string(identity.version); });
    });
}

void PipelineJsonHelpers::print_workflow_value_summary(const ir::WorkflowExprSummary &summary,
                                                       int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("reads", [&]() {
            print_array(indent_level + 1, [&](const auto &item) {
                for (const auto &read : summary.reads) {
                    item([&]() {
                        print_object(indent_level + 2, [&](const auto &read_field) {
                            read_field("kind", [&]() {
                                write_string(read.kind == ir::WorkflowValueSourceKind::WorkflowInput
                                                 ? "workflow_input"
                                                 : "workflow_node_output");
                            });
                            read_field("root_name", [&]() { write_string(read.root_name); });
                            read_field("members", [&]() {
                                print_array(indent_level + 3, [&](const auto &member_item) {
                                    for (const auto &member : read.members) {
                                        member_item([&]() { write_string(member); });
                                    }
                                });
                            });
                        });
                    });
                }
            });
        });
    });
}

void PipelineJsonHelpers::print_workflow_value_summary(const handoff::WorkflowValueSummary &summary,
                                                       int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("reads", [&]() {
            print_array(indent_level + 1, [&](const auto &item) {
                for (const auto &read : summary.reads) {
                    item([&]() {
                        print_object(indent_level + 2, [&](const auto &read_field) {
                            read_field("kind", [&]() {
                                write_string(read.kind ==
                                                     handoff::WorkflowValueSourceKind::WorkflowInput
                                                 ? "workflow_input"
                                                 : "workflow_node_output");
                            });
                            read_field("root_name", [&]() { write_string(read.root_name); });
                            read_field("members", [&]() {
                                print_array(indent_level + 3, [&](const auto &member_item) {
                                    for (const auto &member : read.members) {
                                        member_item([&]() { write_string(member); });
                                    }
                                });
                            });
                        });
                    });
                }
            });
        });
    });
}

void PipelineJsonHelpers::print_capability_binding_ref(
    const handoff::CapabilityBindingReference &binding, int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("capability_name", [&]() { write_string(binding.capability_name); });
        field("binding_key", [&]() { write_string(binding.binding_key); });
    });
}

void PipelineJsonHelpers::print_lifecycle(const handoff::WorkflowNodeLifecycleSummary &lifecycle,
                                          int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("start_condition", [&]() {
            write_string(lifecycle.start_condition ==
                                 handoff::WorkflowNodeStartConditionKind::Immediate
                             ? "immediate"
                             : "after_dependencies_completed");
        });
        field("completion_condition", [&]() {
            switch (lifecycle.completion_condition) {
            case handoff::WorkflowNodeCompletionConditionKind::TargetReachedFinalState:
                write_string("target_reached_final_state");
                return;
            }
            write_string("invalid");
        });
        field("completion_latched",
              [&]() { out_ << (lifecycle.completion_latched ? "true" : "false"); });
        field("target_initial_state", [&]() { write_string(lifecycle.target_initial_state); });
        field("target_final_states", [&]() {
            print_array(indent_level + 1, [&](const auto &item) {
                for (const auto &state : lifecycle.target_final_states) {
                    item([&]() { write_string(state); });
                }
            });
        });
    });
}

void PipelineJsonHelpers::print_string_array(const std::vector<std::string> &values,
                                             int indent_level) {
    print_array(indent_level, [&](const auto &item) {
        for (const auto &value : values) {
            item([&]() { write_string(value); });
        }
    });
}

} // namespace ahfl
