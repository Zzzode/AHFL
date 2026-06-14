#include "ahfl/compiler/semantics/condition_facts.hpp"

#include <optional>
#include <string>
#include <utility>

namespace ahfl {

namespace {

[[nodiscard]] std::optional<Place> extract_place(const ast::ExprSyntax &expr) {
    if (expr.kind == ast::ExprSyntaxKind::Group && expr.first) {
        return extract_place(*expr.first);
    }
    if (expr.kind == ast::ExprSyntaxKind::MemberAccess && expr.first) {
        auto place = extract_place(*expr.first);
        if (!place.has_value()) {
            return std::nullopt;
        }
        place->members.push_back(expr.name);
        return place;
    }
    if (expr.kind != ast::ExprSyntaxKind::Path || !expr.path) {
        return std::nullopt;
    }
    return Place{.root = expr.path->root_name, .members = expr.path->members};
}

[[nodiscard]] bool is_none_literal(const ast::ExprSyntax &expr) noexcept {
    return expr.kind == ast::ExprSyntaxKind::NoneLiteral;
}

[[nodiscard]] bool is_qualified_value(const ast::ExprSyntax &expr) noexcept {
    return expr.kind == ast::ExprSyntaxKind::QualifiedValue && expr.qualified_name != nullptr &&
           expr.qualified_name->segments.size() >= 2;
}

[[nodiscard]] TypeFactKind opposite(TypeFactKind kind) noexcept {
    return kind == TypeFactKind::IsNone ? TypeFactKind::IsNotNone : TypeFactKind::IsNone;
}

[[nodiscard]] std::optional<TypeFact> extract_none_comparison(const ast::ExprSyntax &condition,
                                                              bool truth_value) {
    if (condition.kind == ast::ExprSyntaxKind::Group && condition.first) {
        return extract_none_comparison(*condition.first, truth_value);
    }
    if (condition.kind != ast::ExprSyntaxKind::Binary || !condition.first || !condition.second) {
        return std::nullopt;
    }
    if (condition.binary_op != ast::ExprBinaryOp::Equal &&
        condition.binary_op != ast::ExprBinaryOp::NotEqual) {
        return std::nullopt;
    }

    std::optional<Place> place;
    if (is_none_literal(*condition.first)) {
        place = extract_place(*condition.second);
    } else if (is_none_literal(*condition.second)) {
        place = extract_place(*condition.first);
    }
    if (!place.has_value()) {
        return std::nullopt;
    }

    auto kind = condition.binary_op == ast::ExprBinaryOp::Equal ? TypeFactKind::IsNone
                                                                : TypeFactKind::IsNotNone;
    if (!truth_value) {
        kind = opposite(kind);
    }

    return TypeFact{.place = std::move(*place), .kind = kind, .origin = condition.range};
}

[[nodiscard]] std::optional<TypeFact> extract_variant_comparison(const ast::ExprSyntax &condition,
                                                                bool truth_value) {
    if (condition.kind == ast::ExprSyntaxKind::Group && condition.first) {
        return extract_variant_comparison(*condition.first, truth_value);
    }
    if (condition.kind != ast::ExprSyntaxKind::Binary || !condition.first || !condition.second) {
        return std::nullopt;
    }
    if (condition.binary_op != ast::ExprBinaryOp::Equal &&
        condition.binary_op != ast::ExprBinaryOp::NotEqual) {
        return std::nullopt;
    }

    // Detect: path == QualifiedValue  or  QualifiedValue == path
    std::optional<Place> place;
    const ast::QualifiedName *qualified = nullptr;

    if (is_qualified_value(*condition.second)) {
        place = extract_place(*condition.first);
        qualified = condition.second->qualified_name.get();
    } else if (is_qualified_value(*condition.first)) {
        place = extract_place(*condition.second);
        qualified = condition.first->qualified_name.get();
    }

    if (!place.has_value() || qualified == nullptr) {
        return std::nullopt;
    }

    // segments: ["EnumName", "VariantName"] (possibly more for module-qualified).
    // The last segment is the variant, second-to-last is the enum type.
    const auto &segs = qualified->segments;
    const std::string &variant = segs.back();
    const std::string &enum_type = segs[segs.size() - 2];

    const bool positive = (condition.binary_op == ast::ExprBinaryOp::Equal) == truth_value;

    return TypeFact{
        .place = std::move(*place),
        .kind = positive ? TypeFactKind::IsVariant : TypeFactKind::IsNotVariant,
        .origin = condition.range,
        .enum_name = enum_type,
        .variant_name = variant,
    };
}

void collect_true_facts(const ast::ExprSyntax &condition, FlowFacts &facts) {
    if (condition.kind == ast::ExprSyntaxKind::Group && condition.first) {
        collect_true_facts(*condition.first, facts);
        return;
    }
    if (condition.kind == ast::ExprSyntaxKind::Binary &&
        condition.binary_op == ast::ExprBinaryOp::And && condition.first && condition.second) {
        collect_true_facts(*condition.first, facts);
        collect_true_facts(*condition.second, facts);
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
