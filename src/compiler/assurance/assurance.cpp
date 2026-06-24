#include "compiler/assurance/assurance.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "base/support/json.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

// ---------------------------------------------------------------------------
// Decreases pattern classifier (IR-level)
// ---------------------------------------------------------------------------
// Replicates the four MVP shapes supported by the AST-level recognizer but
// operates on the lowered IR so the assurance bundle can surface obligations
// without a dependency back into the semantic AST layer.
//
// Supported shapes mirror DecreasePatternKind:
//   1. LengthSelf  — self.length  (PathExpr or MemberAccessExpr)
//   2. SelfField   — self.<field> (PathExpr or MemberAccessExpr)
//   3. MinusOne    — <ident> - 1  (BinaryExpr Subtract)
//   4. Wildcard    — *            (synthesized from clause.decreases_wildcard)
//
// Anything else reports Unrecognized. The "well_founded" flag marks measures
// whose semantics the compiler knows how to discharge (either via the SMV
// bounded-rank encoding or by lexical-order convention on integer counters).
// ---------------------------------------------------------------------------

/// Render an IR path to "root.member1.member2" so decreases_pattern entries
/// read like the original source even when the measure only uses the lowered
/// IR representation. Mirrors the convention used by ir_print's render_path.
[[nodiscard]] std::string render_path(const ir::Path &path) {
    std::ostringstream builder;
    switch (path.root_kind) {
    case ir::PathRootKind::Identifier: builder << path.root_name; break;
    case ir::PathRootKind::Input:      builder << "input";         break;
    case ir::PathRootKind::Context:    builder << path.root_name;  break;
    case ir::PathRootKind::Output:     builder << "output";        break;
    case ir::PathRootKind::State:
    case ir::PathRootKind::Local:      builder << path.root_name;  break;
    }
    for (const auto &member : path.members) {
        builder << '.' << member;
    }
    return builder.str();
}

/// Render an IR expression into a compact human-readable string. Mirrors the
/// same pattern ir_print uses but scoped to pure decreases-subject shapes so
/// the obligation entries stay small.
[[nodiscard]] std::string render_decreases_subject(const ir::Expr &expr) {
    return std::visit(
        Overloaded{
            [](const ir::NoneLiteralExpr &) { return std::string("none"); },
            [](const ir::BoolLiteralExpr &v) { return std::string(v.value ? "true" : "false"); },
            [](const ir::IntegerLiteralExpr &v) { return v.spelling; },
            [](const ir::FloatLiteralExpr &v) { return v.spelling; },
            [](const ir::DecimalLiteralExpr &v) { return v.spelling; },
            [](const ir::StringLiteralExpr &v) { return v.spelling; },
            [](const ir::DurationLiteralExpr &v) { return v.spelling; },
            [&](const ir::SomeExpr &v) {
                return std::string("some(") +
                       (v.value ? render_decreases_subject(*v.value) : std::string("none")) + ")";
            },
            [](const ir::PathExpr &v) { return render_path(v.path); },
            [](const ir::QualifiedValueExpr &v) { return v.value; },
            [&](const ir::UnaryExpr &v) {
                const char *op = v.op == ir::ExprUnaryOp::Negate     ? "-"
                                 : v.op == ir::ExprUnaryOp::Positive ? "+"
                                                                     : "!";
                return std::string("(") + op +
                       (v.operand ? render_decreases_subject(*v.operand) : std::string("none")) +
                       ")";
            },
            [&](const ir::BinaryExpr &v) {
                const char *op = [&]() {
                    switch (v.op) {
                    case ir::ExprBinaryOp::Implies:      return "=>";
                    case ir::ExprBinaryOp::Or:           return "||";
                    case ir::ExprBinaryOp::And:          return "&&";
                    case ir::ExprBinaryOp::Equal:        return "==";
                    case ir::ExprBinaryOp::NotEqual:     return "!=";
                    case ir::ExprBinaryOp::Less:         return "<";
                    case ir::ExprBinaryOp::LessEqual:    return "<=";
                    case ir::ExprBinaryOp::Greater:      return ">";
                    case ir::ExprBinaryOp::GreaterEqual: return ">=";
                    case ir::ExprBinaryOp::Add:          return "+";
                    case ir::ExprBinaryOp::Subtract:     return "-";
                    case ir::ExprBinaryOp::Multiply:     return "*";
                    case ir::ExprBinaryOp::Divide:       return "/";
                    case ir::ExprBinaryOp::Modulo:       return "%";
                    }
                    return "?";
                }();
                return std::string("(") + (v.lhs ? render_decreases_subject(*v.lhs)
                                                 : std::string("none")) +
                       " " + op + " " +
                       (v.rhs ? render_decreases_subject(*v.rhs) : std::string("none")) + ")";
            },
            [&](const ir::MemberAccessExpr &v) {
                return (v.base ? render_decreases_subject(*v.base) : std::string("<none>")) +
                       "." + v.member;
            },
            [&](const ir::CallExpr &v) {
                std::ostringstream out;
                out << v.callee << "(";
                bool first = true;
                for (const auto &arg : v.arguments) {
                    if (!first) {
                        out << ", ";
                    }
                    first = false;
                    out << (arg ? render_decreases_subject(*arg) : std::string("none"));
                }
                out << ")";
                return out.str();
            },
            [](const auto &) { return std::string("<expr>"); },
        },
        expr.node);
}

