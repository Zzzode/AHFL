#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/builtins.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::evaluator {

namespace {

[[nodiscard]] EvalResult
eval_expr_impl(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn *call_eval);

// Helper: create an error result
EvalResult make_error(std::string message) {
    EvalResult result;
    result.value = make_none();
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

// ============================================================================
// Deep equality (RFC P7)
// ----------------------------------------------------------------------------
// Structural equality used by `==`/`!=` for the new container/scalar kinds
// (Set/Map/UUID/Timestamp) and for `contains` membership tests. Canonical Set
// and Map values compare via their ordered storage; UUID/Timestamp compare via
// their scalar payload. Falls back to existing per-kind logic otherwise.
// ============================================================================

bool deep_equal(const Value &lhs, const Value &rhs);

bool deep_equal_value_ptr(const std::unique_ptr<Value> &lhs,
                          const std::unique_ptr<Value> &rhs) {
    if (!lhs && !rhs)
        return true;
    if (!lhs || !rhs)
        return false;
    return deep_equal(*lhs, *rhs);
}

bool deep_equal(const Value &lhs, const Value &rhs) {
    if (lhs.node.index() != rhs.node.index())
        return false;
    if (auto *ls = std::get_if<SetValue>(&lhs.node)) {
        const auto *rs = std::get_if<SetValue>(&rhs.node);
        if (ls->items.size() != rs->items.size())
            return false;
        for (size_t i = 0; i < ls->items.size(); ++i) {
            if (!deep_equal_value_ptr(ls->items[i], rs->items[i]))
                return false;
        }
        return true;
    }
    if (auto *lm = std::get_if<MapValue>(&lhs.node)) {
        const auto *rm = std::get_if<MapValue>(&rhs.node);
        if (lm->entries.size() != rm->entries.size())
            return false;
        for (size_t i = 0; i < lm->entries.size(); ++i) {
            if (!deep_equal_value_ptr(lm->entries[i].first, rm->entries[i].first))
                return false;
            if (!deep_equal_value_ptr(lm->entries[i].second, rm->entries[i].second))
                return false;
        }
        return true;
    }
    if (auto *lu = std::get_if<UuidValue>(&lhs.node)) {
        const auto *ru = std::get_if<UuidValue>(&rhs.node);
        return lu->hex == ru->hex;
    }
    if (auto *lt = std::get_if<TimestampValue>(&lhs.node)) {
        const auto *rt = std::get_if<TimestampValue>(&rhs.node);
        return lt->unix_ms == rt->unix_ms;
    }
    if (auto *lv = std::get_if<ListValue>(&lhs.node)) {
        const auto *rv = std::get_if<ListValue>(&rhs.node);
        if (lv->items.size() != rv->items.size())
            return false;
        for (size_t i = 0; i < lv->items.size(); ++i) {
            if (!deep_equal_value_ptr(lv->items[i], rv->items[i]))
                return false;
        }
        return true;
    }
    if (auto *lo = std::get_if<OptionalValue>(&lhs.node)) {
        const auto *ro = std::get_if<OptionalValue>(&rhs.node);
        return deep_equal_value_ptr(lo->inner, ro->inner);
    }
    if (is_none(lhs) && is_none(rhs))
        return true;
    // Scalar kinds: rely on existing evaluator-side primitive equality.
    if (auto *li = std::get_if<IntValue>(&lhs.node)) {
        const auto *ri = std::get_if<IntValue>(&rhs.node);
        return li->value == ri->value;
    }
    if (auto *lf = std::get_if<FloatValue>(&lhs.node)) {
        const auto *rf = std::get_if<FloatValue>(&rhs.node);
        return lf->value == rf->value;
    }
    if (auto *lb = std::get_if<BoolValue>(&lhs.node)) {
        const auto *rb = std::get_if<BoolValue>(&rhs.node);
        return lb->value == rb->value;
    }
    if (auto *lstr = std::get_if<StringValue>(&lhs.node)) {
        const auto *rstr = std::get_if<StringValue>(&rhs.node);
        return lstr->value == rstr->value;
    }
    if (auto *ld = std::get_if<DecimalValue>(&lhs.node)) {
        const auto *rd = std::get_if<DecimalValue>(&rhs.node);
        return ld->spelling == rd->spelling;
    }
    if (auto *ldur = std::get_if<DurationValue>(&lhs.node)) {
        const auto *rdur = std::get_if<DurationValue>(&rhs.node);
        return ldur->spelling == rdur->spelling;
    }
    if (auto *le = std::get_if<EnumValue>(&lhs.node)) {
        const auto *re = std::get_if<EnumValue>(&rhs.node);
        return le->enum_name == re->enum_name && le->variant == re->variant;
    }
    // StructValue: compare fields (unordered).
    if (auto *lst = std::get_if<StructValue>(&lhs.node)) {
        const auto *rst = std::get_if<StructValue>(&rhs.node);
        if (lst->type_name != rst->type_name)
            return false;
        if (lst->fields.size() != rst->fields.size())
            return false;
        for (const auto &[name, val] : lst->fields) {
            auto it = rst->fields.find(name);
            if (it == rst->fields.end())
                return false;
            if (!deep_equal_value_ptr(val, it->second))
                return false;
        }
        return true;
    }
    return false;
}

// ============================================================================
// Literal evaluation
// ============================================================================

EvalResult eval_none_literal(const ir::NoneLiteralExpr & /*expr*/, const EvalContext & /*ctx*/) {
    return EvalResult{make_none(), {}};
}

EvalResult eval_bool_literal(const ir::BoolLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_bool(expr.value), {}};
}

EvalResult eval_integer_literal(const ir::IntegerLiteralExpr &expr, const EvalContext & /*ctx*/) {
    try {
        int64_t val = std::stoll(expr.spelling);
        return EvalResult{make_int(val), {}};
    } catch (const std::invalid_argument &) {
        return make_error("invalid integer format: " + expr.spelling);
    } catch (const std::out_of_range &) {
        return make_error("integer out of range: " + expr.spelling);
    }
}

EvalResult eval_float_literal(const ir::FloatLiteralExpr &expr, const EvalContext & /*ctx*/) {
    try {
        double val = std::stod(expr.spelling);
        return EvalResult{make_float(val), {}};
    } catch (const std::invalid_argument &) {
        return make_error("invalid float format: " + expr.spelling);
    } catch (const std::out_of_range &) {
        return make_error("float out of range: " + expr.spelling);
    }
}

EvalResult eval_decimal_literal(const ir::DecimalLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_decimal(expr.spelling), {}};
}

