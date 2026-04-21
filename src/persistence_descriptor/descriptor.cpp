#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl::persistence_descriptor {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(identity, diagnostics, "persistence descriptor");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(
        summary, diagnostics, "persistence descriptor");
}

[[nodiscard]] const handoff::WorkflowPlan *
find_workflow_plan(const handoff::ExecutionPlan &plan, std::string_view workflow_canonical_name) {
    for (const auto &workflow : plan.workflows) {
        if (workflow.workflow_canonical_name == workflow_canonical_name) {
            return &workflow;
        }
    }

    return nullptr;
}

[[nodiscard]] bool package_identity_equals(const std::optional<handoff::PackageIdentity> &lhs,
                                           const std::optional<handoff::PackageIdentity> &rhs) {
    return validation::package_identity_equals(lhs, rhs);
}

[[nodiscard]] bool failure_summary_equals(
    const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
    const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    return validation::failure_summary_equals(lhs, rhs);
}

[[nodiscard]] bool is_prefix(const std::vector<std::string> &prefix,
                             const std::vector<std::string> &full) {
    return validation::is_prefix(prefix, full);
}

[[nodiscard]] bool vector_equals(const std::vector<std::string> &lhs,
                                 const std::vector<std::string> &rhs) {
    return validation::vector_equals(lhs, rhs);
}

[[nodiscard]] std::string build_planned_durable_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    std::size_t exportable_prefix_size) {
    const auto &identity_anchor =
        source_package_identity.has_value() ? source_package_identity->name
                                            : std::string(workflow_canonical_name);
    return "plan::" + identity_anchor + "::" + std::string(session_id) + "::prefix-" +
           std::to_string(exportable_prefix_size);
}

[[nodiscard]] PersistenceBlockerKind to_persistence_blocker_kind(
    checkpoint_record::CheckpointResumeBlockerKind kind) {
    switch (kind) {
    case checkpoint_record::CheckpointResumeBlockerKind::NotCheckpointFriendly:
    case checkpoint_record::CheckpointResumeBlockerKind::WaitingOnSchedulerState:
        return PersistenceBlockerKind::WaitingOnCheckpointState;
    case checkpoint_record::CheckpointResumeBlockerKind::WorkflowFailure:
        return PersistenceBlockerKind::WorkflowFailure;
    case checkpoint_record::CheckpointResumeBlockerKind::WorkflowPartial:
        return PersistenceBlockerKind::WorkflowPartial;
    }

    return PersistenceBlockerKind::WaitingOnCheckpointState;
}

} // namespace

