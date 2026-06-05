#include "compiler/assurance/assurance.hpp"

#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "base/support/json.hpp"
#include "base/support/overloaded.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace ahfl::assurance {
namespace {

[[nodiscard]] std::string_view effect_kind_name(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Unknown:
        return "unknown";
    case ir::CapabilityEffectKind::Read:
        return "read";
    case ir::CapabilityEffectKind::ExternalSideEffect:
        return "external_side_effect";
    case ir::CapabilityEffectKind::DurableWrite:
        return "durable_write";
    case ir::CapabilityEffectKind::FinancialWrite:
        return "financial_write";
    }

    return "unknown";
}

[[nodiscard]] std::string_view receipt_mode_name(ir::CapabilityReceiptMode mode) {
    switch (mode) {
    case ir::CapabilityReceiptMode::None:
        return "none";
    case ir::CapabilityReceiptMode::Optional:
        return "optional";
    case ir::CapabilityReceiptMode::Required:
        return "required";
    }

    return "none";
}

[[nodiscard]] std::string_view retry_mode_name(ir::CapabilityRetryMode mode) {
    switch (mode) {
    case ir::CapabilityRetryMode::Unsafe:
        return "unsafe";
    case ir::CapabilityRetryMode::SafeIfIdempotent:
        return "safe_if_idempotent";
    case ir::CapabilityRetryMode::Safe:
        return "safe";
    }

    return "unsafe";
}

[[nodiscard]] bool is_external_effect(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::ExternalSideEffect ||
           kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
}

[[nodiscard]] bool is_durable_effect(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
}

[[nodiscard]] bool has_policy_suffix(const std::vector<std::string> &policies,
                                     std::string_view suffix) {
    return std::ranges::any_of(policies, [&](const std::string &policy) {
        return policy == suffix ||
               (policy.size() > suffix.size() &&
                policy.compare(policy.size() - suffix.size(), suffix.size(), suffix) == 0);
    });
}

