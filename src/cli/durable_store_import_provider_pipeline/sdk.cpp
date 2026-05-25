#include "../pipeline_durable_store_import_provider.hpp"

#include "../pipeline_durable_store_import.hpp"
#include "../pipeline_durable_store_import_provider_steps.hpp"
#include "../pipeline_emit.hpp"

#include "durable_store_import/artifacts.hpp"
#include "durable_store_import/provider.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkRequestEnvelopePlan>
build_provider_sdk_envelope_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto preflight = build_provider_runtime_preflight_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkHandoffReadinessReview>
build_provider_sdk_handoff_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_sdk_envelope_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterRequestPlan>
build_provider_sdk_adapter_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_provider_local_host_execution_receipt_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterResponsePlaceholder>
build_provider_sdk_adapter_response_placeholder_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_sdk_adapter_request_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterReadinessReview>
build_provider_sdk_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder =
        build_provider_sdk_adapter_response_placeholder_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfacePlan>
build_provider_sdk_adapter_interface_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_sdk_adapter_request_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfaceReview>
build_provider_sdk_adapter_interface_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_sdk_adapter_interface_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadMaterializationPlan>
build_provider_sdk_payload_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto harness_review =
        build_provider_local_host_harness_review_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadAuditSummary>
build_provider_sdk_payload_audit_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_sdk_payload_plan_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterContract>
build_provider_sdk_mock_adapter_contract_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto audit = build_provider_sdk_payload_audit_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterExecutionResult>
build_provider_sdk_mock_adapter_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto contract = build_provider_sdk_mock_adapter_contract_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterReadiness>
build_provider_sdk_mock_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto result = build_provider_sdk_mock_adapter_execution_for_cli(
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

} // namespace ahfl::cli
