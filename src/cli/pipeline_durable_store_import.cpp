#include "pipeline_durable_store_import.hpp"

#include "cli_pipeline_artifacts.hpp"
#include "pipeline_emit.hpp"
#include "provider_pipeline_cache.hpp"

#include "durable_store_import/artifacts.hpp"

#include "durable_store_import/adapter_execution.hpp"
#include "durable_store_import/decision.hpp"
#include "durable_store_import/decision_review.hpp"
#include "durable_store_import/provider/binding/adapter.hpp"
#include "durable_store_import/provider/binding/driver.hpp"
#include "durable_store_import/receipt.hpp"
#include "durable_store_import/receipt_persistence.hpp"
#include "durable_store_import/receipt_persistence_response.hpp"
#include "durable_store_import/receipt_persistence_response_review.hpp"
#include "durable_store_import/receipt_persistence_review.hpp"
#include "durable_store_import/receipt_review.hpp"
#include "durable_store_import/recovery_preview.hpp"
#include "durable_store_import/request.hpp"
#include "durable_store_import/review.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::durable_store_import::Request>
build_import_request_for_cli(const ahfl::ir::Program &program,
                                           const ahfl::handoff::PackageMetadata &metadata,
                                           const ahfl::dry_run::CapabilityMockSet &mock_set,
                                           const CommandLineOptions &options,
                                           std::string_view command_name) {
    CliPipelineArtifacts artifacts({
        program,
        metadata,
        mock_set,
        options,
        command_name,
    });
    const auto *store_import_descriptor = artifacts.store_import_descriptor();
    if (store_import_descriptor == nullptr) {
        return std::nullopt;
    }

    const auto request_result =
        ahfl::durable_store_import::build_request(*store_import_descriptor);
    request_result.diagnostics.render(std::cerr);
    if (request_result.has_errors() || !request_result.request.has_value()) {
        return std::nullopt;
    }

    return *request_result.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::Decision>
build_import_decision_for_cli(const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet &mock_set,
                                            const CommandLineOptions &options,
                                            std::string_view command_name) {
    const auto request = build_import_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_decision(*request);
    decision.diagnostics.render(std::cerr);
    if (decision.has_errors() || !decision.decision.has_value()) {
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::Receipt>
build_receipt_for_cli(const ahfl::ir::Program &program,
                                           const ahfl::handoff::PackageMetadata &metadata,
                                           const ahfl::dry_run::CapabilityMockSet &mock_set,
                                           const CommandLineOptions &options,
                                           std::string_view command_name) {
    const auto decision = build_import_decision_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        ahfl::durable_store_import::build_receipt(*decision);
    receipt.diagnostics.render(std::cerr);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceRequest>
build_receipt_persistence_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto request =
        ahfl::durable_store_import::build_persistence_request(
            *receipt);
    request.diagnostics.render(std::cerr);
    if (request.has_errors() || !request.request.has_value()) {
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] int
emit_durable_store_import_request_with_diagnostics(const ahfl::ir::Program &program,
                                                   const ahfl::handoff::PackageMetadata &metadata,
                                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                   const CommandLineOptions &options) {
    return emit_cli_artifact(build_import_request_for_cli,
                             ahfl::print_durable_store_import_request_json,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-request");
}

[[nodiscard]] int
emit_durable_store_import_review_with_diagnostics(const ahfl::ir::Program &program,
                                                  const ahfl::handoff::PackageMetadata &metadata,
                                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                  const CommandLineOptions &options) {
    const auto request = build_import_request_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_decision_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_import_decision_for_cli,
                             ahfl::print_durable_store_import_decision_json,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-decision");
}

[[nodiscard]] int
emit_durable_store_import_receipt_with_diagnostics(const ahfl::ir::Program &program,
                                                   const ahfl::handoff::PackageMetadata &metadata,
                                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                   const CommandLineOptions &options) {
    return emit_cli_artifact(build_receipt_for_cli,
                             ahfl::print_durable_store_import_receipt_json,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-receipt");
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(
        build_receipt_persistence_request_for_cli,
        ahfl::print_durable_store_import_receipt_persistence_request_json,
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-request");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ReceiptReviewSummary>
build_receipt_review_for_cli(const ahfl::ir::Program &program,
                                                  const ahfl::handoff::PackageMetadata &metadata,
                                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                  const CommandLineOptions &options,
                                                  std::string_view command_name) {
    const auto receipt = build_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_receipt_review_summary(
            *receipt);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_receipt_review_for_cli,
                             ahfl::print_durable_store_import_receipt_review,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-receipt-review");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceReviewSummary>
build_receipt_persistence_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request = build_receipt_persistence_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_persistence_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_receipt_persistence_review_for_cli,
                             ahfl::print_durable_store_import_receipt_persistence_review,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-receipt-persistence-review");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceResponse>
build_receipt_persistence_response_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request = build_receipt_persistence_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto response = ahfl::durable_store_import::
        build_persistence_response(*request);
    response.diagnostics.render(std::cerr);
    if (response.has_errors() || !response.response.has_value()) {
        return std::nullopt;
    }

    return *response.response;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_response_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_receipt_persistence_response_for_cli,
                             ahfl::print_durable_store_import_receipt_persistence_response_json,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-receipt-persistence-response");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceResponseReviewSummary>
build_receipt_persistence_response_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto response = build_receipt_persistence_response_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!response.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::
        build_persistence_response_review(*response);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_response_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(
        build_receipt_persistence_response_review_for_cli,
        ahfl::print_durable_store_import_receipt_persistence_response_review,
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-response-review");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::AdapterExecutionReceipt>
build_adapter_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto response = build_receipt_persistence_response_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!response.has_value()) {
        return std::nullopt;
    }

    const auto execution = ahfl::durable_store_import::build_adapter_execution_receipt(*response);
    execution.diagnostics.render(std::cerr);
    if (execution.has_errors() || !execution.receipt.has_value()) {
        return std::nullopt;
    }

    return *execution.receipt;
}

[[nodiscard]] int emit_durable_store_import_adapter_execution_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_adapter_execution_for_cli,
                             ahfl::print_durable_store_import_adapter_execution_json,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-adapter-execution");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::RecoveryCommandPreview>
build_recovery_preview_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto execution = build_adapter_execution_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!execution.has_value()) {
        return std::nullopt;
    }

    const auto preview = ahfl::durable_store_import::build_recovery_command_preview(*execution);
    preview.diagnostics.render(std::cerr);
    if (preview.has_errors() || !preview.preview.has_value()) {
        return std::nullopt;
    }

    return *preview.preview;
}

[[nodiscard]] int emit_durable_store_import_recovery_preview_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    return emit_cli_artifact(build_recovery_preview_for_cli,
                             ahfl::print_durable_store_import_recovery_preview,
                             program,
                             metadata,
                             mock_set,
                             options,
                             "emit-durable-store-import-recovery-preview");
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteAttemptPreview>
build_provider_write_attempt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name,
    ProviderPipelineCache & /*cache*/) {
    const auto execution = build_adapter_execution_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!execution.has_value()) {
        return std::nullopt;
    }

    const auto preview =
        ahfl::durable_store_import::build_provider_write_attempt_preview(*execution);
    preview.diagnostics.render(std::cerr);
    if (preview.has_errors() || !preview.preview.has_value()) {
        return std::nullopt;
    }

    return *preview.preview;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRecoveryHandoffPreview>
build_provider_recovery_handoff_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *write_attempt = cache.get_WriteAttempt();
    if (write_attempt == nullptr) {
        return std::nullopt;
    }

    const auto handoff =
        ahfl::durable_store_import::build_provider_recovery_handoff_preview(*write_attempt);
    handoff.diagnostics.render(std::cerr);
    if (handoff.has_errors() || !handoff.preview.has_value()) {
        return std::nullopt;
    }

    return *handoff.preview;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderDriverBindingPlan>
build_provider_driver_binding_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *write_attempt = cache.get_WriteAttempt();
    if (write_attempt == nullptr) {
        return std::nullopt;
    }

    const auto plan =
        ahfl::durable_store_import::build_provider_driver_binding_plan(*write_attempt);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderDriverReadinessReview>
build_provider_driver_readiness_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *plan = cache.get_DriverBinding();
    if (plan == nullptr) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_driver_readiness_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }

    return *review.review;
}

[[nodiscard]] int emit_durable_store_import_decision_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_import_decision_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-decision-review");
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_decision_review_summary(*decision);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_decision_review(*review.summary, std::cout);
    return 0;
}

} // namespace ahfl::cli
