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

} // namespace ahfl::cli
