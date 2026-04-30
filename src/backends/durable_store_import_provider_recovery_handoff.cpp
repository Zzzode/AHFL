#include "ahfl/backends/durable_store_import_provider_recovery_handoff.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
planning_status_name(durable_store_import::ProviderWritePlanningStatus status) {
    switch (status) {
    case durable_store_import::ProviderWritePlanningStatus::Planned:
        return "planned";
    case durable_store_import::ProviderWritePlanningStatus::NotPlanned:
        return "not_planned";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderRecoveryHandoffNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::NoRecoveryRequired:
        return "no_recovery_required";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::RetryUnavailable:
        return "retry_unavailable";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::ResumeUnavailable:
        return "resume_unavailable";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::ProviderCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderCapabilityKind::PlanProviderWrite:
        return "plan_provider_write";
    case durable_store_import::ProviderCapabilityKind::PlanRetryPlaceholder:
        return "plan_retry_placeholder";
    case durable_store_import::ProviderCapabilityKind::PlanResumePlaceholder:
        return "plan_resume_placeholder";
    case durable_store_import::ProviderCapabilityKind::PlanRecoveryHandoff:
        return "plan_recovery_handoff";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderPlanningFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderPlanningFailureKind::SourceExecutionNotPersisted:
        return "source_execution_not_persisted";
    case durable_store_import::ProviderPlanningFailureKind::UnsupportedProviderCapability:
        return "unsupported_provider_capability";
    }

    return "invalid";
}

void print_retry_resume_placeholder(
    std::ostream &out,
    int indent_level,
    const durable_store_import::ProviderRetryResumePlaceholder &placeholder) {
    line(out, indent_level, "retry_resume_placeholder {");
    line(out,
         indent_level + 1,
         "retry_placeholder_available " +
             std::string(placeholder.retry_placeholder_available ? "true" : "false"));
    line(out,
         indent_level + 1,
         "resume_placeholder_available " +
             std::string(placeholder.resume_placeholder_available ? "true" : "false"));
    line(out,
         indent_level + 1,
         "retry_placeholder_identity " + (placeholder.retry_placeholder_identity.has_value()
                                              ? *placeholder.retry_placeholder_identity
                                              : std::string("none")));
    line(out,
         indent_level + 1,
         "resume_placeholder_identity " + (placeholder.resume_placeholder_identity.has_value()
                                               ? *placeholder.resume_placeholder_identity
                                               : std::string("none")));
    line(out, indent_level, "}");
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderPlanningFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "missing_capability " + (failure->missing_capability.has_value()
                                      ? capability_name(*failure->missing_capability)
                                      : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_recovery_handoff(
    const durable_store_import::ProviderRecoveryHandoffPreview &preview, std::ostream &out) {
    out << preview.format_version << '\n';
    line(out,
         0,
         "source_provider_write_attempt_format " +
             preview.source_durable_store_import_provider_write_attempt_preview_format_version);
    line(out,
         0,
         "source_adapter_execution_format " +
             preview.source_durable_store_import_adapter_execution_format_version);
    line(out, 0, "workflow " + preview.workflow_canonical_name);
    line(out, 0, "session " + preview.session_id);
    line(out, 0, "run_id " + (preview.run_id.has_value() ? *preview.run_id : "none"));
    line(out, 0, "input_fixture " + preview.input_fixture);
    line(out,
         0,
         "durable_store_import_adapter_execution_identity " +
             preview.durable_store_import_adapter_execution_identity);
    line(out,
         0,
         "durable_store_import_provider_write_attempt_identity " +
             preview.durable_store_import_provider_write_attempt_identity);
    line(out, 0, "planning_status " + planning_status_name(preview.planning_status));
    line(out,
         0,
         "provider_persistence_id " + (preview.provider_persistence_id.has_value()
                                           ? *preview.provider_persistence_id
                                           : std::string("none")));
    line(out, 0, "next_action " + next_action_name(preview.next_action));
    line(out, 0, "recovery_handoff_boundary " + preview.recovery_handoff_boundary_summary);
    line(out, 0, "next_step " + preview.next_step_recommendation);
    print_retry_resume_placeholder(out, 0, preview.retry_resume_placeholder);
    print_failure_attribution(out, 0, preview.failure_attribution);
}

} // namespace ahfl
