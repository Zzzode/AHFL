#include "tooling/cli/pipeline_runner.hpp"

#include "pipeline_context.hpp"
#include "pipeline_core_commands.hpp"
#include "provider/pipeline_durable_store_import.hpp"

#include <array>
#include <iostream>
#include <optional>

namespace ahfl::cli {
namespace {

// O(1) dispatch table indexed by CommandKind enum value.
constexpr auto kDurableStoreImportCommandHandlers = [] {
    std::array<PackageCommandHandler, static_cast<std::size_t>(CommandKind::_Count)> table{};

    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportRequest)] =
        invoke_package_command<emit_durable_store_import_request_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReview)] =
        invoke_package_command<emit_durable_store_import_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportDecision)] =
        invoke_package_command<emit_durable_store_import_decision_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceipt)] =
        invoke_package_command<emit_durable_store_import_receipt_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceRequest)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_request_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportDecisionReview)] =
        invoke_package_command<emit_durable_store_import_decision_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptReview)] =
        invoke_package_command<emit_durable_store_import_receipt_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceReview)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceResponse)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_response_with_diagnostics>;
    table[static_cast<std::size_t>(
        CommandKind::EmitDurableStoreImportReceiptPersistenceResponseReview)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_response_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportAdapterExecution)] =
        invoke_package_command<emit_durable_store_import_adapter_execution_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportRecoveryPreview)] =
        invoke_package_command<emit_durable_store_import_recovery_preview_with_diagnostics>;

    return table;
}();

[[nodiscard]] std::optional<int>
dispatch_package_command_impl(CommandKind command, const PackagePipelineContext &context) {
    const auto idx = static_cast<std::size_t>(command);
    if (idx >= kDurableStoreImportCommandHandlers.size()) {
        return std::nullopt;
    }
    const auto handler = kDurableStoreImportCommandHandlers[idx];
    if (handler == nullptr) {
        return std::nullopt;
    }
    return handler(context);
}

} // namespace

bool handles_package_command(CommandKind command) {
    const auto idx = static_cast<std::size_t>(command);
    return handles_core_pipeline_command(command) ||
           (idx < kDurableStoreImportCommandHandlers.size() &&
            kDurableStoreImportCommandHandlers[idx] != nullptr);
}

std::optional<int> dispatch_package_command(CommandKind command,
                                            const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet *mock_set,
                                            const CommandLineOptions &options) {
    const PackagePipelineContext context{
        program,
        metadata,
        mock_set,
        options,
        {},
    };
    if (auto result = dispatch_core_pipeline_command(command, context); result.has_value()) {
        return result;
    }
    return dispatch_package_command_impl(command, context);
}

} // namespace ahfl::cli
