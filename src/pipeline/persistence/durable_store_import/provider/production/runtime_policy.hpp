#pragma once

#include "ahfl/base/support/diagnostics.hpp"
#include "pipeline/persistence/durable_store_import/provider/configuration/config_bundle_validation.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/registry.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/approval_workflow.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/opt_in_guard.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/production_readiness.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Runtime Policy Report format version constant
inline constexpr std::string_view kProviderRuntimePolicyReportFormatVersion =
    "ahfl.durable-store-import-provider-runtime-policy-report.v1";

// Policy decision enum
enum class PolicyDecision {
    Permit,         // Execution allowed
    Deny,           // Denied (default)
    DenyWithWarning // Denied with a warning
};

// Policy violation type enum
enum class PolicyViolationType {
    OptInNotGranted,  // Opt-in not granted
    ApprovalMissing,  // Approval missing
    ConfigInvalid,    // Configuration invalid
    RegistryMismatch, // Registry mismatch
    ReadinessNotMet,  // Readiness check not met
    ScopeExceeded,    // Scope exceeded
    EvidenceStale,    // Evidence stale
};

// Result of a single policy gate check
struct PolicyGateResult {
    std::string gate_name; // "opt_in", "approval", "config", "registry", "readiness"
    bool passed{false};
    std::vector<PolicyViolationType> violations;
    std::string evidence_reference;
};

// Provider Runtime Policy Report - v0.49
// Aggregates all policy evidence and emits a unified permit / deny decision
struct ProviderRuntimePolicyReport {
    std::string format_version{std::string(kProviderRuntimePolicyReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Upstream artifact identity references
    std::string opt_in_decision_report_identity;
    std::string approval_receipt_identity;
    std::string config_validation_report_identity;
    std::string registry_selection_plan_identity;
    std::string production_readiness_evidence_identity;

    // Policy gates check results
    std::vector<PolicyGateResult> policy_gates;

    // Aggregated decision
    PolicyDecision decision{PolicyDecision::Deny};
    int gates_passed{0};
    int gates_failed{0};
    int blocking_violation_count{0};
    int warning_violation_count{0};

    // Aggregated summaries
    std::string policy_summary;
    std::string violation_summary;
    std::string next_operator_action;

    // Safe default: false
    bool is_execution_permitted{false};
};

// Validation result
struct ProviderRuntimePolicyReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build result
struct ProviderRuntimePolicyReportResult {
    std::optional<ProviderRuntimePolicyReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Validation function
[[nodiscard]] ProviderRuntimePolicyReportValidationResult
validate_provider_runtime_policy_report(const ProviderRuntimePolicyReport &report);

// Build function - aggregates all policy evidence
[[nodiscard]] ProviderRuntimePolicyReportResult
build_provider_runtime_policy_report(const ProviderOptInDecisionReport &opt_in_report,
                                     const ApprovalReceipt &approval_receipt,
                                     const ProviderConfigBundleValidationReport &config_report,
                                     const ProviderSelectionPlan &selection_plan,
                                     const ProviderProductionReadinessEvidence &readiness_evidence);

// Helper function: convert PolicyDecision to a string
[[nodiscard]] std::string_view to_string_view(PolicyDecision decision);

// Helper function: convert PolicyViolationType to a string
[[nodiscard]] std::string_view to_string_view(PolicyViolationType violation);

} // namespace ahfl::durable_store_import
