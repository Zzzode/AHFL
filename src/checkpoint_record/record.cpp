#include "ahfl/checkpoint_record/record.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace ahfl::checkpoint_record {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    if (identity.format_version != handoff::kFormatVersion) {
        diagnostics.error("checkpoint record source_package_identity format_version must be '" +
                          std::string(handoff::kFormatVersion) + "'");
    }

    if (identity.name.empty()) {
        diagnostics.error("checkpoint record source_package_identity name must not be empty");
    }

    if (identity.version.empty()) {
        diagnostics.error("checkpoint record source_package_identity version must not be empty");
    }
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error("checkpoint record workflow_failure_summary message must not be empty");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error(
            "checkpoint record workflow_failure_summary node_name must not be empty");
    }
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
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->format_version == rhs->format_version && lhs->name == rhs->name &&
           lhs->version == rhs->version;
}

[[nodiscard]] bool failure_summary_equals(
    const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
    const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->kind == rhs->kind && lhs->node_name == rhs->node_name &&
           lhs->message == rhs->message;
}

[[nodiscard]] bool is_prefix(const std::vector<std::string> &prefix,
                             const std::vector<std::string> &full) {
    if (prefix.size() > full.size()) {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (prefix[index] != full[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool vector_equals(const std::vector<std::string> &lhs,
                                 const std::vector<std::string> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }

    return true;
}

} // namespace

CheckpointRecordValidationResult
validate_checkpoint_record(const CheckpointRecord &record) {
    CheckpointRecordValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (record.format_version != kCheckpointRecordFormatVersion) {
        diagnostics.error("checkpoint record format_version must be '" +
                          std::string(kCheckpointRecordFormatVersion) + "'");
    }

    if (record.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error("checkpoint record source_execution_plan_format_version must be '" +
                          std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (record.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "checkpoint record source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (record.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "checkpoint record source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (record.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error("checkpoint record source_replay_view_format_version must be '" +
                          std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (record.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "checkpoint record source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (record.workflow_canonical_name.empty()) {
        diagnostics.error("checkpoint record workflow_canonical_name must not be empty");
    }

    if (record.session_id.empty()) {
        diagnostics.error("checkpoint record session_id must not be empty");
    }

    if (record.input_fixture.empty()) {
        diagnostics.error("checkpoint record input_fixture must not be empty");
    }

    if (record.source_package_identity.has_value()) {
        validate_package_identity(*record.source_package_identity, diagnostics);
    }

    if (record.workflow_failure_summary.has_value()) {
        validate_failure_summary(*record.workflow_failure_summary, diagnostics);
    }

    std::unordered_set<std::string> execution_nodes;
    for (const auto &node_name : record.execution_order) {
        if (node_name.empty()) {
            diagnostics.error("checkpoint record execution_order contains empty node name");
            continue;
        }

        if (!execution_nodes.insert(node_name).second) {
            diagnostics.error("checkpoint record execution_order contains duplicate node '" +
                              node_name + "'");
        }
    }

    if (record.cursor.persistable_prefix_size != record.cursor.persistable_prefix.size()) {
        diagnostics.error(
            "checkpoint record cursor persistable_prefix_size must match persistable_prefix length");
    }

    if (record.cursor.persistable_prefix.size() > record.execution_order.size()) {
        diagnostics.error(
            "checkpoint record cursor persistable_prefix cannot be longer than execution_order");
    }

    for (std::size_t index = 0; index < record.cursor.persistable_prefix.size(); ++index) {
        if (record.cursor.persistable_prefix[index] != record.execution_order[index]) {
            diagnostics.error(
                "checkpoint record cursor persistable_prefix must be a prefix of execution_order");
            break;
        }
    }

    if (record.cursor.resume_candidate_node_name.has_value()) {
        if (record.cursor.resume_candidate_node_name->empty()) {
            diagnostics.error(
                "checkpoint record cursor resume_candidate_node_name must not be empty");
        } else if (!execution_nodes.contains(*record.cursor.resume_candidate_node_name)) {
            diagnostics.error("checkpoint record cursor resume_candidate_node_name '" +
                              *record.cursor.resume_candidate_node_name +
                              "' does not exist in execution_order");
        }
    }

    if (record.cursor.resume_candidate_node_name.has_value()) {
        for (const auto &persisted_node : record.cursor.persistable_prefix) {
            if (persisted_node == *record.cursor.resume_candidate_node_name) {
                diagnostics.error("checkpoint record cursor resume_candidate_node_name '" +
                                  *record.cursor.resume_candidate_node_name +
                                  "' cannot already be in persistable_prefix");
                break;
            }
        }
    }

    if (record.cursor.resume_ready && record.resume_blocker.has_value()) {
        diagnostics.error(
            "checkpoint record cannot contain resume_blocker when cursor resume_ready is true");
    }

    if (!record.cursor.resume_ready &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalCompleted &&
        !record.resume_blocker.has_value()) {
        diagnostics.error(
            "checkpoint record must contain resume_blocker when cursor resume_ready is false");
    }

    if (record.resume_blocker.has_value()) {
        if (record.resume_blocker->message.empty()) {
            diagnostics.error("checkpoint record resume_blocker message must not be empty");
        }

        if (record.resume_blocker->node_name.has_value() &&
            record.resume_blocker->node_name->empty()) {
            diagnostics.error("checkpoint record resume_blocker node_name must not be empty");
        }
    }

    if (!record.checkpoint_friendly_source &&
        record.basis_kind == CheckpointBasisKind::DurableAdjacent) {
        diagnostics.error(
            "checkpoint record durable-adjacent basis requires checkpoint_friendly_source");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::ReadyToPersist &&
        !record.cursor.resume_ready) {
        diagnostics.error(
            "checkpoint record ReadyToPersist status requires cursor resume_ready");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::ReadyToPersist &&
        !record.cursor.resume_candidate_node_name.has_value()) {
        diagnostics.error(
            "checkpoint record ReadyToPersist status requires resume_candidate_node_name");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalCompleted &&
        record.cursor.persistable_prefix.size() != record.execution_order.size()) {
        diagnostics.error(
            "checkpoint record TerminalCompleted status requires full persistable_prefix");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalCompleted &&
        record.cursor.resume_candidate_node_name.has_value()) {
        diagnostics.error(
            "checkpoint record TerminalCompleted status cannot have resume_candidate_node_name");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalCompleted &&
        record.resume_blocker.has_value()) {
        diagnostics.error("checkpoint record TerminalCompleted status cannot have resume_blocker");
    }

    if ((record.checkpoint_status == CheckpointRecordStatus::TerminalFailed ||
         record.checkpoint_status == CheckpointRecordStatus::TerminalPartial) &&
        !record.resume_blocker.has_value()) {
        diagnostics.error(
            "checkpoint record terminal blocked status requires resume_blocker");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalFailed &&
        !record.workflow_failure_summary.has_value()) {
        diagnostics.error(
            "checkpoint record TerminalFailed status requires workflow_failure_summary");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalFailed &&
        record.cursor.resume_ready) {
        diagnostics.error("checkpoint record TerminalFailed status cannot be resume_ready");
    }

    if (record.checkpoint_status == CheckpointRecordStatus::TerminalPartial &&
        record.cursor.resume_ready) {
        diagnostics.error("checkpoint record TerminalPartial status cannot be resume_ready");
    }

    if ((record.checkpoint_status == CheckpointRecordStatus::TerminalFailed ||
         record.checkpoint_status == CheckpointRecordStatus::TerminalPartial) &&
        record.cursor.resume_candidate_node_name.has_value()) {
        diagnostics.error(
            "checkpoint record terminal blocked status cannot have resume_candidate_node_name");
    }

    if (record.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalCompleted) {
        diagnostics.error(
            "checkpoint record completed workflow_status requires TerminalCompleted checkpoint_status");
    }

    if (record.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalFailed) {
        diagnostics.error(
            "checkpoint record failed workflow_status requires TerminalFailed checkpoint_status");
    }

    if (record.snapshot_status == scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalCompleted) {
        diagnostics.error(
            "checkpoint record terminal-completed snapshot_status requires TerminalCompleted checkpoint_status");
    }

    if (record.snapshot_status == scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalFailed) {
        diagnostics.error(
            "checkpoint record terminal-failed snapshot_status requires TerminalFailed checkpoint_status");
    }

    if (record.snapshot_status == scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial &&
        record.checkpoint_status != CheckpointRecordStatus::TerminalPartial) {
        diagnostics.error(
            "checkpoint record terminal-partial snapshot_status requires TerminalPartial checkpoint_status");
    }

    return result;
}

CheckpointRecordResult
build_checkpoint_record(const handoff::ExecutionPlan &plan,
                        const runtime_session::RuntimeSession &session,
                        const execution_journal::ExecutionJournal &journal,
                        const replay_view::ReplayView &replay,
                        const scheduler_snapshot::SchedulerSnapshot &snapshot) {
    CheckpointRecordResult result{
        .record = std::nullopt,
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

    if (result.has_errors()) {
        return result;
    }

    if (session.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap runtime session source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view source_execution_plan_format_version does not match execution plan");
    }

    if (replay.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot source_execution_plan_format_version does not match execution plan");
    }

    if (snapshot.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot source_runtime_session_format_version does not match runtime session");
    }

    if (snapshot.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot source_replay_view_format_version does not match replay view");
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        result.diagnostics.error(
            "checkpoint record bootstrap runtime session source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, replay.source_package_identity)) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, snapshot.source_package_identity)) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot source_package_identity does not match execution plan");
    }

    if (session.workflow_canonical_name != journal.workflow_canonical_name) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != replay.workflow_canonical_name) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != snapshot.workflow_canonical_name) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot workflow_canonical_name does not match runtime session");
    }

    if (session.session_id != journal.session_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal session_id does not match runtime session");
    }

    if (session.session_id != replay.session_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view session_id does not match runtime session");
    }

    if (session.session_id != snapshot.session_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot session_id does not match runtime session");
    }

    if (session.run_id != journal.run_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap execution journal run_id does not match runtime session");
    }

    if (session.run_id != replay.run_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view run_id does not match runtime session");
    }

    if (session.run_id != snapshot.run_id) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot run_id does not match runtime session");
    }

    if (session.input_fixture != replay.input_fixture) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view input_fixture does not match runtime session");
    }

    if (session.input_fixture != snapshot.input_fixture) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot input_fixture does not match runtime session");
    }

    if (session.workflow_status != replay.workflow_status) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view workflow_status does not match runtime session");
    }

    if (session.workflow_status != snapshot.workflow_status) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot workflow_status does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, replay.workflow_failure_summary)) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, snapshot.workflow_failure_summary)) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot workflow_failure_summary does not match runtime session");
    }

    if (!replay.consistency.plan_matches_session || !replay.consistency.session_matches_journal ||
        !replay.consistency.journal_matches_execution_order) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view consistency must hold before building checkpoint record");
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error("checkpoint record bootstrap workflow '" +
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
            "checkpoint record bootstrap scheduler snapshot execution_order does not match execution plan workflow order");
    }

    if (!vector_equals(replay.execution_order, session.execution_order)) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view execution_order does not match runtime session");
    }

    if (!is_prefix(replay.execution_order, snapshot.execution_order)) {
        result.diagnostics.error(
            "checkpoint record bootstrap replay view execution_order must be a prefix of scheduler snapshot execution_order");
    }

    if (!is_prefix(snapshot.cursor.completed_prefix, replay.execution_order)) {
        result.diagnostics.error(
            "checkpoint record bootstrap scheduler snapshot completed_prefix must be a prefix of replay view execution_order");
    }

    if (result.has_errors()) {
        return result;
    }

    CheckpointRecord record{
        .format_version = std::string(kCheckpointRecordFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_replay_view_format_version = replay.format_version,
        .source_scheduler_snapshot_format_version = snapshot.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = session.workflow_canonical_name,
        .session_id = session.session_id,
        .run_id = session.run_id,
        .input_fixture = session.input_fixture,
        .workflow_status = session.workflow_status,
        .snapshot_status = snapshot.snapshot_status,
        .workflow_failure_summary = session.failure_summary,
        .execution_order = snapshot.execution_order,
        .basis_kind = CheckpointBasisKind::LocalOnly,
        .checkpoint_friendly_source = snapshot.cursor.checkpoint_friendly,
        .cursor =
            CheckpointCursor{
                .persistable_prefix_size = snapshot.cursor.completed_prefix_size,
                .persistable_prefix = snapshot.cursor.completed_prefix,
                .resume_candidate_node_name = snapshot.cursor.next_candidate_node_name,
                .resume_ready = false,
            },
        .resume_blocker = std::nullopt,
    };

    switch (snapshot.snapshot_status) {
    case scheduler_snapshot::SchedulerSnapshotStatus::Runnable:
        if (record.checkpoint_friendly_source) {
            record.checkpoint_status = CheckpointRecordStatus::ReadyToPersist;
            record.cursor.resume_ready = true;
        } else {
            record.checkpoint_status = CheckpointRecordStatus::Blocked;
            record.resume_blocker = CheckpointResumeBlocker{
                .kind = CheckpointResumeBlockerKind::NotCheckpointFriendly,
                .message = "scheduler snapshot is not checkpoint friendly",
                .node_name = record.cursor.resume_candidate_node_name,
            };
        }
        break;
    case scheduler_snapshot::SchedulerSnapshotStatus::Waiting:
        record.checkpoint_status = CheckpointRecordStatus::Blocked;
        record.resume_blocker = CheckpointResumeBlocker{
            .kind = record.checkpoint_friendly_source
                        ? CheckpointResumeBlockerKind::WaitingOnSchedulerState
                        : CheckpointResumeBlockerKind::NotCheckpointFriendly,
            .message = record.checkpoint_friendly_source
                           ? "scheduler frontier is not yet runnable"
                           : "scheduler snapshot is not checkpoint friendly",
            .node_name = record.cursor.resume_candidate_node_name,
        };
        break;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted:
        record.checkpoint_status = CheckpointRecordStatus::TerminalCompleted;
        record.cursor.resume_candidate_node_name = std::nullopt;
        break;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed:
        record.checkpoint_status = CheckpointRecordStatus::TerminalFailed;
        record.cursor.resume_candidate_node_name = std::nullopt;
        record.resume_blocker = CheckpointResumeBlocker{
            .kind = CheckpointResumeBlockerKind::WorkflowFailure,
            .message = record.workflow_failure_summary.has_value()
                           ? record.workflow_failure_summary->message
                           : "workflow failed before checkpoint became resumable",
            .node_name = record.workflow_failure_summary.has_value()
                             ? record.workflow_failure_summary->node_name
                             : std::nullopt,
        };
        break;
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial:
        record.checkpoint_status = CheckpointRecordStatus::TerminalPartial;
        record.cursor.resume_candidate_node_name = std::nullopt;
        record.resume_blocker = CheckpointResumeBlocker{
            .kind = CheckpointResumeBlockerKind::WorkflowPartial,
            .message = "workflow remained partial without a resumable checkpoint basis",
            .node_name = std::nullopt,
        };
        break;
    }

    const auto record_validation = validate_checkpoint_record(record);
    result.diagnostics.append(record_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.record = std::move(record);
    return result;
}

} // namespace ahfl::checkpoint_record
