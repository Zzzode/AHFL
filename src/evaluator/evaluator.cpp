#include "ahfl/evaluator/evaluator.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <variant>

namespace ahfl::evaluator {

namespace {

// Helper: create an error result
EvalResult make_error(std::string message) {
    EvalResult result;
    result.value = make_none();
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

// ============================================================================
// Literal evaluation
// ============================================================================

EvalResult eval_none_literal(const ir::NoneLiteralExpr & /*expr*/,
                             const EvalContext & /*ctx*/) {
    return EvalResult{make_none(), {}};
}

EvalResult eval_bool_literal(const ir::BoolLiteralExpr &expr,
                             const EvalContext & /*ctx*/) {
    return EvalResult{make_bool(expr.value), {}};
}

EvalResult eval_integer_literal(const ir::IntegerLiteralExpr &expr,
                                const EvalContext & /*ctx*/) {
    try {
        int64_t val = std::stoll(expr.spelling);
        return EvalResult{make_int(val), {}};
    } catch (...) {
        return make_error("failed to parse integer: " + expr.spelling);
    }
}

EvalResult eval_float_literal(const ir::FloatLiteralExpr &expr,
                              const EvalContext & /*ctx*/) {
    try {
        double val = std::stod(expr.spelling);
        return EvalResult{make_float(val), {}};
    } catch (...) {
        return make_error("failed to parse float: " + expr.spelling);
    }
}

EvalResult eval_decimal_literal(const ir::DecimalLiteralExpr &expr,
                                const EvalContext & /*ctx*/) {
    return EvalResult{make_decimal(expr.spelling), {}};
}

EvalResult eval_string_literal(const ir::StringLiteralExpr &expr,
                               const EvalContext & /*ctx*/) {
    return EvalResult{make_string(expr.spelling), {}};
}

EvalResult eval_duration_literal(const ir::DurationLiteralExpr &expr,
                                 const EvalContext & /*ctx*/) {
    return EvalResult{make_duration(expr.spelling), {}};
}

// ============================================================================
// SomeExpr
// ============================================================================

EvalResult eval_some_expr(const ir::SomeExpr &expr, const EvalContext &ctx) {
    if (!expr.value) {
        return make_error("SomeExpr has null inner expression");
    }
    auto inner = eval_expr(*expr.value, ctx);
    if (inner.has_errors()) return inner;
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
        if (val.has_value()) return EvalResult{std::move(*val), {}};
        return make_error("unresolved path: " + path.root_name);
    }

    // 优先检查 local scope 中是否存在此 root（如 let result = ...）
    auto local_val = ctx.get_local(path.root_name);
    if (local_val.has_value()) {
        // 从 local 变量中逐级访问成员
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
        if (val.has_value()) return EvalResult{std::move(*val), {}};
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
    // 使用 rfind 找到最后一个 :: 分隔符
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

EvalResult eval_struct_literal(const ir::StructLiteralExpr &expr, const EvalContext &ctx) {
    std::unordered_map<std::string, Value> fields;
    DiagnosticBag diagnostics;

    for (const auto &field_init : expr.fields) {
        if (!field_init.value) {
            std::move(diagnostics.error())
                .message("struct field '" + field_init.name + "' has null initializer")
                .emit();
            continue;
        }
        auto result = eval_expr(*field_init.value, ctx);
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

EvalResult eval_list_literal(const ir::ListLiteralExpr &expr, const EvalContext &ctx) {
    std::vector<Value> items;
    DiagnosticBag diagnostics;

    for (const auto &item_expr : expr.items) {
        if (!item_expr) {
            std::move(diagnostics.error()).message("list item has null expression").emit();
            continue;
        }
        auto result = eval_expr(*item_expr, ctx);
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
// UnaryExpr
// ============================================================================

EvalResult eval_unary_expr(const ir::UnaryExpr &expr, const EvalContext &ctx) {
    if (!expr.operand) {
        return make_error("UnaryExpr has null operand");
    }
    auto operand = eval_expr(*expr.operand, ctx);
    if (operand.has_errors()) return operand;

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

EvalResult eval_binary_expr(const ir::BinaryExpr &expr, const EvalContext &ctx) {
    if (!expr.lhs || !expr.rhs) {
        return make_error("BinaryExpr has null operand");
    }

    auto lhs = eval_expr(*expr.lhs, ctx);
    if (lhs.has_errors()) return lhs;
    auto rhs = eval_expr(*expr.rhs, ctx);
    if (rhs.has_errors()) return rhs;

    switch (expr.op) {
    // --- Logical operators ---
    case ir::ExprBinaryOp::And: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb) return make_error("'and' requires Bool operands");
        return EvalResult{make_bool(lb->value && rb->value), {}};
    }
    case ir::ExprBinaryOp::Or: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb) return make_error("'or' requires Bool operands");
        return EvalResult{make_bool(lb->value || rb->value), {}};
    }
    case ir::ExprBinaryOp::Implies: {
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb = std::get_if<BoolValue>(&rhs.value.node);
        if (!lb || !rb) return make_error("'implies' requires Bool operands");
        return EvalResult{make_bool(!lb->value || rb->value), {}};
    }

    // --- Equality operators ---
    case ir::ExprBinaryOp::Equal: {
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
        if (lb && rb) return EvalResult{make_bool(lb->value == rb->value), {}};
        // String == String
        auto *ls = std::get_if<StringValue>(&lhs.value.node);
        auto *rs = std::get_if<StringValue>(&rhs.value.node);
        if (ls && rs) return EvalResult{make_bool(ls->value == rs->value), {}};
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
        // Inline equality check and negate result
        if (auto nums = extract_numeric(lhs.value, rhs.value)) {
            if (nums->both_int) return EvalResult{make_bool(nums->lhs_int != nums->rhs_int), {}};
            return EvalResult{make_bool(nums->lhs_float != nums->rhs_float), {}};
        }
        auto *lb = std::get_if<BoolValue>(&lhs.value.node);
        auto *rb2 = std::get_if<BoolValue>(&rhs.value.node);
        if (lb && rb2) return EvalResult{make_bool(lb->value != rb2->value), {}};
        auto *ls = std::get_if<StringValue>(&lhs.value.node);
        auto *rs = std::get_if<StringValue>(&rhs.value.node);
        if (ls && rs) return EvalResult{make_bool(ls->value != rs->value), {}};
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
        if (!nums) return make_error("'<' requires numeric operands");
        if (nums->both_int) return EvalResult{make_bool(nums->lhs_int < nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float < nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::LessEqual: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'<=' requires numeric operands");
        if (nums->both_int) return EvalResult{make_bool(nums->lhs_int <= nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float <= nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Greater: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'>' requires numeric operands");
        if (nums->both_int) return EvalResult{make_bool(nums->lhs_int > nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float > nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::GreaterEqual: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'>=' requires numeric operands");
        if (nums->both_int) return EvalResult{make_bool(nums->lhs_int >= nums->rhs_int), {}};
        return EvalResult{make_bool(nums->lhs_float >= nums->rhs_float), {}};
    }

    // --- Arithmetic operators ---
    case ir::ExprBinaryOp::Add: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'+' requires numeric operands");
        if (nums->both_int) return EvalResult{make_int(nums->lhs_int + nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float + nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Subtract: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'-' requires numeric operands");
        if (nums->both_int) return EvalResult{make_int(nums->lhs_int - nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float - nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Multiply: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'*' requires numeric operands");
        if (nums->both_int) return EvalResult{make_int(nums->lhs_int * nums->rhs_int), {}};
        return EvalResult{make_float(nums->lhs_float * nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Divide: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'/' requires numeric operands");
        if (nums->both_int) {
            if (nums->rhs_int == 0) return make_error("division by zero");
            return EvalResult{make_int(nums->lhs_int / nums->rhs_int), {}};
        }
        if (nums->rhs_float == 0.0) return make_error("division by zero");
        return EvalResult{make_float(nums->lhs_float / nums->rhs_float), {}};
    }
    case ir::ExprBinaryOp::Modulo: {
        auto nums = extract_numeric(lhs.value, rhs.value);
        if (!nums) return make_error("'%' requires numeric operands");
        if (nums->both_int) {
            if (nums->rhs_int == 0) return make_error("modulo by zero");
            return EvalResult{make_int(nums->lhs_int % nums->rhs_int), {}};
        }
        if (nums->rhs_float == 0.0) return make_error("modulo by zero");
        return EvalResult{make_float(std::fmod(nums->lhs_float, nums->rhs_float)), {}};
    }
    }

    return make_error("unknown binary operator");
}

// ============================================================================
// MemberAccessExpr
// ============================================================================

EvalResult eval_member_access(const ir::MemberAccessExpr &expr, const EvalContext &ctx) {
    if (!expr.base) {
        return make_error("MemberAccessExpr has null base");
    }
    auto base = eval_expr(*expr.base, ctx);
    if (base.has_errors()) return base;

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

    return make_error("cannot access member '" + expr.member + "' on this value");
}

// ============================================================================
// IndexAccessExpr
// ============================================================================

EvalResult eval_index_access(const ir::IndexAccessExpr &expr, const EvalContext &ctx) {
    if (!expr.base || !expr.index) {
        return make_error("IndexAccessExpr has null operand");
    }
    auto base = eval_expr(*expr.base, ctx);
    if (base.has_errors()) return base;
    auto index = eval_expr(*expr.index, ctx);
    if (index.has_errors()) return index;

    auto *lv = std::get_if<ListValue>(&base.value.node);
    if (!lv) {
        return make_error("index access requires list base");
    }
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

// ============================================================================
// GroupExpr
// ============================================================================

EvalResult eval_group_expr(const ir::GroupExpr &expr, const EvalContext &ctx) {
    if (!expr.expr) {
        return make_error("GroupExpr has null inner expression");
    }
    return eval_expr(*expr.expr, ctx);
}

// ============================================================================
// CallExpr - NOT SUPPORTED in v0.51
// ============================================================================

EvalResult eval_call_expr(const ir::CallExpr & /*expr*/, const EvalContext & /*ctx*/) {
    return make_error("CallExpr not supported in v0.51");
}

} // anonymous namespace

// ============================================================================
// Main eval_expr dispatcher
// ============================================================================

EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx) {
    return std::visit([&ctx](const auto &node) -> EvalResult {
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
            return eval_some_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::PathExpr>) {
            return eval_path_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::QualifiedValueExpr>) {
            return eval_qualified_value_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::CallExpr>) {
            return eval_call_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::StructLiteralExpr>) {
            return eval_struct_literal(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::ListLiteralExpr>) {
            return eval_list_literal(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::SetLiteralExpr>) {
            return make_error("SetLiteralExpr not supported in v0.51");
        } else if constexpr (std::is_same_v<T, ir::MapLiteralExpr>) {
            return make_error("MapLiteralExpr not supported in v0.51");
        } else if constexpr (std::is_same_v<T, ir::UnaryExpr>) {
            return eval_unary_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::BinaryExpr>) {
            return eval_binary_expr(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::MemberAccessExpr>) {
            return eval_member_access(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::IndexAccessExpr>) {
            return eval_index_access(node, ctx);
        } else if constexpr (std::is_same_v<T, ir::GroupExpr>) {
            return eval_group_expr(node, ctx);
        }
    }, expr.node);
}

} // namespace ahfl::evaluator
