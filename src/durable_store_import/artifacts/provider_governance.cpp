#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_approval_receipt {

void print_durable_store_import_provider_approval_receipt_json(
    const durable_store_import::ApprovalReceipt &receipt, std::ostream &out) {
    out << receipt.format_version << '\n';
    out << "workflow " << receipt.workflow_canonical_name << '\n';
    out << "session " << receipt.session_id << '\n';
    if (receipt.run_id.has_value()) {
        out << "run_id " << *receipt.run_id << '\n';
    }
    out << "approval_request " << receipt.approval_request_identity << '\n';
    out << "approval_decision " << receipt.approval_decision_identity << '\n';
    out << "final_decision " << durable_store_import::to_string_view(receipt.final_decision)
        << '\n';
    out << "release_evidence_archive " << receipt.release_evidence_archive_manifest_identity
        << '\n';
    out << "production_readiness_evidence " << receipt.production_readiness_evidence_identity
        << '\n';
    out << "is_approved " << (receipt.is_approved ? "true" : "false") << '\n';
    out << "receipt_summary " << receipt.receipt_summary << '\n';
    out << "blocking_reason_summary " << receipt.blocking_reason_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_approval_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_capability_negotiation_review {
namespace {

void print_selection(durable_store_import::ProviderSelectionStatus status, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Selected,
                                        "selected");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSelectionStatus::FallbackSelected, "fallback_selected");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Blocked,
                                        "blocked"));
}