/// True iff expr is a PathExpr whose root is exactly "self" with no members.
[[nodiscard]] bool is_self_path(const ir::Expr &expr) {
    const auto *path = std::get_if<ir::PathExpr>(&expr.node);
    if (path == nullptr) {
        return false;
    }
    return path->path.root_name == "self" && path->path.members.empty();
}

/// Render a SourceRange to a compact "begin..end" string using the byte
/// offsets that IR nodes carry natively (R-04). Missing ranges collapse to an
/// empty string so the JSON field is always present but callers can tell the
/// location was unavailable.
[[nodiscard]] std::string encode_location(const ir::SourceRangeOpt &range) {
    if (!range.has_value()) {
        return {};
    }
    const SourceRange &r = *range;
    std::ostringstream out;
    out << r.begin_offset << ".." << r.end_offset;
    return out.str();
}

[[nodiscard]] bool is_integer_literal_one(const ir::Expr &expr) {
    const auto *lit = std::get_if<ir::IntegerLiteralExpr>(&expr.node);
    if (lit == nullptr) {
        return false;
    }
    return lit->spelling == "1" || lit->spelling == "+1" || lit->spelling == "0x1";
}

/// Result of the IR-level classifier: carries everything
/// build_assurance_bundle needs to populate a VerificationObligation.
struct ClassifiedDecreasesTerm {
    std::string pattern_render;
    VerificationObligationStatus status{VerificationObligationStatus::Unrecognized};
    bool well_founded{false};
};

[[nodiscard]] ClassifiedDecreasesTerm classify_decreases_term(const ir::Expr &expr) {
    ClassifiedDecreasesTerm result{.pattern_render = render_decreases_subject(expr)};

    // ① LengthSelf — member access self.length OR flattened path self.length.
    if (const auto *member = std::get_if<ir::MemberAccessExpr>(&expr.node); member != nullptr) {
        if (member->member == "length" && member->base && is_self_path(*member->base)) {
            result.status = VerificationObligationStatus::Recognized;
            result.well_founded = true;
            return result;
        }
        // ② SelfField on a member access receiver: self.<field>.
        if (member->base && is_self_path(*member->base)) {
            result.status = VerificationObligationStatus::Recognized;
            result.well_founded = true;
            return result;
        }
        result.status = VerificationObligationStatus::Unrecognized;
        result.well_founded = false;
        return result;
    }

    if (const auto *path = std::get_if<ir::PathExpr>(&expr.node); path != nullptr) {
        // ① Flat PathExpr: root = self, first member = length.
        if (path->path.root_name == "self" && path->path.members.size() == 1 &&
            path->path.members.front() == "length") {
            result.status = VerificationObligationStatus::Recognized;
            result.well_founded = true;
            return result;
        }
        // ② Flat PathExpr: root = self, any single member access.
        if (path->path.root_name == "self" && path->path.members.size() == 1) {
            result.status = VerificationObligationStatus::Recognized;
            result.well_founded = true;
            return result;
        }
    }

    // ③ MinusOne: <ident> - 1 where <ident> is a bare PathExpr with no
    //    member traversal.
    if (const auto *bin = std::get_if<ir::BinaryExpr>(&expr.node);
        bin != nullptr && bin->op == ir::ExprBinaryOp::Subtract && bin->lhs && bin->rhs &&
        is_integer_literal_one(*bin->rhs)) {
        const auto *lhs_path = std::get_if<ir::PathExpr>(&bin->lhs->node);
        if (lhs_path != nullptr && lhs_path->path.members.empty() &&
            !lhs_path->path.root_name.empty()) {
            result.status = VerificationObligationStatus::Recognized;
            result.well_founded = true;
            return result;
        }
    }

    result.status = VerificationObligationStatus::Unrecognized;
    result.well_founded = false;
    return result;
}

