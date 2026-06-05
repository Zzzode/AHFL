#pragma once

#include <string>
#include <vector>

#include "base/support/json.hpp"
#include "pipeline/execution/runtime_session/session.hpp"
#include "pipeline/observation/checkpoint_record/record.hpp"
#include "pipeline/observation/scheduler_snapshot/snapshot.hpp"
#include "pipeline/persistence/descriptor/descriptor.hpp"
#include "pipeline/persistence/export/manifest.hpp"

namespace ahfl {

namespace handoff {
struct CapabilityBindingReference;
struct PackageIdentity;
struct WorkflowValueSummary;
struct WorkflowNodeLifecycleSummary;
} // namespace handoff

namespace ir {
struct WorkflowExprSummary;
} // namespace ir

/// Shared JSON printing helpers for pipeline backends.
///
/// Extracts duplicated methods that appear across multiple JSON printer backends
/// (checkpoint_record, scheduler_snapshot, persistence_descriptor,
/// persistence_export_manifest, execution_plan, runtime_session).
///
/// Derived classes should inherit from this class (typically `private`) instead
/// of directly from PrettyJsonWriter when they need these common methods.
class PipelineJsonHelpers : protected PrettyJsonWriter {
  protected:
    using PrettyJsonWriter::PrettyJsonWriter;

    // -- PackageIdentity ---------------------------------------------------

    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level);

    // -- RuntimeFailureSummary ---------------------------------------------

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level);

    // -- WorkflowExprSummary -----------------------------------------------

    void print_workflow_value_summary(const ir::WorkflowExprSummary &summary, int indent_level);
    void print_workflow_value_summary(const handoff::WorkflowValueSummary &summary,
                                      int indent_level);

    // -- Status enums (string serialization) --------------------------------

    void print_workflow_status(runtime_session::WorkflowSessionStatus status);

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status);

    void print_snapshot_status(scheduler_snapshot::SchedulerSnapshotStatus status);

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status);

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status);

    // -- Capability binding reference ---------------------------------------

    void print_capability_binding_ref(const handoff::CapabilityBindingReference &binding,
                                      int indent_level);

    // -- Lifecycle summary --------------------------------------------------

    void print_lifecycle(const handoff::WorkflowNodeLifecycleSummary &lifecycle, int indent_level);

    // -- String array -------------------------------------------------------

    void print_string_array(const std::vector<std::string> &values, int indent_level);
};

} // namespace ahfl