void print_negotiation(durable_store_import::ProviderCapabilityNegotiationStatus status,
                       std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Compatible, "compatible");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Degraded, "degraded");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_capability_negotiation_review(
    const durable_store_import::ProviderCapabilityNegotiationReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "selection_plan " << review.durable_store_import_provider_selection_plan_identity
        << '\n';
    out << "selected_provider " << review.selected_provider_id << '\n';
    out << "selection_status ";
    print_selection(review.selection_status, out);
    out << '\n';
    out << "negotiation_status ";
    print_negotiation(review.negotiation_status, out);
    out << '\n';
    out << "summary " << review.negotiation_summary << '\n';
    out << "recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_capability_negotiation_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_compatibility_report {
namespace {

class CompatibilityReportJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit CompatibilityReportJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("compatibility_report_identity", [&]() {
                write_string(report.durable_store_import_provider_compatibility_report_identity);
            });
            field("status", [&]() { print_status(report.status); });
            field("mock_adapter_compatible", [&]() { write_bool(report.mock_adapter_compatible); });
            field("local_filesystem_alpha_compatible",
                  [&]() { write_bool(report.local_filesystem_alpha_compatible); });
            field("capability_matrix_complete",
                  [&]() { write_bool(report.capability_matrix_complete); });
            field("external_service_required",
                  [&]() { write_bool(report.external_service_required); });
            field("compatibility_summary", [&]() { write_string(report.compatibility_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderCompatibilityStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Passed,
                                    "passed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Failed,
                                    "failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_compatibility_report_json(
    const durable_store_import::ProviderCompatibilityReport &report, std::ostream &out) {
    CompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_compatibility_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_compatibility_test_manifest {
namespace {

class ManifestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ManifestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityTestManifest &manifest) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(manifest.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(manifest.workflow_canonical_name); });
            field("session_id", [&]() { write_string(manifest.session_id); });
            field("run_id", [&]() { print_optional_string(manifest.run_id); });
            field("input_fixture", [&]() { write_string(manifest.input_fixture); });
            field("compatibility_test_manifest_identity", [&]() {
                write_string(
                    manifest.durable_store_import_provider_compatibility_test_manifest_identity);
            });
            field("includes_mock_adapter", [&]() { write_bool(manifest.includes_mock_adapter); });
            field("includes_local_filesystem_alpha",
                  [&]() { write_bool(manifest.includes_local_filesystem_alpha); });
            field("provider_count", [&]() { out_ << manifest.provider_count; });
            field("fixture_count", [&]() { out_ << manifest.fixture_count; });
            field("capability_matrix_reference",
                  [&]() { write_string(manifest.capability_matrix_reference); });
            field("external_service_required",
                  [&]() { write_bool(manifest.external_service_required); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_compatibility_test_manifest_json(
    const durable_store_import::ProviderCompatibilityTestManifest &manifest, std::ostream &out) {
    ManifestJsonPrinter(out).print(manifest);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_compatibility_test_manifest

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_conformance_report {
namespace {

// 辅助函数：输出检查结果
void print_result(durable_store_import::ConformanceCheckResult result, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        result,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Pass, "pass");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Fail, "fail");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Skipped,
                                        "skipped"));
}

} // namespace

void print_durable_store_import_provider_conformance_report(
    const durable_store_import::ProviderConformanceReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "input_fixture " << report.input_fixture << '\n';
    out << "compatibility_report "
        << report.durable_store_import_provider_compatibility_report_identity << '\n';
    out << "registry " << report.durable_store_import_provider_registry_identity << '\n';
    out << "readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "pass_count " << report.pass_count << '\n';
    out << "fail_count " << report.fail_count << '\n';
    out << "skipped_count " << report.skipped_count << '\n';
    out << "checks " << report.checks.size() << '\n';
    for (const auto &check : report.checks) {
        out << "check " << check.check_name << ' ';
        print_result(check.result, out);
        out << ' ' << check.artifact_reference;
        if (check.failure_reason.has_value()) {
            out << " \"" << *check.failure_reason << '"';
        }
        out << '\n';
    }
    out << "summary " << report.conformance_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_conformance_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_driver_binding {
namespace {

class DurableStoreImportProviderDriverBindingJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderDriverBindingJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderDriverBindingPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_write_attempt_preview_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_write_attempt_preview_format_version);
            });
            field("source_durable_store_import_provider_adapter_config_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_adapter_config_format_version);
            });
            field("source_durable_store_import_provider_capability_matrix_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_capability_matrix_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_write_attempt_identity", [&]() {
                write_string(plan.durable_store_import_provider_write_attempt_identity);
            });
            field("durable_store_import_provider_driver_binding_identity", [&]() {
                write_string(plan.durable_store_import_provider_driver_binding_identity);
            });
            field("source_planning_status",
                  [&]() { print_planning_status(plan.source_planning_status); });
            field("provider_persistence_id",
                  [&]() { print_optional_string(plan.provider_persistence_id); });
            field("driver_profile", [&]() { print_driver_profile(plan.driver_profile, 1); });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("binding_status", [&]() { print_binding_status(plan.binding_status); });
            field("operation_descriptor_identity",
                  [&]() { print_optional_string(plan.operation_descriptor_identity); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_driver_kind(durable_store_import::ProviderDriverKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::ProviderNeutralPreviewDriver,
                "provider_neutral_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::AwsObjectStorePreviewDriver,
                "aws_object_store_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::GcpObjectStorePreviewDriver,
                "gcp_object_store_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::AzureObjectStorePreviewDriver,
                "azure_object_store_preview_driver"));
    }

    void print_capability(durable_store_import::ProviderDriverCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile,
                "load_secret_free_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent,
                "translate_write_intent");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateRetryPlaceholder,
                "translate_retry_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateResumePlaceholder,
                "translate_resume_placeholder"));
    }

    void print_operation_kind(durable_store_import::ProviderDriverOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::TranslateProviderPersistReceipt,
                "translate_provider_persist_receipt");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopSourceNotPlanned,
                "noop_source_not_planned");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch,
                "noop_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopUnsupportedDriverCapability,
                "noop_unsupported_driver_capability"));
    }

    void print_binding_status(durable_store_import::ProviderDriverBindingStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::Bound,
                                    "bound");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::NotBound,
                                    "not_bound"));
    }

    void print_planning_status(durable_store_import::ProviderWritePlanningStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::Planned,
                                    "planned");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::NotPlanned,
                                    "not_planned"));
    }

    void print_failure_kind(durable_store_import::ProviderDriverBindingFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingFailureKind::
                                        SourceWriteAttemptNotPlanned,
                                    "source_write_attempt_not_planned");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch,
                "driver_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverBindingFailureKind::UnsupportedDriverCapability,
                "unsupported_driver_capability"));
    }

    void print_driver_profile(const durable_store_import::ProviderDriverProfile &profile,
                              int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(profile.format_version); });
            field("driver_kind", [&]() { print_driver_kind(profile.driver_kind); });
            field("driver_profile_identity",
                  [&]() { write_string(profile.driver_profile_identity); });
            field("provider_profile_ref", [&]() { write_string(profile.provider_profile_ref); });
            field("provider_namespace", [&]() { write_string(profile.provider_namespace); });
            field("secret_free_config_ref",
                  [&]() { write_string(profile.secret_free_config_ref); });
            field("credential_free", [&]() { write_bool(profile.credential_free); });
            field("supports_secret_free_profile_load",
                  [&]() { write_bool(profile.supports_secret_free_profile_load); });
            field("supports_write_intent_translation",
                  [&]() { write_bool(profile.supports_write_intent_translation); });
            field("supports_retry_placeholder_translation",
                  [&]() { write_bool(profile.supports_retry_placeholder_translation); });
            field("supports_resume_placeholder_translation",
                  [&]() { write_bool(profile.supports_resume_placeholder_translation); });
            field("credential_reference",
                  [&]() { print_optional_string(profile.credential_reference); });
            field("endpoint_uri", [&]() { print_optional_string(profile.endpoint_uri); });
            field("endpoint_secret_reference",
                  [&]() { print_optional_string(profile.endpoint_secret_reference); });
            field("object_path", [&]() { print_optional_string(profile.object_path); });
            field("database_table", [&]() { print_optional_string(profile.database_table); });
            field("sdk_payload_schema",
                  [&]() { print_optional_string(profile.sdk_payload_schema); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderDriverBindingFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("missing_capability", [&]() {
                print_optional(failure.missing_capability,
                               [&](const auto &value) { print_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_provider_driver_binding_json(
    const durable_store_import::ProviderDriverBindingPlan &plan, std::ostream &out) {
    DurableStoreImportProviderDriverBindingJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_driver_binding

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_driver_readiness {
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

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_driver_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_execution_audit_event {
namespace {

class AuditEventJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AuditEventJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderExecutionAuditEvent &event) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(event.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(event.workflow_canonical_name); });
            field("session_id", [&]() { write_string(event.session_id); });
            field("run_id", [&]() { print_optional_string(event.run_id); });
            field("input_fixture", [&]() { write_string(event.input_fixture); });
            field("execution_audit_event_identity", [&]() {
                write_string(event.durable_store_import_provider_execution_audit_event_identity);
            });
            field("event_name", [&]() { write_string(event.event_name); });
            field("outcome", [&]() { print_outcome(event.outcome); });
            field("redaction_policy_version",
                  [&]() { write_string(event.redaction_policy_version); });
            field("event_summary", [&]() { write_string(event.event_summary); });
            field("secret_free", [&]() { write_bool(event.secret_free); });
            field("raw_telemetry_persisted", [&]() { write_bool(event.raw_telemetry_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_outcome(durable_store_import::ProviderAuditOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                    "retry_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                    "recovery_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_execution_audit_event_json(
    const durable_store_import::ProviderExecutionAuditEvent &event, std::ostream &out) {
    AuditEventJsonPrinter(out).print(event);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_execution_audit_event

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_failure_taxonomy_report {
namespace {

class FailureTaxonomyJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit FailureTaxonomyJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderFailureTaxonomyReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("failure_taxonomy_report_identity", [&]() {
                write_string(report.durable_store_import_provider_failure_taxonomy_report_identity);
            });
            field("failure_kind", [&]() { print_kind(report.failure_kind); });
            field("failure_category", [&]() { print_category(report.failure_category); });
            field("retryability", [&]() { print_retryability(report.retryability); });
            field("operator_action", [&]() { print_action(report.operator_action); });
            field("security_sensitivity",
                  [&]() { print_sensitivity(report.security_sensitivity); });
            field("redacted_failure_summary",
                  [&]() { write_string(report.redacted_failure_summary); });
            field("recovery_review_failure_summary",
                  [&]() { write_string(report.recovery_review_failure_summary); });
            field("secret_bearing_error_persisted",
                  [&]() { write_bool(report.secret_bearing_error_persisted); });
            field("raw_provider_error_persisted",
                  [&]() { write_bool(report.raw_provider_error_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_kind(durable_store_import::ProviderFailureKindV1 kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authentication,
                                    "authentication");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authorization,
                                    "authorization");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Network,
                                    "network");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Throttling,
                                    "throttling");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Conflict,
                                    "conflict");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::SchemaMismatch,
                                    "schema_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::ProviderInternal,
                                    "provider_internal");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Unknown,
                                    "unknown");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Blocked,
                                    "blocked"));
    }

    void print_category(durable_store_import::ProviderFailureCategoryV1 category) {
        AHFL_ARTIFACT_PRINT_ENUM(
            category,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Security,
                                    "security");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Transport,
                                    "transport");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::RateLimit,
                                    "rate_limit");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Consistency,
                                    "consistency");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Contract,
                                    "contract");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Provider,
                                    "provider");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Unknown,
                                    "unknown");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Blocked,
                                    "blocked"));
    }

    void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            retryability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::NotApplicable,
                "not_applicable");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureRetryabilityV1::Retryable,
                                    "retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::NonRetryable, "non_retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired,
                "duplicate_review_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureRetryabilityV1::Blocked,
                                    "blocked"));
    }

    void print_action(durable_store_import::ProviderFailureOperatorActionV1 action) {
        AHFL_ARTIFACT_PRINT_ENUM(
            action,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureOperatorActionV1::None,
                                    "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::RotateCredentials,
                "rotate_credentials");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::GrantPermission,
                "grant_permission");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::RetryLater, "retry_later");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::InspectDuplicate,
                "inspect_duplicate");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::FixSchema, "fix_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::EscalateProvider,
                "escalate_provider");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::ManualReview,
                "manual_review"));
    }

    void print_sensitivity(durable_store_import::ProviderFailureSecuritySensitivityV1 sensitivity) {
        AHFL_ARTIFACT_PRINT_ENUM(
            sensitivity,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::SecretRelated,
                "secret_related");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::PermissionRelated,
                "permission_related");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::NonSensitive,
                "non_sensitive");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::Unknown, "unknown"));
    }
};

} // namespace

