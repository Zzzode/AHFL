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

} // namespace ahfl::cli
