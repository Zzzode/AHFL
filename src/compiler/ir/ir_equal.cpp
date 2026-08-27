#include "ahfl/compiler/ir/ir_equal.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <variant>

namespace ahfl::ir {

namespace {

[[nodiscard]] bool path_equal(const Path &lhs, const Path &rhs) {
    return lhs.root_kind == rhs.root_kind && lhs.root_name == rhs.root_name &&
           lhs.members == rhs.members;
}

[[nodiscard]] bool symbol_ref_equal(const SymbolRef &lhs, const SymbolRef &rhs) {
    // Canonical name is the stable identity of a resolved symbol; kind guards
    // against a name that coincides across namespaces.
    return lhs.kind == rhs.kind && lhs.canonical_name == rhs.canonical_name;
}

[[nodiscard]] bool refs_equal(const std::vector<ExprRef> &lhs, const std::vector<ExprRef> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!exprs_structurally_equal(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool exprs_structurally_equal(const ExprRef &lhs, const ExprRef &rhs) {
    if (lhs.get() == nullptr || rhs.get() == nullptr) {
        return lhs.get() == rhs.get(); // both absent → equal; one absent → unequal
    }
    return exprs_structurally_equal(*lhs, *rhs);
}

bool exprs_structurally_equal(const Expr &lhs, const Expr &rhs) {
    if (lhs.node.index() != rhs.node.index()) {
        return false;
    }
    return std::visit(
        Overloaded{
            [&](const BoolLiteralExpr &a) {
                return a.value == std::get<BoolLiteralExpr>(rhs.node).value;
            },
            [&](const IntegerLiteralExpr &a) {
                return a.spelling == std::get<IntegerLiteralExpr>(rhs.node).spelling;
            },
            [&](const FloatLiteralExpr &a) {
                return a.spelling == std::get<FloatLiteralExpr>(rhs.node).spelling;
            },
            [&](const DecimalLiteralExpr &a) {
                return a.spelling == std::get<DecimalLiteralExpr>(rhs.node).spelling;
            },
            [&](const StringLiteralExpr &a) {
                return a.spelling == std::get<StringLiteralExpr>(rhs.node).spelling;
            },
            [&](const DurationLiteralExpr &a) {
                return a.spelling == std::get<DurationLiteralExpr>(rhs.node).spelling;
            },
            [&](const PathExpr &a) { return path_equal(a.path, std::get<PathExpr>(rhs.node).path); },
            [&](const QualifiedValueExpr &a) {
                return a.value == std::get<QualifiedValueExpr>(rhs.node).value;
            },
            [&](const CallExpr &a) {
                const auto &b = std::get<CallExpr>(rhs.node);
                return a.callee == b.callee && symbol_ref_equal(a.callee_ref, b.callee_ref) &&
                       refs_equal(a.arguments, b.arguments);
            },
            [&](const MethodCallExpr &a) {
                const auto &b = std::get<MethodCallExpr>(rhs.node);
                return a.method == b.method && symbol_ref_equal(a.method_ref, b.method_ref) &&
                       exprs_structurally_equal(a.receiver, b.receiver) &&
                       refs_equal(a.arguments, b.arguments);
            },
            [&](const LambdaExpr &a) {
                const auto &b = std::get<LambdaExpr>(rhs.node);
                return a.params == b.params && a.captures == b.captures &&
                       exprs_structurally_equal(a.body, b.body);
            },
            [&](const StructLiteralExpr &a) {
                const auto &b = std::get<StructLiteralExpr>(rhs.node);
                if (a.type_name != b.type_name || a.is_enum_variant != b.is_enum_variant ||
                    a.enum_name != b.enum_name || a.variant_name != b.variant_name ||
                    a.fields.size() != b.fields.size()) {
                    return false;
                }
                for (std::size_t index = 0; index < a.fields.size(); ++index) {
                    if (a.fields[index].name != b.fields[index].name ||
                        !exprs_structurally_equal(a.fields[index].value, b.fields[index].value)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const UnaryExpr &a) {
                const auto &b = std::get<UnaryExpr>(rhs.node);
                return a.op == b.op && exprs_structurally_equal(a.operand, b.operand);
            },
            [&](const BinaryExpr &a) {
                const auto &b = std::get<BinaryExpr>(rhs.node);
                return a.op == b.op && exprs_structurally_equal(a.lhs, b.lhs) &&
                       exprs_structurally_equal(a.rhs, b.rhs);
            },
            [&](const MemberAccessExpr &a) {
                const auto &b = std::get<MemberAccessExpr>(rhs.node);
                return a.member == b.member && exprs_structurally_equal(a.base, b.base);
            },
            [&](const IndexAccessExpr &a) {
                const auto &b = std::get<IndexAccessExpr>(rhs.node);
                return exprs_structurally_equal(a.base, b.base) &&
                       exprs_structurally_equal(a.index, b.index);
            },
            [&](const MatchExpr &a) {
                const auto &b = std::get<MatchExpr>(rhs.node);
                if (!exprs_structurally_equal(a.scrutinee, b.scrutinee) ||
                    a.arms.size() != b.arms.size()) {
                    return false;
                }
                for (std::size_t index = 0; index < a.arms.size(); ++index) {
                    // Patterns compare by their canonical source text (the IR
                    // does not expose a structural pattern-equality primitive;
                    // `text` is the normalized spelling produced at lowering).
                    if (a.arms[index].pattern.text != b.arms[index].pattern.text ||
                        !exprs_structurally_equal(a.arms[index].guard, b.arms[index].guard) ||
                        !exprs_structurally_equal(a.arms[index].body, b.arms[index].body)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const UnwrapExpr &a) {
                const auto &b = std::get<UnwrapExpr>(rhs.node);
                return exprs_structurally_equal(a.operand, b.operand) &&
                       exprs_structurally_equal(a.fallback_none_message, b.fallback_none_message);
            },
            [&](const UnitLiteralExpr &) { return true; },
            [&](const QuantifierExpr &a) {
                const auto &b = std::get<QuantifierExpr>(rhs.node);
                return a.kind == b.kind && a.binder == b.binder &&
                       a.value_binder == b.value_binder &&
                       exprs_structurally_equal(a.collection, b.collection) &&
                       exprs_structurally_equal(a.body, b.body);
            },
        },
        lhs.node);
}

bool temporal_exprs_structurally_equal(const TemporalExpr &lhs, const TemporalExpr &rhs) {
    if (lhs.node.index() != rhs.node.index()) {
        return false;
    }
    return std::visit(
        Overloaded{
            [&](const EmbeddedTemporalExpr &a) {
                return exprs_structurally_equal(a.expr,
                                                std::get<EmbeddedTemporalExpr>(rhs.node).expr);
            },
            [&](const CalledTemporalExpr &a) {
                return a.capability == std::get<CalledTemporalExpr>(rhs.node).capability;
            },
            [&](const InStateTemporalExpr &a) {
                return a.state == std::get<InStateTemporalExpr>(rhs.node).state;
            },
            [&](const RunningTemporalExpr &a) {
                return a.node == std::get<RunningTemporalExpr>(rhs.node).node;
            },
            [&](const CompletedTemporalExpr &a) {
                const auto &b = std::get<CompletedTemporalExpr>(rhs.node);
                return a.node == b.node && a.state_name == b.state_name;
            },
            [&](const TemporalUnaryExpr &a) {
                const auto &b = std::get<TemporalUnaryExpr>(rhs.node);
                if (a.op != b.op) {
                    return false;
                }
                if (a.operand == nullptr || b.operand == nullptr) {
                    return a.operand == b.operand;
                }
                return temporal_exprs_structurally_equal(*a.operand, *b.operand);
            },
            [&](const TemporalBinaryExpr &a) {
                const auto &b = std::get<TemporalBinaryExpr>(rhs.node);
                if (a.op != b.op) {
                    return false;
                }
                const auto ptr_equal = [](const TemporalExprPtr &x, const TemporalExprPtr &y) {
                    if (x == nullptr || y == nullptr) {
                        return x == y;
                    }
                    return temporal_exprs_structurally_equal(*x, *y);
                };
                return ptr_equal(a.lhs, b.lhs) && ptr_equal(a.rhs, b.rhs);
            },
        },
        lhs.node);
}

} // namespace ahfl::ir
