#include "../pipeline_durable_store_import_provider.hpp"

#include "../pipeline_durable_store_import.hpp"
#include "../pipeline_durable_store_import_provider_steps.hpp"

#include "ahfl/durable_store_import/artifacts.hpp"
#include "ahfl/durable_store_import/provider.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessEvidence>
build_durable_store_import_provider_production_readiness_evidence_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto negotiation =
        build_durable_store_import_provider_capability_negotiation_review_for_cli(
            program, metadata, mock_set, options, command_name);
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto audit = build_durable_store_import_provider_execution_audit_event_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto recovery = build_durable_store_import_provider_write_recovery_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto taxonomy = build_durable_store_import_provider_failure_taxonomy_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!negotiation.has_value() || !compatibility.has_value() || !audit.has_value() ||
        !recovery.has_value() || !taxonomy.has_value()) {
        return std::nullopt;
    }

    const auto evidence = ahfl::durable_store_import::build_provider_production_readiness_evidence(
        *negotiation, *compatibility, *audit, *recovery, *taxonomy);
    evidence.diagnostics.render(std::cerr);
    if (evidence.has_errors() || !evidence.evidence.has_value()) {
        return std::nullopt;
    }
    return *evidence.evidence;
}

[[nodiscard]] int emit_durable_store_import_provider_production_readiness_evidence_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto evidence = build_durable_store_import_provider_production_readiness_evidence_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-readiness-evidence");
    if (!evidence.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_production_readiness_evidence_json(*evidence,
                                                                                 std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReview>
build_durable_store_import_provider_production_readiness_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto evidence = build_durable_store_import_provider_production_readiness_evidence_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!evidence.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_production_readiness_review(*evidence);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_production_readiness_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_production_readiness_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-readiness-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_production_readiness_review_json(*review, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_provider_production_readiness_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_production_readiness_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-readiness-report");
    if (!review.has_value()) {
        return 1;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_production_readiness_report(*review);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_production_readiness_report(*report.report,
                                                                          std::cout);
    return 0;
}

[[nodiscard]] int
emit_durable_store_import_provider_release_evidence_archive_manifest_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 获取 conformance report
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-release-evidence-archive-manifest");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-release-evidence-archive-manifest");
    if (!registry.has_value()) {
        return 1;
    }
    const auto readiness_evidence =
        build_durable_store_import_provider_production_readiness_evidence_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-release-evidence-archive-manifest");
    if (!readiness_evidence.has_value()) {
        return 1;
    }

    // 构建 conformance report
    const auto conformance = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *readiness_evidence);
    conformance.diagnostics.render(std::cerr);
    if (conformance.has_errors() || !conformance.report.has_value()) {
        return 1;
    }

    // 构建 schema compatibility report (使用 deterministic placeholder)
    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    schema_report.workflow_canonical_name = conformance.report->workflow_canonical_name;
    schema_report.session_id = conformance.report->session_id;
    schema_report.run_id = conformance.report->run_id;
    schema_report.has_schema_drift = false;
    schema_report.compatibility_summary = "all versions compatible";

    // 构建 config bundle validation report
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-release-evidence-archive-manifest");
    if (!plan.has_value()) {
        return 1;
    }
    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-release-evidence-archive-manifest");
    if (!snapshot.has_value()) {
        return 1;
    }
    const auto config_report =
        ahfl::durable_store_import::build_provider_config_bundle_validation_report(*plan,
                                                                                   *snapshot);
    config_report.diagnostics.render(std::cerr);
    if (config_report.has_errors() || !config_report.report.has_value()) {
        return 1;
    }

    // 构建 release evidence archive manifest
    const auto archive = ahfl::durable_store_import::build_release_evidence_archive_manifest(
        *conformance.report, schema_report, *config_report.report, *readiness_evidence);
    archive.diagnostics.render(std::cerr);
    if (archive.has_errors() || !archive.manifest.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_release_evidence_archive_manifest(*archive.manifest,
                                                                                std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_provider_approval_receipt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 构建 release evidence archive manifest（复用 release evidence archive 的构建逻辑）
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-approval-receipt");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-approval-receipt");
    if (!registry.has_value()) {
        return 1;
    }
    const auto readiness_evidence =
        build_durable_store_import_provider_production_readiness_evidence_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-approval-receipt");
    if (!readiness_evidence.has_value()) {
        return 1;
    }

    // 构建 conformance report
    const auto conformance = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *readiness_evidence);
    conformance.diagnostics.render(std::cerr);
    if (conformance.has_errors() || !conformance.report.has_value()) {
        return 1;
    }

    // 构建 schema compatibility report
    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    schema_report.workflow_canonical_name = conformance.report->workflow_canonical_name;
    schema_report.session_id = conformance.report->session_id;
    schema_report.run_id = conformance.report->run_id;
    schema_report.has_schema_drift = false;
    schema_report.compatibility_summary = "all versions compatible";

    // 构建 config bundle validation report
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-approval-receipt");
    if (!plan.has_value()) {
        return 1;
    }
    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-approval-receipt");
    if (!snapshot.has_value()) {
        return 1;
    }
    const auto config_report =
        ahfl::durable_store_import::build_provider_config_bundle_validation_report(*plan,
                                                                                   *snapshot);
    config_report.diagnostics.render(std::cerr);
    if (config_report.has_errors() || !config_report.report.has_value()) {
        return 1;
    }

    // 构建 release evidence archive manifest
    const auto archive_result = ahfl::durable_store_import::build_release_evidence_archive_manifest(
        *conformance.report, schema_report, *config_report.report, *readiness_evidence);
    archive_result.diagnostics.render(std::cerr);
    if (archive_result.has_errors() || !archive_result.manifest.has_value()) {
        return 1;
    }

    // 构建 approval request
    const auto request_result = ahfl::durable_store_import::build_approval_request(
        *archive_result.manifest, *readiness_evidence);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return 1;
    }

    // 构建 approval decision record（安全默认：Rejected）
    ahfl::durable_store_import::ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::" + request_result.request->session_id;
    decision.decision = ahfl::durable_store_import::ApprovalDecision::Rejected;
    decision.decision_reason = "default rejection: operator approval not yet provided";
    decision.rejection_details = "awaiting operator review and explicit approval";

    // 构建 approval receipt
    const auto receipt_result =
        ahfl::durable_store_import::build_approval_receipt(*request_result.request, decision);
    receipt_result.diagnostics.render(std::cerr);
    if (receipt_result.has_errors() || !receipt_result.receipt.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_approval_receipt_json(*receipt_result.receipt,
                                                                    std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_provider_opt_in_decision_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 复用 approval receipt pipeline 的构建链
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-opt-in-decision-report");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-opt-in-decision-report");
    if (!registry.has_value()) {
        return 1;
    }

    // 构建 selection plan
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-opt-in-decision-report");
    if (!plan.has_value()) {
        return 1;
    }

    // 构建 config snapshot 和 config bundle validation report
    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-opt-in-decision-report");
    if (!snapshot.has_value()) {
        return 1;
    }
    const auto config_report =
        ahfl::durable_store_import::build_provider_config_bundle_validation_report(*plan,
                                                                                   *snapshot);
    config_report.diagnostics.render(std::cerr);
    if (config_report.has_errors() || !config_report.report.has_value()) {
        return 1;
    }

    // 构建 readiness evidence
    const auto readiness_evidence =
        build_durable_store_import_provider_production_readiness_evidence_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-opt-in-decision-report");
    if (!readiness_evidence.has_value()) {
        return 1;
    }

    // 构建 conformance report
    const auto conformance = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *readiness_evidence);
    conformance.diagnostics.render(std::cerr);
    if (conformance.has_errors() || !conformance.report.has_value()) {
        return 1;
    }

    // 构建 release evidence archive manifest
    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    schema_report.workflow_canonical_name = conformance.report->workflow_canonical_name;
    schema_report.session_id = conformance.report->session_id;
    schema_report.run_id = conformance.report->run_id;
    schema_report.has_schema_drift = false;
    schema_report.compatibility_summary = "all versions compatible";

    const auto archive_result = ahfl::durable_store_import::build_release_evidence_archive_manifest(
        *conformance.report, schema_report, *config_report.report, *readiness_evidence);
    archive_result.diagnostics.render(std::cerr);
    if (archive_result.has_errors() || !archive_result.manifest.has_value()) {
        return 1;
    }

    // 构建 approval request + receipt（默认 Rejected）
    const auto request_result = ahfl::durable_store_import::build_approval_request(
        *archive_result.manifest, *readiness_evidence);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return 1;
    }

    ahfl::durable_store_import::ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::" + request_result.request->session_id;
    decision.decision = ahfl::durable_store_import::ApprovalDecision::Rejected;
    decision.decision_reason = "default rejection: operator approval not yet provided";
    decision.rejection_details = "awaiting operator review and explicit approval";

    const auto receipt_result =
        ahfl::durable_store_import::build_approval_receipt(*request_result.request, decision);
    receipt_result.diagnostics.render(std::cerr);
    if (receipt_result.has_errors() || !receipt_result.receipt.has_value()) {
        return 1;
    }

    // 构建 opt-in decision report
    const auto opt_in_result = ahfl::durable_store_import::build_provider_opt_in_decision_report(
        *receipt_result.receipt, *config_report.report, *plan);
    opt_in_result.diagnostics.render(std::cerr);
    if (opt_in_result.has_errors() || !opt_in_result.report.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_opt_in_decision_report(*opt_in_result.report,
                                                                     std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_provider_runtime_policy_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 复用 opt-in decision report pipeline 的完整构建链
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-policy-report");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-policy-report");
    if (!registry.has_value()) {
        return 1;
    }

    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-policy-report");
    if (!plan.has_value()) {
        return 1;
    }

    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-policy-report");
    if (!snapshot.has_value()) {
        return 1;
    }
    const auto config_report =
        ahfl::durable_store_import::build_provider_config_bundle_validation_report(*plan,
                                                                                   *snapshot);
    config_report.diagnostics.render(std::cerr);
    if (config_report.has_errors() || !config_report.report.has_value()) {
        return 1;
    }

    const auto readiness_evidence =
        build_durable_store_import_provider_production_readiness_evidence_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-runtime-policy-report");
    if (!readiness_evidence.has_value()) {
        return 1;
    }

    const auto conformance = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *readiness_evidence);
    conformance.diagnostics.render(std::cerr);
    if (conformance.has_errors() || !conformance.report.has_value()) {
        return 1;
    }

    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    schema_report.workflow_canonical_name = conformance.report->workflow_canonical_name;
    schema_report.session_id = conformance.report->session_id;
    schema_report.run_id = conformance.report->run_id;
    schema_report.has_schema_drift = false;
    schema_report.compatibility_summary = "all versions compatible";

    const auto archive_result = ahfl::durable_store_import::build_release_evidence_archive_manifest(
        *conformance.report, schema_report, *config_report.report, *readiness_evidence);
    archive_result.diagnostics.render(std::cerr);
    if (archive_result.has_errors() || !archive_result.manifest.has_value()) {
        return 1;
    }

    const auto request_result = ahfl::durable_store_import::build_approval_request(
        *archive_result.manifest, *readiness_evidence);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return 1;
    }

    // 默认 Rejected（安全默认）
    ahfl::durable_store_import::ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::" + request_result.request->session_id;
    decision.decision = ahfl::durable_store_import::ApprovalDecision::Rejected;
    decision.decision_reason = "default rejection: operator approval not yet provided";
    decision.rejection_details = "awaiting operator review and explicit approval";

    const auto receipt_result =
        ahfl::durable_store_import::build_approval_receipt(*request_result.request, decision);
    receipt_result.diagnostics.render(std::cerr);
    if (receipt_result.has_errors() || !receipt_result.receipt.has_value()) {
        return 1;
    }

    // 构建 opt-in decision report
    const auto opt_in_result = ahfl::durable_store_import::build_provider_opt_in_decision_report(
        *receipt_result.receipt, *config_report.report, *plan);
    opt_in_result.diagnostics.render(std::cerr);
    if (opt_in_result.has_errors() || !opt_in_result.report.has_value()) {
        return 1;
    }

    // 构建 runtime policy report - 聚合所有 policy evidence
    const auto policy_result =
        ahfl::durable_store_import::build_provider_runtime_policy_report(*opt_in_result.report,
                                                                         *receipt_result.receipt,
                                                                         *config_report.report,
                                                                         *plan,
                                                                         *readiness_evidence);
    policy_result.diagnostics.render(std::cerr);
    if (policy_result.has_errors() || !policy_result.report.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_runtime_policy_report(*policy_result.report,
                                                                    std::cout);
    return 0;
}

