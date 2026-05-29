#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_host_execution {
namespace {

class DurableStoreImportProviderHostExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderHostExecutionJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderHostExecutionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("source_durable_store_import_provider_sdk_envelope_policy_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_envelope_policy_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("source_host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.source_host_handoff_descriptor_identity); });
            field("source_envelope_status",
                  [&]() { print_envelope_status(plan.source_envelope_status); });
            field("host_execution_policy", [&]() { print_policy(plan.host_execution_policy, 1); });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("execution_status", [&]() { print_execution_status(plan.execution_status); });
            field("provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(plan.provider_host_execution_descriptor_identity);
            });
            field("provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(plan.provider_host_receipt_placeholder_identity);
            });
            field("starts_host_process", [&]() { write_bool(plan.starts_host_process); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_envelope_status(durable_store_import::ProviderSdkEnvelopeStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Blocked,
                                    "blocked"));
    }

    void print_capability(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanLocalHostExecutionDescriptor:
            write_string("plan_local_host_execution_descriptor");
            return;
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanDryRunHostReceiptPlaceholder:
            write_string("plan_dry_run_host_receipt_placeholder");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
            write_string("plan_provider_host_execution");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
            write_string("noop_sdk_handoff_not_ready");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
            write_string("noop_host_policy_mismatch");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::
            NoopUnsupportedHostCapability:
            write_string("noop_unsupported_host_capability");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderHostExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady,
                "sdk_handoff_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch,
                "host_policy_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability,
                "unsupported_host_capability"));
    }

    void print_policy(const durable_store_import::ProviderHostExecutionPolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("host_execution_policy_identity",
                  [&]() { write_string(policy.host_execution_policy_identity); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("execution_profile_ref", [&]() { write_string(policy.execution_profile_ref); });
            field("sandbox_policy_ref", [&]() { write_string(policy.sandbox_policy_ref); });
            field("timeout_policy_ref", [&]() { write_string(policy.timeout_policy_ref); });
            field("credential_free", [&]() { write_bool(policy.credential_free); });
            field("network_free", [&]() { write_bool(policy.network_free); });
            field("supports_local_host_execution_descriptor_planning", [&]() {
                out_ << (policy.supports_local_host_execution_descriptor_planning ? "true"
                                                                                  : "false");
            });
            field("supports_dry_run_host_receipt_placeholder_planning", [&]() {
                out_ << (policy.supports_dry_run_host_receipt_placeholder_planning ? "true"
                                                                                   : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("host_environment_secret",
                  [&]() { print_optional_string(policy.host_environment_secret); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderHostExecutionFailureAttribution &failure,
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

void print_durable_store_import_provider_host_execution_json(
    const durable_store_import::ProviderHostExecutionPlan &plan, std::ostream &out) {
    DurableStoreImportProviderHostExecutionJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_host_execution

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_host_execution_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
execution_status_name(durable_store_import::ProviderHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderHostExecutionStatus::Ready:
        return "ready";
    case durable_store_import::ProviderHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
        return "plan_provider_host_execution";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
        return "noop_sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
        return "noop_host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopUnsupportedHostCapability:
        return "noop_unsupported_host_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderHostExecutionReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::
        ReadyForLocalHostExecutionHarness:
        return "ready_for_local_host_execution_harness";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::FixHostPolicy:
        return "fix_host_policy";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::WaitForHostCapability:
        return "wait_for_host_capability";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanLocalHostExecutionDescriptor:
        return "plan_local_host_execution_descriptor";
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanDryRunHostReceiptPlaceholder:
        return "plan_dry_run_host_receipt_placeholder";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady:
        return "sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch:
        return "host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability:
        return "unsupported_host_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderHostExecutionFailureAttribution> &failure) {
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

void print_durable_store_import_provider_host_execution_readiness(
    const durable_store_import::ProviderHostExecutionReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out,
         0,
         "durable_store_import_provider_host_execution_identity " +
             review.durable_store_import_provider_host_execution_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_host_execution_descriptor_identity " +
             (review.provider_host_execution_descriptor_identity.has_value()
                  ? *review.provider_host_execution_descriptor_identity
                  : std::string("none")));
    line(out,
         0,
         "provider_host_receipt_placeholder_identity " +
             (review.provider_host_receipt_placeholder_identity.has_value()
                  ? *review.provider_host_receipt_placeholder_identity
                  : std::string("none")));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "host_execution_boundary " + review.host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_host_execution_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_plan {
namespace {

class AlphaPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AlphaPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("local_filesystem_alpha_plan_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_filesystem_alpha_plan_identity);
            });
            field("plan_status", [&]() { print_status(plan.plan_status); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("real_provider_alpha", [&]() { write_bool(plan.real_provider_alpha); });
            field("fake_adapter_default_path_preserved",
                  [&]() { write_bool(plan.fake_adapter_default_path_preserved); });
            field("opt_in_required", [&]() { write_bool(plan.opt_in_required); });
            field("opt_in_enabled", [&]() { write_bool(plan.opt_in_enabled); });
            field("target_directory", [&]() { print_optional_string(plan.target_directory); });
            field("target_object_name", [&]() { write_string(plan.target_object_name); });
            field("planned_payload_digest", [&]() { write_string(plan.planned_payload_digest); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_failure_kind(durable_store_import::ProviderLocalFilesystemAlphaFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        MockReadinessNotReady,
                                    "mock_readiness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
                "alpha_plan_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
                "opt_in_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        FilesystemWriteFailed,
                                    "filesystem_write_failed"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalFilesystemAlphaFailureAttribution>
            &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_filesystem_alpha_plan_json(
    const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan, std::ostream &out) {
    AlphaPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_readiness {
namespace {

void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status,
                  std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready ? "ready"
                                                                                      : "blocked");
}

void print_next(durable_store_import::ProviderLocalFilesystemAlphaNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::
        ReadyForIdempotencyContract:
        out << "ready_for_idempotency_contract";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::WaitForMockAdapter:
        out << "wait_for_mock_adapter";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_filesystem_alpha_readiness(
    const durable_store_import::ProviderLocalFilesystemAlphaReadiness &readiness,
    std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "alpha_result "
        << readiness.durable_store_import_provider_local_filesystem_alpha_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "summary " << readiness.readiness_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_result {
namespace {

class AlphaResultJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AlphaResultJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("local_filesystem_alpha_result_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_local_filesystem_alpha_result_identity);
            });
            field("result_status", [&]() { print_status(result.result_status); });
            field("normalized_result", [&]() { print_result(result.normalized_result); });
            field("provider_commit_reference",
                  [&]() { write_string(result.provider_commit_reference); });
            field("provider_result_digest", [&]() { write_string(result.provider_result_digest); });
            field("wrote_local_file", [&]() { write_bool(result.wrote_local_file); });
            field("opt_in_used", [&]() { write_bool(result.opt_in_used); });
            field("failure_attribution", [&]() { print_failure(result.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_result(durable_store_import::ProviderLocalFilesystemAlphaResultKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::DryRunOnly,
                "dry_run_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::WriteFailed,
                "write_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::Blocked, "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalFilesystemAlphaFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        MockReadinessNotReady,
                                    "mock_readiness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
                "alpha_plan_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
                "opt_in_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        FilesystemWriteFailed,
                                    "filesystem_write_failed"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalFilesystemAlphaFailureAttribution>
            &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_filesystem_alpha_result_json(
    const durable_store_import::ProviderLocalFilesystemAlphaResult &result, std::ostream &out) {
    AlphaResultJsonPrinter(out).print(result);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_result

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_execution_receipt {
namespace {

class DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter final
    : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostExecutionReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    receipt
                        .source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(receipt.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(receipt.durable_store_import_provider_host_execution_identity);
            });
            field("source_host_execution_status",
                  [&]() { print_host_execution_status(receipt.source_host_execution_status); });
            field("source_provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(receipt.source_provider_host_execution_descriptor_identity);
            });
            field("source_provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(receipt.source_provider_host_receipt_placeholder_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    receipt.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(receipt.operation_kind); });
            field("execution_status", [&]() { print_execution_status(receipt.execution_status); });
            field("provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(receipt.provider_local_host_execution_receipt_identity);
            });
            field("starts_host_process", [&]() { write_bool(receipt.starts_host_process); });
            field("reads_host_environment", [&]() { write_bool(receipt.reads_host_environment); });
            field("opens_network_connection",
                  [&]() { write_bool(receipt.opens_network_connection); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(receipt.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(receipt.invokes_provider_sdk); });
            field("writes_host_filesystem", [&]() { write_bool(receipt.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(receipt.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_host_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            SimulateProviderLocalHostExecutionReceipt:
            write_string("simulate_provider_local_host_execution_receipt");
            return;
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            NoopHostExecutionNotReady:
            write_string("noop_host_execution_not_ready");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady,
                "simulated_ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady,
                "host_execution_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderLocalHostExecutionFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_json(
    const durable_store_import::ProviderLocalHostExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_execution_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_execution_receipt_review {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
execution_status_name(durable_store_import::ProviderLocalHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady:
        return "simulated_ready";
    case durable_store_import::ProviderLocalHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionOperationKind::
        SimulateProviderLocalHostExecutionReceipt:
        return "simulate_provider_local_host_execution_receipt";
    case durable_store_import::ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady:
        return "noop_host_execution_not_ready";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(
    durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ReadyForProviderSdkAdapterPrototype:
        return "ready_for_provider_sdk_adapter_prototype";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        WaitForHostExecutionPlan:
        return "wait_for_host_execution_plan";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady:
        return "host_execution_not_ready";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderLocalHostExecutionFailureAttribution>
        &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_review(
    const durable_store_import::ProviderLocalHostExecutionReceiptReview &review,
    std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_local_host_execution_receipt_format " +
             review
                 .source_durable_store_import_provider_local_host_execution_receipt_format_version);
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out,
         0,
         "durable_store_import_provider_host_execution_identity " +
             review.durable_store_import_provider_host_execution_identity);
    line(out,
         0,
         "durable_store_import_provider_local_host_execution_receipt_identity " +
             review.durable_store_import_provider_local_host_execution_receipt_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_local_host_execution_receipt_identity " +
             (review.provider_local_host_execution_receipt_identity.has_value()
                  ? *review.provider_local_host_execution_receipt_identity
                  : std::string("none")));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "local_host_execution_boundary " + review.local_host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_execution_receipt_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_record {
namespace {

class HarnessRecordJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit HarnessRecordJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(record.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(record.workflow_canonical_name); });
            field("session_id", [&]() { write_string(record.session_id); });
            field("run_id", [&]() { print_optional_string(record.run_id); });
            field("input_fixture", [&]() { write_string(record.input_fixture); });
            field("harness_request_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("harness_record_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_record_identity);
            });
            field("operation_kind", [&]() { print_operation(record.operation_kind); });
            field("record_status", [&]() { print_status(record.record_status); });
            field("outcome_kind", [&]() { print_outcome(record.outcome_kind); });
            field("exit_code", [&]() { out_ << record.exit_code; });
            field("timed_out", [&]() { write_bool(record.timed_out); });
            field("sandbox_denied", [&]() { write_bool(record.sandbox_denied); });
            field("captured_diagnostic_summary",
                  [&]() { write_string(record.captured_diagnostic_summary); });
            field("captured_stdout_excerpt",
                  [&]() { print_optional_string(record.captured_stdout_excerpt); });
            field("captured_stderr_excerpt",
                  [&]() { print_optional_string(record.captured_stderr_excerpt); });
            field("opens_network_connection",
                  [&]() { write_bool(record.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(record.reads_secret_material); });
            field("writes_host_filesystem", [&]() { write_bool(record.writes_host_filesystem); });
            field("failure_attribution", [&]() { print_failure(record.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded, "succeeded");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut, "timed_out");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied,
                "sandbox_denied");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked, "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
                "secret_policy_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
                "harness_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
            &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_harness_record_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record,
    std::ostream &out) {
    HarnessRecordJsonPrinter(out).print(record);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_record

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_request {
namespace {

class HarnessRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit HarnessRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("secret_response_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_secret_resolver_response_identity);
            });
            field("harness_request_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("operation_kind", [&]() { print_operation(request.operation_kind); });
            field("request_status", [&]() { print_status(request.request_status); });
            field("sandbox_policy", [&]() { print_sandbox(request.sandbox_policy, 1); });
            field("timeout_milliseconds", [&]() { out_ << request.timeout_milliseconds; });
            field("fixture_kind", [&]() { print_fixture(request.fixture_kind); });
            field("opens_network_connection",
                  [&]() { write_bool(request.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(request.reads_secret_material); });
            field("writes_host_filesystem", [&]() { write_bool(request.writes_host_filesystem); });
            field("failure_attribution", [&]() { print_failure(request.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_fixture(durable_store_import::ProviderLocalHostHarnessFixtureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
                "secret_policy_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
                "harness_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_sandbox(const durable_store_import::ProviderLocalHostHarnessSandboxPolicy &policy,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("test_only", [&]() { write_bool(policy.test_only); });
            field("allow_network", [&]() { write_bool(policy.allow_network); });
            field("allow_secret", [&]() { write_bool(policy.allow_secret); });
            field("allow_filesystem_write", [&]() { write_bool(policy.allow_filesystem_write); });
            field("allow_host_environment", [&]() { write_bool(policy.allow_host_environment); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
            &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_harness_request_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request,
    std::ostream &out) {
    HarnessRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_review {
namespace {

void print_status(durable_store_import::ProviderLocalHostHarnessStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalHostHarnessStatus::Ready ? "ready"
                                                                                  : "blocked");
}

void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind,
                   std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded, "succeeded");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed, "failed");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut, "timed_out");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied,
            "sandbox_denied");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderLocalHostHarnessNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::
        ReadyForSdkPayloadMaterialization:
        out << "ready_for_sdk_payload_materialization";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::WaitForSecretPolicy:
        out << "wait_for_secret_policy";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_host_harness_review(
    const durable_store_import::ProviderLocalHostHarnessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "harness_record "
        << review.durable_store_import_provider_local_host_harness_record_identity << '\n';
    out << "record_status ";
    print_status(review.record_status, out);
    out << '\n';
    out << "outcome ";
    print_outcome(review.outcome_kind, out);
    out << '\n';
    out << "sandbox test_only=" << (review.sandbox_policy.test_only ? "true" : "false")
        << " network=" << (review.sandbox_policy.allow_network ? "true" : "false")
        << " secret=" << (review.sandbox_policy.allow_secret ? "true" : "false")
        << " filesystem_write=" << (review.sandbox_policy.allow_filesystem_write ? "true" : "false")
        << " host_env=" << (review.sandbox_policy.allow_host_environment ? "true" : "false")
        << '\n';
    out << "summary " << review.harness_boundary_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_review