EvalResult eval_string_literal(const ir::StringLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_string(expr.spelling), {}};
}

EvalResult eval_duration_literal(const ir::DurationLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_duration(expr.spelling), {}};
}

// ============================================================================
// SomeExpr
// ============================================================================

EvalResult
eval_some_expr(const ir::SomeExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    if (!expr.value) {
        return make_error("SomeExpr has null inner expression");
    }
    auto inner = eval_expr_impl(*expr.value, ctx, call_eval);
    if (inner.has_errors())
        return inner;
    return EvalResult{make_optional_some(std::move(inner.value)), {}};
}

// ============================================================================
// PathExpr
// ============================================================================

EvalResult eval_path_expr(const ir::PathExpr &expr, const EvalContext &ctx) {
    const auto &path = expr.path;

    // Simple identifier (no members) -> look up in local scope
    if (path.members.empty()) {
        auto val = ctx.get_local(path.root_name);
        if (val.has_value())
            return EvalResult{std::move(*val), {}};
        return make_error("unresolved path: " + path.root_name);
    }

    // Prefer checking the local scope first for this root (e.g. let result = ...)
    auto local_val = ctx.get_local(path.root_name);
    if (local_val.has_value()) {
        // Walk member access level by level from the local variable
        std::optional<Value> val = std::move(local_val);
        for (const auto &member : path.members) {
            auto *sv = std::get_if<StructValue>(&val->node);
            if (!sv) {
                return make_error("cannot access member '" + member + "' on non-struct value");
            }
            auto field_it = sv->fields.find(member);
            if (field_it == sv->fields.end() || !field_it->second) {
                return make_error("struct has no field: " + member);
            }
            val = clone_value(*field_it->second);
        }
        return EvalResult{std::move(*val), {}};
    }

    // Single member: root.member -> lookup_path (input/ctx/node_output scope)
    if (path.members.size() == 1) {
        auto val = ctx.lookup_path(path.root_name, path.members[0]);
        if (val.has_value())
            return EvalResult{std::move(*val), {}};
        return make_error("unresolved path: " + path.root_name + "." + path.members[0]);
    }

    // Multiple members: root.m1.m2... -> lookup first, then member access
    auto val = ctx.lookup_path(path.root_name, path.members[0]);
    if (!val.has_value()) {
        return make_error("unresolved path: " + path.root_name + "." + path.members[0]);
    }

    // Walk remaining members
    for (size_t i = 1; i < path.members.size(); ++i) {
        const auto &member = path.members[i];
        auto *sv = std::get_if<StructValue>(&val->node);
        if (!sv) {
            return make_error("cannot access member '" + member + "' on non-struct value");
        }
        auto field_it = sv->fields.find(member);
        if (field_it == sv->fields.end() || !field_it->second) {
            return make_error("struct has no field: " + member);
        }
        val = clone_value(*field_it->second);
    }
    return EvalResult{std::move(*val), {}};
}

