#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "pipeline/persistence/durable_store_import/provider/configuration/config_bundle_validation.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/conformance.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/schema_compatibility.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/approval_workflow.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/opt_in_guard.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/release_evidence_archive.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/runtime_policy.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Production Integration Dry Run Report format version constant
inline constexpr std::string_view kProviderProductionIntegrationDryRunReportFormatVersion =
    "ahfl.durable-store-import-provider-production-integration-dry-run-report.v1";

// Production Readiness State enum
enum class ProductionReadinessState {
    ReadyForControlledRollout, // Fully ready, can enter controlled rollout
    ReadyWithConditions,       // Conditionally ready, requires addressing specified conditions
    Blocked,                   // Blocked, blocking items exist
    InsufficientEvidence,      // Insufficient evidence, missing required evidence
};

// Evidence Chain Item - status description of a single evidence
struct EvidenceChainItem {
    std::string evidence_type;     // "conformance", "schema_compatibility", "config_validation",
                                   // "release_archive", "approval", "opt_in", "runtime_policy"
    std::string evidence_identity; // referenced artifact identity
    std::string format_version;    // format_version of this evidence
    bool is_present{false};        // whether the evidence exists
    bool is_valid{false};          // whether the evidence passes validation
    bool is_fresh{true};           // whether the evidence is fresh (not expired)
};

// Blocking Item - blocker description
struct BlockingItem {
    std::string block_type;   // "missing_evidence", "invalid_evidence", "policy_violation", etc.
    std::string block_reason; // blocker reason description
    std::string responsible_artifact; // responsible artifact identifier
    std::string suggested_action;     // suggested unblocking action
};

// Next Operator Action - next-step action recommendation
struct NextOperatorAction {
    std::string action_type;        // "approve", "fix_config", "retry_evidence", "resolve_blocker"
    std::string action_description; // action description
    std::string action_target;      // action target (artifact identity or specific object)
    int priority{1};                // priority (1 = highest)
};

// Provider Production Integration Dry Run Report - v0.50
// Terminal node: aggregates the full evidence chain from v0.43-v0.49
struct ProviderProductionIntegrationDryRunReport {
    std::string format_version{
        std::string(kProviderProductionIntegrationDryRunReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // ===== Full evidence chain references =====
    // v0.43: Provider Conformance Report
    std::string conformance_report_identity;
    // v0.44: Provider Schema Compatibility Report
    std::string schema_compatibility_report_identity;
    // v0.45: Provider Config Bundle Validation Report
    std::string config_validation_report_identity;
    // v0.46: Release Evidence Archive Manifest
    std::string release_evidence_archive_manifest_identity;
    // v0.47: Approval Receipt
    std::string approval_receipt_identity;
    // v0.48: Provider Opt-In Decision Report
    std::string opt_in_decision_report_identity;
    // v0.49: Provider Runtime Policy Report
    std::string runtime_policy_report_identity;

    // ===== Evidence chain summary =====
    std::vector<EvidenceChainItem> evidence_chain;
    int total_evidence_count{7};
    int valid_evidence_count{0};
    int invalid_evidence_count{0};
    int missing_evidence_count{0};

    // ===== Readiness state =====
    ProductionReadinessState readiness_state{ProductionReadinessState::Blocked};

    // ===== Blocking summary =====
    std::vector<BlockingItem> blocking_items;
    int blocking_item_count{0};
    std::string blocking_summary;

    // ===== Next actions =====
    std::vector<NextOperatorAction> next_operator_actions;

    // ===== Final summary =====
    std::string dry_run_summary;
    // Safe default: false - do not auto-approve production integration
    bool is_ready_for_controlled_rollout{false};
    // Defaults to non-mutating mode
    bool is_non_mutating_mode{true};
    // Generation timestamp (ISO 8601)
    std::string generated_at;
};

// Validation result
struct ProviderProductionIntegrationDryRunReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build result
struct ProviderProductionIntegrationDryRunReportResult {
    std::optional<ProviderProductionIntegrationDryRunReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Validation function
[[nodiscard]] ProviderProductionIntegrationDryRunReportValidationResult
validate_provider_production_integration_dry_run_report(
    const ProviderProductionIntegrationDryRunReport &report);

// Build function - terminal aggregation: aggregates all evidence from v0.43-v0.49
[[nodiscard]] ProviderProductionIntegrationDryRunReportResult
build_provider_production_integration_dry_run_report(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ReleaseEvidenceArchiveManifest &evidence_archive,
    const ApprovalReceipt &approval_receipt,
    const ProviderOptInDecisionReport &opt_in_report,
    const ProviderRuntimePolicyReport &runtime_policy_report);

// Helper function: converts ProductionReadinessState to a string
[[nodiscard]] std::string_view to_string_view(ProductionReadinessState state);

} // namespace ahfl::durable_store_import