void push_unique(std::vector<std::string> &values, std::string value) {
    if (std::ranges::find(values, value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void push_obligation(std::vector<AssuranceObligation> &obligations,
                     std::string target,
                     std::string kind,
                     std::string enforcement,
                     std::string reason,
                     bool satisfied) {
    obligations.push_back(AssuranceObligation{
        .target = std::move(target),
        .kind = std::move(kind),
        .enforcement = std::move(enforcement),
        .reason = std::move(reason),
        .satisfied = satisfied,
    });
}

void push_violation(std::vector<AssuranceViolation> &violations,
                    std::string scope,
                    std::string target,
                    std::string kind,
                    std::string message) {
    violations.push_back(AssuranceViolation{
        .scope = std::move(scope),
        .target = std::move(target),
        .kind = std::move(kind),
        .message = std::move(message),
    });
}

[[nodiscard]] bool is_suffix_match(std::string_view value, std::string_view suffix) {
    return value == suffix || (value.size() > suffix.size() + 2 &&
                               value.substr(value.size() - suffix.size() - 2, 2) == "::" &&
                               value.substr(value.size() - suffix.size()) == suffix);
}

[[nodiscard]] const ir::CapabilityDecl *
find_capability(const std::map<std::string, const ir::CapabilityDecl *> &capabilities,
                std::string_view name) {
    if (const auto it = capabilities.find(std::string(name)); it != capabilities.end()) {
        return it->second;
    }

    for (const auto &[capability_name, capability] : capabilities) {
        if (is_suffix_match(capability_name, name)) {
            return capability;
        }
    }

    return nullptr;
}

[[nodiscard]] CapabilityAssurance
analyze_capability(const ir::CapabilityDecl &capability,
                   std::vector<AssuranceObligation> &policy_obligations,
                   std::vector<AssuranceObligation> &recovery_obligations) {
    const auto &effect = capability.effect;
    CapabilityAssurance result{
        .name = capability.name,
        .effect_declared = effect.declared,
        .effect_kind = std::string(effect_kind_name(effect.kind)),
        .domain = effect.domain,
        .idempotency_key = effect.idempotency_key,
        .receipt_mode = std::string(receipt_mode_name(effect.receipt_mode)),
        .retry_mode = std::string(retry_mode_name(effect.retry_mode)),
        .timeout = effect.timeout,
        .compensation = effect.compensation,
        .policies = effect.policies,
        .production_blockers = {},
    };

    if (!effect.declared) {
        result.production_blockers.push_back("missing_effect_spec");
        push_obligation(policy_obligations,
                        capability.name,
                        "effect_profile_declaration",
                        "compiler_reject_or_gate_release",
                        "production assurance requires every capability side effect to be declared",
                        false);
        return result;
    }

    if (effect.kind == ir::CapabilityEffectKind::Unknown) {
        result.production_blockers.push_back("unknown_effect_kind");
        push_obligation(policy_obligations,
                        capability.name,
                        "effect_profile_declaration",
                        "compiler_reject_or_gate_release",
                        "unknown effect cannot be lowered into a verifiable control model",
                        false);
    }

    if (is_external_effect(effect.kind)) {
        push_obligation(policy_obligations,
                        capability.name,
                        "audit_event",
                        "runtime_policy",
                        "external effects must be observable in the assurance bundle",
                        has_policy_suffix(effect.policies, "audit_event"));
    }

    if (is_durable_effect(effect.kind)) {
        const auto has_idempotency = effect.idempotency_key.has_value();
        const auto has_required_receipt =
            effect.receipt_mode == ir::CapabilityReceiptMode::Required;

        if (!has_idempotency) {
            result.production_blockers.push_back("missing_idempotency_key");
        }
        if (!has_required_receipt) {
            result.production_blockers.push_back("missing_required_receipt");
        }

        push_obligation(policy_obligations,
                        capability.name,
                        "provider_receipt",
                        "runtime_policy",
                        "durable writes must produce receipts for replay and recovery",
                        has_required_receipt);
        push_obligation(recovery_obligations,
                        capability.name,
                        "idempotency_guard",
                        "compiler_contract_and_runtime_policy",
                        "durable effects must be retryable without duplicate external writes",
                        has_idempotency);
    }

    if (effect.kind == ir::CapabilityEffectKind::FinancialWrite) {
        const auto has_approval = has_policy_suffix(effect.policies, "approval_required");
        const auto has_compensation = effect.compensation.has_value();

        if (!has_approval) {
            result.production_blockers.push_back("missing_financial_approval_policy");
        }
        if (!has_compensation) {
            result.production_blockers.push_back("missing_financial_compensation");
        }

        push_obligation(policy_obligations,
                        capability.name,
                        "approval_required",
                        "runtime_policy",
                        "financial side effects require an explicit approval gate",
                        has_approval);
        push_obligation(recovery_obligations,
                        capability.name,
                        "compensation_path",
                        "capability_contract",
                        "financial side effects require a named compensating action",
                        has_compensation);
    }

    if (effect.retry_mode == ir::CapabilityRetryMode::SafeIfIdempotent &&
        !effect.idempotency_key.has_value()) {
        result.production_blockers.push_back("retry_safe_if_idempotent_without_key");
        push_obligation(recovery_obligations,
                        capability.name,
                        "retry_idempotency_proof",
                        "compiler_contract",
                        "safe_if_idempotent retry mode must name an idempotency key",
                        false);
    }

    return result;
}

[[nodiscard]] FormalModelProfile build_formal_model_profile(const AssuranceBundle &bundle) {
    bool has_effect_events = false;
    bool requires_checkpoint = false;
    bool requires_recovery = false;

    for (const auto &summary : bundle.flow_effects) {
        has_effect_events = has_effect_events || !summary.effect_kinds.empty();
        requires_checkpoint = requires_checkpoint || summary.requires_checkpoint;
        requires_recovery = requires_recovery || summary.requires_recovery;
    }

    std::string profile_name = "finite_control_system_v0";
    if (requires_recovery) {
        profile_name = "finite_effect_recovery_control_v0";
    } else if (has_effect_events) {
        profile_name = "finite_effect_control_v0";
    }

    FormalModelProfile profile{
        .profile = std::move(profile_name),
        .selected_backend = "smv",
        .included =
            {
                "agent_state_space",
                "flow_transition_graph",
                "workflow_dag_dependencies",
                "workflow_node_lifecycle_phase",
                "compiler_generated_lifecycle_final_dependency_obligations",
                "temporal_control_observations_called_in_state_running_completed",
                "bounded_boolean_integer_data_predicates",
                "unbounded_data_observation_assumptions",
                "counterexample_source_mapping",
            },
        .abstracted =
            {
                "provider_implementation",
                "capability_return_values",
                "unbounded_or_provider_data_predicates",
                "external_system_state",
                "provider_failure_injection_semantics",
            },
        .runtime_monitored = {},
        .unsupported =
            {
                "full_provider_semantics",
                "unbounded_data_domain_model_checking",
                "distributed_fairness_proof",
            },
    };

    if (has_effect_events) {
        push_unique(profile.included, "flow_call_events");
        push_unique(profile.included, "capability_effect_events");
        push_unique(profile.included, "call_effect_commit_failure_events");
        push_unique(profile.included, "effect_policy_obligation_checks");
        push_unique(profile.included, "capability_effect_classes");
    }

    if (requires_checkpoint) {
        push_unique(profile.runtime_monitored, "provider_receipts");
        push_unique(profile.runtime_monitored, "checkpoint_records");
        push_unique(profile.runtime_monitored, "audit_events");
        push_unique(profile.runtime_monitored, "failure_events");
    }

    if (requires_recovery) {
        push_unique(profile.included, "recovery_failure_lifecycle_hooks");
        push_unique(profile.included, "generic_failure_environment_inputs");
    }

    for (const auto &obligation : bundle.policy_obligations) {
        if (!obligation.satisfied) {
            push_unique(profile.runtime_monitored, obligation.kind);
        }
    }
    for (const auto &obligation : bundle.recovery_obligations) {
        if (!obligation.satisfied) {
            push_unique(profile.runtime_monitored, obligation.kind);
        }
    }

    if (bundle.status != "ready") {
        push_unique(profile.unsupported, "production_assurance_until_blockers_are_resolved");
    }

    return profile;
}

class AssuranceJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit AssuranceJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const AssuranceBundle &bundle) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(bundle.format_version); });
            field("status", [&]() { write_string(bundle.status); });
            field("capabilities", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &capability : bundle.capabilities) {
                        item([&]() { print_capability(capability, 2); });
                    }
                });
            });
            field("flow_effects", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &summary : bundle.flow_effects) {
                        item([&]() { print_flow(summary, 2); });
                    }
                });
            });
            field("policy_obligations", [&]() { print_obligations(bundle.policy_obligations, 1); });
            field("recovery_obligations",
                  [&]() { print_obligations(bundle.recovery_obligations, 1); });
            field("formal_model_profile",
                  [&]() { print_formal_model_profile(bundle.formal_model_profile, 1); });
        });
        out_ << '\n';
    }

  private:
    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_null() {
        out_ << "null";
    }

    void write_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }

        write_null();
    }

    void write_string_array(const std::vector<std::string> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &value : values) {
                item([&]() { write_string(value); });
            }
        });
    }

    void print_capability(const CapabilityAssurance &capability, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(capability.name); });
            field("effect_declared", [&]() { write_bool(capability.effect_declared); });
            field("effect_kind", [&]() { write_string(capability.effect_kind); });
            field("domain", [&]() { write_optional_string(capability.domain); });
            field("idempotency_key", [&]() { write_optional_string(capability.idempotency_key); });
            field("receipt_mode", [&]() { write_string(capability.receipt_mode); });
            field("retry_mode", [&]() { write_string(capability.retry_mode); });
            field("timeout", [&]() { write_optional_string(capability.timeout); });
            field("compensation", [&]() { write_optional_string(capability.compensation); });
            field("policies", [&]() { write_string_array(capability.policies, indent_level + 1); });
            field("production_blockers",
                  [&]() { write_string_array(capability.production_blockers, indent_level + 1); });
        });
    }

    void print_flow(const FlowEffectSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("flow_target", [&]() { write_string(summary.flow_target); });
            field("state", [&]() { write_string(summary.state); });
            field("called_targets",
                  [&]() { write_string_array(summary.called_targets, indent_level + 1); });
            field("effect_kinds",
                  [&]() { write_string_array(summary.effect_kinds, indent_level + 1); });
            field("requires_checkpoint", [&]() { write_bool(summary.requires_checkpoint); });
            field("requires_recovery", [&]() { write_bool(summary.requires_recovery); });
            field("production_blockers",
                  [&]() { write_string_array(summary.production_blockers, indent_level + 1); });
        });
    }

    void print_obligation(const AssuranceObligation &obligation, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("target", [&]() { write_string(obligation.target); });
            field("kind", [&]() { write_string(obligation.kind); });
            field("enforcement", [&]() { write_string(obligation.enforcement); });
            field("reason", [&]() { write_string(obligation.reason); });
            field("satisfied", [&]() { write_bool(obligation.satisfied); });
        });
    }

    void print_obligations(const std::vector<AssuranceObligation> &obligations, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &obligation : obligations) {
                item([&]() { print_obligation(obligation, indent_level + 1); });
            }
        });
    }

    void print_formal_model_profile(const FormalModelProfile &profile, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("profile", [&]() { write_string(profile.profile); });
            field("selected_backend", [&]() { write_string(profile.selected_backend); });
            field("included", [&]() { write_string_array(profile.included, indent_level + 1); });
            field("abstracted",
                  [&]() { write_string_array(profile.abstracted, indent_level + 1); });
            field("runtime_monitored",
                  [&]() { write_string_array(profile.runtime_monitored, indent_level + 1); });
            field("unsupported",
                  [&]() { write_string_array(profile.unsupported, indent_level + 1); });
        });
    }
};

} // namespace