/// Walk every ContractDecl → Decreases clause and expand it into flat
/// VerificationObligation entries that preserve source order.
void build_verification_profile(const ir::Program &program, VerificationProfile &out_profile) {
    for (const auto &declaration : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&declaration);
        if (contract == nullptr) {
            continue;
        }
        for (const auto &clause : contract->clauses) {
            if (clause.kind != ir::ContractClauseKind::Decreases) {
                continue;
            }
            if (clause.decreases_wildcard) {
                VerificationObligation obligation;
                obligation.decreases_pattern = "*";
                obligation.well_founded = true;
                obligation.location = encode_location(clause.source_range);
                obligation.status = VerificationObligationStatus::Wildcard;
                out_profile.obligations.push_back(std::move(obligation));
                continue;
            }
            for (const auto &term : clause.decreases_terms) {
                if (!term.has_value()) {
                    // Defensive: a dangling ExprRef would mean a lowering
                    // bug; keep an unrecognized sentinel entry so the
                    // obligation count still matches the total number of
                    // decreases terms.
                    VerificationObligation obligation;
                    obligation.decreases_pattern = "<missing-term>";
                    obligation.well_founded = false;
                    obligation.location = encode_location(clause.source_range);
                    obligation.status = VerificationObligationStatus::Unrecognized;
                    out_profile.obligations.push_back(std::move(obligation));
                    continue;
                }
                const auto classified = classify_decreases_term(*term);
                VerificationObligation obligation;
                obligation.decreases_pattern = classified.pattern_render;
                obligation.well_founded = classified.well_founded;
                obligation.location = encode_location(term->source_range);
                if (obligation.location.empty()) {
                    obligation.location = encode_location(clause.source_range);
                }
                obligation.status = classified.status;
                out_profile.obligations.push_back(std::move(obligation));
            }
        }
    }
}

[[nodiscard]] std::string_view
verification_status_name(VerificationObligationStatus status) noexcept {
    switch (status) {
    case VerificationObligationStatus::Recognized:   return "recognized";
    case VerificationObligationStatus::Wildcard:     return "wildcard";
    case VerificationObligationStatus::Unrecognized: return "unrecognized";
    }
    return "unrecognized";
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
            field("verification",
                  [&]() { print_verification_profile(bundle.verification, 1); });
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

    void print_verification_obligation(const VerificationObligation &obligation,
                                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("decreases_pattern", [&]() { write_string(obligation.decreases_pattern); });
            field("well_founded", [&]() { write_bool(obligation.well_founded); });
            field("location", [&]() { write_string(obligation.location); });
            field("status", [&]() { write_string(std::string(verification_status_name(obligation.status))); });
        });
    }

    void print_verification_profile(const VerificationProfile &profile, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("obligations", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &obligation : profile.obligations) {
                        item([&]() { print_verification_obligation(obligation, indent_level + 2); });
                    }
                });
            });
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
    build_verification_profile(program, bundle.verification);
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
