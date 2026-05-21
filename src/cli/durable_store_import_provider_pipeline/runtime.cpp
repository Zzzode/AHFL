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

} // namespace ahfl::cli