void print_durable_store_import_provider_failure_taxonomy_report_json(
    const durable_store_import::ProviderFailureTaxonomyReport &report, std::ostream &out) {
    FailureTaxonomyJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_failure_taxonomy_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_failure_taxonomy_review {
namespace {

void print_kind(durable_store_import::ProviderFailureKindV1 kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::None, "none");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authentication,
                                        "authentication");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authorization,
                                        "authorization");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Network,
                                        "network");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Throttling,
                                        "throttling");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Conflict,
                                        "conflict");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::SchemaMismatch,
                                        "schema_mismatch");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureKindV1::ProviderInternal, "provider_internal");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Unknown,
                                        "unknown");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Blocked,
                                        "blocked"));
}

void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability,
                        std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        retryability,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::NotApplicable, "not_applicable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::Retryable, "retryable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::NonRetryable, "non_retryable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired,
            "duplicate_review_required");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_failure_taxonomy_review(
    const durable_store_import::ProviderFailureTaxonomyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "taxonomy_report "
        << review.durable_store_import_provider_failure_taxonomy_report_identity << '\n';
    out << "failure_kind ";
    print_kind(review.failure_kind, out);
    out << '\n';
    out << "retryability ";
    print_retryability(review.retryability, out);
    out << '\n';
    out << "summary " << review.taxonomy_review_summary << '\n';
    out << "operator_recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_failure_taxonomy_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_fixture_matrix {
namespace {

class FixtureMatrixJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit FixtureMatrixJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderFixtureMatrix &matrix) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(matrix.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(matrix.workflow_canonical_name); });
            field("session_id", [&]() { write_string(matrix.session_id); });
            field("run_id", [&]() { print_optional_string(matrix.run_id); });
            field("input_fixture", [&]() { write_string(matrix.input_fixture); });
            field("fixture_matrix_identity", [&]() {
                write_string(matrix.durable_store_import_provider_fixture_matrix_identity);
            });
            field("covers_mock_adapter", [&]() { write_bool(matrix.covers_mock_adapter); });
            field("covers_local_filesystem_alpha",
                  [&]() { write_bool(matrix.covers_local_filesystem_alpha); });
            field("covers_success_fixture", [&]() { write_bool(matrix.covers_success_fixture); });
            field("covers_timeout_fixture", [&]() { write_bool(matrix.covers_timeout_fixture); });
            field("covers_conflict_fixture", [&]() { write_bool(matrix.covers_conflict_fixture); });
            field("covers_schema_mismatch_fixture",
                  [&]() { write_bool(matrix.covers_schema_mismatch_fixture); });
            field("provider_count", [&]() { out_ << matrix.provider_count; });
            field("fixture_count", [&]() { out_ << matrix.fixture_count; });
            field("external_service_required",
                  [&]() { write_bool(matrix.external_service_required); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_fixture_matrix_json(
    const durable_store_import::ProviderFixtureMatrix &matrix, std::ostream &out) {
    FixtureMatrixJsonPrinter(out).print(matrix);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_fixture_matrix

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_operator_review_event {
namespace {

void print_outcome(durable_store_import::ProviderAuditOutcome outcome, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        outcome,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success,
                                        "success");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure,
                                        "failure");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                        "retry_pending");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                        "recovery_pending");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                        "blocked"));
}

