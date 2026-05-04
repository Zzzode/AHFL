#include "ahfl/durable_store_import/provider_sdk_payload.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_SDK_PAYLOAD";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderSdkPayloadStatus status) {
    return status == ProviderSdkPayloadStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string plan_identity(const ProviderLocalHostHarnessReview &review,
                                        ProviderSdkPayloadStatus status) {
    return "durable-store-import-provider-sdk-payload-plan::" + review.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string stable_fake_digest(const ProviderLocalHostHarnessReview &review) {
    return "sha256:fake-provider-payload:" + review.session_id + ":" + review.input_fixture;
}

[[nodiscard]] ProviderSdkPayloadFailureAttribution
harness_not_ready_failure(const ProviderLocalHostHarnessReview &review) {
    std::string message =
        "SDK payload materialization cannot proceed because local host harness is not ready";
    if (review.failure_attribution.has_value()) {
        message = review.failure_attribution->message;
    }
    return ProviderSdkPayloadFailureAttribution{
        .kind = ProviderSdkPayloadFailureKind::HarnessNotReady,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderSdkPayloadFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_redaction(const ProviderSdkPayloadRedactionSummary &summary,
                        DiagnosticBag &diagnostics,
                        std::string_view owner) {
    if (!summary.secret_free || !summary.credential_free || !summary.token_free ||
        summary.summary.empty() || summary.redacted_field_count < 0) {
        emit_validation_error(
            diagnostics,
            std::string(owner) +
                " redaction summary must be secret-free, credential-free, and token-free");
    }
}

void validate_no_forbidden_material(const ProviderSdkPayloadMaterializationPlan &plan,
                                    DiagnosticBag &diagnostics) {
    if (plan.raw_payload.has_value()) {
        emit_validation_error(diagnostics, "provider SDK payload plan cannot persist raw_payload");
    }
    if (plan.secret_value.has_value()) {
        emit_validation_error(diagnostics, "provider SDK payload plan cannot contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan cannot contain credential_material");
    }
    if (plan.token_value.has_value()) {
        emit_validation_error(diagnostics, "provider SDK payload plan cannot contain token_value");
    }
}

void validate_materialization_boundary(const ProviderSdkPayloadMaterializationPlan &plan,
                                       DiagnosticBag &diagnostics) {
    if (!plan.fake_provider_only) {
        emit_validation_error(diagnostics, "provider SDK payload plan must be fake-provider-only");
    }
    if (plan.provider_request_payload_schema_ref != kProviderFakeSdkPayloadSchemaVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan schema ref must be '" +
                                  std::string(kProviderFakeSdkPayloadSchemaVersion) + "'");
    }
    if (plan.payload_digest.empty()) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan payload_digest must not be empty");
    }
    if (plan.persists_materialized_payload) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan cannot persist materialized payload");
    }
    if (plan.opens_network_connection || plan.reads_secret_material ||
        plan.materializes_secret_value || plan.materializes_credential_material ||
        plan.materializes_token_value || plan.invokes_provider_sdk) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan cannot open network, read secret, "
                              "materialize secret material, or invoke provider SDK");
    }
}

[[nodiscard]] ProviderSdkPayloadNextActionKind
next_action_for_plan(const ProviderSdkPayloadMaterializationPlan &plan) {
    if (plan.payload_status == ProviderSdkPayloadStatus::Ready) {
        return ProviderSdkPayloadNextActionKind::ReadyForMockAdapter;
    }
    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderSdkPayloadFailureKind::HarnessNotReady) {
        return ProviderSdkPayloadNextActionKind::WaitForLocalHarness;
    }
    return ProviderSdkPayloadNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderSdkPayloadPlanValidationResult validate_provider_sdk_payload_materialization_plan(
    const ProviderSdkPayloadMaterializationPlan &plan) {
    ProviderSdkPayloadPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderSdkPayloadMaterializationPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan format_version must be '" +
                                  std::string(kProviderSdkPayloadMaterializationPlanFormatVersion) +
                                  "'");
    }
    if (plan.source_durable_store_import_provider_local_host_harness_review_format_version !=
        kProviderLocalHostHarnessReviewFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan source format_version must be '" +
                                  std::string(kProviderLocalHostHarnessReviewFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        plan.durable_store_import_provider_sdk_payload_plan_identity.empty()) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan identity fields must not be empty");
    }
    validate_failure(plan.failure_attribution, diagnostics, "provider SDK payload plan");
    validate_redaction(plan.redaction_summary, diagnostics, "provider SDK payload plan");
    validate_materialization_boundary(plan, diagnostics);
    validate_no_forbidden_material(plan, diagnostics);
    if (plan.payload_status == ProviderSdkPayloadStatus::Ready) {
        if (plan.source_harness_next_action !=
                ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization ||
            plan.operation_kind !=
                ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization ||
            plan.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "provider SDK payload plan ready status requires ready harness and no failure");
        }
    } else if (plan.operation_kind ==
                   ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization ||
               !plan.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider SDK payload plan blocked status requires noop operation "
                              "and failure_attribution");
    }
    return result;
}

