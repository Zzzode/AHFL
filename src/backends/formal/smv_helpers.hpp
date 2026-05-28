#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/ir/ir.hpp"
#include "backends/formal/smv_naming.hpp"
#include "support/overloaded.hpp"
#include "support/string_utils.hpp"

namespace ahfl::smv_detail {

[[nodiscard]] inline bool capability_name_matches(std::string_view candidate,
                                                  std::string_view requested_name) {
    if (candidate == requested_name) {
        return true;
    }

    const auto requested = std::string(requested_name);
    const auto candidate_text = std::string(candidate);
    if (candidate_text.size() <= requested.size()) {
        return false;
    }

    return candidate_text.ends_with("::" + requested);
}

[[nodiscard]] inline bool has_policy(const ir::CapabilityEffectSpec &effect,
                                     std::string_view requested_policy) {
    return std::ranges::any_of(effect.policies, [&](const auto &policy) {
        return policy == requested_policy || capability_name_matches(policy, requested_policy);
    });
}

[[nodiscard]] inline bool is_external_or_stronger(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::ExternalSideEffect ||
           kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
}

[[nodiscard]] inline bool is_durable_or_stronger(ir::CapabilityEffectKind kind) {
    return kind == ir::CapabilityEffectKind::DurableWrite ||
           kind == ir::CapabilityEffectKind::FinancialWrite;
}

[[nodiscard]] inline std::string called_observation_key(std::string_view agent_name,
                                                        std::string_view capability_name) {
    return std::string(agent_name) + "\n" + std::string(capability_name);
}

[[nodiscard]] inline std::string embedded_observation_key(ir::FormalObservationScopeKind kind,
                                                          std::string_view owner,
                                                          std::size_t clause_index,
                                                          std::size_t atom_index) {
    return smv::observation_scope_kind_key(kind) + "\n" + std::string(owner) + "\n" +
           std::to_string(clause_index) + "\n" + std::to_string(atom_index);
}

[[nodiscard]] inline std::string render_state_membership(std::string_view state_var,
                                                         const std::vector<std::string> &states) {
    if (states.empty()) {
        return "FALSE";
    }

    std::vector<std::string> clauses;
    clauses.reserve(states.size());

    for (const auto &state : states) {
        clauses.push_back("(" + std::string(state_var) + " = " + state + ")");
    }

    if (clauses.size() == 1) {
        return clauses.front();
    }

    return "(" + join(clauses, " | ") + ")";
}

[[nodiscard]] inline std::string render_disjunction(const std::vector<std::string> &guards) {
    if (guards.empty()) {
        return "FALSE";
    }

    if (guards.size() == 1) {
        return guards.front();
    }

    return "(" + join(guards, " | ") + ")";
}

[[nodiscard]] inline std::string render_conjunction(const std::vector<std::string> &guards) {
    if (guards.empty()) {
        return "TRUE";
    }

    if (guards.size() == 1) {
        return guards.front();
    }

    return "(" + join(guards, " & ") + ")";
}

[[nodiscard]] inline std::string contract_clause_kind_name(ir::ContractClauseKind kind) {
    switch (kind) {
    case ir::ContractClauseKind::Requires:
        return "requires";
    case ir::ContractClauseKind::Ensures:
        return "ensures";
    case ir::ContractClauseKind::Invariant:
        return "invariant";
    case ir::ContractClauseKind::Forbid:
        return "forbid";
    }

    return "invalid";
}

[[nodiscard]] inline bool is_integer_literal_spelling(std::string_view spelling) {
    if (spelling.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (spelling.front() == '-' || spelling.front() == '+') {
        index = 1;
    }

    if (index == spelling.size()) {
        return false;
    }

    for (; index < spelling.size(); ++index) {
        if (std::isdigit(static_cast<unsigned char>(spelling[index])) == 0) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline std::optional<std::string> smv_expr_unary_op(ir::ExprUnaryOp op) {
    switch (op) {
    case ir::ExprUnaryOp::Not:
        return "!";
    case ir::ExprUnaryOp::Negate:
        return "-";
    case ir::ExprUnaryOp::Positive:
        return "";
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> smv_expr_binary_op(ir::ExprBinaryOp op) {
    switch (op) {
    case ir::ExprBinaryOp::Implies:
        return "->";
    case ir::ExprBinaryOp::Or:
        return "|";
    case ir::ExprBinaryOp::And:
        return "&";
    case ir::ExprBinaryOp::Equal:
        return "=";
    case ir::ExprBinaryOp::NotEqual:
        return "!=";
    case ir::ExprBinaryOp::Less:
        return "<";
    case ir::ExprBinaryOp::LessEqual:
        return "<=";
    case ir::ExprBinaryOp::Greater:
        return ">";
    case ir::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ir::ExprBinaryOp::Add:
        return "+";
    case ir::ExprBinaryOp::Subtract:
        return "-";
    case ir::ExprBinaryOp::Multiply:
        return "*";
    case ir::ExprBinaryOp::Divide:
        return "/";
    case ir::ExprBinaryOp::Modulo:
        return "mod";
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> render_bounded_expr(const ir::Expr &expr) {
    return std::visit(Overloaded{
                          [](const ir::BoolLiteralExpr &value) -> std::optional<std::string> {
                              return value.value ? "TRUE" : "FALSE";
                          },
                          [](const ir::IntegerLiteralExpr &value) -> std::optional<std::string> {
                              if (!is_integer_literal_spelling(value.spelling)) {
                                  return std::nullopt;
                              }
                              return value.spelling;
                          },
                          [](const ir::UnaryExpr &value) -> std::optional<std::string> {
                              const auto operand = render_bounded_expr(*value.operand);
                              const auto op = smv_expr_unary_op(value.op);
                              if (!operand.has_value() || !op.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *op + "(" + *operand + "))";
                          },
                          [](const ir::BinaryExpr &value) -> std::optional<std::string> {
                              const auto lhs = render_bounded_expr(*value.lhs);
                              const auto rhs = render_bounded_expr(*value.rhs);
                              const auto op = smv_expr_binary_op(value.op);
                              if (!lhs.has_value() || !rhs.has_value() || !op.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *lhs + " " + *op + " " + *rhs + ")";
                          },
                          [](const ir::GroupExpr &value) -> std::optional<std::string> {
                              const auto nested = render_bounded_expr(*value.expr);
                              if (!nested.has_value()) {
                                  return std::nullopt;
                              }
                              return "(" + *nested + ")";
                          },
                          [](const auto &) -> std::optional<std::string> { return std::nullopt; },
                      },
                      expr.node);
}

[[nodiscard]] inline std::string source_suffix(std::string_view source_path,
                                               const ir::SourceRangeOpt &range) {
    if (source_path.empty() && !range.has_value()) {
        return {};
    }

    std::string suffix = " @ ";
    suffix += source_path.empty() ? "<unknown>" : std::string(source_path);
    if (range.has_value()) {
        suffix += ":";
        suffix += std::to_string(range->begin_offset);
        suffix += "-";
        suffix += std::to_string(range->end_offset);
    }
    return suffix;
}

} // namespace ahfl::smv_detail