void print_next(durable_store_import::ProviderAuditNextActionKind action, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        action,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditNextActionKind::Archive,
                                        "archive");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::RetryWithIdempotency,
            "retry_with_idempotency");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::RecoveryReview, "recovery_review");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::OperatorReview, "operator_review"));
}

} // namespace

void print_durable_store_import_provider_operator_review_event(
    const durable_store_import::ProviderOperatorReviewEvent &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "telemetry_summary " << review.durable_store_import_provider_telemetry_summary_identity
        << '\n';
    out << "outcome ";
    print_outcome(review.outcome, out);
    out << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "summary " << review.operator_review_summary << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_operator_review_event

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_integration_dry_run_report {

void print_durable_store_import_provider_production_integration_dry_run_report(
    const durable_store_import::ProviderProductionIntegrationDryRunReport &report,
    std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "conformance_report " << report.conformance_report_identity << '\n';
    out << "schema_compatibility_report " << report.schema_compatibility_report_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "release_evidence_archive_manifest " << report.release_evidence_archive_manifest_identity
        << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "runtime_policy_report " << report.runtime_policy_report_identity << '\n';
    out << "total_evidence_count " << report.total_evidence_count << '\n';
    out << "valid_evidence_count " << report.valid_evidence_count << '\n';
    out << "invalid_evidence_count " << report.invalid_evidence_count << '\n';
    out << "missing_evidence_count " << report.missing_evidence_count << '\n';
    for (const auto &item : report.evidence_chain) {
        out << "evidence " << item.evidence_type
            << " present=" << (item.is_present ? "true" : "false")
            << " valid=" << (item.is_valid ? "true" : "false")
            << " fresh=" << (item.is_fresh ? "true" : "false") << '\n';
    }
    out << "readiness_state " << durable_store_import::to_string_view(report.readiness_state)
        << '\n';
    out << "blocking_item_count " << report.blocking_item_count << '\n';
    for (const auto &blocker : report.blocking_items) {
        out << "blocker " << blocker.block_type << " reason=" << blocker.block_reason << '\n';
    }
    for (const auto &action : report.next_operator_actions) {
        out << "next_action priority=" << action.priority << " type=" << action.action_type << " "
            << action.action_description << '\n';
    }
    out << "is_ready_for_controlled_rollout "
        << (report.is_ready_for_controlled_rollout ? "true" : "false") << '\n';
    out << "is_non_mutating_mode " << (report.is_non_mutating_mode ? "true" : "false") << '\n';
    out << "dry_run_summary " << report.dry_run_summary << '\n';
    out << "blocking_summary " << report.blocking_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_integration_dry_run_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_evidence {
namespace {

class EvidenceJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit EvidenceJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessEvidence &evidence) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(evidence.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(evidence.workflow_canonical_name); });
            field("session_id", [&]() { write_string(evidence.session_id); });
            field("run_id", [&]() { print_optional_string(evidence.run_id); });
            field("input_fixture", [&]() { write_string(evidence.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    evidence.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("security_evidence_passed",
                  [&]() { write_bool(evidence.security_evidence_passed); });
            field("recovery_evidence_passed",
                  [&]() { write_bool(evidence.recovery_evidence_passed); });
            field("compatibility_evidence_passed",
                  [&]() { write_bool(evidence.compatibility_evidence_passed); });
            field("observability_evidence_passed",
                  [&]() { write_bool(evidence.observability_evidence_passed); });
            field("registry_evidence_passed",
                  [&]() { write_bool(evidence.registry_evidence_passed); });
            field("blocking_issue_count", [&]() { out_ << evidence.blocking_issue_count; });
            field("evidence_summary", [&]() { write_string(evidence.evidence_summary); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_production_readiness_evidence_json(
    const durable_store_import::ProviderProductionReadinessEvidence &evidence, std::ostream &out) {
    EvidenceJsonPrinter(out).print(evidence);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_evidence

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_report {
namespace {

void print_gate(durable_store_import::ProviderProductionReleaseGate gate, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        gate,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview,
            "ready_for_production_review");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::Conditional, "conditional");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_production_readiness_report(
    const durable_store_import::ProviderProductionReadinessReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "production_readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "release_gate ";
    print_gate(report.release_gate, out);
    out << '\n';
    out << "summary " << report.operator_report_summary << '\n';
    out << "next_step " << report.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_review {
namespace {

class ReadinessReviewJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ReadinessReviewJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessReview &review) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(review.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(review.workflow_canonical_name); });
            field("session_id", [&]() { write_string(review.session_id); });
            field("run_id", [&]() { print_optional_string(review.run_id); });
            field("input_fixture", [&]() { write_string(review.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    review.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("release_gate", [&]() { print_gate(review.release_gate); });
            field("blocking_issue_count", [&]() { out_ << review.blocking_issue_count; });
            field("blocking_issue_summary", [&]() { write_string(review.blocking_issue_summary); });
            field("readiness_summary", [&]() { write_string(review.readiness_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_gate(durable_store_import::ProviderProductionReleaseGate gate) {
        AHFL_ARTIFACT_PRINT_ENUM(
            gate,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview,
                "ready_for_production_review");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderProductionReleaseGate::Conditional, "conditional");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderProductionReleaseGate::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_production_readiness_review_json(
    const durable_store_import::ProviderProductionReadinessReview &review, std::ostream &out) {
    ReadinessReviewJsonPrinter(out).print(review);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_review

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_registry {
namespace {

class RegistryJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit RegistryJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderRegistry &registry) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(registry.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(registry.workflow_canonical_name); });
            field("session_id", [&]() { write_string(registry.session_id); });
            field("run_id", [&]() { print_optional_string(registry.run_id); });
            field("input_fixture", [&]() { write_string(registry.input_fixture); });
            field("registry_identity", [&]() {
                write_string(registry.durable_store_import_provider_registry_identity);
            });
            field("primary_provider_id", [&]() { write_string(registry.primary_provider_id); });
            field("fallback_provider_id", [&]() { write_string(registry.fallback_provider_id); });
            field("mock_adapter_registered",
                  [&]() { write_bool(registry.mock_adapter_registered); });
            field("local_filesystem_alpha_registered",
                  [&]() { write_bool(registry.local_filesystem_alpha_registered); });
            field("unaudited_fallback_allowed",
                  [&]() { write_bool(registry.unaudited_fallback_allowed); });
            field("registered_provider_count",
                  [&]() { out_ << registry.registered_provider_count; });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_registry_json(
    const durable_store_import::ProviderRegistry &registry, std::ostream &out) {
    RegistryJsonPrinter(out).print(registry);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_registry

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_release_evidence_archive_manifest {

void print_durable_store_import_provider_release_evidence_archive_manifest(
    const durable_store_import::ReleaseEvidenceArchiveManifest &manifest, std::ostream &out) {
    out << manifest.format_version << '\n';
    out << "workflow " << manifest.workflow_canonical_name << '\n';
    out << "session " << manifest.session_id << '\n';
    if (manifest.run_id.has_value()) {
        out << "run_id " << *manifest.run_id << '\n';
    }
    out << "conformance_report "
        << manifest.durable_store_import_provider_conformance_report_identity << '\n';
    out << "schema_compatibility_report "
        << manifest.durable_store_import_provider_schema_compatibility_report_identity << '\n';
    out << "config_bundle_validation_report "
        << manifest.durable_store_import_provider_config_bundle_validation_report_identity << '\n';
    out << "production_readiness_evidence "
        << manifest.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "total_evidence_count " << manifest.total_evidence_count << '\n';
    out << "present_evidence_count " << manifest.present_evidence_count << '\n';
    out << "valid_evidence_count " << manifest.valid_evidence_count << '\n';
    out << "missing_evidence_count " << manifest.missing_evidence_count << '\n';
    out << "invalid_evidence_count " << manifest.invalid_evidence_count << '\n';
    out << "is_release_ready " << (manifest.is_release_ready ? "true" : "false") << '\n';
    out << "evidence_items " << manifest.evidence_items.size() << '\n';
    for (const auto &item : manifest.evidence_items) {
        out << "evidence " << item.evidence_type << ' ' << item.evidence_identity << ' '
            << item.format_version << ' ' << (item.is_present ? "present" : "missing") << ' '
            << (item.is_valid ? "valid" : "invalid") << '\n';
    }
    out << "archive_summary " << manifest.archive_summary << '\n';
    out << "missing_evidence_summary " << manifest.missing_evidence_summary << '\n';
    out << "stale_evidence_summary " << manifest.stale_evidence_summary << '\n';
    out << "incompatible_evidence_summary " << manifest.incompatible_evidence_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_release_evidence_archive_manifest

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_schema_compatibility_report {
namespace {

class SchemaCompatibilityReportJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SchemaCompatibilityReportJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSchemaCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("version_checks", [&]() { print_version_checks(report.version_checks); });
            field("source_chain_checks",
                  [&]() { print_source_chain_checks(report.source_chain_checks); });
            field("reference_checks", [&]() { print_reference_checks(report.reference_checks); });
            field("compatible_count", [&]() { out_ << report.compatible_count; });
            field("incompatible_count", [&]() { out_ << report.incompatible_count; });
            field("unknown_count", [&]() { out_ << report.unknown_count; });
            field("compatibility_summary", [&]() { write_string(report.compatibility_summary); });
            field("has_schema_drift", [&]() { write_bool(report.has_schema_drift); });
            field("drift_details", [&]() { print_optional_string(report.drift_details); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::SchemaCompatibilityStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Compatible,
                                    "compatible");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Incompatible,
                                    "incompatible");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Unknown,
                                    "unknown"));
    }

    void
    print_version_checks(const std::vector<durable_store_import::ArtifactVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("artifact_type", [&]() { write_string(check.artifact_type); });
                        field("artifact_identity",
                              [&]() { write_string(check.artifact_identity); });
                        field("format_version", [&]() { write_string(check.format_version); });
                        field("status", [&]() { print_status(check.status); });
                        field("expected_format_version",
                              [&]() { print_optional_string(check.expected_format_version); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void
    print_source_chain_checks(const std::vector<durable_store_import::SourceChainCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("source_artifact_type",
                              [&]() { write_string(check.source_artifact_type); });
                        field("source_artifact_identity",
                              [&]() { write_string(check.source_artifact_identity); });
                        field("source_format_version",
                              [&]() { write_string(check.source_format_version); });
                        field("expected_source_format_version",
                              [&]() { write_string(check.expected_source_format_version); });
                        field("is_compatible", [&]() { write_bool(check.is_compatible); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void
    print_reference_checks(const std::vector<durable_store_import::ReferenceVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("reference_type", [&]() { write_string(check.reference_type); });
                        field("reference_identity",
                              [&]() { write_string(check.reference_identity); });
                        field("referenced_format_version",
                              [&]() { write_string(check.referenced_format_version); });
                        field("expected_format_version",
                              [&]() { write_string(check.expected_format_version); });
                        field("is_compatible", [&]() { write_bool(check.is_compatible); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }
};

} // namespace

void print_durable_store_import_provider_schema_compatibility_report_json(
    const durable_store_import::ProviderSchemaCompatibilityReport &report, std::ostream &out) {
    SchemaCompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_schema_compatibility_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_selection_plan {
namespace {

class SelectionPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SelectionPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSelectionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("selection_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_selection_plan_identity);
            });
            field("selected_provider_id", [&]() { write_string(plan.selected_provider_id); });
            field("fallback_provider_id", [&]() { write_string(plan.fallback_provider_id); });
            field("selection_status", [&]() { print_status(plan.selection_status); });
            field("degradation_allowed", [&]() { write_bool(plan.degradation_allowed); });
            field("requires_operator_review", [&]() { write_bool(plan.requires_operator_review); });
            field("fallback_policy", [&]() { write_string(plan.fallback_policy); });
            field("capability_gap_summary", [&]() { write_string(plan.capability_gap_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSelectionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Selected,
                                    "selected");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::FallbackSelected,
                                    "fallback_selected");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_selection_plan_json(
    const durable_store_import::ProviderSelectionPlan &plan, std::ostream &out) {
    SelectionPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_selection_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_telemetry_summary {
namespace {

class TelemetrySummaryJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit TelemetrySummaryJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderTelemetrySummary &summary) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(summary.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(summary.workflow_canonical_name); });
            field("session_id", [&]() { write_string(summary.session_id); });
            field("run_id", [&]() { print_optional_string(summary.run_id); });
            field("input_fixture", [&]() { write_string(summary.input_fixture); });
            field("telemetry_summary_identity", [&]() {
                write_string(summary.durable_store_import_provider_telemetry_summary_identity);
            });
            field("outcome", [&]() { print_outcome(summary.outcome); });
            field("provider_write_attempted",
                  [&]() { write_bool(summary.provider_write_attempted); });
            field("provider_write_committed",
                  [&]() { write_bool(summary.provider_write_committed); });
            field("retry_recommended", [&]() { write_bool(summary.retry_recommended); });
            field("recovery_required", [&]() { write_bool(summary.recovery_required); });
            field("telemetry_summary", [&]() { write_string(summary.telemetry_summary); });
            field("secret_free", [&]() { write_bool(summary.secret_free); });
            field("raw_telemetry_persisted",
                  [&]() { write_bool(summary.raw_telemetry_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_outcome(durable_store_import::ProviderAuditOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                    "retry_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                    "recovery_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_telemetry_summary_json(
    const durable_store_import::ProviderTelemetrySummary &summary, std::ostream &out) {
    TelemetrySummaryJsonPrinter(out).print(summary);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_telemetry_summary