AssuranceBundle build_assurance_bundle(const ir::Program &program) {
    AssuranceBundle bundle;
    std::map<std::string, const ir::CapabilityDecl *> capability_index;
    std::map<std::string, CapabilityAssurance> capability_assurance_index;

    for (const auto &declaration : program.declarations) {
        if (const auto *capability = std::get_if<ir::CapabilityDecl>(&declaration)) {
            capability_index.emplace(capability->name, capability);
        }
    }

    for (const auto &[name, capability] : capability_index) {
        auto assurance =
            analyze_capability(*capability, bundle.policy_obligations, bundle.recovery_obligations);
        capability_assurance_index.emplace(name, assurance);
        bundle.capabilities.push_back(std::move(assurance));
    }

    for (const auto &declaration : program.declarations) {
        const auto *flow = std::get_if<ir::FlowDecl>(&declaration);
        if (flow == nullptr) {
            continue;
        }

        const auto flow_target = std::string(ir::symbol_canonical_name(flow->target_ref));
        for (const auto &handler : flow->state_handlers) {
            const auto *handler_summary = ir::find_state_handler_summary(program, *flow, handler);
            FlowEffectSummary summary{
                .flow_target = flow_target,
                .state = handler.state_name,
                .called_targets = handler_summary == nullptr ? std::vector<std::string>{}
                                                             : handler_summary->called_targets,
                .effect_kinds = {},
                .requires_checkpoint = false,
                .requires_recovery = false,
                .production_blockers = {},
            };

            for (const auto &target : summary.called_targets) {
                const auto *capability = find_capability(capability_index, target);
                if (capability == nullptr) {
                    push_unique(summary.production_blockers,
                                "unresolved_capability_call:" + target);
                    continue;
                }

                push_unique(summary.effect_kinds,
                            std::string(effect_kind_name(capability->effect.kind)));
                summary.requires_checkpoint =
                    summary.requires_checkpoint || is_external_effect(capability->effect.kind);
                summary.requires_recovery =
                    summary.requires_recovery || is_durable_effect(capability->effect.kind);

                if (const auto assurance_it = capability_assurance_index.find(capability->name);
                    assurance_it != capability_assurance_index.end()) {
                    for (const auto &blocker : assurance_it->second.production_blockers) {
                        push_unique(summary.production_blockers, capability->name + ":" + blocker);
                    }
                }
            }

            bundle.flow_effects.push_back(std::move(summary));
        }
    }

    const auto has_blockers =
        std::ranges::any_of(bundle.capabilities,
                            [](const CapabilityAssurance &capability) {
                                return !capability.production_blockers.empty();
                            }) ||
        std::ranges::any_of(bundle.flow_effects, [](const FlowEffectSummary &summary) {
            return !summary.production_blockers.empty();
        });
    bundle.status = has_blockers ? "blocked" : "ready";
    bundle.formal_model_profile = build_formal_model_profile(bundle);
    return bundle;
}

