#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/receipt_persistence_response.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kAdapterExecutionFormatVersion =
    "ahfl.durable-store-import-adapter-execution.v1";

inline constexpr std::string_view kLocalFakeDurableStoreContractVersion =
    "ahfl.local-fake-durable-store.v1";

enum class AdapterExecutionStoreKind {
    LocalFakeDurableStore,
};

enum class StoreMutationIntentKind {
    PersistReceipt,
    NoopBlocked,
    NoopDeferred,
    NoopRejected,
};

enum class StoreMutationStatus {
    Persisted,
    NotMutated,
};

enum class AdapterExecutionFailureKind {
    SourceResponseBlocked,
    SourceResponseDeferred,
    SourceResponseRejected,
};

struct StoreMutationIntent {
    StoreMutationIntentKind kind{StoreMutationIntentKind::NoopBlocked};
    std::string source_response_identity;
    std::string target_receipt_identity;
    std::string target_planned_durable_identity;
    bool mutates_store{false};
};

struct AdapterExecutionFailureAttribution {
    AdapterExecutionFailureKind kind{AdapterExecutionFailureKind::SourceResponseBlocked};
    std::string message;
    std::optional<AdapterCapabilityKind> required_capability;
};

struct AdapterExecutionReceipt {
    std::string format_version{std::string(kAdapterExecutionFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_response_format_version{
        std::string(kPersistenceResponseFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_request_format_version{
        std::string(kPersistenceRequestFormatVersion)};
    std::string source_durable_store_import_decision_receipt_format_version{
        std::string(kReceiptFormatVersion)};
    std::string source_durable_store_import_decision_format_version{
        std::string(kDecisionFormatVersion)};
    std::string source_durable_store_import_request_format_version{
        std::string(kRequestFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_decision_identity;
    std::string durable_store_import_receipt_identity;
    std::string durable_store_import_receipt_persistence_request_identity;
    std::string durable_store_import_receipt_persistence_response_identity;
    std::string durable_store_import_adapter_execution_identity;
    std::string planned_durable_identity;
    PersistenceResponseStatus response_status{PersistenceResponseStatus::Blocked};
    PersistenceResponseOutcome response_outcome{PersistenceResponseOutcome::BlockBlockedRequest};
    PersistenceResponseBoundaryKind response_boundary_kind{
        PersistenceResponseBoundaryKind::LocalContractOnly};
    bool acknowledged_for_response{false};
    AdapterExecutionStoreKind store_kind{AdapterExecutionStoreKind::LocalFakeDurableStore};
    std::string local_fake_store_contract_version{
        std::string(kLocalFakeDurableStoreContractVersion)};
    StoreMutationIntent mutation_intent;
    StoreMutationStatus mutation_status{StoreMutationStatus::NotMutated};
    std::optional<std::string> persistence_id;
    std::optional<AdapterExecutionFailureAttribution> failure_attribution;
};

struct AdapterExecutionValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct AdapterExecutionResult {
    std::optional<AdapterExecutionReceipt> receipt;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] AdapterExecutionValidationResult
validate_adapter_execution_receipt(const AdapterExecutionReceipt &receipt);

[[nodiscard]] AdapterExecutionResult
build_adapter_execution_receipt(const PersistenceResponse &response);

} // namespace ahfl::durable_store_import