ProviderSdkPayloadAuditValidationResult
validate_provider_sdk_payload_audit_summary(const ProviderSdkPayloadAuditSummary &audit) {
    ProviderSdkPayloadAuditValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (audit.format_version != kProviderSdkPayloadAuditSummaryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK payload audit summary format_version must be '" +
                                  std::string(kProviderSdkPayloadAuditSummaryFormatVersion) + "'");
    }
    if (audit
            .source_durable_store_import_provider_sdk_payload_materialization_plan_format_version !=
        kProviderSdkPayloadMaterializationPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK payload audit summary source format_version must be '" +
                                  std::string(kProviderSdkPayloadMaterializationPlanFormatVersion) +
                                  "'");
    }
    if (audit.workflow_canonical_name.empty() || audit.session_id.empty() ||
        audit.input_fixture.empty() ||
        audit.durable_store_import_provider_sdk_payload_plan_identity.empty() ||
        audit.provider_request_payload_schema_ref.empty() || audit.payload_digest.empty() ||
        audit.audit_summary.empty() || audit.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary identity and summary fields must not be empty");
    }
    if (!audit.fake_provider_only || audit.raw_payload_persisted) {
        emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary must be fake-only with no raw payload persistence");
    }
    validate_redaction(audit.redaction_summary, diagnostics, "provider SDK payload audit summary");
    validate_failure(audit.failure_attribution, diagnostics, "provider SDK payload audit summary");
    if (audit.next_action == ProviderSdkPayloadNextActionKind::ReadyForMockAdapter &&
        (audit.payload_status != ProviderSdkPayloadStatus::Ready ||
         audit.failure_attribution.has_value())) {
        emit_validation_error(diagnostics,
                              "provider SDK payload audit summary mock adapter next action "
                              "requires ready payload and no failure");
    }
    return result;
}

ProviderSdkPayloadPlanResult
build_provider_sdk_payload_materialization_plan(const ProviderLocalHostHarnessReview &review) {
    ProviderSdkPayloadPlanResult result;
    result.diagnostics.append(validate_provider_local_host_harness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = review.next_action ==
                       ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization;
    const auto status = ready ? ProviderSdkPayloadStatus::Ready : ProviderSdkPayloadStatus::Blocked;
    ProviderSdkPayloadMaterializationPlan plan;
    plan.workflow_canonical_name = review.workflow_canonical_name;
    plan.session_id = review.session_id;
    plan.run_id = review.run_id;
    plan.input_fixture = review.input_fixture;
    plan.durable_store_import_provider_local_host_harness_record_identity =
        review.durable_store_import_provider_local_host_harness_record_identity;
    plan.source_harness_next_action = review.next_action;
    plan.durable_store_import_provider_sdk_payload_plan_identity = plan_identity(review, status);
    plan.payload_status = status;
    plan.payload_digest = ready ? stable_fake_digest(review) : "sha256:blocked";
    if (ready) {
        plan.operation_kind =
            ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization;
    } else {
        plan.operation_kind = ProviderSdkPayloadOperationKind::NoopHarnessNotReady;
        plan.failure_attribution = harness_not_ready_failure(review);
    }
    result.diagnostics.append(validate_provider_sdk_payload_materialization_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderSdkPayloadAuditResult
build_provider_sdk_payload_audit_summary(const ProviderSdkPayloadMaterializationPlan &plan) {
    ProviderSdkPayloadAuditResult result;
    result.diagnostics.append(validate_provider_sdk_payload_materialization_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderSdkPayloadAuditSummary audit;
    audit.workflow_canonical_name = plan.workflow_canonical_name;
    audit.session_id = plan.session_id;
    audit.run_id = plan.run_id;
    audit.input_fixture = plan.input_fixture;
    audit.durable_store_import_provider_sdk_payload_plan_identity =
        plan.durable_store_import_provider_sdk_payload_plan_identity;
    audit.payload_status = plan.payload_status;
    audit.provider_request_payload_schema_ref = plan.provider_request_payload_schema_ref;
    audit.payload_digest = plan.payload_digest;
    audit.redaction_summary = plan.redaction_summary;
    audit.fake_provider_only = plan.fake_provider_only;
    audit.raw_payload_persisted = plan.persists_materialized_payload;
    audit.audit_summary = plan.payload_status == ProviderSdkPayloadStatus::Ready
                              ? "fake SDK payload digest is ready and raw payload was not persisted"
                              : "fake SDK payload materialization is blocked";
    audit.next_action = next_action_for_plan(plan);
    audit.next_step_recommendation =
        audit.next_action == ProviderSdkPayloadNextActionKind::ReadyForMockAdapter
            ? "ready for provider SDK mock adapter contract"
            : "wait for successful local harness before mock adapter execution";
    audit.failure_attribution = plan.failure_attribution;
    result.diagnostics.append(validate_provider_sdk_payload_audit_summary(audit).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.audit = std::move(audit);
    return result;
}

} // namespace ahfl::durable_store_import