// ============================================================================
// QualifiedValueExpr (enum)
// ============================================================================

EvalResult eval_qualified_value_expr(const ir::QualifiedValueExpr &expr,
                                     const EvalContext & /*ctx*/) {
    // Format: "EnumName::Variant" or "module::path::EnumName::Variant"
    // Use rfind to locate the last :: separator
    auto pos = expr.value.rfind("::");
    if (pos == std::string::npos) {
        return EvalResult{make_enum("", expr.value), {}};
    }
    auto enum_name = expr.value.substr(0, pos);
    auto variant = expr.value.substr(pos + 2);
    return EvalResult{make_enum(std::move(enum_name), std::move(variant)), {}};
}

// ============================================================================
// StructLiteralExpr
// ============================================================================

EvalResult eval_struct_literal(const ir::StructLiteralExpr &expr,
                               const EvalContext &ctx,
                               const CallEvalFn *call_eval) {
    std::unordered_map<std::string, Value> fields;
    DiagnosticBag diagnostics;

    for (const auto &field_init : expr.fields) {
        if (!field_init.value) {
            std::move(diagnostics.error())
                .message("struct field '" + field_init.name + "' has null initializer")
                .emit();
            continue;
        }
        auto result = eval_expr_impl(*field_init.value, ctx, call_eval);
        diagnostics.append(result.diagnostics);
        if (!result.has_errors()) {
            fields.emplace(field_init.name, std::move(result.value));
        }
    }

    if (diagnostics.has_error()) {
        return EvalResult{make_none(), std::move(diagnostics)};
    }
    return EvalResult{make_struct(expr.type_name, std::move(fields)), std::move(diagnostics)};
}

// ============================================================================
// ListLiteralExpr
// ============================================================================

EvalResult eval_list_literal(const ir::ListLiteralExpr &expr,
                             const EvalContext &ctx,
                             const CallEvalFn *call_eval) {
    std::vector<Value> items;
    DiagnosticBag diagnostics;

    for (const auto &item_expr : expr.items) {
        if (!item_expr) {
            std::move(diagnostics.error()).message("list item has null expression").emit();
            continue;
        }
        auto result = eval_expr_impl(*item_expr, ctx, call_eval);
        diagnostics.append(result.diagnostics);
        if (!result.has_errors()) {
            items.push_back(std::move(result.value));
        }
    }

    if (diagnostics.has_error()) {
        return EvalResult{make_none(), std::move(diagnostics)};
    }
    return EvalResult{make_list(std::move(items)), std::move(diagnostics)};
}

// ============================================================================
// SetLiteralExpr / MapLiteralExpr  (RFC P7 runtime)
// ============================================================================

EvalResult eval_set_literal(const ir::SetLiteralExpr &expr,
                            const EvalContext &ctx,
                            const CallEvalFn *call_eval) {
    std::vector<Value> items;
    DiagnosticBag diagnostics;

    for (const auto &item_expr : expr.items) {
        if (!item_expr) {
            std::move(diagnostics.error()).message("set element has null expression").emit();
            continue;
        }
        auto result = eval_expr_impl(*item_expr, ctx, call_eval);
        diagnostics.append(result.diagnostics);
        if (!result.has_errors()) {
            items.push_back(std::move(result.value));
        }
    }

    if (diagnostics.has_error()) {
        return EvalResult{make_none(), std::move(diagnostics)};
    }
    // make_set canonicalizes (de-dup + order).
    return EvalResult{make_set(std::move(items)), std::move(diagnostics)};
}

