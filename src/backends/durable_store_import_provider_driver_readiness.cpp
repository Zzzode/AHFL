#include "ahfl/backends/durable_store_import_provider_driver_readiness.hpp"

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
binding_status_name(durable_store_import::ProviderDriverBindingStatus status) {
    switch (status) {
    case durable_store_import::ProviderDriverBindingStatus::Bound:
        return "bound";
    case durable_store_import::ProviderDriverBindingStatus::NotBound:
        return "not_bound";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderDriverOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderDriverOperationKind::TranslateProviderPersistReceipt:
        return "translate_provider_persist_receipt";
    case durable_store_import::ProviderDriverOperationKind::NoopSourceNotPlanned:
        return "noop_source_not_planned";
    case durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch:
        return "noop_profile_mismatch";
    case durable_store_import::ProviderDriverOperationKind::NoopUnsupportedDriverCapability:
        return "noop_unsupported_driver_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderDriverReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation:
        return "ready_for_driver_implementation";
    case durable_store_import::ProviderDriverReadinessNextActionKind::FixProviderProfile:
        return "fix_provider_profile";
    case durable_store_import::ProviderDriverReadinessNextActionKind::WaitForDriverCapability:
        return "wait_for_driver_capability";
    case durable_store_import::ProviderDriverReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderDriverCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile:
        return "load_secret_free_profile";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent:
        return "translate_write_intent";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateRetryPlaceholder:
        return "translate_retry_placeholder";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateResumePlaceholder:
        return "translate_resume_placeholder";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderDriverBindingFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned:
        return "source_write_attempt_not_planned";
    case durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch:
        return "driver_profile_mismatch";
    case durable_store_import::ProviderDriverBindingFailureKind::UnsupportedDriverCapability:
        return "unsupported_driver_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderDriverBindingFailureAttribution> &failure) {
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

void print_durable_store_import_provider_driver_readiness(
    const durable_store_import::ProviderDriverReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_driver_binding_plan_format " +
             review.source_durable_store_import_provider_driver_binding_plan_format_version);
    line(out,
         0,
         "source_provider_write_attempt_format " +
             review.source_durable_store_import_provider_write_attempt_preview_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_write_attempt_identity " +
             review.durable_store_import_provider_write_attempt_identity);
    line(out,
         0,
         "durable_store_import_provider_driver_binding_identity " +
             review.durable_store_import_provider_driver_binding_identity);
    line(out, 0, "binding_status " + binding_status_name(review.binding_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "operation_descriptor_identity " + (review.operation_descriptor_identity.has_value()
                                                 ? *review.operation_descriptor_identity
                                                 : std::string("none")));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "driver_boundary " + review.driver_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl
