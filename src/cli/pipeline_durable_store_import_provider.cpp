#include "pipeline_durable_store_import_provider.hpp"

#include "cli_pipeline_artifacts.hpp"
#include "pipeline_durable_store_import.hpp"

#include "ahfl/durable_store_import/provider_adapter.hpp"
#include "ahfl/durable_store_import/provider_approval_workflow.hpp"
#include "ahfl/durable_store_import/provider_audit.hpp"
#include "ahfl/durable_store_import/provider_commit.hpp"
#include "ahfl/durable_store_import/provider_compatibility.hpp"
#include "ahfl/durable_store_import/provider_config.hpp"
#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/durable_store_import/provider_conformance.hpp"
#include "ahfl/durable_store_import/provider_driver.hpp"
#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"
#include "ahfl/durable_store_import/provider_host_execution.hpp"
#include "ahfl/durable_store_import/provider_local_filesystem_alpha.hpp"
#include "ahfl/durable_store_import/provider_local_host_execution.hpp"
#include "ahfl/durable_store_import/provider_local_host_harness.hpp"
#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"
#include "ahfl/durable_store_import/provider_production_integration_dry_run.hpp"
#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_recovery.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"
#include "ahfl/durable_store_import/provider_retry.hpp"
#include "ahfl/durable_store_import/provider_runtime.hpp"
#include "ahfl/durable_store_import/provider_runtime_policy.hpp"
#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"
#include "ahfl/durable_store_import/provider_sdk.hpp"
#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"
#include "ahfl/durable_store_import/provider_sdk_interface.hpp"
#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"
#include "ahfl/durable_store_import/provider_sdk_payload.hpp"
#include "ahfl/durable_store_import/provider_secret.hpp"

