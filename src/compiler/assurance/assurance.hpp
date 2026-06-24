#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::assurance {

inline constexpr std::string_view kAssuranceBundleFormatVersion = "ahfl.assurance-bundle.v0.1";

struct AssuranceObligation {
    std::string target;
    std::string kind;
    std::string enforcement;
    std::string reason;
    bool satisfied{false};
};

struct CapabilityAssurance {
    std::string name;
    bool effect_declared{false};
    std::string effect_kind;
    std::optional<std::string> domain;
    std::optional<std::string> idempotency_key;
    std::string receipt_mode;
    std::string retry_mode;
    std::optional<std::string> timeout;
    std::optional<std::string> compensation;
    std::vector<std::string> policies;
    std::vector<std::string> production_blockers;
};

struct FlowEffectSummary {
    std::string flow_target;
    std::string state;
    std::vector<std::string> called_targets;
    std::vector<std::string> effect_kinds;
    bool requires_checkpoint{false};
    bool requires_recovery{false};
    std::vector<std::string> production_blockers;
};

struct FormalModelProfile {
    std::string profile;
    std::string selected_backend;
    std::vector<std::string> included;
    std::vector<std::string> abstracted;
    std::vector<std::string> runtime_monitored;
    std::vector<std::string> unsupported;
};

/// Status of a decreases termination-measure obligation.
enum class VerificationObligationStatus {
    Recognized,   // Pattern matches a supported MVP well-founded shape
    Wildcard,     // The obligation comes from `decreases *` (no concrete measure)
    Unrecognized, // The expression was not classified as a supported MVP pattern
};

/// Single verification obligation derived from a decreases clause.
struct VerificationObligation {
    /// Human-readable rendering of the decreases measure expression, or "*"
    /// when the clause used the wildcard form.
    std::string decreases_pattern;
    /// True iff the classifier determined the measure has a compiler-known
    /// well-founded semantics (bounded-rank container size, integer counter,
    /// explicit wildcard accepted under non-recursive scope, …).
    bool well_founded{false};
    /// Stable string-encoded source location (file:line:col) when available;
    /// empty string otherwise.
    std::string location;
    /// Classifier status for this obligation.
    VerificationObligationStatus status{VerificationObligationStatus::Unrecognized};
};

/// Container-level verification profile surfaced by the assurance bundle.
struct VerificationProfile {
    /// One entry per decreases measure, in source order across all contracts.
    /// Wildcard clauses contribute a single entry; a concrete lexical-order
    /// clause with N terms contributes N entries.
    std::vector<VerificationObligation> obligations;
};

struct AssuranceBundle {
    std::string format_version{std::string(kAssuranceBundleFormatVersion)};
    std::string status;
    std::vector<CapabilityAssurance> capabilities;
    std::vector<FlowEffectSummary> flow_effects;
    std::vector<AssuranceObligation> policy_obligations;
    std::vector<AssuranceObligation> recovery_obligations;
    FormalModelProfile formal_model_profile;
    VerificationProfile verification;
};

struct AssuranceViolation {
    std::string scope;
    std::string target;
    std::string kind;
    std::string message;
};

struct AssuranceValidationResult {
    bool ok{true};
    std::vector<AssuranceViolation> violations;
};

[[nodiscard]] AssuranceBundle build_assurance_bundle(const ir::Program &program);
[[nodiscard]] AssuranceValidationResult validate_assurance_bundle(const AssuranceBundle &bundle);

void print_assurance_bundle_json(const AssuranceBundle &bundle, std::ostream &out);
void print_program_assurance_json(const ir::Program &program, std::ostream &out);
void print_assurance_validation_report(const AssuranceValidationResult &result, std::ostream &out);

} // namespace ahfl::assurance
