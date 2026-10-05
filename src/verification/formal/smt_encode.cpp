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
    std::vector<SmtSymbol> symbols;
    std::optional<SmtEncodeRejection> rejection;

    // RFC 0024/0025: active quantifier binder substitutions. When encoding a
    // quantifier body at unroll index i, each binder name maps to the SMT
    // symbol standing for the i-th element (e.g. `coll@i`). A bare PathExpr
    // whose root matches a bound binder encodes as that element symbol instead
    // of a free path symbol. Innermost binding wins (back()-to-front lookup),
    // so nested quantifiers over the same binder name shadow correctly.
    struct BinderBinding {
        std::string name;         // source binder name (e.g. "x")
        std::string symbol;       // substituted element symbol (e.g. "coll@0")
        SmtSort sort{SmtSort::Int};
    };
    std::vector<BinderBinding> binder_stack;

    [[nodiscard]] const BinderBinding *find_binder(std::string_view name) const {
        for (auto it = binder_stack.rbegin(); it != binder_stack.rend(); ++it) {
            if (it->name == name) {
                return &*it;
            }
        }
        return nullptr;
    }

    // Records the first rejection encountered and returns nullopt so callers
    // can short-circuit. Subsequent rejections do not overwrite the first.
    std::optional<std::string> reject(SmtEncodeRejection reason) {
        if (!rejection.has_value()) {
            rejection = reason;
        }
        return std::nullopt;
    }

    // Registers a free symbol with the sort implied by its resolved type. Only
    // scalar Bool / Int-family types get a declaration; a non-scalar base of a
    // member access (e.g. a struct root) is not a standalone SMT term and is
    // skipped so only the projected scalar leaf is declared. The first
    // declaration wins; a later reference with the same name is ignored
    // (identical spelling ⇒ identical symbol). Deterministic first-encounter
    // order is preserved for byte-identical artifacts.
    //
    // A non-scalar *leaf* type (String / Decimal / Struct / Enum / …) has no
    // SMT sort mapping: the clause is outside the verifiable subset. Rejecting
    // here (rather than silently skipping) ensures the caller abstracts the
    // clause with NOT_IN_VERIFIED_SUBSET instead of emitting a term that
    // references an undeclared symbol — which Z3 would error on but still
    // return `sat` with an empty model, misread as a genuine refutation.
    void note_symbol(const std::string &name, const ir::TypeRef &type,
                     const ir::SourceRangeOpt &source_range = std::nullopt) {
        SmtSort sort{SmtSort::Int};
        switch (type.kind) {
        case ir::TypeRefKind::Bool:
            sort = SmtSort::Bool;
            break;
        case ir::TypeRefKind::Int:
        case ir::TypeRefKind::BoundedInt:
            sort = SmtSort::Int;
            break;
        case ir::TypeRefKind::Unresolved:
            // Type not resolved (test-only / edge case) — skip without
            // declaring or rejecting. In production the type checker always
            // resolves types before the SMT encoder runs.
            return;
        default:
            // Non-scalar leaf (String/Decimal/Struct/Enum/…) — no SMT sort
            // mapping in the verifiable subset.
            reject(SmtEncodeRejection::UnsupportedType);
            return;
        }
        for (auto &existing : symbols) {
            if (existing.name == name) {
                // First-encounter order is canonical, but backfill a source
                // range if this later encounter carries one the first lacked.
                if (!existing.source_range.has_value() && source_range.has_value()) {
                    existing.source_range = source_range;
                }
                return;
            }
        }
        SmtSymbol symbol;
        symbol.name = name;
        symbol.sort = sort;
        if (type.kind == ir::TypeRefKind::BoundedInt) {
            symbol.int_bounds = type.int_bounds;
        }
        symbol.source_range = source_range;
        symbols.push_back(std::move(symbol));
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

    // RFC 0025: scalar SMT sort implied by an element type ref. Only Bool /
    // Int-family element types are in the subset; anything else is unsupported
    // and the quantifier body encoding will reject when it references the
    // binder (its element symbol has no scalar sort).
    [[nodiscard]] static std::optional<SmtSort> element_sort(const ir::TypeRef *type) noexcept {
        if (type == nullptr) {
            return std::nullopt;
        }
        switch (type->kind) {
        case ir::TypeRefKind::Bool:
            return SmtSort::Bool;
        case ir::TypeRefKind::Int:
        case ir::TypeRefKind::BoundedInt:
            return SmtSort::Int;
        default:
            return std::nullopt;
        }
    }

    // RFC 0024/0025: encode a bounded quantifier by finite unrolling.
    //
    // The collection's resolved type must be a bounded collection
    // (collection_capacity present); otherwise the clause is fail-closed with
    // UnboundedQuantifier. For a capacity N the body is encoded once per index
    // 0..N-1 with the binder(s) substituted by fresh per-index element symbols,
    // then folded into (and ...) for forall / (or ...) for exists. The empty
    // collection (N == 0) encodes to the vacuous truth value: true for forall,
    // false for exists.
    [[nodiscard]] std::optional<std::string> encode_quantifier(const ir::QuantifierExpr &q,
                                                               const ir::Expr &node);

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
                // RFC 0024/0025: a bare binder reference inside a quantifier
                // body encodes as the per-index element symbol, not a free
                // path symbol. Only a single-segment path (no member access)
                // can name a binder.
                if (e.path.members.empty()) {
                    if (const auto *binding = find_binder(e.path.root_name); binding != nullptr) {
                        return binding->symbol;
                    }
                }
                auto symbol = path_symbol(e.path);
                // A bare non-scalar root (e.g. a struct base for member
                // access) is just a namespace prefix, never a standalone SMT
                // term — skip declaration. The leaf type is checked by
                // note_symbol when the path has members.
                const bool bare_non_scalar_root =
                    e.path.members.empty() &&
                    ref.ptr->resolved_type.kind != ir::TypeRefKind::Bool &&
                    ref.ptr->resolved_type.kind != ir::TypeRefKind::Int &&
                    ref.ptr->resolved_type.kind != ir::TypeRefKind::BoundedInt &&
                    ref.ptr->resolved_type.kind != ir::TypeRefKind::Unresolved;
                if (!bare_non_scalar_root) {
                    note_symbol(symbol, ref.ptr->resolved_type, ref.ptr->source_range);
                }
                return symbol;
            },
            [&](const ir::MemberAccessExpr &e) -> std::optional<std::string> {
                auto base = encode(e.base);
                if (!base.has_value()) {
                    return std::nullopt;
                }
                // Field projection folds into the flat symbol namespace.
                auto symbol = *base + "__" + e.member;
                note_symbol(symbol, ref.ptr->resolved_type, ref.ptr->source_range);
                return symbol;
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
            // RFC 0024/0025: bounded quantifier — finite unrolling.
            [&](const ir::QuantifierExpr &e) -> std::optional<std::string> {
                return encode_quantifier(e, *ref.ptr);
            },
            [&](const auto &) -> std::optional<std::string> {
                return reject(SmtEncodeRejection::UnsupportedNode);
            },
        },
        ref.ptr->node);
}

