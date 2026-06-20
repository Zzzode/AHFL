#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_bundle_validation_report {
namespace {

void print_validation_status(durable_store_import::ConfigValidationStatus status,
                             std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(status,
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Valid, "valid");
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Invalid, "invalid");
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Missing, "missing"));
}

} // namespace

void print_durable_store_import_provider_config_bundle_validation_report(
    const durable_store_import::ProviderConfigBundleValidationReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "config_bundle " << report.config_bundle_identity << '\n';

    // Provider references
    for (const auto &ref : report.provider_references) {
        out << "provider_reference " << ref.provider_name << " ";
        print_validation_status(ref.status, out);
        if (ref.validation_error.has_value()) {
            out << " error=" << *ref.validation_error;
        }
        out << '\n';
    }

    // Secret handles (redacted output, does not expose the secret value)
    for (const auto &sh : report.secret_handles) {
        out << "secret_handle " << sh.secret_name << " scope=" << sh.secret_scope << " presence=";
        print_validation_status(sh.presence_status, out);
        out << " scope_status=";
        print_validation_status(sh.scope_status, out);
        out << " redaction=" << (sh.has_redaction_policy ? "yes" : "no") << '\n';
    }

    // Endpoint shapes
    for (const auto &ep : report.endpoint_shapes) {
        out << "endpoint_shape " << ep.endpoint_name << " expected=" << ep.expected_shape << " ";
        print_validation_status(ep.status, out);
        out << '\n';
    }

    // Environment bindings
    for (const auto &eb : report.environment_bindings) {
        out << "environment_binding " << eb.binding_name << " ";
        print_validation_status(eb.status, out);
        out << '\n';
    }

    // Summary
    out << "summary " << report.validation_summary << '\n';
    out << "blocking " << report.blocking_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_bundle_validation_report

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_load {
namespace {

class ProviderConfigLoadJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderConfigLoadJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderConfigLoadPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_sdk_adapter_interface_plan_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version);
                });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_interface_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_interface_identity);
            });
            field("source_adapter_interface_status",
                  [&]() { print_interface_status(plan.source_adapter_interface_status); });
            field("provider_registry_identity",
                  [&]() { write_string(plan.provider_registry_identity); });
            field("adapter_descriptor_identity",
                  [&]() { write_string(plan.adapter_descriptor_identity); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("durable_store_import_provider_config_load_identity",
                  [&]() { write_string(plan.durable_store_import_provider_config_load_identity); });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("config_load_status", [&]() { print_status(plan.config_load_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(plan.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity",
                  [&]() { write_string(plan.provider_config_snapshot_placeholder_identity); });
            field("profile_descriptor", [&]() { print_profile(plan.profile_descriptor, 1); });
            print_side_effect_fields(field,
                                     plan.reads_secret_material,
                                     plan.materializes_secret_value,
                                     plan.materializes_credential_material,
                                     plan.opens_network_connection,
                                     plan.reads_host_environment,
                                     plan.writes_host_filesystem,
                                     plan.materializes_sdk_request_payload,
                                     plan.invokes_provider_sdk);
            field("secret_value", [&]() { print_optional_string(plan.secret_value); });
            field("credential_material",
                  [&]() { print_optional_string(plan.credential_material); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("remote_config_uri", [&]() { print_optional_string(plan.remote_config_uri); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    template <typename Field>
    void print_side_effect_fields(const Field &field,
                                  bool reads_secret_material,
                                  bool materializes_secret_value,
                                  bool materializes_credential_material,
                                  bool opens_network_connection,
                                  bool reads_host_environment,
                                  bool writes_host_filesystem,
                                  bool materializes_sdk_request_payload,
                                  bool invokes_provider_sdk) {
        field("reads_secret_material", [&]() { write_bool(reads_secret_material); });
        field("materializes_secret_value", [&]() { write_bool(materializes_secret_value); });
        field("materializes_credential_material",
              [&]() { write_bool(materializes_credential_material); });
        field("opens_network_connection", [&]() { write_bool(opens_network_connection); });
        field("reads_host_environment", [&]() { write_bool(reads_host_environment); });
        field("writes_host_filesystem", [&]() { write_bool(writes_host_filesystem); });
        field("materializes_sdk_request_payload",
              [&]() { write_bool(materializes_sdk_request_payload); });
        field("invokes_provider_sdk", [&]() { write_bool(invokes_provider_sdk); });
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
    }

    void print_status(durable_store_import::ProviderConfigStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                    "blocked"));
    }

    void print_operation(durable_store_import::ProviderConfigOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad:
            write_string("plan_provider_config_load");
            return;
        case durable_store_import::ProviderConfigOperationKind::
            PlanProviderConfigSnapshotPlaceholder:
            write_string("plan_provider_config_snapshot_placeholder");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady:
            write_string("noop_adapter_interface_not_ready");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady:
            write_string("noop_config_load_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderConfigFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady,
                "adapter_interface_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady,
                "config_load_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::MissingConfigProfile,
                "missing_config_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema,
                "incompatible_config_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile,
                "unsupported_config_profile"));
    }

    void print_profile(const durable_store_import::ProviderConfigProfileDescriptor &profile,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("profile_key", [&]() { write_string(profile.profile_key); });
            field("provider_key", [&]() { write_string(profile.provider_key); });
            field("config_schema_version", [&]() { write_string(profile.config_schema_version); });
            field("requires_secret_handle", [&]() { write_bool(profile.requires_secret_handle); });
            field("materializes_secret_value",
                  [&]() { write_bool(profile.materializes_secret_value); });
            field("contains_endpoint_uri", [&]() { write_bool(profile.contains_endpoint_uri); });
            field("opens_network_connection",
                  [&]() { write_bool(profile.opens_network_connection); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderConfigFailureAttribution> &failure,
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

void print_durable_store_import_provider_config_load_json(
    const durable_store_import::ProviderConfigLoadPlan &plan, std::ostream &out) {
    ProviderConfigLoadJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_load

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_readiness {
namespace {

void print_status(durable_store_import::ProviderConfigStatus status, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                        "blocked"));
}

void print_operation(durable_store_import::ProviderConfigOperationKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad,
            "plan_provider_config_load");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigOperationKind::
                                            PlanProviderConfigSnapshotPlaceholder,
                                        "plan_provider_config_snapshot_placeholder");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady,
            "noop_adapter_interface_not_ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady,
            "noop_config_load_not_ready"));
}

void print_next_action(durable_store_import::ProviderConfigReadinessNextActionKind kind,
                       std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::
                ReadyForSecretHandleResolver,
            "ready_for_secret_handle_resolver");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::WaitForAdapterInterface,
            "wait_for_adapter_interface");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

void print_failure(const durable_store_import::ProviderConfigReadinessReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady:
        out << "adapter_interface_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady:
        out << "config_load_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::MissingConfigProfile:
        out << "missing_config_profile ";
        break;
    case durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema:
        out << "incompatible_config_schema ";
        break;
    case durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile:
        out << "unsupported_config_profile ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_config_readiness(
    const durable_store_import::ProviderConfigReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "config_load " << review.durable_store_import_provider_config_load_identity << '\n';
    out << "config_snapshot " << review.durable_store_import_provider_config_snapshot_identity
        << '\n';
    out << "snapshot_status ";
    print_status(review.snapshot_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "profile " << review.provider_config_profile_identity << '\n';
    out << "snapshot_placeholder " << review.provider_config_snapshot_placeholder_identity << '\n';
    out << "config_schema " << review.config_schema_version << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " sdk_payload=" << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false") << '\n';
    out << "config_boundary " << review.config_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_snapshot {
namespace {

class ProviderConfigSnapshotJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderConfigSnapshotJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderConfigSnapshotPlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field("source_durable_store_import_provider_config_load_plan_format_version", [&]() {
                write_string(
                    placeholder
                        .source_durable_store_import_provider_config_load_plan_format_version);
            });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_config_load_identity", [&]() {
                write_string(placeholder.durable_store_import_provider_config_load_identity);
            });
            field("source_config_load_status",
                  [&]() { print_status(placeholder.source_config_load_status); });
            field("durable_store_import_provider_config_snapshot_identity", [&]() {
                write_string(placeholder.durable_store_import_provider_config_snapshot_identity);
            });
            field("operation_kind", [&]() { print_operation(placeholder.operation_kind); });
            field("snapshot_status", [&]() { print_status(placeholder.snapshot_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(placeholder.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity", [&]() {
                write_string(placeholder.provider_config_snapshot_placeholder_identity);
            });
            field("config_schema_version",
                  [&]() { write_string(placeholder.config_schema_version); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(placeholder.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(placeholder.materializes_credential_material); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(placeholder.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(placeholder.invokes_provider_sdk); });
            field("failure_attribution",
                  [&]() { print_failure(placeholder.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderConfigStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                    "blocked"));
    }

    void print_operation(durable_store_import::ProviderConfigOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad:
            write_string("plan_provider_config_load");
            return;
        case durable_store_import::ProviderConfigOperationKind::
            PlanProviderConfigSnapshotPlaceholder:
            write_string("plan_provider_config_snapshot_placeholder");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady:
            write_string("noop_adapter_interface_not_ready");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady:
            write_string("noop_config_load_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderConfigFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady,
                "adapter_interface_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady,
                "config_load_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::MissingConfigProfile,
                "missing_config_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema,
                "incompatible_config_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile,
                "unsupported_config_profile"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderConfigFailureAttribution> &failure,
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

void print_durable_store_import_provider_config_snapshot_json(
    const durable_store_import::ProviderConfigSnapshotPlaceholder &placeholder, std::ostream &out) {
    ProviderConfigSnapshotJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_snapshot

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_opt_in_decision_report {

void print_durable_store_import_provider_opt_in_decision_report(
    const durable_store_import::ProviderOptInDecisionReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    for (const auto &gate : report.gate_checks) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        if (gate.denial_reason.has_value()) {
            out << " reason=" << durable_store_import::to_string_view(*gate.denial_reason);
        }
        out << '\n';
    }
    if (report.scoped_override.has_value()) {
        out << "override_scope " << report.scoped_override->override_scope << '\n';
        out << "override_approver " << report.scoped_override->override_approver << '\n';
        out << "override_valid " << (report.scoped_override->is_valid ? "true" : "false") << '\n';
    }
    out << "is_real_provider_traffic_allowed "
        << (report.is_real_provider_traffic_allowed ? "true" : "false") << '\n';
    out << "decision_summary " << report.decision_summary << '\n';
    out << "denial_reason_summary " << report.denial_reason_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_opt_in_decision_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_policy_report {

void print_durable_store_import_provider_runtime_policy_report(
    const durable_store_import::ProviderRuntimePolicyReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "production_readiness_evidence " << report.production_readiness_evidence_identity
        << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    out << "blocking_violation_count " << report.blocking_violation_count << '\n';
    out << "warning_violation_count " << report.warning_violation_count << '\n';
    for (const auto &gate : report.policy_gates) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        for (const auto &v : gate.violations) {
            out << " violation=" << durable_store_import::to_string_view(v);
        }
        out << '\n';
    }
    out << "is_execution_permitted " << (report.is_execution_permitted ? "true" : "false") << '\n';
    out << "policy_summary " << report.policy_summary << '\n';
    out << "violation_summary " << report.violation_summary << '\n';
    out << "next_operator_action " << report.next_operator_action << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_policy_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_policy_review {
namespace {

void print_status(durable_store_import::ProviderSecretStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSecretStatus::Ready ? "ready" : "blocked");
}

void print_operation(durable_store_import::ProviderSecretOperationKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest,
            "plan_secret_resolver_request");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSecretOperationKind::
                                            PlanSecretResolverResponsePlaceholder,
                                        "plan_secret_resolver_response_placeholder");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady,
            "noop_config_snapshot_not_ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady,
            "noop_secret_resolver_request_not_ready"));
}

void print_lifecycle(durable_store_import::ProviderCredentialLifecycleState state,
                     std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        state,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::HandlePlanned,
            "handle_planned");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::PlaceholderPendingResolution,
            "placeholder_pending_resolution");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderSecretPolicyNextActionKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness,
            "ready_for_local_host_harness");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::WaitForConfigSnapshot,
            "wait_for_config_snapshot");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

void print_failure(const durable_store_import::ProviderSecretPolicyReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady:
        out << "config_snapshot_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady:
        out << "secret_resolver_request_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation:
        out << "secret_policy_violation ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_secret_policy_review(
    const durable_store_import::ProviderSecretPolicyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "secret_request "
        << review.durable_store_import_provider_secret_resolver_request_identity << '\n';
    out << "secret_response "
        << review.durable_store_import_provider_secret_resolver_response_identity << '\n';
    out << "response_status ";
    print_status(review.response_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "secret_handle " << review.secret_handle.handle_identity << '\n';
    out << "credential_lifecycle " << review.credential_lifecycle_version << ' ';
    print_lifecycle(review.credential_lifecycle_state, out);
    out << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " token=" << (review.materializes_token_value ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " secret_manager=" << (review.invokes_secret_manager ? "true" : "false") << '\n';
    out << "secret_policy " << review.secret_policy_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_policy_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_resolver_request {
namespace {

class SecretRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SecretRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSecretResolverRequestPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_config_snapshot_placeholder_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_config_snapshot_placeholder_format_version);
                });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_config_snapshot_identity", [&]() {
                write_string(plan.durable_store_import_provider_config_snapshot_identity);
            });
            field("source_config_snapshot_status",
                  [&]() { print_config_status(plan.source_config_snapshot_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(plan.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity",
                  [&]() { write_string(plan.provider_config_snapshot_placeholder_identity); });
            field("durable_store_import_provider_secret_resolver_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_secret_resolver_request_identity);
            });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("request_status", [&]() { print_status(plan.request_status); });
            field("secret_handle", [&]() { print_handle(plan.secret_handle, 1); });
            field("provider_secret_resolver_response_placeholder_identity", [&]() {
                write_string(plan.provider_secret_resolver_response_placeholder_identity);
            });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(plan.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(plan.materializes_credential_material); });
            field("materializes_token_value", [&]() { write_bool(plan.materializes_token_value); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("invokes_secret_manager", [&]() { write_bool(plan.invokes_secret_manager); });
            field("secret_value", [&]() { print_optional_string(plan.secret_value); });
            field("credential_material",
                  [&]() { print_optional_string(plan.credential_material); });
            field("token_value", [&]() { print_optional_string(plan.token_value); });
            field("secret_manager_response",
                  [&]() { print_optional_string(plan.secret_manager_response); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_config_status(durable_store_import::ProviderConfigStatus status) {
        write_string(status == durable_store_import::ProviderConfigStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_status(durable_store_import::ProviderSecretStatus status) {
        write_string(status == durable_store_import::ProviderSecretStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_operation(durable_store_import::ProviderSecretOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest:
            write_string("plan_secret_resolver_request");
            return;
        case durable_store_import::ProviderSecretOperationKind::
            PlanSecretResolverResponsePlaceholder:
            write_string("plan_secret_resolver_response_placeholder");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady:
            write_string("noop_config_snapshot_not_ready");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady:
            write_string("noop_secret_resolver_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSecretFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady,
                "config_snapshot_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady,
                "secret_resolver_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation,
                "secret_policy_violation"));
    }

    void print_handle(const durable_store_import::ProviderSecretHandleReference &handle,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("handle_schema_version", [&]() { write_string(handle.handle_schema_version); });
            field("handle_identity", [&]() { write_string(handle.handle_identity); });
            field("provider_key", [&]() { write_string(handle.provider_key); });
            field("profile_key", [&]() { write_string(handle.profile_key); });
            field("purpose", [&]() { write_string(handle.purpose); });
            field("contains_secret_value", [&]() { write_bool(handle.contains_secret_value); });
            field("contains_credential_material",
                  [&]() { write_bool(handle.contains_credential_material); });
            field("contains_token_value", [&]() { write_bool(handle.contains_token_value); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSecretFailureAttribution> &failure,
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

void print_durable_store_import_provider_secret_resolver_request_json(
    const durable_store_import::ProviderSecretResolverRequestPlan &plan, std::ostream &out) {
    SecretRequestJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_resolver_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_resolver_response {
namespace {

class SecretResponseJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SecretResponseJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field(
                "source_durable_store_import_provider_secret_resolver_request_plan_format_version",
                [&]() {
                    write_string(
                        placeholder
                            .source_durable_store_import_provider_secret_resolver_request_plan_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_secret_resolver_request_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_secret_resolver_request_identity);
            });
            field("source_secret_resolver_request_status",
                  [&]() { print_status(placeholder.source_secret_resolver_request_status); });
            field("durable_store_import_provider_secret_resolver_response_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_secret_resolver_response_identity);
            });
            field("operation_kind", [&]() { print_operation(placeholder.operation_kind); });
            field("response_status", [&]() { print_status(placeholder.response_status); });
            field("secret_handle", [&]() { print_handle(placeholder.secret_handle, 1); });
            field("credential_lifecycle_version",
                  [&]() { write_string(placeholder.credential_lifecycle_version); });
            field("credential_lifecycle_state",
                  [&]() { print_lifecycle(placeholder.credential_lifecycle_state); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(placeholder.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(placeholder.materializes_credential_material); });
            field("materializes_token_value",
                  [&]() { write_bool(placeholder.materializes_token_value); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("invokes_secret_manager",
                  [&]() { write_bool(placeholder.invokes_secret_manager); });
            field("failure_attribution",
                  [&]() { print_failure(placeholder.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSecretStatus status) {
        write_string(status == durable_store_import::ProviderSecretStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_operation(durable_store_import::ProviderSecretOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest:
            write_string("plan_secret_resolver_request");
            return;
        case durable_store_import::ProviderSecretOperationKind::
            PlanSecretResolverResponsePlaceholder:
            write_string("plan_secret_resolver_response_placeholder");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady:
            write_string("noop_config_snapshot_not_ready");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady:
            write_string("noop_secret_resolver_request_not_ready");
            return;
        }
    }

    void print_lifecycle(durable_store_import::ProviderCredentialLifecycleState state) {
        AHFL_ARTIFACT_PRINT_ENUM(
            state,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderCredentialLifecycleState::HandlePlanned,
                "handle_planned");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCredentialLifecycleState::
                                        PlaceholderPendingResolution,
                                    "placeholder_pending_resolution");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCredentialLifecycleState::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSecretFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady,
                "config_snapshot_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady,
                "secret_resolver_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation,
                "secret_policy_violation"));
    }

    void print_handle(const durable_store_import::ProviderSecretHandleReference &handle,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("handle_schema_version", [&]() { write_string(handle.handle_schema_version); });
            field("handle_identity", [&]() { write_string(handle.handle_identity); });
            field("provider_key", [&]() { write_string(handle.provider_key); });
            field("profile_key", [&]() { write_string(handle.profile_key); });
            field("purpose", [&]() { write_string(handle.purpose); });
            field("contains_secret_value", [&]() { write_bool(handle.contains_secret_value); });
            field("contains_credential_material",
                  [&]() { write_bool(handle.contains_credential_material); });
            field("contains_token_value", [&]() { write_bool(handle.contains_token_value); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSecretFailureAttribution> &failure,
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

void print_durable_store_import_provider_secret_resolver_response_json(
    const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder,
    std::ostream &out) {
    SecretResponseJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_resolver_response