#include "ahfl/backends/durable_store_import_provider_approval_receipt.hpp"
#include "ahfl/backends/durable_store_import_provider_capability_negotiation_review.hpp"
#include "ahfl/backends/durable_store_import_provider_compatibility_report.hpp"
#include "ahfl/backends/durable_store_import_provider_compatibility_test_manifest.hpp"
#include "ahfl/backends/durable_store_import_provider_config_bundle_validation_report.hpp"
#include "ahfl/backends/durable_store_import_provider_config_load.hpp"
#include "ahfl/backends/durable_store_import_provider_config_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_config_snapshot.hpp"
#include "ahfl/backends/durable_store_import_provider_conformance_report.hpp"
#include "ahfl/backends/durable_store_import_provider_driver_binding.hpp"
#include "ahfl/backends/durable_store_import_provider_driver_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_execution_audit_event.hpp"
#include "ahfl/backends/durable_store_import_provider_failure_taxonomy_report.hpp"
#include "ahfl/backends/durable_store_import_provider_failure_taxonomy_review.hpp"
#include "ahfl/backends/durable_store_import_provider_fixture_matrix.hpp"
#include "ahfl/backends/durable_store_import_provider_host_execution.hpp"
#include "ahfl/backends/durable_store_import_provider_host_execution_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_plan.hpp"
#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_result.hpp"
#include "ahfl/backends/durable_store_import_provider_local_host_execution_receipt.hpp"
#include "ahfl/backends/durable_store_import_provider_local_host_execution_receipt_review.hpp"
#include "ahfl/backends/durable_store_import_provider_local_host_harness_record.hpp"
#include "ahfl/backends/durable_store_import_provider_local_host_harness_request.hpp"
#include "ahfl/backends/durable_store_import_provider_local_host_harness_review.hpp"
#include "ahfl/backends/durable_store_import_provider_operator_review_event.hpp"
#include "ahfl/backends/durable_store_import_provider_opt_in_decision_report.hpp"
#include "ahfl/backends/durable_store_import_provider_production_integration_dry_run_report.hpp"
#include "ahfl/backends/durable_store_import_provider_production_readiness_evidence.hpp"
#include "ahfl/backends/durable_store_import_provider_production_readiness_report.hpp"
#include "ahfl/backends/durable_store_import_provider_production_readiness_review.hpp"
#include "ahfl/backends/durable_store_import_provider_recovery_handoff.hpp"
#include "ahfl/backends/durable_store_import_provider_registry.hpp"
#include "ahfl/backends/durable_store_import_provider_release_evidence_archive_manifest.hpp"
#include "ahfl/backends/durable_store_import_provider_runtime_policy_report.hpp"
#include "ahfl/backends/durable_store_import_provider_runtime_preflight.hpp"
#include "ahfl/backends/durable_store_import_provider_runtime_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_schema_compatibility_report.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_adapter_interface.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_adapter_interface_review.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_adapter_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_adapter_request.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_adapter_response_placeholder.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_envelope.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_handoff_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_mock_adapter_contract.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_mock_adapter_execution.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_mock_adapter_readiness.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_payload_audit.hpp"
#include "ahfl/backends/durable_store_import_provider_sdk_payload_plan.hpp"
#include "ahfl/backends/durable_store_import_provider_secret_policy_review.hpp"
#include "ahfl/backends/durable_store_import_provider_secret_resolver_request.hpp"
#include "ahfl/backends/durable_store_import_provider_secret_resolver_response.hpp"
#include "ahfl/backends/durable_store_import_provider_selection_plan.hpp"
#include "ahfl/backends/durable_store_import_provider_telemetry_summary.hpp"
#include "ahfl/backends/durable_store_import_provider_write_attempt.hpp"
#include "ahfl/backends/durable_store_import_provider_write_commit_receipt.hpp"
#include "ahfl/backends/durable_store_import_provider_write_commit_review.hpp"
#include "ahfl/backends/durable_store_import_provider_write_recovery_checkpoint.hpp"
#include "ahfl/backends/durable_store_import_provider_write_recovery_plan.hpp"
#include "ahfl/backends/durable_store_import_provider_write_recovery_review.hpp"
#include "ahfl/backends/durable_store_import_provider_write_retry_decision.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRuntimePreflightPlan>
build_durable_store_import_provider_runtime_preflight_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto driver_binding = build_durable_store_import_provider_driver_binding_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!driver_binding.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_runtime_preflight_plan(*driver_binding);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_runtime_preflight_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_runtime_preflight_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-preflight");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_runtime_preflight_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRuntimeReadinessReview>
build_durable_store_import_provider_runtime_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_runtime_preflight_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_runtime_readiness_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_runtime_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_runtime_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-runtime-readiness");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_runtime_readiness(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkRequestEnvelopePlan>
build_durable_store_import_provider_sdk_envelope_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto preflight = build_durable_store_import_provider_runtime_preflight_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!preflight.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_sdk_request_envelope_plan(*preflight);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_envelope_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_sdk_envelope_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-sdk-envelope");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_envelope_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkHandoffReadinessReview>
build_durable_store_import_provider_sdk_handoff_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_sdk_envelope_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_handoff_readiness_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_handoff_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_sdk_handoff_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-handoff-readiness");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_handoff_readiness(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderHostExecutionPlan>
build_durable_store_import_provider_host_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto envelope = build_durable_store_import_provider_sdk_envelope_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!envelope.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_host_execution_plan(*envelope);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_host_execution_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_host_execution_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-host-execution");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_host_execution_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderHostExecutionReadinessReview>
build_durable_store_import_provider_host_execution_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_host_execution_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_host_execution_readiness_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_host_execution_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_host_execution_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-host-execution-readiness");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_host_execution_readiness(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostExecutionReceipt>
build_durable_store_import_provider_local_host_execution_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_host_execution_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt(*plan);
    receipt.diagnostics.render(std::cerr);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] int emit_durable_store_import_provider_local_host_execution_receipt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto receipt = build_durable_store_import_provider_local_host_execution_receipt_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-host-execution-receipt");
    if (!receipt.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_local_host_execution_receipt_json(*receipt,
                                                                                std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostExecutionReceiptReview>
build_durable_store_import_provider_local_host_execution_receipt_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_provider_local_host_execution_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_local_host_execution_receipt_review(*receipt);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int
emit_durable_store_import_provider_local_host_execution_receipt_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review =
        build_durable_store_import_provider_local_host_execution_receipt_review_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-local-host-execution-receipt-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_local_host_execution_receipt_review(*review,
                                                                                  std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterRequestPlan>
build_durable_store_import_provider_sdk_adapter_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_provider_local_host_execution_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_sdk_adapter_request_plan(*receipt);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_adapter_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_sdk_adapter_request_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-adapter-request");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_adapter_request_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterResponsePlaceholder>
build_durable_store_import_provider_sdk_adapter_response_placeholder_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_sdk_adapter_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_sdk_adapter_response_placeholder(*plan);
    placeholder.diagnostics.render(std::cerr);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        return std::nullopt;
    }

    return *placeholder.placeholder;
}

[[nodiscard]] int
emit_durable_store_import_provider_sdk_adapter_response_placeholder_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto placeholder =
        build_durable_store_import_provider_sdk_adapter_response_placeholder_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-sdk-adapter-response-placeholder");
    if (!placeholder.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_adapter_response_placeholder_json(*placeholder,
                                                                                    std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterReadinessReview>
build_durable_store_import_provider_sdk_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder =
        build_durable_store_import_provider_sdk_adapter_response_placeholder_for_cli(
            program, metadata, mock_set, options, command_name);
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_readiness_review(*placeholder);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_adapter_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_sdk_adapter_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-adapter-readiness");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_adapter_readiness(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfacePlan>
build_durable_store_import_provider_sdk_adapter_interface_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_sdk_adapter_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto interface_plan =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_plan(*plan);
    interface_plan.diagnostics.render(std::cerr);
    if (interface_plan.has_errors() || !interface_plan.plan.has_value()) {
        return std::nullopt;
    }

    return *interface_plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_adapter_interface_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_sdk_adapter_interface_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-adapter-interface");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_adapter_interface_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfaceReview>
build_durable_store_import_provider_sdk_adapter_interface_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_sdk_adapter_interface_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_sdk_adapter_interface_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_adapter_interface_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_sdk_adapter_interface_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-adapter-interface-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_sdk_adapter_interface_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigLoadPlan>
build_durable_store_import_provider_config_load_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto interface_plan = build_durable_store_import_provider_sdk_adapter_interface_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!interface_plan.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_config_load_plan(*interface_plan);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_config_load_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_config_load_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-config-load");
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_config_load_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigSnapshotPlaceholder>
build_durable_store_import_provider_config_snapshot_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_config_load_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_config_snapshot_placeholder(*plan);
    placeholder.diagnostics.render(std::cerr);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        return std::nullopt;
    }

    return *placeholder.placeholder;
}

[[nodiscard]] int emit_durable_store_import_provider_config_snapshot_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto placeholder = build_durable_store_import_provider_config_snapshot_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-config-snapshot");
    if (!placeholder.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_config_snapshot_json(*placeholder, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigReadinessReview>
build_durable_store_import_provider_config_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder = build_durable_store_import_provider_config_snapshot_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_config_readiness_review(*placeholder);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_config_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_config_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-config-readiness");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_provider_config_readiness(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverRequestPlan>
build_durable_store_import_provider_secret_resolver_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_secret_resolver_request_plan(*snapshot);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_secret_resolver_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_secret_resolver_request_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-secret-resolver-request");
    if (!plan.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_secret_resolver_request_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverResponsePlaceholder>
build_durable_store_import_provider_secret_resolver_response_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_secret_resolver_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto placeholder =
        ahfl::durable_store_import::build_provider_secret_resolver_response_placeholder(*plan);
    placeholder.diagnostics.render(std::cerr);
    if (placeholder.has_errors() || !placeholder.placeholder.has_value()) {
        return std::nullopt;
    }
    return *placeholder.placeholder;
}

[[nodiscard]] int emit_durable_store_import_provider_secret_resolver_response_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto placeholder = build_durable_store_import_provider_secret_resolver_response_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-secret-resolver-response");
    if (!placeholder.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_secret_resolver_response_json(*placeholder,
                                                                            std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretPolicyReview>
build_durable_store_import_provider_secret_policy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder = build_durable_store_import_provider_secret_resolver_response_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!placeholder.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_secret_policy_review(*placeholder);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_secret_policy_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_secret_policy_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-secret-policy-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_secret_policy_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRequest>
build_durable_store_import_provider_local_host_harness_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto secret_review = build_durable_store_import_provider_secret_policy_review_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!secret_review.has_value()) {
        return std::nullopt;
    }

    const auto request =
        ahfl::durable_store_import::build_provider_local_host_harness_execution_request(
            *secret_review);
    request.diagnostics.render(std::cerr);
    if (request.has_errors() || !request.request.has_value()) {
        return std::nullopt;
    }
    return *request.request;
}

[[nodiscard]] int emit_durable_store_import_provider_local_host_harness_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_provider_local_host_harness_request_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-host-harness-request");
    if (!request.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_host_harness_request_json(*request, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRecord>
build_durable_store_import_provider_local_host_harness_record_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request = build_durable_store_import_provider_local_host_harness_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto record = ahfl::durable_store_import::run_provider_local_host_test_harness(*request);
    record.diagnostics.render(std::cerr);
    if (record.has_errors() || !record.record.has_value()) {
        return std::nullopt;
    }
    return *record.record;
}

[[nodiscard]] int emit_durable_store_import_provider_local_host_harness_record_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto record = build_durable_store_import_provider_local_host_harness_record_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-host-harness-record");
    if (!record.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_host_harness_record_json(*record, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessReview>
build_durable_store_import_provider_local_host_harness_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto record = build_durable_store_import_provider_local_host_harness_record_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!record.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_local_host_harness_review(*record);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_local_host_harness_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_local_host_harness_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-host-harness-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_host_harness_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadMaterializationPlan>
build_durable_store_import_provider_sdk_payload_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto harness_review =
        build_durable_store_import_provider_local_host_harness_review_for_cli(
            program, metadata, mock_set, options, command_name);
    if (!harness_review.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_sdk_payload_materialization_plan(
        *harness_review);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_payload_plan_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_sdk_payload_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-payload-plan");
    if (!plan.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_sdk_payload_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadAuditSummary>
build_durable_store_import_provider_sdk_payload_audit_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_sdk_payload_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto audit = ahfl::durable_store_import::build_provider_sdk_payload_audit_summary(*plan);
    audit.diagnostics.render(std::cerr);
    if (audit.has_errors() || !audit.audit.has_value()) {
        return std::nullopt;
    }
    return *audit.audit;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_payload_audit_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto audit = build_durable_store_import_provider_sdk_payload_audit_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-payload-audit");
    if (!audit.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_sdk_payload_audit(*audit, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterContract>
build_durable_store_import_provider_sdk_mock_adapter_contract_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto audit = build_durable_store_import_provider_sdk_payload_audit_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!audit.has_value()) {
        return std::nullopt;
    }

    const auto contract =
        ahfl::durable_store_import::build_provider_sdk_mock_adapter_contract(*audit);
    contract.diagnostics.render(std::cerr);
    if (contract.has_errors() || !contract.contract.has_value()) {
        return std::nullopt;
    }
    return *contract.contract;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_mock_adapter_contract_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto contract = build_durable_store_import_provider_sdk_mock_adapter_contract_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-mock-adapter-contract");
    if (!contract.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_sdk_mock_adapter_contract_json(*contract, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterExecutionResult>
build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto contract = build_durable_store_import_provider_sdk_mock_adapter_contract_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!contract.has_value()) {
        return std::nullopt;
    }

    const auto result = ahfl::durable_store_import::run_provider_sdk_mock_adapter(*contract);
    result.diagnostics.render(std::cerr);
    if (result.has_errors() || !result.result.has_value()) {
        return std::nullopt;
    }
    return *result.result;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_mock_adapter_execution_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto result = build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-mock-adapter-execution");
    if (!result.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_sdk_mock_adapter_execution_json(*result, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterReadiness>
build_durable_store_import_provider_sdk_mock_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto result = build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!result.has_value()) {
        return std::nullopt;
    }

    const auto readiness =
        ahfl::durable_store_import::build_provider_sdk_mock_adapter_readiness(*result);
    readiness.diagnostics.render(std::cerr);
    if (readiness.has_errors() || !readiness.readiness.has_value()) {
        return std::nullopt;
    }
    return *readiness.readiness;
}

[[nodiscard]] int emit_durable_store_import_provider_sdk_mock_adapter_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto readiness = build_durable_store_import_provider_sdk_mock_adapter_readiness_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-sdk-mock-adapter-readiness");
    if (!readiness.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_sdk_mock_adapter_readiness(*readiness, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaPlan>
build_durable_store_import_provider_local_filesystem_alpha_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto readiness = build_durable_store_import_provider_sdk_mock_adapter_readiness_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!readiness.has_value()) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_local_filesystem_alpha_plan(*readiness);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_local_filesystem_alpha_plan_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_local_filesystem_alpha_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-filesystem-alpha-plan");
    if (!plan.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_filesystem_alpha_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaResult>
build_durable_store_import_provider_local_filesystem_alpha_result_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_local_filesystem_alpha_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto result = ahfl::durable_store_import::run_provider_local_filesystem_alpha(*plan);
    result.diagnostics.render(std::cerr);
    if (result.has_errors() || !result.result.has_value()) {
        return std::nullopt;
    }
    return *result.result;
}

[[nodiscard]] int emit_durable_store_import_provider_local_filesystem_alpha_result_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto result = build_durable_store_import_provider_local_filesystem_alpha_result_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-local-filesystem-alpha-result");
    if (!result.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_filesystem_alpha_result_json(*result,
                                                                                 std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaReadiness>
build_durable_store_import_provider_local_filesystem_alpha_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto result = build_durable_store_import_provider_local_filesystem_alpha_result_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!result.has_value()) {
        return std::nullopt;
    }

    const auto readiness =
        ahfl::durable_store_import::build_provider_local_filesystem_alpha_readiness(*result);
    readiness.diagnostics.render(std::cerr);
    if (readiness.has_errors() || !readiness.readiness.has_value()) {
        return std::nullopt;
    }
    return *readiness.readiness;
}

[[nodiscard]] int
emit_durable_store_import_provider_local_filesystem_alpha_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto readiness =
        build_durable_store_import_provider_local_filesystem_alpha_readiness_for_cli(
            program,
            metadata,
            mock_set,
            options,
            "emit-durable-store-import-provider-local-filesystem-alpha-readiness");
    if (!readiness.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_local_filesystem_alpha_readiness(*readiness,
                                                                               std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRetryDecision>
build_durable_store_import_provider_write_retry_decision_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto adapter_result =
        build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
            program, metadata, mock_set, options, command_name);
    if (!adapter_result.has_value()) {
        return std::nullopt;
    }

    const auto decision =
        ahfl::durable_store_import::build_provider_write_retry_decision(*adapter_result);
    decision.diagnostics.render(std::cerr);
    if (decision.has_errors() || !decision.decision.has_value()) {
        return std::nullopt;
    }
    return *decision.decision;
}

[[nodiscard]] int emit_durable_store_import_provider_write_retry_decision_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_durable_store_import_provider_write_retry_decision_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-retry-decision");
    if (!decision.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_retry_decision_json(*decision, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReceipt>
build_durable_store_import_provider_write_commit_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto decision = build_durable_store_import_provider_write_retry_decision_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt = ahfl::durable_store_import::build_provider_write_commit_receipt(*decision);
    receipt.diagnostics.render(std::cerr);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        return std::nullopt;
    }
    return *receipt.receipt;
}

[[nodiscard]] int emit_durable_store_import_provider_write_commit_receipt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto receipt = build_durable_store_import_provider_write_commit_receipt_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-commit-receipt");
    if (!receipt.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_commit_receipt_json(*receipt, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReview>
build_durable_store_import_provider_write_commit_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_provider_write_commit_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_write_commit_review(*receipt);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_write_commit_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_write_commit_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-commit-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_commit_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryCheckpoint>
build_durable_store_import_provider_write_recovery_checkpoint_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_provider_write_commit_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto checkpoint =
        ahfl::durable_store_import::build_provider_write_recovery_checkpoint(*receipt);
    checkpoint.diagnostics.render(std::cerr);
    if (checkpoint.has_errors() || !checkpoint.checkpoint.has_value()) {
        return std::nullopt;
    }
    return *checkpoint.checkpoint;
}

[[nodiscard]] int emit_durable_store_import_provider_write_recovery_checkpoint_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto checkpoint = build_durable_store_import_provider_write_recovery_checkpoint_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-recovery-checkpoint");
    if (!checkpoint.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_recovery_checkpoint_json(*checkpoint,
                                                                             std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryPlan>
build_durable_store_import_provider_write_recovery_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto checkpoint = build_durable_store_import_provider_write_recovery_checkpoint_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!checkpoint.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_write_recovery_plan(*checkpoint);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_write_recovery_plan_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_write_recovery_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-recovery-plan");
    if (!plan.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_recovery_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryReview>
build_durable_store_import_provider_write_recovery_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_write_recovery_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_write_recovery_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_write_recovery_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_write_recovery_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-write-recovery-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_write_recovery_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReport>
build_durable_store_import_provider_failure_taxonomy_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto adapter_result =
        build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
            program, metadata, mock_set, options, command_name);
    if (!adapter_result.has_value()) {
        return std::nullopt;
    }
    const auto recovery_plan = build_durable_store_import_provider_write_recovery_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!recovery_plan.has_value()) {
        return std::nullopt;
    }

    const auto report = ahfl::durable_store_import::build_provider_failure_taxonomy_report(
        *adapter_result, *recovery_plan);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

[[nodiscard]] int emit_durable_store_import_provider_failure_taxonomy_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto report = build_durable_store_import_provider_failure_taxonomy_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-failure-taxonomy-report");
    if (!report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_failure_taxonomy_report_json(*report, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReview>
build_durable_store_import_provider_failure_taxonomy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_durable_store_import_provider_failure_taxonomy_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_failure_taxonomy_review(*report);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_failure_taxonomy_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_failure_taxonomy_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-failure-taxonomy-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_failure_taxonomy_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderExecutionAuditEvent>
build_durable_store_import_provider_execution_audit_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_durable_store_import_provider_failure_taxonomy_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto event = ahfl::durable_store_import::build_provider_execution_audit_event(*report);
    event.diagnostics.render(std::cerr);
    if (event.has_errors() || !event.event.has_value()) {
        return std::nullopt;
    }
    return *event.event;
}

[[nodiscard]] int emit_durable_store_import_provider_execution_audit_event_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto event = build_durable_store_import_provider_execution_audit_event_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-execution-audit-event");
    if (!event.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_execution_audit_event_json(*event, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderTelemetrySummary>
build_durable_store_import_provider_telemetry_summary_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto event = build_durable_store_import_provider_execution_audit_event_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!event.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::build_provider_telemetry_summary(*event);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return std::nullopt;
    }
    return *summary.summary;
}

[[nodiscard]] int emit_durable_store_import_provider_telemetry_summary_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto summary = build_durable_store_import_provider_telemetry_summary_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-telemetry-summary");
    if (!summary.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_telemetry_summary_json(*summary, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderOperatorReviewEvent>
build_durable_store_import_provider_operator_review_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto summary = build_durable_store_import_provider_telemetry_summary_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!summary.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_operator_review_event(*summary);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_operator_review_event_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_operator_review_event_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-operator-review-event");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_operator_review_event(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityTestManifest>
build_durable_store_import_provider_compatibility_test_manifest_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto review = build_durable_store_import_provider_operator_review_event_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!review.has_value()) {
        return std::nullopt;
    }

    const auto manifest =
        ahfl::durable_store_import::build_provider_compatibility_test_manifest(*review);
    manifest.diagnostics.render(std::cerr);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        return std::nullopt;
    }
    return *manifest.manifest;
}

[[nodiscard]] int emit_durable_store_import_provider_compatibility_test_manifest_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto manifest = build_durable_store_import_provider_compatibility_test_manifest_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-compatibility-test-manifest");
    if (!manifest.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_compatibility_test_manifest_json(*manifest,
                                                                               std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFixtureMatrix>
build_durable_store_import_provider_fixture_matrix_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto manifest = build_durable_store_import_provider_compatibility_test_manifest_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!manifest.has_value()) {
        return std::nullopt;
    }

    const auto matrix = ahfl::durable_store_import::build_provider_fixture_matrix(*manifest);
    matrix.diagnostics.render(std::cerr);
    if (matrix.has_errors() || !matrix.matrix.has_value()) {
        return std::nullopt;
    }
    return *matrix.matrix;
}

[[nodiscard]] int emit_durable_store_import_provider_fixture_matrix_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto matrix = build_durable_store_import_provider_fixture_matrix_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-fixture-matrix");
    if (!matrix.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_fixture_matrix_json(*matrix, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityReport>
build_durable_store_import_provider_compatibility_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto matrix = build_durable_store_import_provider_fixture_matrix_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto telemetry = build_durable_store_import_provider_telemetry_summary_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!matrix.has_value() || !telemetry.has_value()) {
        return std::nullopt;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_compatibility_report(*matrix, *telemetry);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

[[nodiscard]] int emit_durable_store_import_provider_compatibility_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto report = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-compatibility-report");
    if (!report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_compatibility_report_json(*report, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRegistry>
build_durable_store_import_provider_registry_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_durable_store_import_provider_compatibility_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto registry = ahfl::durable_store_import::build_provider_registry(*report);
    registry.diagnostics.render(std::cerr);
    if (registry.has_errors() || !registry.registry.has_value()) {
        return std::nullopt;
    }
    return *registry.registry;
}

[[nodiscard]] int emit_durable_store_import_provider_registry_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-registry");
    if (!registry.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_registry_json(*registry, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSelectionPlan>
build_durable_store_import_provider_selection_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!registry.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_selection_plan(*registry);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] int emit_durable_store_import_provider_selection_plan_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-provider-selection-plan");
    if (!plan.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_selection_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCapabilityNegotiationReview>
build_durable_store_import_provider_capability_negotiation_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_capability_negotiation_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_provider_capability_negotiation_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_provider_capability_negotiation_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-capability-negotiation-review");
    if (!review.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_capability_negotiation_review(*review, std::cout);
    return 0;
}

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

[[nodiscard]] int emit_durable_store_import_provider_conformance_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 复用已有 pipeline 获取上游 artifact
    const auto compatibility = build_durable_store_import_provider_compatibility_report_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-conformance-report");
    if (!compatibility.has_value()) {
        return 1;
    }
    const auto registry = build_durable_store_import_provider_registry_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-conformance-report");
    if (!registry.has_value()) {
        return 1;
    }
    const auto evidence = build_durable_store_import_provider_production_readiness_evidence_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-conformance-report");
    if (!evidence.has_value()) {
        return 1;
    }

    // 构建 conformance report
    const auto report = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *evidence);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_conformance_report(*report.report, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_provider_schema_compatibility_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 获取 production readiness evidence 作为版本检查基础
    const auto evidence = build_durable_store_import_provider_production_readiness_evidence_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-schema-compatibility-report");
    if (!evidence.has_value()) {
        return 1;
    }

    // 构建 version checks - 检查 evidence 自身的 format version
    std::vector<ahfl::durable_store_import::ArtifactVersionCheck> version_checks;
    version_checks.push_back({
        "provider-production-readiness-evidence",
        evidence->durable_store_import_provider_production_readiness_evidence_identity,
        evidence->format_version,
        ahfl::durable_store_import::SchemaCompatibilityStatus::Compatible,
        std::string(ahfl::durable_store_import::kProviderProductionReadinessEvidenceFormatVersion),
        std::nullopt,
    });

    // 构建 source chain checks - 检查各 source 版本是否与期望一致
    std::vector<ahfl::durable_store_import::SourceChainCheck> source_chain_checks;
    source_chain_checks.push_back({
        "provider-capability-negotiation-review",
        evidence->durable_store_import_provider_selection_plan_identity,
        evidence->source_durable_store_import_provider_capability_negotiation_review_format_version,
        std::string(ahfl::durable_store_import::kProviderCapabilityNegotiationReviewFormatVersion),
        evidence->source_durable_store_import_provider_capability_negotiation_review_format_version ==
            ahfl::durable_store_import::kProviderCapabilityNegotiationReviewFormatVersion,
        std::nullopt,
    });
    source_chain_checks.push_back({
        "provider-compatibility-report",
        evidence->durable_store_import_provider_compatibility_report_identity,
        evidence->source_durable_store_import_provider_compatibility_report_format_version,
        std::string(ahfl::durable_store_import::kProviderCompatibilityReportFormatVersion),
        evidence->source_durable_store_import_provider_compatibility_report_format_version ==
            ahfl::durable_store_import::kProviderCompatibilityReportFormatVersion,
        std::nullopt,
    });
    source_chain_checks.push_back({
        "provider-failure-taxonomy-report",
        evidence->durable_store_import_provider_failure_taxonomy_report_identity,
        evidence->source_durable_store_import_provider_failure_taxonomy_report_format_version,
        std::string(ahfl::durable_store_import::kProviderFailureTaxonomyReportFormatVersion),
        evidence->source_durable_store_import_provider_failure_taxonomy_report_format_version ==
            ahfl::durable_store_import::kProviderFailureTaxonomyReportFormatVersion,
        std::nullopt,
    });

    std::vector<ahfl::durable_store_import::ReferenceVersionCheck> reference_checks;

    const auto report = ahfl::durable_store_import::build_provider_schema_compatibility_report(
        version_checks,
        source_chain_checks,
        reference_checks,
        evidence->workflow_canonical_name,
        evidence->session_id,
        evidence->run_id);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_schema_compatibility_report_json(*report.report,
                                                                               std::cout);
    return 0;
}

[[nodiscard]] int
emit_durable_store_import_provider_config_bundle_validation_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    // 获取 selection plan
    const auto plan = build_durable_store_import_provider_selection_plan_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-config-bundle-validation-report");
    if (!plan.has_value()) {
        return 1;
    }

    // 获取 config snapshot
    const auto snapshot = build_durable_store_import_provider_config_snapshot_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-provider-config-bundle-validation-report");
    if (!snapshot.has_value()) {
        return 1;
    }

    // 构建 config bundle validation report
    const auto report = ahfl::durable_store_import::build_provider_config_bundle_validation_report(
        *plan, *snapshot);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }
    ahfl::print_durable_store_import_provider_config_bundle_validation_report(*report.report,
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
