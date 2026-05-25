#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/ir/ir.hpp"

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

struct AssuranceBundle {
    std::string format_version{std::string(kAssuranceBundleFormatVersion)};
    std::string status;
    std::vector<CapabilityAssurance> capabilities;
    std::vector<FlowEffectSummary> flow_effects;
    std::vector<AssuranceObligation> policy_obligations;
    std::vector<AssuranceObligation> recovery_obligations;
    FormalModelProfile formal_model_profile;
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
