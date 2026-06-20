#pragma once

#include "pipeline/persistence/durable_store_import/provider/production/approval_workflow.hpp"
#include "pipeline/persistence/durable_store_import/provider/configuration/config_bundle_validation.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/registry.hpp"
#include "ahfl/base/support/diagnostics.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::durable_store_import {

// Opt-In Decision Report format version constant
inline constexpr std::string_view kProviderOptInDecisionReportFormatVersion =
    "ahfl.durable-store-import-provider-opt-in-decision-report.v1";

// Opt-In decision enum
enum class OptInDecision {
    Allow,           // Allow real provider traffic
    Deny,            // Deny (default)
    DenyWithOverride // Deny with a scoped override
};

// Denial reason enum
enum class OptInDenialReason {
    NoApproval,       // Missing a valid approval
    ConfigInvalid,    // Configuration validation failed
    RegistryMismatch, // Provider registration info does not match
    ReadinessNotMet,  // Readiness check not satisfied
    ExplicitDeny,     // Explicit denial
    ScopeExceeded,    // Scope exceeded
};

// Gate check item
struct OptInGateCheck {
    std::string gate_name;
    bool passed{false};
    std::optional<OptInDenialReason> denial_reason;
    std::string evidence_reference;
};

// Scoped Override structure
struct ScopedOverride {
    std::string override_scope;
    std::string override_justification;
    std::string override_approver;
    bool is_valid{false};
};

// Opt-In Decision Report - v0.48
// Operator-facing opt-in decision report
struct ProviderOptInDecisionReport {
    std::string format_version{std::string(kProviderOptInDecisionReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Upstream artifact references
    std::string approval_receipt_identity;
    std::string config_validation_report_identity;
    std::string registry_selection_plan_identity;

    // Gate check results
    std::vector<OptInGateCheck> gate_checks;
    std::optional<ScopedOverride> scoped_override;

    // Final decision
    OptInDecision decision{OptInDecision::Deny};
    std::vector<OptInDenialReason> denial_reasons;

    // Summary
    int gates_passed{0};
    int gates_failed{0};
    std::string decision_summary;
    std::string denial_reason_summary;

    // Safe default: false
    bool is_real_provider_traffic_allowed{false};
};

// Validation result
struct ProviderOptInDecisionReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build result
struct ProviderOptInDecisionReportResult {
    std::optional<ProviderOptInDecisionReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Validation function
[[nodiscard]] ProviderOptInDecisionReportValidationResult
validate_provider_opt_in_decision_report(const ProviderOptInDecisionReport &report);

// Build function
// Build an OptInDecisionReport from ApprovalReceipt, ProviderConfigBundleValidationReport, and ProviderSelectionPlan
[[nodiscard]] ProviderOptInDecisionReportResult
build_provider_opt_in_decision_report(const ApprovalReceipt &approval_receipt,
                                      const ProviderConfigBundleValidationReport &config_report,
                                      const ProviderSelectionPlan &selection_plan);

// Helper function: convert OptInDecision to a string
[[nodiscard]] std::string_view to_string_view(OptInDecision decision);

// Helper function: convert OptInDenialReason to a string
[[nodiscard]] std::string_view to_string_view(OptInDenialReason reason);

} // namespace ahfl::durable_store_import