EvalResult eval_map_literal(const ir::MapLiteralExpr &expr,
                            const EvalContext &ctx,
                            const CallEvalFn *call_eval) {
    std::vector<std::pair<Value, Value>> entries;
    DiagnosticBag diagnostics;

    for (const auto &entry : expr.entries) {
        if (!entry.key || !entry.value) {
            std::move(diagnostics.error()).message("map entry has null key/value").emit();
            continue;
        }
        auto key_result = eval_expr_impl(*entry.key, ctx, call_eval);
        diagnostics.append(key_result.diagnostics);
        if (key_result.has_errors()) {
            continue;
        }
        auto val_result = eval_expr_impl(*entry.value, ctx, call_eval);
        diagnostics.append(val_result.diagnostics);
        if (val_result.has_errors()) {
            continue;
        }
        entries.emplace_back(std::move(key_result.value), std::move(val_result.value));
    }

    if (diagnostics.has_error()) {
        return EvalResult{make_none(), std::move(diagnostics)};
    }
    // make_map canonicalizes (last-write-wins + key order).
    return EvalResult{make_map(std::move(entries)), std::move(diagnostics)};
}

// ============================================================================
// UnaryExpr
// ============================================================================

EvalResult
eval_unary_expr(const ir::UnaryExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    if (!expr.operand) {
        return make_error("UnaryExpr has null operand");
    }
    auto operand = eval_expr_impl(*expr.operand, ctx, call_eval);
    if (operand.has_errors())
        return operand;

    switch (expr.op) {
    case ir::ExprUnaryOp::Not: {
        auto *bv = std::get_if<BoolValue>(&operand.value.node);
        if (!bv) {
            return make_error("'not' operator requires Bool operand");
        }
        return EvalResult{make_bool(!bv->value), {}};
    }
    case ir::ExprUnaryOp::Negate: {
        if (auto *iv = std::get_if<IntValue>(&operand.value.node)) {
            return EvalResult{make_int(-iv->value), {}};
        }
        if (auto *fv = std::get_if<FloatValue>(&operand.value.node)) {
            return EvalResult{make_float(-fv->value), {}};
        }
        return make_error("'negate' operator requires Int or Float operand");
    }
    case ir::ExprUnaryOp::Positive: {
        if (std::holds_alternative<IntValue>(operand.value.node) ||
            std::holds_alternative<FloatValue>(operand.value.node)) {
            return operand;
        }
        return make_error("'+' operator requires Int or Float operand");
    }
    }
    return make_error("unknown unary operator");
}

// ============================================================================
// BinaryExpr
// ============================================================================

// Helper: extract numeric as double
struct NumericPair {
    bool both_int{false};
    int64_t lhs_int{0};
    int64_t rhs_int{0};
    double lhs_float{0.0};
    double rhs_float{0.0};
};

std::optional<NumericPair> extract_numeric(const Value &lhs, const Value &rhs) {
    NumericPair pair;

    auto *li = std::get_if<IntValue>(&lhs.node);
    auto *lf = std::get_if<FloatValue>(&lhs.node);
    auto *ri = std::get_if<IntValue>(&rhs.node);
    auto *rf = std::get_if<FloatValue>(&rhs.node);

    if (li && ri) {
        pair.both_int = true;
        pair.lhs_int = li->value;
        pair.rhs_int = ri->value;
        pair.lhs_float = static_cast<double>(li->value);
        pair.rhs_float = static_cast<double>(ri->value);
        return pair;
    }
    if (li && rf) {
        pair.both_int = false;
        pair.lhs_float = static_cast<double>(li->value);
        pair.rhs_float = rf->value;
        return pair;
    }
    if (lf && ri) {
        pair.both_int = false;
        pair.lhs_float = lf->value;
        pair.rhs_float = static_cast<double>(ri->value);
        return pair;
    }
    if (lf && rf) {
        pair.both_int = false;
        pair.lhs_float = lf->value;
        pair.rhs_float = rf->value;
        return pair;
    }
    return std::nullopt;
}

