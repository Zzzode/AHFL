#include "tooling/cli/provider/pipeline_durable_store_import_provider_steps.hpp"

#include "tooling/cli/cli_pipeline_artifacts.hpp"
#include "tooling/cli/pipeline_emit.hpp"
#include "tooling/cli/provider/provider_pipeline_cache.hpp"

#include "pipeline/persistence/durable_store_import/adapter_execution.hpp"
#include "pipeline/persistence/durable_store_import/decision.hpp"
#include "pipeline/persistence/durable_store_import/provider/binding/adapter.hpp"
#include "pipeline/persistence/durable_store_import/provider/binding/driver.hpp"
#include "pipeline/persistence/durable_store_import/receipt.hpp"
#include "pipeline/persistence/durable_store_import/receipt_persistence.hpp"
#include "pipeline/persistence/durable_store_import/receipt_persistence_response.hpp"
#include "pipeline/persistence/durable_store_import/request.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {
namespace {

namespace dsi = ahfl::durable_store_import;

[[nodiscard]] std::optional<dsi::AdapterExecutionReceipt>
build_adapter_execution_for_provider_cli(const ahfl::ir::Program &program,
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

    const auto request = unwrap_cli_result(dsi::build_request(*store_import_descriptor),
                                           &dsi::RequestResult::request);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision =
        unwrap_cli_result(dsi::build_decision(*request), &dsi::DecisionResult::decision);
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        unwrap_cli_result(dsi::build_receipt(*decision), &dsi::ReceiptResult::receipt);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto persistence_request = unwrap_cli_result(dsi::build_persistence_request(*receipt),
                                                       &dsi::PersistenceRequestResult::request);
    if (!persistence_request.has_value()) {
        return std::nullopt;
    }

    const auto persistence_response =
        unwrap_cli_result(dsi::build_persistence_response(*persistence_request),
                          &dsi::PersistenceResponseResult::response);
    if (!persistence_response.has_value()) {
        return std::nullopt;
    }

    return unwrap_cli_result(dsi::build_adapter_execution_receipt(*persistence_response),
                             &dsi::AdapterExecutionResult::receipt);
}

} // namespace

std::optional<ahfl::durable_store_import::ProviderWriteAttemptPreview>
build_provider_write_attempt_for_cli(const ahfl::ir::Program &program,
                                     const ahfl::handoff::PackageMetadata &metadata,
                                     const ahfl::dry_run::CapabilityMockSet &mock_set,
                                     const CommandLineOptions &options,
                                     std::string_view command_name,
                                     ProviderPipelineCache & /*cache*/) {
    const auto execution = build_adapter_execution_for_provider_cli(
        program, metadata, mock_set, options, command_name);
    if (!execution.has_value()) {
        return std::nullopt;
    }

    return unwrap_cli_result(dsi::build_provider_write_attempt_preview(*execution),
                             &dsi::ProviderWriteAttemptResult::preview);
}

std::optional<ahfl::durable_store_import::ProviderRecoveryHandoffPreview>
build_provider_recovery_handoff_for_cli(const ahfl::ir::Program & /*program*/,
                                        const ahfl::handoff::PackageMetadata & /*metadata*/,
                                        const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                        const CommandLineOptions & /*options*/,
                                        std::string_view /*command_name*/,
                                        ProviderPipelineCache &cache) {
    const auto *write_attempt = cache.get_WriteAttempt();
    if (write_attempt == nullptr) {
        return std::nullopt;
    }

    return unwrap_cli_result(dsi::build_provider_recovery_handoff_preview(*write_attempt),
                             &dsi::ProviderRecoveryHandoffResult::preview);
}

std::optional<ahfl::durable_store_import::ProviderDriverBindingPlan>
build_provider_driver_binding_for_cli(const ahfl::ir::Program & /*program*/,
                                      const ahfl::handoff::PackageMetadata & /*metadata*/,
                                      const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                      const CommandLineOptions & /*options*/,
                                      std::string_view /*command_name*/,
                                      ProviderPipelineCache &cache) {
    const auto *write_attempt = cache.get_WriteAttempt();
    if (write_attempt == nullptr) {
        return std::nullopt;
    }

    return unwrap_cli_result(dsi::build_provider_driver_binding_plan(*write_attempt),
                             &dsi::ProviderDriverBindingPlanResult::plan);
}

std::optional<ahfl::durable_store_import::ProviderDriverReadinessReview>
build_provider_driver_readiness_for_cli(const ahfl::ir::Program & /*program*/,
                                        const ahfl::handoff::PackageMetadata & /*metadata*/,
                                        const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                        const CommandLineOptions & /*options*/,
                                        std::string_view /*command_name*/,
                                        ProviderPipelineCache &cache) {
    const auto *plan = cache.get_DriverBinding();
    if (plan == nullptr) {
        return std::nullopt;
    }

    return unwrap_cli_result(dsi::build_provider_driver_readiness_review(*plan),
                             &dsi::ProviderDriverReadinessReviewResult::review);
}

} // namespace ahfl::cli
