#include "ahfl/compiler/semantics/decreases_recognizer.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"

namespace ahfl::sema {

namespace {

// ---------------------------------------------------------------------------
// Pattern matchers — return true and fill detail if matches a known shape.
// ---------------------------------------------------------------------------

/// Tests whether the expression is `length(self)`.
/// Shape: CallExpr where callee is "length" with exactly 1 arg, arg is PathExpr
/// with root == "self" and no members.
[[nodiscard]] bool matches_length_self(ast::ExprSyntax const &expr,
                                       PatternLengthSelf &out,
                                       std::size_t index) {
    if (!expr.is<ast::CallExpr>()) {
        return false;
    }
    auto const &call = expr.as<ast::CallExpr>();
    if (!call.callee) {
        return false;
    }
    if (call.callee->segments.size() != 1 || call.callee->segments.front() != "length") {
        return false;
    }
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        return false;
    }
    auto const &arg = *call.arguments.front();
    if (!arg.is<ast::PathExpr>()) {
        return false;
    }
    auto const &path = arg.as<ast::PathExpr>().path;
    if (!path) {
        return false;
    }
    if (path->root_kind != ast::PathRootKind::Identifier &&
        path->root_kind != ast::PathRootKind::Input) {
        return false;
    }
    if (path->root_name != "self") {
        return false;
    }
    if (!path->members.empty()) {
        return false;
    }
    out = PatternLengthSelf{
        .range = expr.range,
        .subterm_index = index,
    };
    return true;
}

/// Tests whether the expression is `self.<field>` (a member access rooted at
/// "self"). Accepts both the legacy "self as Identifier Path + MemberAccess"
/// shape and the multi-member Path shape (root="self", members=[field]).
[[nodiscard]] bool matches_self_field(ast::ExprSyntax const &expr,
                                      PatternSelfField &out,
                                      std::size_t index) {
    // Shape A: PathExpr with root "self" and exactly one member.
    if (expr.is<ast::PathExpr>()) {
        auto const &path = expr.as<ast::PathExpr>().path;
        if (!path) {
            return false;
        }
        if (path->root_name != "self") {
            return false;
        }
        if (path->members.size() != 1) {
            return false;
        }
        out = PatternSelfField{
            .range = expr.range,
            .subterm_index = index,
            .field_name = path->members.front(),
        };
        return true;
    }

    // Shape B: MemberAccessExpr where base is the self PathExpr (no members).
    if (expr.is<ast::MemberAccessExpr>()) {
        auto const &ma = expr.as<ast::MemberAccessExpr>();
        if (!ma.base) {
            return false;
        }
        if (!ma.base->is<ast::PathExpr>()) {
            return false;
        }
        auto const &base_path = ma.base->as<ast::PathExpr>().path;
        if (!base_path) {
            return false;
        }
        if (base_path->root_name != "self") {
            return false;
        }
        if (!base_path->members.empty()) {
            return false;
        }
        out = PatternSelfField{
            .range = expr.range,
            .subterm_index = index,
            .field_name = ma.member,
        };
        return true;
    }
    return false;
}

/// Tests whether the expression is `<identifier> - 1` (integer literal subtract-1).
/// Shape: BinaryExpr with Subtract op, LHS is PathExpr identifier,
/// RHS is IntegerLiteralExpr with value == 1.
[[nodiscard]] bool matches_minus_one(ast::ExprSyntax const &expr,
                                     PatternMinusOne &out,
                                     std::size_t index) {
    // Optionally unwrap a single GroupExpr parenthesis.
    ast::ExprSyntax const *target = &expr;
    if (expr.is<ast::GroupExpr>()) {
        auto const &inner = expr.as<ast::GroupExpr>().inner;
        if (!inner) {
            return false;
        }
        target = inner.get();
    }
    if (!target->is<ast::BinaryExpr>()) {
        return false;
    }
    auto const &bin = target->as<ast::BinaryExpr>();
    if (bin.op != ast::ExprBinaryOp::Subtract) {
        return false;
    }
    if (!bin.lhs || !bin.rhs) {
        return false;
    }
    // LHS must be a plain identifier path.
    if (!bin.lhs->is<ast::PathExpr>()) {
        return false;
    }
    auto const &lhs_path = bin.lhs->as<ast::PathExpr>().path;
    if (!lhs_path) {
        return false;
    }
    if (!lhs_path->members.empty()) {
        return false;
    }
    if (lhs_path->root_kind != ast::PathRootKind::Identifier) {
        return false;
    }
    if (lhs_path->root_name.empty()) {
        return false;
    }
    // RHS must be IntegerLiteral with value == 1.
    if (!bin.rhs->is<ast::IntegerLiteralExpr>()) {
        return false;
    }
    auto const &rhs_lit = bin.rhs->as<ast::IntegerLiteralExpr>().literal;
    if (!rhs_lit) {
        return false;
    }
    if (rhs_lit->value != 1) {
        return false;
    }
    out = PatternMinusOne{
        .range = expr.range,
        .subterm_index = index,
        .identifier = lhs_path->root_name,
    };
    return true;
}

/// Tests whether the expression is a wildcard `*` represented as an identifier
/// path whose root name is exactly "*".
[[nodiscard]] bool matches_wildcard(ast::ExprSyntax const &expr,
                                    PatternWildcard &out,
                                    std::size_t index) {
    if (!expr.is<ast::PathExpr>()) {
        return false;
    }
    auto const &path = expr.as<ast::PathExpr>().path;
    if (!path) {
        return false;
    }
    if (!path->members.empty()) {
        return false;
    }
    if (path->root_name != "*") {
        return false;
    }
    out = PatternWildcard{
        .range = expr.range,
        .subterm_index = index,
    };
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public to_string.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string_view to_string(DecreasePatternKind kind) noexcept {
    switch (kind) {
        case DecreasePatternKind::LengthSelf: return "LengthSelf";
        case DecreasePatternKind::SelfField:  return "SelfField";
        case DecreasePatternKind::MinusOne:   return "MinusOne";
        case DecreasePatternKind::Wildcard:   return "Wildcard";
        case DecreasePatternKind::Unknown:    return "Unknown";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// LexOrder helpers.
// ---------------------------------------------------------------------------

[[nodiscard]] std::size_t LexOrder::recognized_count() const noexcept {
    std::size_t count = 0;
    for (auto const &entry : entries) {
        if (entry.kind != DecreasePatternKind::Unknown) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool LexOrder::fully_recognized() const noexcept {
    if (entries.empty()) {
        return false;
    }
    if (wildcard_illegal) {
        return false;
    }
    for (auto const &entry : entries) {
        if (entry.kind == DecreasePatternKind::Unknown) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// recognize_single.
// ---------------------------------------------------------------------------

[[nodiscard]] LexOrderEntry
recognize_single(ast::ExprSyntax const &expr, std::size_t index) {
    // ① length(self)
    {
        PatternLengthSelf detail{};
        if (matches_length_self(expr, detail, index)) {
            return LexOrderEntry{
                .kind = DecreasePatternKind::LengthSelf,
                .detail = std::move(detail),
            };
        }
    }
    // ② self.<field>
    {
        PatternSelfField detail{};
        if (matches_self_field(expr, detail, index)) {
            return LexOrderEntry{
                .kind = DecreasePatternKind::SelfField,
                .detail = std::move(detail),
            };
        }
    }
    // ③ <ident> - 1
    {
        PatternMinusOne detail{};
        if (matches_minus_one(expr, detail, index)) {
            return LexOrderEntry{
                .kind = DecreasePatternKind::MinusOne,
                .detail = std::move(detail),
            };
        }
    }
    // ④ wildcard *
    {
        PatternWildcard detail{};
        if (matches_wildcard(expr, detail, index)) {
            return LexOrderEntry{
                .kind = DecreasePatternKind::Wildcard,
                .detail = std::move(detail),
            };
        }
    }
    // Unknown fallback.
    return LexOrderEntry{
        .kind = DecreasePatternKind::Unknown,
        .detail = PatternUnknown{
            .range = expr.range,
            .subterm_index = index,
            .source_text = expr.text,
        },
    };
}

// ---------------------------------------------------------------------------
// recognize_decreases.
// ---------------------------------------------------------------------------

[[nodiscard]] LexOrder
recognize_decreases(std::vector<ast::ExprSyntax> const &subterms,
                  RecognizerContext const &context) {
    LexOrder result;
    result.entries.reserve(subterms.size());
    for (std::size_t i = 0; i < subterms.size(); ++i) {
        result.entries.push_back(recognize_single(subterms[i], i));
    }
    // Wildcard handling: legality check.
    for (auto const &entry : result.entries) {
        if (entry.kind == DecreasePatternKind::Wildcard) {
            result.has_wildcard = true;
            if (context.is_recursive || context.contains_loop) {
                result.wildcard_illegal = true;
            }
            break;  // wildcard either is either present once or treated uniformly if multiple.
        }
    }
    return result;
}

}  // namespace ahfl::sema
