#include "ahfl/compiler/semantics/condition_facts.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace ahfl {

namespace {

[[nodiscard]] std::optional<Place> extract_place(const ast::ExprSyntax &expr) {
    return std::visit(Overloaded{
                          [](const ast::GroupExpr &group) -> std::optional<Place> {
                              if (!group.inner) {
                                  return std::nullopt;
                              }
                              return extract_place(*group.inner);
                          },
                          [](const ast::MemberAccessExpr &member) -> std::optional<Place> {
                              if (!member.base) {
                                  return std::nullopt;
                              }
                              auto place = extract_place(*member.base);
                              if (!place.has_value()) {
                                  return std::nullopt;
                              }
                              place->members.push_back(member.member);
                              return place;
                          },
                          [](const ast::PathExpr &path_expr) -> std::optional<Place> {
                              if (!path_expr.path) {
                                  return std::nullopt;
                              }
                              return Place{.root = path_expr.path->root_name,
                                           .members = path_expr.path->members};
                          },
                          [](const auto &) -> std::optional<Place> { return std::nullopt; },
                      },
                      expr.node);
}

[[nodiscard]] bool is_none_like(const ast::ExprSyntax &expr) noexcept {
    if (expr.is<ast::NoneLiteralExpr>()) {
        return true;
    }
    if (!expr.is<ast::QualifiedValueExpr>()) {
        return false;
    }
    const auto &qualified = expr.as<ast::QualifiedValueExpr>();
    return qualified.name != nullptr && !qualified.name->segments.empty() &&
           qualified.name->segments.back() == "None";
}

[[nodiscard]] bool is_qualified_value(const ast::ExprSyntax &expr) noexcept {
    if (!expr.is<ast::QualifiedValueExpr>()) {
        return false;
    }
    const auto &q = expr.as<ast::QualifiedValueExpr>();
    return q.name != nullptr && q.name->segments.size() >= 2;
}

[[nodiscard]] TypeFactKind opposite(TypeFactKind kind) noexcept {
    return kind == TypeFactKind::IsNone ? TypeFactKind::IsNotNone : TypeFactKind::IsNone;
}

[[nodiscard]] std::optional<TypeFact> extract_none_comparison(const ast::ExprSyntax &condition,
                                                              bool truth_value) {
    return std::visit(
        Overloaded{
            [truth_value](const ast::GroupExpr &group) -> std::optional<TypeFact> {
                if (!group.inner) {
                    return std::nullopt;
                }
                return extract_none_comparison(*group.inner, truth_value);
            },
            [truth_value, &condition](const ast::BinaryExpr &binary) -> std::optional<TypeFact> {
                if (!binary.lhs || !binary.rhs) {
                    return std::nullopt;
                }
                if (binary.op != ast::ExprBinaryOp::Equal &&
                    binary.op != ast::ExprBinaryOp::NotEqual) {
                    return std::nullopt;
                }

                std::optional<Place> place;
                if (is_none_like(*binary.lhs)) {
                    place = extract_place(*binary.rhs);
                } else if (is_none_like(*binary.rhs)) {
                    place = extract_place(*binary.lhs);
                }
                if (!place.has_value()) {
                    return std::nullopt;
                }

                auto kind = binary.op == ast::ExprBinaryOp::Equal ? TypeFactKind::IsNone
                                                                  : TypeFactKind::IsNotNone;
                if (!truth_value) {
                    kind = opposite(kind);
                }

                return TypeFact{
                    .place = std::move(*place),
                    .kind = kind,
                    .origin = condition.range,
                    .enum_name = {},
                    .variant_name = {},
                };
            },
            [](const auto &) -> std::optional<TypeFact> { return std::nullopt; },
        },
        condition.node);
}

[[nodiscard]] std::optional<TypeFact> extract_variant_comparison(const ast::ExprSyntax &condition,
                                                                 bool truth_value) {
    return std::visit(
        Overloaded{
            [truth_value](const ast::GroupExpr &group) -> std::optional<TypeFact> {
                if (!group.inner) {
                    return std::nullopt;
                }
                return extract_variant_comparison(*group.inner, truth_value);
            },
            [truth_value, &condition](const ast::BinaryExpr &binary) -> std::optional<TypeFact> {
                if (!binary.lhs || !binary.rhs) {
                    return std::nullopt;
                }
                if (binary.op != ast::ExprBinaryOp::Equal &&
                    binary.op != ast::ExprBinaryOp::NotEqual) {
                    return std::nullopt;
                }

                // Detect: path == QualifiedValue  or  QualifiedValue == path
                std::optional<Place> place;
                const ast::QualifiedName *qualified = nullptr;

                if (is_qualified_value(*binary.rhs)) {
                    place = extract_place(*binary.lhs);
                    qualified = binary.rhs->as<ast::QualifiedValueExpr>().name.get();
                } else if (is_qualified_value(*binary.lhs)) {
                    place = extract_place(*binary.rhs);
                    qualified = binary.lhs->as<ast::QualifiedValueExpr>().name.get();
                }

                if (!place.has_value() || qualified == nullptr) {
                    return std::nullopt;
                }

                // segments: ["EnumName", "VariantName"] (possibly more for module-qualified).
                // The last segment is the variant, second-to-last is the enum type.
                const auto &segs = qualified->segments;
                const std::string &variant = segs.back();
                const std::string &enum_type = segs[segs.size() - 2];

                const bool positive = (binary.op == ast::ExprBinaryOp::Equal) == truth_value;

                return TypeFact{
                    .place = std::move(*place),
                    .kind = positive ? TypeFactKind::IsVariant : TypeFactKind::IsNotVariant,
                    .origin = condition.range,
                    .enum_name = enum_type,
                    .variant_name = variant,
                };
            },
            [](const auto &) -> std::optional<TypeFact> { return std::nullopt; },
        },
        condition.node);
}

void collect_true_facts(const ast::ExprSyntax &condition, FlowFacts &facts) {
    const bool handled =
        std::visit(Overloaded{
                       [&facts](const ast::GroupExpr &group) -> bool {
                           if (!group.inner) {
                               return false;
                           }
                           collect_true_facts(*group.inner, facts);
                           return true;
                       },
                       [&facts](const ast::BinaryExpr &binary) -> bool {
                           if (binary.op != ast::ExprBinaryOp::And || !binary.lhs || !binary.rhs) {
                               return false;
                           }
                           collect_true_facts(*binary.lhs, facts);
                           collect_true_facts(*binary.rhs, facts);
                           return true;
                       },
                       [](const auto &) -> bool { return false; },
                   },
                   condition.node);

    if (handled) {
        return;
    }

    if (auto fact = extract_none_comparison(condition, true); fact.has_value()) {
        facts.add(std::move(*fact));
    }
    if (auto fact = extract_variant_comparison(condition, true); fact.has_value()) {
        facts.add(std::move(*fact));
    }
}

} // namespace

ConditionFacts extract_condition_facts(const ast::ExprSyntax &condition) {
    ConditionFacts result;
    collect_true_facts(condition, result.when_true);

    // A single comparison has an exact complement on the false edge. For
    // conjunctions, the false edge is a disjunction of failed conjuncts; keep it
    // conservative until the flow environment can represent disjunctive facts.
    if (auto fact = extract_none_comparison(condition, false); fact.has_value()) {
        result.when_false.add(std::move(*fact));
    }
    if (auto fact = extract_variant_comparison(condition, false); fact.has_value()) {
        result.when_false.add(std::move(*fact));
    }
    return result;
}

} // namespace ahfl