EvalResult
eval_binary_expr(const ir::BinaryExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    if (!expr.lhs || !expr.rhs) {
        return make_error("BinaryExpr has null operand");
    }

    auto lhs = eval_expr_impl(*expr.lhs, ctx, call_eval);
    if (lhs.has_errors())
        return lhs;
    auto rhs = eval_expr_impl(*expr.rhs, ctx, call_eval);
    if (rhs.has_errors())
        return rhs;

    switch (expr.op) {
    // --- Logical operators ---
    case ir::ExprBinaryOp::And: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb)
            return make_error("'and' requires Bool operands");
        return EvalResult{make_bool(lb->value && rb->value), {}};
    }
    case ir::ExprBinaryOp::Or: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb)
            return make_error("'or' requires Bool operands");
        return EvalResult{make_bool(lb->value || rb->value), {}};
    }
    case ir::ExprBinaryOp::Implies: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb)
            return make_error("'implies' requires Bool operands");
        return EvalResult{make_bool(!lb->value || rb->value), {}};
    }

    // --- Equality operators ---
    case ir::ExprBinaryOp::Equal: {
        // RFC P7: Set / Map / UUID / Timestamp (and unified deep equality for
        // List / Optional / Struct) — only applies when both sides share a kind.
        if (lhs.value.node.index() == rhs.value.node.index()) {
            const auto &lnode = lhs.value.node;
            if (std::holds_alternative<SetValue>(lnode) ||
                std::holds_alternative<MapValue>(lnode) ||
                std::holds_alternative<UuidValue>(lnode) ||
                std::holds_alternative<TimestampValue>(lnode) ||
                std::holds_alternative<ListValue>(lnode) ||
                std::holds_alternative<OptionalValue>(lnode) ||
                std::holds_alternative<StructValue>(lnode)) {
                return EvalResult{make_bool(deep_equal(lhs.value, rhs.value)), {}};
            }
        }
        // Compare same-type values
        if (auto nums = extract_numeric(lhs.value, rhs.value)) {
            if (nums->both_int) {
                return EvalResult{make_bool(nums->lhs_int == nums->rhs_int), {}};
            }
            return EvalResult{make_bool(nums->lhs_float == nums->rhs_float), {}};
        }
        // Bool == Bool
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (lb && rb)
            return EvalResult{make_bool(lb->value == rb->value), {}};
        // String == String
        auto *ls = std::get_if<StringValue>(&lhs.value.node);
        auto *rs = std::get_if<StringValue>(&rhs.value.node);
        if (ls && rs)
            return EvalResult{make_bool(ls->value == rs->value), {}};
        // Enum == Enum
        auto *le = std::get_if<EnumValue>(&lhs.value.node);
        auto *re = std::get_if<EnumValue>(&rhs.value.node);
        if (le && re) {
            return EvalResult{
                make_bool(le->enum_name == re->enum_name && le->variant == re->variant), {}};
        }
        // None == None
        if (is_none(lhs.value) && is_none(rhs.value)) {
            return EvalResult{make_bool(true), {}};
        }
        return EvalResult{make_bool(false), {}};
    }
    case ir::ExprBinaryOp::NotEqual: {
        // RFC P7: symmetric with Equal for the unified kinds.
        if (lhs.value.node.index() == rhs.value.node.index()) {
            const auto &lnode = lhs.value.node;
            if (std::holds_alternative<SetValue>(lnode) ||
                std::holds_alternative<MapValue>(lnode) ||
                std::holds_alternative<UuidValue>(lnode) ||
                std::holds_alternative<TimestampValue>(lnode) ||
                std::holds_alternative<ListValue>(lnode) ||
                std::holds_alternative<OptionalValue>(lnode) ||
                std::holds_alternative<StructValue>(lnode)) {
                return EvalResult{make_bool(!deep_equal(lhs.value, rhs.value)), {}};
            }
        }
        // Inline equality check and negate result
        if (auto nums = extract_numeric(lhs.value, rhs.value)) {
            if (nums->both_int)
                return EvalResult{make_bool(nums->lhs_int != nums->rhs_int), {}};
            return EvalResult{make_bool(nums->lhs_float != nums->rhs_float), {}};
        }
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb2 = std::get_if<BoolValue>(&rhs.value.node);
        if (lb && rb2)
            return EvalResult{make_bool(lb->value != rb2->value), {}};
        auto *ls = std::get_if<StringValue>(&lhs.value.node);
        auto *rs = std::get_if<StringValue>(&rhs.value.node);
        if (ls && rs)
            return EvalResult{make_bool(ls->value != rs->value), {}};
        auto *le = std::get_if<EnumValue>(&lhs.value.node);
        auto *re = std::get_if<EnumValue>(&rhs.value.node);
        if (le && re) {
            return EvalResult{
                make_bool(le->enum_name != re->enum_name || le->variant != re->variant), {}};
        }
        if (is_none(lhs.value) && is_none(rhs.value)) {
            return EvalResult{make_bool(false), {}};
        }
        return EvalResult{make_bool(true), {}};
    }

    // --- Comparison operators ---
    case ir::ExprBinaryOp::Less: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'<' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_bool(nums->lhs_int < nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float < nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::LessEqual: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'<=' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_bool(nums->lhs_int <= nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float <= nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Greater: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'>' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_bool(nums->lhs_int > nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float > nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::GreaterEqual: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'>=' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_bool(nums->lhs_int >= nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float >= nums->rhs_float), {}};
    }

    // --- Arithmetic operators ---
    case ir::ExprBinaryOp::Add: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'+' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_int(nums->lhs_int + nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float + nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Subtract: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'-' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_int(nums->lhs_int - nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float - nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Multiply: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'*' requires numeric operands");
        if (nums->both_int)
            return EvalResult{make_int(nums->lhs_int * nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float * nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Divide: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'/' requires numeric operands");
        if (nums->both_int) {
            if (nums->rhs_int == 0)
                return make_error("division by zero");
            return EvalResult{make_int(nums->lhs_int / nums->rhs_int), {}};
        }
        if (nums->rhs_float == 0.0)
            return make_error("division by zero");
        return EvalResult{make_float(nums->lhs_float / nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Modulo: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums)
            return make_error("'%' requires numeric operands");
        if (nums->both_int) {
            if (nums->rhs_int == 0)
                return make_error("modulo by zero");
            return EvalResult{make_int(nums->lhs_int % nums->rhs_int), {}};
        }
        if (nums->rhs_float == 0.0)
            return make_error("modulo by zero");
        return EvalResult{make_float(std::fmod(nums->lhs_float, nums->rhs_float)), {}};
    }
    }

    return make_error("unknown binary operator");
}

// ============================================================================
// MemberAccessExpr
// ============================================================================

EvalResult eval_member_access(const ir::MemberAccessExpr &expr,
                              const EvalContext &ctx,
                              const CallEvalFn *call_eval) {
    if (!expr.base) {
        return make_error("MemberAccessExpr has null base");
    }
    auto base = eval_expr_impl(*expr.base, ctx, call_eval);
    if (base.has_errors())
        return base;

    // Struct member access
    if (auto *sv = std::get_if<StructValue>(&base.value.node)) {
        auto field_it = sv->fields.find(expr.member);
        if (field_it == sv->fields.end() || !field_it->second) {
            return make_error("struct has no field: " + expr.member);
        }
        return EvalResult{clone_value(*field_it->second), {}};
    }

    // List.length
    if (auto *lv = std::get_if<ListValue>(&base.value.node)) {
        if (expr.member == "length") {
            return EvalResult{make_int(static_cast<int64_t>(lv->items.size())), {}};
        }
        return make_error("list has no member: " + expr.member);
    }

    // RFC P7: Set.length / Set.size
    if (auto *sv = std::get_if<SetValue>(&base.value.node)) {
        if (expr.member == "length" || expr.member == "size") {
            return EvalResult{make_int(static_cast<int64_t>(sv->items.size())), {}};
        }
        return make_error("set has no member: " + expr.member);
    }

    // RFC P7: Map.length / Map.size
    if (auto *mv = std::get_if<MapValue>(&base.value.node)) {
        if (expr.member == "length" || expr.member == "size") {
            return EvalResult{make_int(static_cast<int64_t>(mv->entries.size())), {}};
        }
        return make_error("map has no member: " + expr.member);
    }

    return make_error("cannot access member '" + expr.member + "' on this value");
}

// ============================================================================
// IndexAccessExpr
// ============================================================================

EvalResult eval_index_access(const ir::IndexAccessExpr &expr,
                             const EvalContext &ctx,
                             const CallEvalFn *call_eval) {
    if (!expr.base || !expr.index) {
        return make_error("IndexAccessExpr has null operand");
    }
    auto base = eval_expr_impl(*expr.base, ctx, call_eval);
    if (base.has_errors())
        return base;
    auto index = eval_expr_impl(*expr.index, ctx, call_eval);
    if (index.has_errors())
        return index;

    auto *lv = std::get_if<ListValue>(&base.value.node);
    if (lv) {
        auto *iv = std::get_if<IntValue>(&index.value.node);
        if (!iv) {
            return make_error("index must be an integer");
        }
        auto idx = iv->value;
        if (idx < 0 || static_cast<size_t>(idx) >= lv->items.size()) {
            return make_error("index out of bounds: " + std::to_string(idx));
        }
        if (!lv->items[static_cast<size_t>(idx)]) {
            return make_error("list item at index is null");
        }
        return EvalResult{clone_value(*lv->items[static_cast<size_t>(idx)]), {}};
    }

    // RFC P7: Map[key] — returns the value for an equal key, error if absent.
    if (auto *mv = std::get_if<MapValue>(&base.value.node)) {
        for (const auto &entry : mv->entries) {
            if (entry.first && deep_equal(*entry.first, index.value)) {
                if (!entry.second) {
                    return make_error("map entry value is null");
                }
                return EvalResult{clone_value(*entry.second), {}};
            }
        }
        return make_error("map key not found");
    }

    // RFC P7: Set[member] — membership test (Boolean). Mirrors `contains`.
    if (auto *sv = std::get_if<SetValue>(&base.value.node)) {
        for (const auto &item : sv->items) {
            if (item && deep_equal(*item, index.value)) {
                return EvalResult{make_bool(true), {}};
            }
        }
        return EvalResult{make_bool(false), {}};
    }

    return make_error("index access requires list, map, or set base");
}

// ============================================================================
// CallExpr - NOT SUPPORTED in v0.51
// ============================================================================

EvalResult
eval_call_expr(const ir::CallExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    if (call_eval != nullptr && *call_eval) {
        return (*call_eval)(expr, ctx);
    }
    // Fall back to builtin table — used for @builtin functions from stdlib
    // that are lowered directly into IR CallExpr nodes.
    const BuiltinTable &table = BuiltinTable::instance();
    const BuiltinFn *fn = table.find(expr.callee);
    if (fn != nullptr) {
        // Evaluate all arguments
        std::vector<Value> args;
        args.reserve(expr.arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : expr.arguments) {
            if (!arg_ref) {
                return make_error("builtin call: null argument expression");
            }
            EvalResult arg_result = eval_expr_impl(*arg_ref, ctx, call_eval);
            if (arg_result.has_errors()) {
                diags.append(std::move(arg_result.diagnostics));
                return EvalResult{make_none(), std::move(diags)};
            }
            diags.append(std::move(arg_result.diagnostics));
            args.push_back(std::move(arg_result.value));
        }
        EvalResult result = (*fn)(args, ctx);
        result.diagnostics.append(std::move(diags));
        return result;
    }
    return make_error("CallExpr not supported: unknown callee '" + expr.callee + "'");
}

// ============================================================================
// Main eval_expr dispatcher
// ============================================================================

EvalResult
eval_expr_impl(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    return std::visit(
        [&ctx, call_eval](const auto &node) -> EvalResult {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ir::NoneLiteralExpr>) {
                return eval_none_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::BoolLiteralExpr>) {
                return eval_bool_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::IntegerLiteralExpr>) {
                return eval_integer_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::FloatLiteralExpr>) {
                return eval_float_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::DecimalLiteralExpr>) {
                return eval_decimal_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::StringLiteralExpr>) {
                return eval_string_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::DurationLiteralExpr>) {
                return eval_duration_literal(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::SomeExpr>) {
                return eval_some_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::PathExpr>) {
                return eval_path_expr(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::QualifiedValueExpr>) {
                return eval_qualified_value_expr(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::CallExpr>) {
                return eval_call_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::StructLiteralExpr>) {
                return eval_struct_literal(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::ListLiteralExpr>) {
                return eval_list_literal(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::SetLiteralExpr>) {
                return eval_set_literal(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::MapLiteralExpr>) {
                return eval_map_literal(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::UnaryExpr>) {
                return eval_unary_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::BinaryExpr>) {
                return eval_binary_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::MemberAccessExpr>) {
                return eval_member_access(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::IndexAccessExpr>) {
                return eval_index_access(node, ctx, call_eval);
            }
        },
        expr.node);
}

} // anonymous namespace

EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx) {
    return eval_expr_impl(expr, ctx, nullptr);
}

EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn &call_eval) {
    return eval_expr_impl(expr, ctx, &call_eval);
}

} // namespace ahfl::evaluator
