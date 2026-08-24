#include "verification/formal/smt_encode.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::formal {

namespace {

// Accumulates state while recursively encoding one predicate: the divisor
// obligations collected across the tree and the encoding options. Kept local
// so encode_predicate stays a pure entry point.
struct Encoder {
    const SmtEncodeOptions &options;
    std::vector<SmtObligation> obligations;
    std::optional<SmtEncodeRejection> rejection;

    // Records the first rejection encountered and returns nullopt so callers
    // can short-circuit. Subsequent rejections do not overwrite the first.
    std::optional<std::string> reject(SmtEncodeRejection reason) {
        if (!rejection.has_value()) {
            rejection = reason;
        }
        return std::nullopt;
    }

    // Encodes an SMT-LIB 2 path symbol from a contract path (input.field,
    // ctx.field, output.field, node.field). The symbol name is derived from
    // the path spelling deterministically; the leading root plus dotted
    // members become a single flat SMT symbol (dots -> "__").
    [[nodiscard]] static std::string path_symbol(const ir::Path &path) {
        std::string symbol = path.root_name;
        for (const auto &member : path.members) {
            symbol += "__";
            symbol += member;
        }
        return symbol;
    }

    // Encodes the binary operator into its SMT-LIB 2 function symbol, or
    // nullopt when the operator is outside the encodable set.
    [[nodiscard]] static std::optional<std::string_view> binary_symbol(ir::ExprBinaryOp op) {
        switch (op) {
        case ir::ExprBinaryOp::Implies:
            return "=>";
        case ir::ExprBinaryOp::Or:
            return "or";
        case ir::ExprBinaryOp::And:
            return "and";
        case ir::ExprBinaryOp::Equal:
            return "=";
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
        // NotEqual, Divide, Modulo need structural rewrites (see encode_binary).
        case ir::ExprBinaryOp::NotEqual:
        case ir::ExprBinaryOp::Divide:
        case ir::ExprBinaryOp::Modulo:
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> encode(const ir::ExprRef &ref);

    [[nodiscard]] std::optional<std::string> encode_binary(const ir::BinaryExpr &bin) {
        auto lhs = encode(bin.lhs);
        if (!lhs.has_value()) {
            return std::nullopt;
        }
        auto rhs = encode(bin.rhs);
        if (!rhs.has_value()) {
            return std::nullopt;
        }

        // NotEqual: (not (= lhs rhs)).
        if (bin.op == ir::ExprBinaryOp::NotEqual) {
            return "(not (= " + *lhs + " " + *rhs + "))";
        }

        // Divide / Modulo: SMT-LIB integer div/mod, plus a divisor != 0
        // verification obligation aligned with the runtime's divide-by-zero
        // error (evaluator.cpp).
        if (bin.op == ir::ExprBinaryOp::Divide || bin.op == ir::ExprBinaryOp::Modulo) {
            obligations.push_back(SmtObligation{"(not (= " + *rhs + " 0))"});
            const std::string_view fn = bin.op == ir::ExprBinaryOp::Divide ? "div" : "mod";
            return "(" + std::string(fn) + " " + *lhs + " " + *rhs + ")";
        }

        const auto symbol = binary_symbol(bin.op);
        if (!symbol.has_value()) {
            return reject(SmtEncodeRejection::UnsupportedOperator);
        }
        auto result = "(" + std::string(*symbol) + " " + *lhs + " " + *rhs + ")";

        // Optional integer overflow guard for additive/multiplicative ops.
        if (options.emit_overflow_checks &&
            (bin.op == ir::ExprBinaryOp::Add || bin.op == ir::ExprBinaryOp::Subtract ||
             bin.op == ir::ExprBinaryOp::Multiply)) {
            obligations.push_back(SmtObligation{
                "(and (<= " + result + " 9223372036854775807) (>= " + result +
                " (- 9223372036854775808)))"});
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string> encode_unary(const ir::UnaryExpr &un) {
        auto operand = encode(un.operand);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        switch (un.op) {
        case ir::ExprUnaryOp::Not:
            return "(not " + *operand + ")";
        case ir::ExprUnaryOp::Negate:
            return "(- " + *operand + ")";
        case ir::ExprUnaryOp::Positive:
            return operand; // unary plus is identity
        }
        return reject(SmtEncodeRejection::UnsupportedOperator);
    }
};

std::optional<std::string> Encoder::encode(const ir::ExprRef &ref) {
    if (!ref.has_value() || ref.ptr == nullptr) {
        return reject(SmtEncodeRejection::NullExpr);
    }
    return std::visit(
        Overloaded{
            [&](const ir::BoolLiteralExpr &e) -> std::optional<std::string> {
                return std::string(e.value ? "true" : "false");
            },
            [&](const ir::IntegerLiteralExpr &e) -> std::optional<std::string> {
                // The spelling is the canonical integer text; negative literals
                // encode as (- n) per SMT-LIB.
                if (!e.spelling.empty() && e.spelling.front() == '-') {
                    return "(- " + e.spelling.substr(1) + ")";
                }
                return e.spelling;
            },
            [&](const ir::PathExpr &e) -> std::optional<std::string> {
                return path_symbol(e.path);
            },
            [&](const ir::MemberAccessExpr &e) -> std::optional<std::string> {
                auto base = encode(e.base);
                if (!base.has_value()) {
                    return std::nullopt;
                }
                // Field projection folds into the flat symbol namespace.
                return *base + "__" + e.member;
            },
            [&](const ir::BinaryExpr &e) -> std::optional<std::string> {
                return encode_binary(e);
            },
            [&](const ir::UnaryExpr &e) -> std::optional<std::string> { return encode_unary(e); },
            // Everything else is outside the verifiable data-predicate subset:
            // string content, decimals/floats (deferred), calls, lambdas,
            // struct/qualified literals, index access, match, unwrap, unit.
            [&](const ir::StringLiteralExpr &) -> std::optional<std::string> {
                return reject(SmtEncodeRejection::StringContent);
            },
            [&](const auto &) -> std::optional<std::string> {
                return reject(SmtEncodeRejection::UnsupportedNode);
            },
        },
        ref.ptr->node);
}

} // namespace

SmtEncodeResult encode_predicate(const ir::ExprRef &expr, const SmtEncodeOptions &options) {
    Encoder encoder{options, {}, std::nullopt};
    auto term = encoder.encode(expr);
    SmtEncodeResult result;
    if (!term.has_value()) {
        result.rejection = encoder.rejection.value_or(SmtEncodeRejection::UnsupportedNode);
        return result;
    }
    result.term = std::move(term);
    result.obligations = std::move(encoder.obligations);
    return result;
}

std::string_view describe_rejection(SmtEncodeRejection rejection) noexcept {
    switch (rejection) {
    case SmtEncodeRejection::UnsupportedNode:
        return "expression uses a construct outside the verifiable data-predicate subset";
    case SmtEncodeRejection::UnsupportedOperator:
        return "operator is outside the verifiable data-predicate subset";
    case SmtEncodeRejection::StringContent:
        return "String content predicates are not in the verifiable subset";
    case SmtEncodeRejection::UnsupportedType:
        return "operand type has no SMT sort mapping in the verifiable subset";
    case SmtEncodeRejection::NullExpr:
        return "contract predicate is malformed (null expression)";
    }
    return "unknown rejection";
}

} // namespace ahfl::formal
