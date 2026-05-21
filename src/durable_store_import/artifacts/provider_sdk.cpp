#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_interface {
namespace {

class ProviderSdkAdapterInterfaceJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterInterfaceJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterInterfacePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("source_sdk_adapter_request_status",
                  [&]() { print_adapter_status(plan.source_sdk_adapter_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(plan.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_interface_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_interface_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("interface_status", [&]() { print_interface_status(plan.interface_status); });
            field("provider_registry_identity",
                  [&]() { write_string(plan.provider_registry_identity); });
            field("adapter_descriptor_identity",
                  [&]() { write_string(plan.adapter_descriptor_identity); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("adapter_name", [&]() { write_string(plan.adapter_name); });
            field("adapter_version", [&]() { write_string(plan.adapter_version); });
            field("interface_abi_version", [&]() { write_string(plan.interface_abi_version); });
            field("capability_descriptor_identity",
                  [&]() { write_string(plan.capability_descriptor_identity); });
            field("capability_descriptor",
                  [&]() { print_capability_descriptor(plan.capability_descriptor, 1); });
            field("error_taxonomy_version", [&]() { write_string(plan.error_taxonomy_version); });
            field("normalized_error_kind",
                  [&]() { print_normalized_error_kind(plan.normalized_error_kind); });
            field("returns_placeholder_result",
                  [&]() { write_bool(plan.returns_placeholder_result); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("credential_reference",
                  [&]() { print_optional_string(plan.credential_reference); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(plan.sdk_response_payload); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_adapter_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            PlanProviderSdkAdapterInterface:
            write_string("plan_provider_sdk_adapter_interface");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopUnsupportedCapability:
            write_string("noop_unsupported_capability");
            return;
        }
    }

    void
    print_support_status(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported,
                "supported");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported,
                "unsupported"));
    }

    void
    print_normalized_error_kind(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability,
                "unsupported_capability");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterNormalizedErrorKind::
                                        SdkAdapterRequestNotReady,
                                    "sdk_adapter_request_not_ready"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterInterfaceFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::
            SdkAdapterRequestNotReady:
            write_string("sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
            write_string("unsupported_capability");
            return;
        }
    }

    void print_capability_descriptor(
        const durable_store_import::ProviderSdkAdapterCapabilityDescriptor &descriptor,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_key", [&]() { write_string(descriptor.capability_key); });
            field("support_status", [&]() { print_support_status(descriptor.support_status); });
            field("input_contract_format_version",
                  [&]() { write_string(descriptor.input_contract_format_version); });
            field("output_contract_format_version",
                  [&]() { write_string(descriptor.output_contract_format_version); });
            field("supports_placeholder_response",
                  [&]() { write_bool(descriptor.supports_placeholder_response); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterInterfaceFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_json(
    const durable_store_import::ProviderSdkAdapterInterfacePlan &plan, std::ostream &out) {
    ProviderSdkAdapterInterfaceJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_interface

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_interface_review {
namespace {

void print_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status,
                  std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready, "ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
}

void print_operation(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind,
                     std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        PlanProviderSdkAdapterInterface:
        out << "plan_provider_sdk_adapter_interface";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        NoopSdkAdapterRequestNotReady:
        out << "noop_sdk_adapter_request_not_ready";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::NoopUnsupportedCapability:
        out << "noop_unsupported_capability";
        return;
    }
}

void print_support(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status,
                   std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported,
            "supported");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported,
            "unsupported"));
}

void print_error(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind,
                 std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None, "none");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability,
            "unsupported_capability");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::SdkAdapterRequestNotReady,
            "sdk_adapter_request_not_ready"));
}

void print_next_action(durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind kind,
                       std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ReadyForMockAdapterImplementation:
        out << "ready_for_mock_adapter_implementation";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        WaitForSdkAdapterRequest:
        out << "wait_for_sdk_adapter_request";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

void print_failure(const durable_store_import::ProviderSdkAdapterInterfaceReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }

    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady:
        out << "sdk_adapter_request_not_ready ";
        break;
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
        out << "unsupported_capability ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_review(
    const durable_store_import::ProviderSdkAdapterInterfaceReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "adapter_request " << review.durable_store_import_provider_sdk_adapter_request_identity
        << '\n';
    out << "interface_artifact "
        << review.durable_store_import_provider_sdk_adapter_interface_identity << '\n';
    out << "interface_status ";
    print_status(review.interface_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "registry " << review.provider_registry_identity << '\n';
    out << "adapter_descriptor " << review.adapter_descriptor_identity << '\n';
    out << "capability_descriptor " << review.capability_descriptor_identity << '\n';
    out << "capability_support ";
    print_support(review.capability_support_status, out);
    out << '\n';
    out << "error_taxonomy " << review.error_taxonomy_version << '\n';
    out << "normalized_error ";
    print_error(review.normalized_error_kind, out);
    out << '\n';
    out << "returns_placeholder_result " << (review.returns_placeholder_result ? "true" : "false")
        << '\n';
    out << "side_effects sdk_payload="
        << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " secret=" << (review.reads_secret_material ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false") << '\n';
    out << "interface_boundary " << review.interface_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_interface_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string status_name(durable_store_import::ProviderSdkAdapterStatus status) {
    switch (status) {
    case durable_store_import::ProviderSdkAdapterStatus::Ready:
        return "ready";
    case durable_store_import::ProviderSdkAdapterStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderSdkAdapterOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
        return "plan_provider_sdk_adapter_request";
    case durable_store_import::ProviderSdkAdapterOperationKind::
        PlanProviderSdkAdapterResponsePlaceholder:
        return "plan_provider_sdk_adapter_response_placeholder";
    case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
        return "noop_local_host_execution_not_ready";
    case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
        return "noop_sdk_adapter_request_not_ready";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderSdkAdapterReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::
        ReadyForProviderSdkAdapterInterface:
        return "ready_for_provider_sdk_adapter_interface";
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::
        WaitForLocalHostExecutionReceipt:
        return "wait_for_local_host_execution_receipt";
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderSdkAdapterFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady:
        return "local_host_execution_not_ready";
    case durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady:
        return "sdk_adapter_request_not_ready";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderSdkAdapterFailureAttribution> &failure) {
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

void print_durable_store_import_provider_sdk_adapter_readiness(
    const durable_store_import::ProviderSdkAdapterReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(
        out,
        0,
        "source_provider_sdk_adapter_response_placeholder_format " +
            review
                .source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version);
    line(out,
         0,
         "source_provider_sdk_adapter_request_plan_format " +
             review.source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_adapter_request_identity " +
             review.durable_store_import_provider_sdk_adapter_request_identity);
    line(out,
         0,
         "durable_store_import_provider_sdk_adapter_response_placeholder_identity " +
             review.durable_store_import_provider_sdk_adapter_response_placeholder_identity);
    line(out, 0, "response_status " + status_name(review.response_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_sdk_adapter_response_placeholder_identity " +
             (review.provider_sdk_adapter_response_placeholder_identity.has_value()
                  ? *review.provider_sdk_adapter_response_placeholder_identity
                  : std::string("none")));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "reads_secret_material " + std::string(review.reads_secret_material ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "sdk_adapter_boundary " + review.sdk_adapter_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_request {
namespace {

class ProviderSdkAdapterRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterRequestPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_local_host_execution_status",
                  [&]() { print_local_host_status(plan.source_local_host_execution_status); });
            field("source_provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(plan.source_provider_local_host_execution_receipt_identity);
            });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("request_status", [&]() { print_status(plan.request_status); });
            field("provider_sdk_adapter_request_identity",
                  [&]() { print_optional_string(plan.provider_sdk_adapter_request_identity); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(plan.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(plan.object_path); });
            field("database_table", [&]() { print_optional_string(plan.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(plan.sdk_response_payload); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_local_host_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady,
                "simulated_ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
            write_string("plan_provider_sdk_adapter_request");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::
            PlanProviderSdkAdapterResponsePlaceholder:
            write_string("plan_provider_sdk_adapter_response_placeholder");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
            write_string("noop_local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady,
                "local_host_execution_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady,
                "sdk_adapter_request_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_request_json(
    const durable_store_import::ProviderSdkAdapterRequestPlan &plan, std::ostream &out) {
    ProviderSdkAdapterRequestJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_response_placeholder {
namespace {

class ProviderSdkAdapterResponsePlaceholderJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterResponsePlaceholderJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    placeholder
                        .source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        placeholder
                            .source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_request_status",
                  [&]() { print_status(placeholder.source_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(placeholder.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_response_placeholder_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_sdk_adapter_response_placeholder_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(placeholder.operation_kind); });
            field("response_status", [&]() { print_status(placeholder.response_status); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(
                    placeholder.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(placeholder.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(placeholder.invokes_provider_sdk); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(placeholder.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
            write_string("plan_provider_sdk_adapter_request");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::
            PlanProviderSdkAdapterResponsePlaceholder:
            write_string("plan_provider_sdk_adapter_response_placeholder");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
            write_string("noop_local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady,
                "local_host_execution_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady,
                "sdk_adapter_request_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_response_placeholder_json(
    const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder,
    std::ostream &out) {
    ProviderSdkAdapterResponsePlaceholderJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_response_placeholder

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_envelope {
namespace {

class DurableStoreImportProviderSdkEnvelopeJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderSdkEnvelopeJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkRequestEnvelopePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_runtime_preflight_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_preflight_plan_format_version);
            });
            field("source_durable_store_import_provider_runtime_profile_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_profile_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_runtime_preflight_identity", [&]() {
                write_string(plan.durable_store_import_provider_runtime_preflight_identity);
            });
            field("source_sdk_invocation_envelope_identity",
                  [&]() { print_optional_string(plan.source_sdk_invocation_envelope_identity); });
            field("source_preflight_status",
                  [&]() { print_preflight_status(plan.source_preflight_status); });
            field("envelope_policy", [&]() { print_policy(plan.envelope_policy, 1); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("envelope_status", [&]() { print_envelope_status(plan.envelope_status); });
            field("provider_sdk_request_envelope_identity",
                  [&]() { print_optional_string(plan.provider_sdk_request_envelope_identity); });
            field("host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.host_handoff_descriptor_identity); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("starts_host_process", [&]() { write_bool(plan.starts_host_process); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_preflight_status(durable_store_import::ProviderRuntimePreflightStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Blocked,
                                    "blocked"));
    }

    void print_capability(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeCapabilityKind::
                                        PlanSecretFreeRequestEnvelope,
                                    "plan_secret_free_request_envelope");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor,
                "plan_host_handoff_descriptor"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
            write_string("plan_provider_sdk_request_envelope");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
            write_string("noop_runtime_preflight_not_ready");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
            write_string("noop_envelope_policy_mismatch");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::
            NoopUnsupportedEnvelopeCapability:
            write_string("noop_unsupported_envelope_capability");
            return;
        }
    }

    void print_envelope_status(durable_store_import::ProviderSdkEnvelopeStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady,
                "runtime_preflight_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch,
                "envelope_policy_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability,
                "unsupported_envelope_capability"));
    }

    void print_policy(const durable_store_import::ProviderSdkEnvelopePolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("sdk_envelope_policy_identity",
                  [&]() { write_string(policy.sdk_envelope_policy_identity); });
            field("runtime_profile_ref", [&]() { write_string(policy.runtime_profile_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("secret_free_request_schema_ref",
                  [&]() { write_string(policy.secret_free_request_schema_ref); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("credential_free", [&]() { write_bool(policy.credential_free); });
            field("supports_secret_free_request_envelope_planning",
                  [&]() { write_bool(policy.supports_secret_free_request_envelope_planning); });
            field("supports_host_handoff_descriptor_planning",
                  [&]() { write_bool(policy.supports_host_handoff_descriptor_planning); });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkEnvelopeFailureAttribution &failure,
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

void print_durable_store_import_provider_sdk_envelope_json(
    const durable_store_import::ProviderSdkRequestEnvelopePlan &plan, std::ostream &out) {
    DurableStoreImportProviderSdkEnvelopeJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_envelope

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_handoff_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
envelope_status_name(durable_store_import::ProviderSdkEnvelopeStatus status) {
    switch (status) {
    case durable_store_import::ProviderSdkEnvelopeStatus::Ready:
        return "ready";
    case durable_store_import::ProviderSdkEnvelopeStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
        return "plan_provider_sdk_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
        return "noop_runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
        return "noop_envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopUnsupportedEnvelopeCapability:
        return "noop_unsupported_envelope_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderSdkHandoffReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::
        ReadyForHostExecutionPrototype:
        return "ready_for_host_execution_prototype";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::FixEnvelopePolicy:
        return "fix_envelope_policy";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::WaitForEnvelopeCapability:
        return "wait_for_envelope_capability";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope:
        return "plan_secret_free_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor:
        return "plan_host_handoff_descriptor";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady:
        return "runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch:
        return "envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability:
        return "unsupported_envelope_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderSdkEnvelopeFailureAttribution> &failure) {
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

void print_durable_store_import_provider_sdk_handoff_readiness(
    const durable_store_import::ProviderSdkHandoffReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out,
         0,
         "source_provider_runtime_preflight_plan_format " +
             review.source_durable_store_import_provider_runtime_preflight_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_runtime_preflight_identity " +
             review.durable_store_import_provider_runtime_preflight_identity);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out, 0, "envelope_status " + envelope_status_name(review.envelope_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_sdk_request_envelope_identity " +
             (review.provider_sdk_request_envelope_identity.has_value()
                  ? *review.provider_sdk_request_envelope_identity
                  : std::string("none")));
    line(out,
         0,
         "host_handoff_descriptor_identity " + (review.host_handoff_descriptor_identity.has_value()
                                                    ? *review.host_handoff_descriptor_identity
                                                    : std::string("none")));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "sdk_boundary " + review.sdk_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_handoff_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_contract {
namespace {

class MockContractJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit MockContractJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkMockAdapterContract &contract) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(contract.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(contract.workflow_canonical_name); });
            field("session_id", [&]() { write_string(contract.session_id); });
            field("run_id", [&]() { print_optional_string(contract.run_id); });
            field("input_fixture", [&]() { write_string(contract.input_fixture); });
            field("sdk_payload_plan_identity", [&]() {
                write_string(contract.durable_store_import_provider_sdk_payload_plan_identity);
            });
            field("mock_adapter_contract_identity", [&]() {
                write_string(
                    contract.durable_store_import_provider_sdk_mock_adapter_contract_identity);
            });
            field("operation_kind", [&]() { print_operation(contract.operation_kind); });
            field("contract_status", [&]() { print_status(contract.contract_status); });
            field("scenario_kind", [&]() { print_scenario(contract.scenario_kind); });
            field("provider_request_payload_schema_ref",
                  [&]() { write_string(contract.provider_request_payload_schema_ref); });
            field("payload_digest", [&]() { write_string(contract.payload_digest); });
            field("fake_provider_only", [&]() { write_bool(contract.fake_provider_only); });
            field("opens_network_connection",
                  [&]() { write_bool(contract.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(contract.reads_secret_material); });
            field("invokes_real_provider_sdk",
                  [&]() { write_bool(contract.invokes_real_provider_sdk); });
            field("failure_attribution", [&]() { print_failure(contract.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkMockAdapterStatus status) {
        write_string(status == durable_store_import::ProviderSdkMockAdapterStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkMockAdapterOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract,
                "plan_mock_adapter_contract");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::RunMockAdapter,
                "run_mock_adapter");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady,
                "noop_payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopContractNotReady,
                "noop_contract_not_ready"));
    }

    void print_scenario(durable_store_import::ProviderSdkMockAdapterScenarioKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkMockAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::PayloadNotReady,
                "payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ContractNotReady,
                "contract_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkMockAdapterFailureAttribution>
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

void print_durable_store_import_provider_sdk_mock_adapter_contract_json(
    const durable_store_import::ProviderSdkMockAdapterContract &contract, std::ostream &out) {
    MockContractJsonPrinter(out).print(contract);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_contract

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_execution {
namespace {

class MockExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit MockExecutionJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkMockAdapterExecutionResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("mock_adapter_contract_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_sdk_mock_adapter_contract_identity);
            });
            field("mock_adapter_result_identity", [&]() {
                write_string(result.durable_store_import_provider_sdk_mock_adapter_result_identity);
            });
            field("operation_kind", [&]() { print_operation(result.operation_kind); });
            field("result_status", [&]() { print_status(result.result_status); });
            field("scenario_kind", [&]() { print_scenario(result.scenario_kind); });
            field("normalized_result", [&]() { print_normalized(result.normalized_result); });
            field("provider_status_code", [&]() { out_ << result.provider_status_code; });
            field("normalized_message", [&]() { write_string(result.normalized_message); });
            field("opens_network_connection",
                  [&]() { write_bool(result.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(result.reads_secret_material); });
            field("invokes_real_provider_sdk",
                  [&]() { write_bool(result.invokes_real_provider_sdk); });
            field("failure_attribution", [&]() { print_failure(result.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkMockAdapterStatus status) {
        write_string(status == durable_store_import::ProviderSdkMockAdapterStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkMockAdapterOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract,
                "plan_mock_adapter_contract");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::RunMockAdapter,
                "run_mock_adapter");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady,
                "noop_payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopContractNotReady,
                "noop_contract_not_ready"));
    }

    void print_scenario(durable_store_import::ProviderSdkMockAdapterScenarioKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted,
                "accepted");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout,
                "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled,
                "throttled");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict,
                "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch,
                "schema_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked,
                "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkMockAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::PayloadNotReady,
                "payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ContractNotReady,
                "contract_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkMockAdapterFailureAttribution>
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

void print_durable_store_import_provider_sdk_mock_adapter_execution_json(
    const durable_store_import::ProviderSdkMockAdapterExecutionResult &result, std::ostream &out) {
    MockExecutionJsonPrinter(out).print(result);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_execution

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_readiness {
namespace {

void print_status(durable_store_import::ProviderSdkMockAdapterStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkMockAdapterStatus::Ready ? "ready"
                                                                                : "blocked");
}

void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind,
                      std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted, "accepted");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure,
            "provider_failure");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout, "timeout");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled,
            "throttled");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict, "conflict");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch,
            "schema_mismatch");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderSdkMockAdapterNextActionKind kind,
                std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity,
            "ready_for_real_adapter_parity");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::WaitForPayload,
            "wait_for_payload");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_sdk_mock_adapter_readiness(
    const durable_store_import::ProviderSdkMockAdapterReadiness &readiness, std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "mock_adapter_result "
        << readiness.durable_store_import_provider_sdk_mock_adapter_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "normalized_result ";
    print_normalized(readiness.normalized_result, out);
    out << '\n';
    out << "normalization " << readiness.normalization_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_payload_audit {
namespace {

void print_status(durable_store_import::ProviderSdkPayloadStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkPayloadStatus::Ready ? "ready" : "blocked");
}

void print_next(durable_store_import::ProviderSdkPayloadNextActionKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::ReadyForMockAdapter,
            "ready_for_mock_adapter");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::WaitForLocalHarness,
            "wait_for_local_harness");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_sdk_payload_audit(
    const durable_store_import::ProviderSdkPayloadAuditSummary &audit, std::ostream &out) {
    out << audit.format_version << '\n';
    out << "workflow " << audit.workflow_canonical_name << '\n';
    out << "session " << audit.session_id << '\n';
    out << "sdk_payload_plan " << audit.durable_store_import_provider_sdk_payload_plan_identity
        << '\n';
    out << "payload_status ";
    print_status(audit.payload_status, out);
    out << '\n';
    out << "schema " << audit.provider_request_payload_schema_ref << '\n';
    out << "payload_digest " << audit.payload_digest << '\n';
    out << "fake_provider_only " << (audit.fake_provider_only ? "true" : "false") << '\n';
    out << "raw_payload_persisted " << (audit.raw_payload_persisted ? "true" : "false") << '\n';
    out << "redaction secret_free=" << (audit.redaction_summary.secret_free ? "true" : "false")
        << " credential_free=" << (audit.redaction_summary.credential_free ? "true" : "false")
        << " token_free=" << (audit.redaction_summary.token_free ? "true" : "false")
        << " redacted_field_count=" << audit.redaction_summary.redacted_field_count << '\n';
    out << "audit " << audit.audit_summary << '\n';
    out << "next_action ";
    print_next(audit.next_action, out);
    out << '\n';
    out << "next_step " << audit.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_payload_audit

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_payload_plan {
namespace {

class PayloadPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit PayloadPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkPayloadMaterializationPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("harness_record_identity", [&]() {
                write_string(plan.durable_store_import_provider_local_host_harness_record_identity);
            });
            field("sdk_payload_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_payload_plan_identity);
            });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("payload_status", [&]() { print_status(plan.payload_status); });
            field("fake_provider_only", [&]() { write_bool(plan.fake_provider_only); });
            field("provider_request_payload_schema_ref",
                  [&]() { write_string(plan.provider_request_payload_schema_ref); });
            field("payload_digest", [&]() { write_string(plan.payload_digest); });
            field("redaction_summary", [&]() { print_redaction(plan.redaction_summary, 1); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("persists_materialized_payload",
                  [&]() { write_bool(plan.persists_materialized_payload); });
            field("raw_payload", [&]() { print_optional_string(plan.raw_payload); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkPayloadStatus status) {
        write_string(status == durable_store_import::ProviderSdkPayloadStatus::Ready ? "ready"
                                                                                     : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkPayloadOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkPayloadOperationKind::
            PlanFakeProviderPayloadMaterialization:
            write_string("plan_fake_provider_payload_materialization");
            return;
        case durable_store_import::ProviderSdkPayloadOperationKind::NoopHarnessNotReady:
            write_string("noop_harness_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkPayloadFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::HarnessNotReady,
                "harness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::ForbiddenPayloadMaterial,
                "forbidden_payload_material");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::UnsupportedProviderSchema,
                "unsupported_provider_schema"));
    }

    void print_redaction(const durable_store_import::ProviderSdkPayloadRedactionSummary &summary,
                         int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("secret_free", [&]() { write_bool(summary.secret_free); });
            field("credential_free", [&]() { write_bool(summary.credential_free); });
            field("token_free", [&]() { write_bool(summary.token_free); });
            field("redacted_field_count", [&]() { out_ << summary.redacted_field_count; });
            field("summary", [&]() { write_string(summary.summary); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkPayloadFailureAttribution> &failure,
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

void print_durable_store_import_provider_sdk_payload_plan_json(
    const durable_store_import::ProviderSdkPayloadMaterializationPlan &plan, std::ostream &out) {
    PayloadPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_payload_plan
