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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigLoadPlan>
build_provider_config_load_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto interface_plan = build_provider_sdk_adapter_interface_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigSnapshotPlaceholder>
build_provider_config_snapshot_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_config_load_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigReadinessReview>
build_provider_config_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder = build_provider_config_snapshot_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverRequestPlan>
build_provider_secret_resolver_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto snapshot = build_provider_config_snapshot_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverResponsePlaceholder>
build_provider_secret_resolver_response_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_secret_resolver_request_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretPolicyReview>
build_provider_secret_policy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto placeholder = build_provider_secret_resolver_response_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigBundleValidationReport>
build_provider_config_bundle_validation_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_selection_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto snapshot = build_provider_config_snapshot_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    const auto report = ahfl::durable_store_import::build_provider_config_bundle_validation_report(
        *plan, *snapshot);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

} // namespace ahfl::cli