std::optional<std::string> Encoder::encode_quantifier(const ir::QuantifierExpr &q,
                                                      const ir::Expr &node) {
    if (!q.collection.has_value() || q.collection.ptr == nullptr || !q.body.has_value() ||
        q.body.ptr == nullptr) {
        return reject(SmtEncodeRejection::NullExpr);
    }

    // The static bound comes solely from the collection's bounded type
    // (RFC 0025 capacity). No capacity => fail-closed (RFC 0024).
    const ir::TypeRef &collection_type = q.collection.ptr->resolved_type;
    if (!collection_type.collection_capacity.has_value()) {
        return reject(SmtEncodeRejection::UnboundedQuantifier);
    }
    const std::uint64_t capacity = *collection_type.collection_capacity;

    const bool is_forall = q.kind == ir::QuantifierExpr::Kind::ForAll;

    // Empty collection: forall is vacuously true, exists vacuously false.
    if (capacity == 0) {
        return std::string(is_forall ? "true" : "false");
    }

    // Element (and, for Map, value) sorts from the collection type args. A
    // non-scalar element sort means the body cannot stay in the scalar subset;
    // the per-index element symbol is registered at that sort and the body
    // encoding rejects on use if it is unsupported. List/Set: params[0] is the
    // element. Map: params[0] key, params[1] value.
    const ir::TypeRef *key_type =
        !collection_type.params.empty() ? collection_type.params.front().get() : nullptr;
    const ir::TypeRef *value_type =
        collection_type.params.size() >= 2 ? collection_type.params[1].get() : nullptr;
    const bool is_map = !q.value_binder.empty();

    // A stable, index-based element symbol base derived from the collection's
    // encoded path. Using the path symbol keeps element names deterministic and
    // tied to the source collection (`coll@i`), never to iteration order.
    std::string collection_base;
    if (const auto *collection_path = std::get_if<ir::PathExpr>(&q.collection.ptr->node)) {
        collection_base = path_symbol(collection_path->path);
    } else {
        // Non-path collection operand (e.g. a nested member access). Encode it
        // to obtain a deterministic symbol base; if that leaves the subset the
        // encoding already recorded the rejection.
        auto encoded = encode(q.collection);
        if (!encoded.has_value()) {
            return std::nullopt;
        }
        collection_base = *encoded;
    }

    std::vector<std::string> terms;
    terms.reserve(capacity);
    for (std::uint64_t i = 0; i < capacity; ++i) {
        const std::string index = std::to_string(i);
        // Bind the element / (key, value) binder(s) to per-index symbols.
        const std::size_t binders_before = binder_stack.size();
        if (is_map) {
            const std::string key_symbol = collection_base + "@" + index + ".key";
            const std::string val_symbol = collection_base + "@" + index + ".val";
            const auto key_sort = element_sort(key_type).value_or(SmtSort::Int);
            const auto val_sort = element_sort(value_type).value_or(SmtSort::Int);
            if (key_type != nullptr) {
                note_symbol(key_symbol, *key_type);
            }
            if (value_type != nullptr) {
                note_symbol(val_symbol, *value_type);
            }
            binder_stack.push_back(BinderBinding{q.binder, key_symbol, key_sort});
            binder_stack.push_back(BinderBinding{q.value_binder, val_symbol, val_sort});
        } else {
            const std::string elem_symbol = collection_base + "@" + index;
            const auto elem_sort = element_sort(key_type).value_or(SmtSort::Int);
            if (key_type != nullptr) {
                note_symbol(elem_symbol, *key_type);
            }
            binder_stack.push_back(BinderBinding{q.binder, elem_symbol, elem_sort});
        }

        auto body_term = encode(q.body);
        binder_stack.resize(binders_before);
        if (!body_term.has_value()) {
            return std::nullopt;
        }
        terms.push_back(std::move(*body_term));
    }

    (void)node;
    const std::string_view connective = is_forall ? "and" : "or";
    std::string result = "(";
    result += connective;
    for (const auto &term : terms) {
        result += ' ';
        result += term;
    }
    result += ')';
    return result;
}

} // namespace

SmtEncodeResult encode_predicate(const ir::ExprRef &expr, const SmtEncodeOptions &options) {
    Encoder encoder{options, {}, {}, std::nullopt, {}};
    auto term = encoder.encode(expr);
    SmtEncodeResult result;
    // A rejection can be set by note_symbol (non-scalar leaf type) even when
    // encode returns a non-nullopt term — the term text is built but the
    // clause is outside the verifiable subset.
    if (!term.has_value() || encoder.rejection.has_value()) {
        result.rejection = encoder.rejection.value_or(SmtEncodeRejection::UnsupportedNode);
        return result;
    }
    result.term = std::move(term);
    result.symbols = std::move(encoder.symbols);
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
    case SmtEncodeRejection::UnboundedQuantifier:
        return "quantified collection has no static capacity bound; give it a bounded "
               "collection type (e.g. List<T>(N)) to verify a quantified property over it";
    }
    return "unknown rejection";
}

std::string_view smt_sort_keyword(SmtSort sort) noexcept {
    switch (sort) {
    case SmtSort::Bool:
        return "Bool";
    case SmtSort::Int:
        return "Int";
    }
    return "Int";
}

} // namespace ahfl::formal