void print_assurance_bundle_json(const AssuranceBundle &bundle, std::ostream &out) {
    AssuranceJsonPrinter(out).print(bundle);
}

AssuranceValidationResult validate_assurance_bundle(const AssuranceBundle &bundle) {
    AssuranceValidationResult result;

    for (const auto &capability : bundle.capabilities) {
        for (const auto &blocker : capability.production_blockers) {
            push_violation(result.violations,
                           "capability",
                           capability.name,
                           blocker,
                           "capability blocks production assurance");
        }
    }

    for (const auto &summary : bundle.flow_effects) {
        for (const auto &blocker : summary.production_blockers) {
            push_violation(result.violations,
                           "flow",
                           summary.flow_target + "::" + summary.state,
                           blocker,
                           "flow state inherits an unresolved capability assurance blocker");
        }
    }

    for (const auto &obligation : bundle.policy_obligations) {
        if (!obligation.satisfied) {
            push_violation(result.violations,
                           "policy_obligation",
                           obligation.target,
                           obligation.kind,
                           obligation.reason);
        }
    }

    for (const auto &obligation : bundle.recovery_obligations) {
        if (!obligation.satisfied) {
            push_violation(result.violations,
                           "recovery_obligation",
                           obligation.target,
                           obligation.kind,
                           obligation.reason);
        }
    }

    if (result.violations.empty() && bundle.status != "ready") {
        push_violation(result.violations,
                       "bundle",
                       "assurance",
                       "status_" + bundle.status,
                       "assurance bundle status is not ready");
    }

    result.ok = result.violations.empty();
    return result;
}

void print_program_assurance_json(const ir::Program &program, std::ostream &out) {
    print_assurance_bundle_json(build_assurance_bundle(program), out);
}

void print_assurance_validation_report(const AssuranceValidationResult &result, std::ostream &out) {
    if (result.ok) {
        out << "ok: assurance validation ready\n";
        return;
    }

    out << "error: assurance validation failed with " << result.violations.size()
        << " violation(s)\n";
    for (const auto &violation : result.violations) {
        out << violation.scope << ' ' << violation.target << ": " << violation.kind << " - "
            << violation.message << '\n';
    }
}

} // namespace ahfl::assurance
