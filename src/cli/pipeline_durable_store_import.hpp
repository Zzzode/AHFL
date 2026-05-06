#pragma once

#include "ahfl/cli/command_catalog.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/durable_store_import/adapter_execution.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/provider_adapter.hpp"
#include "ahfl/durable_store_import/provider_driver.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response_review.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/recovery_preview.hpp"
#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/review.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

// --- build_*_for_cli functions ---

[[nodiscard]] std::optional<ahfl::durable_store_import::Request>
build_durable_store_import_request_for_cli(const ahfl::ir::Program &program,
                                           const ahfl::handoff::PackageMetadata &metadata,
                                           const ahfl::dry_run::CapabilityMockSet &mock_set,
                                           const CommandLineOptions &options,
                                           std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::Decision>
build_durable_store_import_decision_for_cli(const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet &mock_set,
                                            const CommandLineOptions &options,
                                            std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::Receipt>
build_durable_store_import_receipt_for_cli(const ahfl::ir::Program &program,
                                           const ahfl::handoff::PackageMetadata &metadata,
                                           const ahfl::dry_run::CapabilityMockSet &mock_set,
                                           const CommandLineOptions &options,
                                           std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceRequest>
build_durable_store_import_receipt_persistence_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ReceiptReviewSummary>
build_durable_store_import_receipt_review_for_cli(const ahfl::ir::Program &program,
                                                  const ahfl::handoff::PackageMetadata &metadata,
                                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                  const CommandLineOptions &options,
                                                  std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceReviewSummary>
build_durable_store_import_receipt_persistence_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceResponse>
build_durable_store_import_receipt_persistence_response_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::PersistenceResponseReviewSummary>
build_durable_store_import_receipt_persistence_response_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::AdapterExecutionReceipt>
build_durable_store_import_adapter_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::RecoveryCommandPreview>
build_durable_store_import_recovery_preview_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteAttemptPreview>
build_durable_store_import_provider_write_attempt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRecoveryHandoffPreview>
build_durable_store_import_provider_recovery_handoff_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderDriverBindingPlan>
build_durable_store_import_provider_driver_binding_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderDriverReadinessReview>
build_durable_store_import_provider_driver_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

// --- emit_*_with_diagnostics functions ---

[[nodiscard]] int
emit_durable_store_import_request_with_diagnostics(const ahfl::ir::Program &program,
                                                   const ahfl::handoff::PackageMetadata &metadata,
                                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                   const CommandLineOptions &options);

[[nodiscard]] int
emit_durable_store_import_review_with_diagnostics(const ahfl::ir::Program &program,
                                                  const ahfl::handoff::PackageMetadata &metadata,
                                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                  const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_decision_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_decision_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int
emit_durable_store_import_receipt_with_diagnostics(const ahfl::ir::Program &program,
                                                   const ahfl::handoff::PackageMetadata &metadata,
                                                   const ahfl::dry_run::CapabilityMockSet &mock_set,
                                                   const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_receipt_persistence_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_receipt_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_receipt_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_receipt_persistence_response_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_receipt_persistence_response_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_adapter_execution_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_recovery_preview_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_provider_write_attempt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_provider_recovery_handoff_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_provider_driver_binding_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

[[nodiscard]] int emit_durable_store_import_provider_driver_readiness_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options);

} // namespace ahfl::cli
