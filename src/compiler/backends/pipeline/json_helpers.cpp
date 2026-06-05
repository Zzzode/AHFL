#include "compiler/backends/pipeline/json_helpers.hpp"

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

// -- PackageIdentity -----------------------------------------------------------

void PipelineJsonHelpers::print_package_identity(const handoff::PackageIdentity &identity,
                                                 int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("format_version", [&]() { write_string(identity.format_version); });
        field("name", [&]() { write_string(identity.name); });
        field("version", [&]() { write_string(identity.version); });
    });
}

// -- RuntimeFailureSummary -----------------------------------------------------

void PipelineJsonHelpers::print_failure_summary(
    const runtime_session::RuntimeFailureSummary &summary, int indent_level) {
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

// -- WorkflowExprSummary -------------------------------------------------------

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

// -- Status enums (string serialization) ---------------------------------------

void PipelineJsonHelpers::print_workflow_status(runtime_session::WorkflowSessionStatus status) {
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

void PipelineJsonHelpers::print_checkpoint_status(
    checkpoint_record::CheckpointRecordStatus status) {
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

void PipelineJsonHelpers::print_snapshot_status(
    scheduler_snapshot::SchedulerSnapshotStatus status) {
    switch (status) {
    case scheduler_snapshot::SchedulerSnapshotStatus::Runnable:
        write_string("runnable");
        return;
    case scheduler_snapshot::SchedulerSnapshotStatus::Waiting:
        write_string("waiting");
        return;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted:
        write_string("terminal_completed");
        return;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed:
        write_string("terminal_failed");
        return;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial:
        write_string("terminal_partial");
        return;
    }
}

void PipelineJsonHelpers::print_persistence_status(
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

void PipelineJsonHelpers::print_manifest_status(
    persistence_export::PersistenceExportManifestStatus status) {
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

// -- Capability binding reference ----------------------------------------------

void PipelineJsonHelpers::print_capability_binding_ref(
    const handoff::CapabilityBindingReference &binding, int indent_level) {
    print_object(indent_level, [&](const auto &field) {
        field("capability_name", [&]() { write_string(binding.capability_name); });
        field("binding_key", [&]() { write_string(binding.binding_key); });
    });
}

// -- Lifecycle summary ---------------------------------------------------------

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

// -- String array --------------------------------------------------------------

void PipelineJsonHelpers::print_string_array(const std::vector<std::string> &values,
                                             int indent_level) {
    print_array(indent_level, [&](const auto &item) {
        for (const auto &value : values) {
            item([&]() { write_string(value); });
        }
    });
}

} // namespace ahfl
