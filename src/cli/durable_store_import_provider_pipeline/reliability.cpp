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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRetryDecision>
build_provider_write_retry_decision_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto adapter_result =
        build_provider_sdk_mock_adapter_execution_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReceipt>
build_provider_write_commit_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto decision = build_provider_write_retry_decision_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReview>
build_provider_write_commit_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_provider_write_commit_receipt_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryCheckpoint>
build_provider_write_recovery_checkpoint_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_provider_write_commit_receipt_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryPlan>
build_provider_write_recovery_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto checkpoint = build_provider_write_recovery_checkpoint_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryReview>
build_provider_write_recovery_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_write_recovery_plan_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReport>
build_provider_failure_taxonomy_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto adapter_result =
        build_provider_sdk_mock_adapter_execution_for_cli(
            program, metadata, mock_set, options, command_name);
    if (!adapter_result.has_value()) {
        return std::nullopt;
    }
    const auto recovery_plan = build_provider_write_recovery_plan_for_cli(
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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReview>
build_provider_failure_taxonomy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_provider_failure_taxonomy_report_for_cli(
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

} // namespace ahfl::cli