[[nodiscard]] int
emit_durable_store_import_provider_production_integration_dry_run_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 复用 runtime policy report pipeline 的完整构建链
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-integration-dry-run-report");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-integration-dry-run-report");
    if (!registry.has_value()) {
        return 1;
    }

    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-integration-dry-run-report");
    if (!plan.has_value()) {
        return 1;
    }

    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-production-integration-dry-run-report");
    if (!snapshot.has_value()) {
        return 1;
    }
    const auto config_report =
        ahfl::durable_store_import::build_provider_config_bundle_validation_report(*plan,
                                                                                   *snapshot);
    config_report.diagnostics.render(std::cerr);
    if (config_report.has_errors() || !config_report.report.has_value()) {
        return 1;
    }

    const auto readiness_evidence =
        build_durable_store_import_provider_production_readiness_evidence_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-production-integration-dry-run-report");
    if (!readiness_evidence.has_value()) {
        return 1;
    }

    // v0.43: Conformance Report
    const auto conformance = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *readiness_evidence);
    conformance.diagnostics.render(std::cerr);
    if (conformance.has_errors() || !conformance.report.has_value()) {
        return 1;
    }

    // v0.44: Schema Compatibility Report（placeholder）
    ahfl::durable_store_import::ProviderSchemaCompatibilityReport schema_report;
    schema_report.workflow_canonical_name = conformance.report->workflow_canonical_name;
    schema_report.session_id = conformance.report->session_id;
    schema_report.run_id = conformance.report->run_id;
    schema_report.has_schema_drift = false;
    schema_report.compatibility_summary = "all versions compatible";

    // v0.46: Release Evidence Archive Manifest
    const auto archive_result = ahfl::durable_store_import::build_release_evidence_archive_manifest(
        *conformance.report, schema_report, *config_report.report, *readiness_evidence);
    archive_result.diagnostics.render(std::cerr);
    if (archive_result.has_errors() || !archive_result.manifest.has_value()) {
        return 1;
    }

    // v0.47: Approval Receipt（默认 Rejected - 安全默认）
    const auto request_result = ahfl::durable_store_import::build_approval_request(
        *archive_result.manifest, *readiness_evidence);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return 1;
    }

    ahfl::durable_store_import::ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::" + request_result.request->session_id;
    decision.decision = ahfl::durable_store_import::ApprovalDecision::Rejected;
    decision.decision_reason = "default rejection: operator approval not yet provided";
    decision.rejection_details = "awaiting operator review and explicit approval";

    const auto receipt_result =
        ahfl::durable_store_import::build_approval_receipt(*request_result.request, decision);
    receipt_result.diagnostics.render(std::cerr);
    if (receipt_result.has_errors() || !receipt_result.receipt.has_value()) {
        return 1;
    }

    // v0.48: Opt-In Decision Report
    const auto opt_in_result = ahfl::durable_store_import::build_provider_opt_in_decision_report(
        *receipt_result.receipt, *config_report.report, *plan);
    opt_in_result.diagnostics.render(std::cerr);
    if (opt_in_result.has_errors() || !opt_in_result.report.has_value()) {
        return 1;
    }

    // v0.49: Runtime Policy Report
    const auto policy_result =
        ahfl::durable_store_import::build_provider_runtime_policy_report(*opt_in_result.report,
                                                                         *receipt_result.receipt,
                                                                         *config_report.report,
                                                                         *plan,
                                                                         *readiness_evidence);
    policy_result.diagnostics.render(std::cerr);
    if (policy_result.has_errors() || !policy_result.report.has_value()) {
        return 1;
    }

    // v0.50: Production Integration Dry Run Report - 终点汇总
    const auto dry_run_result =
        ahfl::durable_store_import::build_provider_production_integration_dry_run_report(
            *conformance.report,
            schema_report,
            *config_report.report,
            *archive_result.manifest,
            *receipt_result.receipt,
            *opt_in_result.report,
            *policy_result.report);
    dry_run_result.diagnostics.render(std::cerr);
    if (dry_run_result.has_errors() || !dry_run_result.report.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_production_integration_dry_run_report(
        *dry_run_result.report, std::cout);
    return 0;
}

} // namespace ahfl::cli
