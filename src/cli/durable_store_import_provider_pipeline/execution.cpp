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

} // namespace ahfl::cli
