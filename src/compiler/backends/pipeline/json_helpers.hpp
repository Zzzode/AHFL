#pragma once

#include <string>
#include <vector>

#include "base/support/json.hpp"

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

/// Shared JSON printing helpers for handoff and deterministic dry-run artifacts.
class PipelineJsonHelpers : protected PrettyJsonWriter {
  protected:
    using PrettyJsonWriter::PrettyJsonWriter;

    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level);

    void print_workflow_value_summary(const ir::WorkflowExprSummary &summary, int indent_level);
    void print_workflow_value_summary(const handoff::WorkflowValueSummary &summary,
                                      int indent_level);

    void print_capability_binding_ref(const handoff::CapabilityBindingReference &binding,
                                      int indent_level);

    void print_lifecycle(const handoff::WorkflowNodeLifecycleSummary &lifecycle, int indent_level);

    void print_string_array(const std::vector<std::string> &values, int indent_level);
};

} // namespace ahfl