PersistenceDescriptorValidationResult
validate_persistence_descriptor(const CheckpointPersistenceDescriptor &descriptor) {
    PersistenceDescriptorValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (descriptor.format_version != kPersistenceDescriptorFormatVersion) {
        diagnostics.error("persistence descriptor format_version must be '" +
                          std::string(kPersistenceDescriptorFormatVersion) + "'");
    }

    if (descriptor.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (descriptor.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (descriptor.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (descriptor.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (descriptor.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (descriptor.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "persistence descriptor source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (descriptor.workflow_canonical_name.empty()) {
        diagnostics.error("persistence descriptor workflow_canonical_name must not be empty");
    }

    if (descriptor.session_id.empty()) {
        diagnostics.error("persistence descriptor session_id must not be empty");
    }

    if (descriptor.input_fixture.empty()) {
        diagnostics.error("persistence descriptor input_fixture must not be empty");
    }

    if (descriptor.planned_durable_identity.empty()) {
        diagnostics.error("persistence descriptor planned_durable_identity must not be empty");
    }

    if (descriptor.source_package_identity.has_value()) {
        validate_package_identity(*descriptor.source_package_identity, diagnostics);
    }

    if (descriptor.workflow_failure_summary.has_value()) {
        validate_failure_summary(*descriptor.workflow_failure_summary, diagnostics);
    }

    std::unordered_set<std::string> execution_nodes;
    for (const auto &node_name : descriptor.execution_order) {
        if (node_name.empty()) {
            diagnostics.error("persistence descriptor execution_order contains empty node name");
            continue;
        }

        if (!execution_nodes.insert(node_name).second) {
            diagnostics.error("persistence descriptor execution_order contains duplicate node '" +
                              node_name + "'");
        }
    }

    if (descriptor.cursor.exportable_prefix_size != descriptor.cursor.exportable_prefix.size()) {
        diagnostics.error(
            "persistence descriptor cursor exportable_prefix_size must match exportable_prefix length");
    }

    if (descriptor.cursor.exportable_prefix.size() > descriptor.execution_order.size()) {
        diagnostics.error(
            "persistence descriptor cursor exportable_prefix cannot be longer than execution_order");
    }

    for (std::size_t index = 0; index < descriptor.cursor.exportable_prefix.size(); ++index) {
        if (descriptor.cursor.exportable_prefix[index] != descriptor.execution_order[index]) {
            diagnostics.error(
                "persistence descriptor cursor exportable_prefix must be a prefix of execution_order");
            break;
        }
    }

    if (descriptor.cursor.next_export_candidate_node_name.has_value()) {
        if (descriptor.cursor.next_export_candidate_node_name->empty()) {
            diagnostics.error(
                "persistence descriptor cursor next_export_candidate_node_name must not be empty");
        } else if (!execution_nodes.contains(
                       *descriptor.cursor.next_export_candidate_node_name)) {
            diagnostics.error(
                "persistence descriptor cursor next_export_candidate_node_name '" +
                *descriptor.cursor.next_export_candidate_node_name +
                "' does not exist in execution_order");
        }
    }

    if (descriptor.cursor.next_export_candidate_node_name.has_value()) {
        for (const auto &exported_node : descriptor.cursor.exportable_prefix) {
            if (exported_node == *descriptor.cursor.next_export_candidate_node_name) {
                diagnostics.error(
                    "persistence descriptor cursor next_export_candidate_node_name '" +
                    *descriptor.cursor.next_export_candidate_node_name +
                    "' cannot already be in exportable_prefix");
                break;
            }
        }
    }

    if (descriptor.cursor.export_ready && descriptor.persistence_blocker.has_value()) {
        diagnostics.error(
            "persistence descriptor cannot contain persistence_blocker when cursor export_ready is true");
    }

    if (!descriptor.cursor.export_ready &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalCompleted &&
        !descriptor.persistence_blocker.has_value()) {
        diagnostics.error(
            "persistence descriptor must contain persistence_blocker when cursor export_ready is false");
    }

    if (descriptor.persistence_blocker.has_value()) {
        if (descriptor.persistence_blocker->message.empty()) {
            diagnostics.error("persistence descriptor persistence_blocker message must not be empty");
        }

        if (descriptor.persistence_blocker->node_name.has_value() &&
            descriptor.persistence_blocker->node_name->empty()) {
            diagnostics.error(
                "persistence descriptor persistence_blocker node_name must not be empty");
        }
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::ReadyToExport &&
        !descriptor.cursor.export_ready) {
        diagnostics.error(
            "persistence descriptor ReadyToExport status requires cursor export_ready");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::ReadyToExport &&
        !descriptor.cursor.next_export_candidate_node_name.has_value()) {
        diagnostics.error(
            "persistence descriptor ReadyToExport status requires next_export_candidate_node_name");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::ReadyToExport &&
        descriptor.checkpoint_status != checkpoint_record::CheckpointRecordStatus::ReadyToPersist) {
        diagnostics.error(
            "persistence descriptor ReadyToExport status requires ReadyToPersist checkpoint_status");
    }

    if (descriptor.export_basis_kind == PersistenceBasisKind::StoreAdjacent &&
        descriptor.persistence_status != PersistenceDescriptorStatus::ReadyToExport &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence descriptor store-adjacent basis requires ready or completed persistence_status");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::TerminalCompleted &&
        descriptor.cursor.exportable_prefix.size() != descriptor.execution_order.size()) {
        diagnostics.error(
            "persistence descriptor TerminalCompleted status requires full exportable_prefix");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::TerminalCompleted &&
        descriptor.persistence_blocker.has_value()) {
        diagnostics.error(
            "persistence descriptor TerminalCompleted status cannot have persistence_blocker");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::TerminalCompleted &&
        descriptor.cursor.next_export_candidate_node_name.has_value()) {
        diagnostics.error(
            "persistence descriptor TerminalCompleted status cannot have next_export_candidate_node_name");
    }

    if ((descriptor.persistence_status == PersistenceDescriptorStatus::TerminalFailed ||
         descriptor.persistence_status == PersistenceDescriptorStatus::TerminalPartial) &&
        !descriptor.persistence_blocker.has_value()) {
        diagnostics.error(
            "persistence descriptor terminal blocked status requires persistence_blocker");
    }

    if (descriptor.persistence_status == PersistenceDescriptorStatus::TerminalFailed &&
        !descriptor.workflow_failure_summary.has_value()) {
        diagnostics.error(
            "persistence descriptor TerminalFailed status requires workflow_failure_summary");
    }

    if ((descriptor.persistence_status == PersistenceDescriptorStatus::TerminalFailed ||
         descriptor.persistence_status == PersistenceDescriptorStatus::TerminalPartial) &&
        descriptor.cursor.export_ready) {
        diagnostics.error(
            "persistence descriptor terminal blocked status cannot be export_ready");
    }

    if ((descriptor.persistence_status == PersistenceDescriptorStatus::TerminalFailed ||
         descriptor.persistence_status == PersistenceDescriptorStatus::TerminalPartial) &&
        descriptor.cursor.next_export_candidate_node_name.has_value()) {
        diagnostics.error(
            "persistence descriptor terminal blocked status cannot have next_export_candidate_node_name");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence descriptor completed workflow_status requires TerminalCompleted persistence_status");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalFailed) {
        diagnostics.error(
            "persistence descriptor failed workflow_status requires TerminalFailed persistence_status");
    }

    if (descriptor.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence descriptor TerminalCompleted checkpoint_status requires TerminalCompleted persistence_status");
    }

    if (descriptor.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalFailed) {
        diagnostics.error(
            "persistence descriptor TerminalFailed checkpoint_status requires TerminalFailed persistence_status");
    }

    if (descriptor.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalPartial) {
        diagnostics.error(
            "persistence descriptor TerminalPartial checkpoint_status requires TerminalPartial persistence_status");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        descriptor.persistence_status != PersistenceDescriptorStatus::ReadyToExport &&
        descriptor.persistence_status != PersistenceDescriptorStatus::Blocked &&
        descriptor.persistence_status != PersistenceDescriptorStatus::TerminalPartial) {
        diagnostics.error(
            "persistence descriptor partial workflow_status must map to ReadyToExport, Blocked, or TerminalPartial persistence_status");
    }

    return result;
}

PersistenceDescriptorResult
build_persistence_descriptor(const handoff::ExecutionPlan &plan,
                             const runtime_session::RuntimeSession &session,
                             const execution_journal::ExecutionJournal &journal,
                             const replay_view::ReplayView &replay,
                             const scheduler_snapshot::SchedulerSnapshot &snapshot,
                             const checkpoint_record::CheckpointRecord &record) {
    PersistenceDescriptorResult result{
        .descriptor = std::nullopt,
        .diagnostics = {},
    };

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);

    const auto session_validation = runtime_session::validate_runtime_session(session);
    result.diagnostics.append(session_validation.diagnostics);

    const auto journal_validation = execution_journal::validate_execution_journal(journal);
    result.diagnostics.append(journal_validation.diagnostics);

    const auto replay_validation = replay_view::validate_replay_view(replay);
    result.diagnostics.append(replay_validation.diagnostics);

    const auto snapshot_validation = scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    result.diagnostics.append(snapshot_validation.diagnostics);

    const auto record_validation = checkpoint_record::validate_checkpoint_record(record);
    result.diagnostics.append(record_validation.diagnostics);

    if (result.has_errors()) {
        return result;
    }

    if (session.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap runtime session source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view source_execution_plan_format_version does not match execution plan");
    }

    if (replay.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot source_execution_plan_format_version does not match execution plan");
    }

    if (snapshot.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot source_runtime_session_format_version does not match runtime session");
    }

    if (snapshot.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot source_replay_view_format_version does not match replay view");
    }

    if (record.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_execution_plan_format_version does not match execution plan");
    }

    if (record.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_runtime_session_format_version does not match runtime session");
    }

    if (record.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_execution_journal_format_version does not match execution journal");
    }

    if (record.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_replay_view_format_version does not match replay view");
    }

    if (record.source_scheduler_snapshot_format_version != snapshot.format_version) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_scheduler_snapshot_format_version does not match scheduler snapshot");
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap runtime session source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, replay.source_package_identity)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, snapshot.source_package_identity)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, record.source_package_identity)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record source_package_identity does not match execution plan");
    }

    if (session.workflow_canonical_name != journal.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != replay.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != snapshot.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != record.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record workflow_canonical_name does not match runtime session");
    }

    if (session.session_id != journal.session_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal session_id does not match runtime session");
    }

    if (session.session_id != replay.session_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view session_id does not match runtime session");
    }

    if (session.session_id != snapshot.session_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot session_id does not match runtime session");
    }

    if (session.session_id != record.session_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record session_id does not match runtime session");
    }

    if (session.run_id != journal.run_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap execution journal run_id does not match runtime session");
    }

    if (session.run_id != replay.run_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view run_id does not match runtime session");
    }

    if (session.run_id != snapshot.run_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot run_id does not match runtime session");
    }

    if (session.run_id != record.run_id) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record run_id does not match runtime session");
    }

    if (session.input_fixture != replay.input_fixture) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view input_fixture does not match runtime session");
    }

    if (session.input_fixture != snapshot.input_fixture) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot input_fixture does not match runtime session");
    }

    if (session.input_fixture != record.input_fixture) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record input_fixture does not match runtime session");
    }

    if (session.workflow_status != replay.workflow_status) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view workflow_status does not match runtime session");
    }

    if (session.workflow_status != snapshot.workflow_status) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot workflow_status does not match runtime session");
    }

    if (session.workflow_status != record.workflow_status) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record workflow_status does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, replay.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, snapshot.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, record.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record workflow_failure_summary does not match runtime session");
    }

    if (!replay.consistency.plan_matches_session || !replay.consistency.session_matches_journal ||
        !replay.consistency.journal_matches_execution_order) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view consistency must hold before building persistence descriptor");
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error("persistence descriptor bootstrap workflow '" +
                                 session.workflow_canonical_name +
                                 "' does not exist in execution plan");
    }

    if (result.has_errors()) {
        return result;
    }

    std::vector<std::string> plan_execution_order;
    plan_execution_order.reserve(workflow->nodes.size());
    for (const auto &node : workflow->nodes) {
        plan_execution_order.push_back(node.name);
    }

    if (!vector_equals(snapshot.execution_order, plan_execution_order)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot execution_order does not match execution plan workflow order");
    }

    if (!vector_equals(replay.execution_order, session.execution_order)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view execution_order does not match runtime session");
    }

    if (!is_prefix(replay.execution_order, snapshot.execution_order)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap replay view execution_order must be a prefix of scheduler snapshot execution_order");
    }

    if (!is_prefix(snapshot.cursor.completed_prefix, replay.execution_order)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap scheduler snapshot completed_prefix must be a prefix of replay view execution_order");
    }

    if (record.snapshot_status != snapshot.snapshot_status) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record snapshot_status does not match scheduler snapshot");
    }

    if (!vector_equals(record.execution_order, snapshot.execution_order)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record execution_order does not match scheduler snapshot");
    }

    if (!vector_equals(record.cursor.persistable_prefix, snapshot.cursor.completed_prefix)) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record persistable_prefix does not match scheduler snapshot completed_prefix");
    }

    if (record.cursor.resume_candidate_node_name != snapshot.cursor.next_candidate_node_name) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record resume_candidate_node_name does not match scheduler snapshot");
    }

    if (record.checkpoint_friendly_source != snapshot.cursor.checkpoint_friendly) {
        result.diagnostics.error(
            "persistence descriptor bootstrap checkpoint record checkpoint_friendly_source does not match scheduler snapshot");
    }

    if (result.has_errors()) {
        return result;
    }

    CheckpointPersistenceDescriptor descriptor{
        .format_version = std::string(kPersistenceDescriptorFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_replay_view_format_version = replay.format_version,
        .source_scheduler_snapshot_format_version = snapshot.format_version,
        .source_checkpoint_record_format_version = record.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = record.workflow_canonical_name,
        .session_id = record.session_id,
        .run_id = record.run_id,
        .input_fixture = record.input_fixture,
        .workflow_status = record.workflow_status,
        .checkpoint_status = record.checkpoint_status,
        .persistence_status = PersistenceDescriptorStatus::Blocked,
        .workflow_failure_summary = record.workflow_failure_summary,
        .execution_order = record.execution_order,
        .planned_durable_identity =
            build_planned_durable_identity(plan.source_package_identity,
                                           record.workflow_canonical_name,
                                           record.session_id,
                                           record.cursor.persistable_prefix_size),
        .export_basis_kind = PersistenceBasisKind::LocalPlanningOnly,
        .cursor =
            PersistenceCursor{
                .exportable_prefix_size = record.cursor.persistable_prefix_size,
                .exportable_prefix = record.cursor.persistable_prefix,
                .next_export_candidate_node_name = record.cursor.resume_candidate_node_name,
                .export_ready = false,
            },
        .persistence_blocker = std::nullopt,
    };

    switch (record.checkpoint_status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        descriptor.persistence_status = PersistenceDescriptorStatus::ReadyToExport;
        descriptor.cursor.export_ready = true;
        break;
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        descriptor.persistence_status = PersistenceDescriptorStatus::Blocked;
        if (record.resume_blocker.has_value()) {
            descriptor.persistence_blocker = PersistenceBlocker{
                .kind = to_persistence_blocker_kind(record.resume_blocker->kind),
                .message = record.resume_blocker->message,
                .node_name = record.resume_blocker->node_name,
            };
        }
        break;
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        descriptor.persistence_status = PersistenceDescriptorStatus::TerminalCompleted;
        descriptor.cursor.next_export_candidate_node_name = std::nullopt;
        break;
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        descriptor.persistence_status = PersistenceDescriptorStatus::TerminalFailed;
        descriptor.cursor.next_export_candidate_node_name = std::nullopt;
        if (record.resume_blocker.has_value()) {
            descriptor.persistence_blocker = PersistenceBlocker{
                .kind = to_persistence_blocker_kind(record.resume_blocker->kind),
                .message = record.resume_blocker->message,
                .node_name = record.resume_blocker->node_name,
            };
        }
        break;
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        descriptor.persistence_status = PersistenceDescriptorStatus::TerminalPartial;
        descriptor.cursor.next_export_candidate_node_name = std::nullopt;
        if (record.resume_blocker.has_value()) {
            descriptor.persistence_blocker = PersistenceBlocker{
                .kind = to_persistence_blocker_kind(record.resume_blocker->kind),
                .message = record.resume_blocker->message,
                .node_name = record.resume_blocker->node_name,
            };
        }
        break;
    }

    const auto descriptor_validation = validate_persistence_descriptor(descriptor);
    result.diagnostics.append(descriptor_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.descriptor = std::move(descriptor);
    return result;
}

} // namespace ahfl::persistence_descriptor
