#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "checkpoint_record/record.hpp"
#include "persistence_descriptor/descriptor.hpp"
#include "persistence_export/manifest.hpp"
#include "runtime_session/session.hpp"
#include "scheduler_snapshot/snapshot.hpp"

namespace ahfl::pipeline_review {

/// @name Status-to-string converters
/// @{

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status);

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind);

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status);

[[nodiscard]] std::string snapshot_status_name(scheduler_snapshot::SchedulerSnapshotStatus status);

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status);

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status);

/// @}

/// @name Common text-review printing helpers
/// @{

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary);

void print_string_list(std::ostream &out,
                       int indent_level,
                       std::string_view label,
                       const std::vector<std::string> &values);

/// @}

} // namespace ahfl::pipeline_review
