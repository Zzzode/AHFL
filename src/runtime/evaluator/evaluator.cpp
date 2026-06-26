#include "runtime/evaluator/evaluator.hpp"
#include "ahfl/base/support/overloaded.hpp"
#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/executor.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

[[nodiscard]] const ir::FnDecl *find_function(const ir::Program &program,
                                              std::string_view name) noexcept {
    for (const auto &decl : program.declarations) {
        const auto *fn = std::get_if<ir::FnDecl>(&decl);
        if (fn != nullptr && fn->name == name) {
            return fn;
        }
    }
    return nullptr;
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

bool deep_equal_value_ptr(const std::unique_ptr<Value> &lhs, const std::unique_ptr<Value> &rhs) {
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
        if (le->enum_name != re->enum_name || le->variant != re->variant ||
            le->payload.size() != re->payload.size()) {
            return false;
        }
        for (size_t i = 0; i < le->payload.size(); ++i) {
            if (!deep_equal_value_ptr(le->payload[i], re->payload[i])) {
                return false;
            }
        }
        return true;
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

using PatternBindings = std::unordered_map<std::string, Value>;

[[nodiscard]] std::string_view last_path_segment(std::string_view path) {
    const auto pos = path.rfind("::");
    if (pos == std::string_view::npos) {
        return path;
    }
    return path.substr(pos + 2);
}

[[nodiscard]] bool bind_pattern_name(PatternBindings &bindings,
                                     std::string_view name,
                                     const Value &value) {
    if (name.empty() || name == "_") {
        return true;
    }
    bindings.insert_or_assign(std::string{name}, clone_value(value));
    return true;
}

[[nodiscard]] PatternBindings clone_pattern_bindings(const PatternBindings &bindings) {
    PatternBindings cloned;
    for (const auto &[name, value] : bindings) {
        cloned.emplace(name, clone_value(value));
    }
    return cloned;
}

[[nodiscard]] bool match_pattern(const ir::MatchPattern &pattern,
                                 const Value &value,
                                 PatternBindings &bindings);

[[nodiscard]] bool match_literal_pattern(std::string_view spelling, const Value &value) {
    if (spelling == "none") {
        if (std::holds_alternative<NoneValue>(value.node)) {
            return true;
        }
        if (const auto *optional = std::get_if<OptionalValue>(&value.node); optional != nullptr) {
            return optional->inner == nullptr;
        }
        return false;
    }
    if (spelling == "true" || spelling == "false") {
        const auto *boolean = std::get_if<BoolValue>(&value.node);
        return boolean != nullptr && boolean->value == (spelling == "true");
    }
    if (spelling.size() >= 2 && spelling.front() == '"' && spelling.back() == '"') {
        const auto *string_value = std::get_if<StringValue>(&value.node);
        return string_value != nullptr &&
               string_value->value == std::string{spelling.substr(1, spelling.size() - 2)};
    }
    try {
        const auto integer = std::stoll(std::string{spelling});
        const auto *int_value = std::get_if<IntValue>(&value.node);
        return int_value != nullptr && int_value->value == integer;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool match_variant_pattern(const ir::VariantPattern &pattern,
                                         const Value &value,
                                         PatternBindings &bindings) {
    const auto variant_name = last_path_segment(pattern.path);
    if (const auto *optional = std::get_if<OptionalValue>(&value.node); optional != nullptr) {
        if (variant_name == "None") {
            return optional->inner == nullptr && pattern.subpatterns.empty();
        }
        if (variant_name != "Some" || optional->inner == nullptr) {
            return false;
        }
        if (pattern.subpatterns.empty()) {
            return true;
        }
        if (pattern.subpatterns.size() != 1 || !pattern.subpatterns.front()) {
            return false;
        }
        return match_pattern(*pattern.subpatterns.front(), *optional->inner, bindings);
    }

    const auto *enum_value = std::get_if<EnumValue>(&value.node);
    if (enum_value == nullptr || enum_value->variant != variant_name) {
        return false;
    }
    if (pattern.subpatterns.empty()) {
        return true;
    }
    // P3 match payloads are stored two ways in evaluator EnumValues:
    //   * Nominal single-field variants (e.g. Option::Some, JsonValue::JInt)
    //     use the `associated` unique_ptr (set by make_enum(..., associated)).
    //   * Positional multi-field variants use the `payload` vector
    //     (set by make_enum(..., vector<Value>)).
    //
    // The runtime can encounter either depending on the construction path
    // (builtins for nominal, general enum lowering for positional), so a
    // pattern match must try both.
    const bool has_associated = enum_value->associated != nullptr;
    const bool has_payload_vec = !enum_value->payload.empty();
    if (!has_associated && !has_payload_vec) {
        return false;
    }
    if (has_payload_vec && pattern.subpatterns.size() == enum_value->payload.size()) {
        for (std::size_t index = 0; index < pattern.subpatterns.size(); ++index) {
            if (!pattern.subpatterns[index] || !enum_value->payload[index]) {
                return false;
            }
            if (!match_pattern(*pattern.subpatterns[index],
                               *enum_value->payload[index],
                               bindings)) {
                return false;
            }
        }
        return true;
    }
    if (has_associated && pattern.subpatterns.size() == 1) {
        if (!pattern.subpatterns.front()) {
            return false;
        }
        return match_pattern(*pattern.subpatterns.front(),
                             *enum_value->associated,
                             bindings);
    }
    return false;
}

[[nodiscard]] bool match_pattern(const ir::MatchPattern &pattern,
                                 const Value &value,
                                 PatternBindings &bindings) {
    return std::visit(
        Overloaded{
            [&](const ir::LiteralPattern &literal) {
                return match_literal_pattern(literal.spelling, value);
            },
            [&](const ir::VariantPattern &variant) {
                return match_variant_pattern(variant, value, bindings);
            },
            [](const ir::WildcardPattern &) { return true; },
            [&](const ir::BindingPattern &binding) {
                PatternBindings nested_bindings = clone_pattern_bindings(bindings);
                if (binding.nested &&
                    !match_pattern(*binding.nested, value, nested_bindings)) {
                    return false;
                }
                (void)bind_pattern_name(nested_bindings, binding.name, value);
                bindings = std::move(nested_bindings);
                return true;
            },
            [](const ir::TuplePattern &) { return false; },
            [&](const ir::OrPattern &pattern_or) {
                for (const auto &branch : pattern_or.branches) {
                    if (!branch) {
                        continue;
                    }
                    PatternBindings branch_bindings = clone_pattern_bindings(bindings);
                    if (match_pattern(*branch, value, branch_bindings)) {
                        bindings = std::move(branch_bindings);
                        return true;
                    }
                }
                return false;
            },
        },
        pattern.node);
}

// ============================================================================
// Literal evaluation
// ============================================================================

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

std::string decode_string_literal_spelling(std::string_view spelling) {
    if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"') {
        return std::string{spelling};
    }
    std::string decoded;
    decoded.reserve(spelling.size() - 2);
    for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
        const char ch = spelling[index];
        if (ch != '\\' || index + 2 >= spelling.size()) {
            decoded.push_back(ch);
            continue;
        }
        const char escaped = spelling[++index];
        switch (escaped) {
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case '\\':
            decoded.push_back('\\');
            break;
        case '"':
            decoded.push_back('"');
            break;
        default:
            decoded.push_back(escaped);
            break;
        }
    }
    return decoded;
}

EvalResult eval_string_literal(const ir::StringLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_string(decode_string_literal_spelling(expr.spelling)), {}};
}

EvalResult eval_duration_literal(const ir::DurationLiteralExpr &expr, const EvalContext & /*ctx*/) {
    return EvalResult{make_duration(expr.spelling), {}};
}

// ============================================================================
// LambdaExpr
// ============================================================================

EvalResult eval_lambda_expr(const ir::LambdaExpr &expr, const EvalContext &ctx) {
    if (!expr.body) {
        return make_error("LambdaExpr has null body");
    }
    return EvalResult{
        make_callable(expr.params, expr.body.get(), std::make_shared<EvalContext>(ctx)),
        {},
    };
}

EvalResult invoke_callable_value(const CallableValue &callable,
                                 const std::vector<Value> &args,
                                 const CallEvalFn *call_eval) {
    if (callable.body == nullptr) {
        return make_error("callable value has no body");
    }
    if (callable.params.size() != args.size()) {
        return make_error("callable expected " + std::to_string(callable.params.size()) +
                          " arguments, got " + std::to_string(args.size()));
    }

    EvalContext context = callable.captured_context ? *callable.captured_context : EvalContext{};
    for (std::size_t index = 0; index < callable.params.size(); ++index) {
        context.bind_local(callable.params[index], clone_value(args[index]));
    }
    return eval_expr_impl(*callable.body, context, call_eval);
}

struct EvaluatedArguments {
    std::vector<Value> values;
};

[[nodiscard]] std::optional<EvaluatedArguments>
eval_call_arguments(const ir::CallExpr &expr,
                    const EvalContext &ctx,
                    const CallEvalFn *call_eval,
                    DiagnosticBag &diagnostics) {
    EvaluatedArguments result;
    result.values.reserve(expr.arguments.size());
    for (const auto &arg_ref : expr.arguments) {
        if (!arg_ref) {
            std::move(diagnostics.error()).message("call: null argument expression").emit();
            return std::nullopt;
        }
        EvalResult arg_result = eval_expr_impl(*arg_ref, ctx, call_eval);
        diagnostics.append(std::move(arg_result.diagnostics));
        if (diagnostics.has_error()) {
            return std::nullopt;
        }
        result.values.push_back(std::move(arg_result.value));
    }
    return result;
}

[[nodiscard]] EvalResult append_diagnostics(EvalResult result, DiagnosticBag diagnostics) {
    result.diagnostics.append(std::move(diagnostics));
    return result;
}

[[nodiscard]] EvalResult call_arity_error(std::string_view callee,
                                          std::size_t expected,
                                          std::size_t actual,
                                          DiagnosticBag diagnostics) {
    auto result = make_error(std::string{callee} + " expected " + std::to_string(expected) +
                             " arguments, got " + std::to_string(actual));
    result.diagnostics.append(std::move(diagnostics));
    return result;
}

[[nodiscard]] EvalResult call_type_error(std::string_view callee,
                                         std::string_view message,
                                         DiagnosticBag diagnostics) {
    auto result = make_error(std::string{callee} + ": " + std::string{message});
    result.diagnostics.append(std::move(diagnostics));
    return result;
}

struct OptionReader {
    bool is_optional;
    bool is_none;            // meaningful only when is_optional
    const Value *inner;      // inner when is_optional && !is_none, nullptr otherwise
};

[[nodiscard]] OptionReader read_option(const Value &value) noexcept {
    if (const auto *ov = std::get_if<OptionalValue>(&value.node)) {
        if (ov->inner == nullptr) return {true, true, nullptr};
        return {true, false, ov->inner.get()};
    }
    if (const auto *ev = std::get_if<EnumValue>(&value.node)) {
        if (ev->enum_name != "std::option::Option") return {false, false, nullptr};
        if (ev->variant == "None") return {true, true, nullptr};
        if (ev->variant == "Some") {
            const Value *inner = ev->associated.get();
            if (inner == nullptr && !ev->payload.empty()) inner = ev->payload.front().get();
            return {true, false, inner};
        }
    }
    return {false, false, nullptr};
}

[[nodiscard]] const OptionalValue *as_option(const Value &value) noexcept {
    // P5 Big Bang: Option is nominal EnumValue now. Retain legacy OptionalValue
    // path only for construction sites that have not been migrated yet.
    if (auto *legacy = std::get_if<OptionalValue>(&value.node)) return legacy;
    // Fall back to a thread-local conversion for the const OptionalValue*
    // return type (read_option is the preferred dual-path API).
    thread_local OptionalValue cached;
    auto reader = read_option(value);
    if (!reader.is_optional) return nullptr;
    if (reader.is_none) {
        cached = OptionalValue{.inner = nullptr};
    } else {
        cached = OptionalValue{.inner = reader.inner ? std::make_unique<Value>(clone_value(*reader.inner)) : nullptr};
    }
    return &cached;
}

[[nodiscard]] const EnumValue *as_result(const Value &value) noexcept {
    const auto *result = std::get_if<EnumValue>(&value.node);
    if (result == nullptr || result->enum_name != "std::result::Result") {
        return nullptr;
    }
    return result;
}

[[nodiscard]] const CallableValue *as_callable(const Value &value) noexcept {
    return std::get_if<CallableValue>(&value.node);
}

[[nodiscard]] Value make_result_value(std::string variant, Value payload) {
    std::vector<Value> payloads;
    payloads.push_back(std::move(payload));
    return make_enum("std::result::Result", std::move(variant), std::move(payloads));
}

[[nodiscard]] EvalResult invoke_unary_callable(const CallableValue &callable,
                                               const Value &arg,
                                               const CallEvalFn *call_eval) {
    std::vector<Value> args;
    args.push_back(clone_value(arg));
    return invoke_callable_value(callable, args, call_eval);
}

[[nodiscard]] EvalResult invoke_binary_callable(const CallableValue &callable,
                                                const Value &lhs,
                                                const Value &rhs,
                                                const CallEvalFn *call_eval) {
    std::vector<Value> args;
    args.push_back(clone_value(lhs));
    args.push_back(clone_value(rhs));
    return invoke_callable_value(callable, args, call_eval);
}

[[nodiscard]] std::optional<EvalResult>
eval_stdlib_wrapper_call(const ir::CallExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    const std::string_view callee = expr.callee;
    const auto is_known = [callee](std::initializer_list<std::string_view> names) {
        return std::find(names.begin(), names.end(), callee) != names.end();
    };

    if (!is_known({
            "std::option::is_some",
            "std::option::is_none",
            "std::option::map",
            "std::option::and_then",
            "std::option::or_else",
            "std::option::filter",
            "std::option::unwrap_or",
            "std::option::unwrap_or_else",
            "std::option::get_or_insert",
            "std::result::is_ok",
            "std::result::is_err",
            "std::result::map",
            "std::result::map_err",
            "std::result::and_then",
            "std::result::or_else",
            "std::result::unwrap_or",
            "std::result::ok",
            "std::result::err",
            "std::collections::empty",
            "std::collections::singleton",
            "std::collections::is_empty",
            "std::collections::length",
            "std::collections::list_get",
            "std::collections::first",
            "std::collections::last",
            "std::collections::append",
            "std::collections::map",
            "std::collections::filter",
            "std::collections::fold",
            "std::collections::set_empty",
            "std::collections::set_singleton",
            "std::collections::set_is_empty",
            "std::collections::contains",
            "std::collections::map_empty",
            "std::collections::map_singleton",
            "std::collections::map_is_empty",
            "std::collections::contains_key",
            "std::collections::map_get",
            "std::string::is_empty",
            "std::string::length",
            "std::string::concat",
        })) {
        return std::nullopt;
    }

    DiagnosticBag diagnostics;
    auto evaluated = eval_call_arguments(expr, ctx, call_eval, diagnostics);
    if (!evaluated.has_value()) {
        return EvalResult{make_none(), std::move(diagnostics)};
    }
    const auto &args = evaluated->values;
    const auto arity = [&](std::size_t expected) -> std::optional<EvalResult> {
        if (args.size() == expected) {
            return std::nullopt;
        }
        return call_arity_error(callee, expected, args.size(), std::move(diagnostics));
    };

    const auto option_payload = [&](const Value &value,
                                    std::string_view operation) -> const OptionalValue * {
        const auto *option = as_option(value);
        if (option == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": first argument must be Option")
                .emit();
        }
        return option;
    };
    const auto result_payload = [&](const Value &value, std::string_view operation) -> const EnumValue * {
        const auto *result = as_result(value);
        if (result == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": first argument must be Result")
                .emit();
        }
        return result;
    };
    const auto require_callable_arg = [&](std::size_t index,
                                          std::string_view operation) -> const CallableValue * {
        const auto *callable = index < args.size() ? as_callable(args[index]) : nullptr;
        if (callable == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be callable")
                .emit();
        }
        return callable;
    };
    const auto require_list_arg = [&](std::size_t index,
                                      std::string_view operation) -> const ListValue * {
        const auto *list = index < args.size() ? std::get_if<ListValue>(&args[index].node) : nullptr;
        if (list == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be List")
                .emit();
        }
        return list;
    };
    const auto require_set_arg = [&](std::size_t index,
                                     std::string_view operation) -> const SetValue * {
        const auto *set = index < args.size() ? std::get_if<SetValue>(&args[index].node) : nullptr;
        if (set == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be Set")
                .emit();
        }
        return set;
    };
    const auto require_map_arg = [&](std::size_t index,
                                     std::string_view operation) -> const MapValue * {
        const auto *map = index < args.size() ? std::get_if<MapValue>(&args[index].node) : nullptr;
        if (map == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be Map")
                .emit();
        }
        return map;
    };
    const auto require_int_arg = [&](std::size_t index,
                                     std::string_view operation) -> const IntValue * {
        const auto *integer = index < args.size() ? std::get_if<IntValue>(&args[index].node) : nullptr;
        if (integer == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be Int")
                .emit();
        }
        return integer;
    };
    const auto require_string_arg = [&](std::size_t index,
                                        std::string_view operation) -> const StringValue * {
        const auto *string = index < args.size() ? std::get_if<StringValue>(&args[index].node) : nullptr;
        if (string == nullptr) {
            std::move(diagnostics.error())
                .message(std::string{operation} + ": argument " + std::to_string(index + 1) +
                         " must be String")
                .emit();
        }
        return string;
    };
    const auto fail_if_diagnostics = [&]() -> std::optional<EvalResult> {
        if (!diagnostics.has_error()) {
            return std::nullopt;
        }
        return EvalResult{make_none(), std::move(diagnostics)};
    };

    if (callee == "std::option::is_some" || callee == "std::option::is_none") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        const bool is_some = option->inner != nullptr;
        return append_diagnostics(EvalResult{make_bool(callee == "std::option::is_some" ? is_some : !is_some), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::option::map" || callee == "std::option::and_then") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (option->inner == nullptr) {
            return append_diagnostics(EvalResult{make_optional_none(), {}}, std::move(diagnostics));
        }
        EvalResult mapped = invoke_unary_callable(*callable, *option->inner, call_eval);
        if (callee == "std::option::map" && !mapped.has_errors()) {
            mapped.value = make_optional_some(std::move(mapped.value));
        }
        return append_diagnostics(std::move(mapped), std::move(diagnostics));
    }
    if (callee == "std::option::or_else") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (option->inner != nullptr) {
            return append_diagnostics(EvalResult{make_optional_some(clone_value(*option->inner)), {}},
                                      std::move(diagnostics));
        }
        EvalResult alternative = invoke_callable_value(*callable, {}, call_eval);
        return append_diagnostics(std::move(alternative), std::move(diagnostics));
    }
    if (callee == "std::option::filter") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (option->inner == nullptr) {
            return append_diagnostics(EvalResult{make_optional_none(), {}}, std::move(diagnostics));
        }
        EvalResult keep = invoke_unary_callable(*callable, *option->inner, call_eval);
        if (keep.has_errors()) {
            return append_diagnostics(std::move(keep), std::move(diagnostics));
        }
        const auto *bool_value = std::get_if<BoolValue>(&keep.value.node);
        if (bool_value == nullptr) {
            return call_type_error(callee, "predicate must return Bool", std::move(diagnostics));
        }
        return append_diagnostics(
            EvalResult{bool_value->value ? make_optional_some(clone_value(*option->inner))
                                         : make_optional_none(),
                       std::move(keep.diagnostics)},
            std::move(diagnostics));
    }
    if (callee == "std::option::unwrap_or" || callee == "std::option::get_or_insert") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        return append_diagnostics(EvalResult{option->inner ? clone_value(*option->inner)
                                                           : clone_value(args[1]),
                                             {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::option::unwrap_or_else") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *option = option_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (option->inner != nullptr) {
            return append_diagnostics(EvalResult{clone_value(*option->inner), {}},
                                      std::move(diagnostics));
        }
        EvalResult fallback = invoke_callable_value(*callable, {}, call_eval);
        return append_diagnostics(std::move(fallback), std::move(diagnostics));
    }

    if (callee == "std::result::is_ok" || callee == "std::result::is_err") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        const auto *result = result_payload(args[0], callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        const bool is_ok = result->variant == "Ok";
        return append_diagnostics(EvalResult{make_bool(callee == "std::result::is_ok" ? is_ok : !is_ok), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::result::map" || callee == "std::result::map_err") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *result = result_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (result->payload.empty() || result->payload.front() == nullptr) {
            return call_type_error(callee, "Result payload is missing", std::move(diagnostics));
        }
        const bool transform_ok = callee == "std::result::map";
        if ((result->variant == "Ok") != transform_ok) {
            return append_diagnostics(
                EvalResult{make_result_value(result->variant,
                                             clone_value(*result->payload.front())),
                           {}},
                std::move(diagnostics));
        }
        EvalResult transformed = invoke_unary_callable(*callable, *result->payload.front(), call_eval);
        if (transformed.has_errors()) {
            return append_diagnostics(std::move(transformed), std::move(diagnostics));
        }
        transformed.value = make_result_value(result->variant, std::move(transformed.value));
        return append_diagnostics(std::move(transformed), std::move(diagnostics));
    }
    if (callee == "std::result::and_then" || callee == "std::result::or_else") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *result = result_payload(args[0], callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (result->payload.empty() || result->payload.front() == nullptr) {
            return call_type_error(callee, "Result payload is missing", std::move(diagnostics));
        }
        const bool call_on_ok = callee == "std::result::and_then";
        if ((result->variant == "Ok") == call_on_ok) {
            EvalResult chained = invoke_unary_callable(*callable, *result->payload.front(), call_eval);
            return append_diagnostics(std::move(chained), std::move(diagnostics));
        }
        return append_diagnostics(
            EvalResult{make_result_value(result->variant,
                                         clone_value(*result->payload.front())),
                       {}},
            std::move(diagnostics));
    }
    if (callee == "std::result::unwrap_or") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *result = result_payload(args[0], callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (result->variant == "Ok") {
            if (result->payload.empty() || result->payload.front() == nullptr) {
                return call_type_error(callee, "Result payload is missing", std::move(diagnostics));
            }
            return append_diagnostics(EvalResult{clone_value(*result->payload.front()), {}},
                                      std::move(diagnostics));
        }
        return append_diagnostics(EvalResult{clone_value(args[1]), {}}, std::move(diagnostics));
    }
    if (callee == "std::result::ok" || callee == "std::result::err") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        const auto *result = result_payload(args[0], callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (result->payload.empty() || result->payload.front() == nullptr) {
            return call_type_error(callee, "Result payload is missing", std::move(diagnostics));
        }
        const bool want_ok = callee == "std::result::ok";
        return append_diagnostics(
            EvalResult{(result->variant == "Ok") == want_ok
                           ? make_optional_some(clone_value(*result->payload.front()))
                           : make_optional_none(),
                       {}},
            std::move(diagnostics));
    }

    if (callee == "std::collections::empty") {
        if (auto error = arity(0)) {
            return std::move(*error);
        }
        return append_diagnostics(EvalResult{make_list({}), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::singleton") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        std::vector<Value> items;
        items.push_back(clone_value(args[0]));
        return append_diagnostics(EvalResult{make_list(std::move(items)), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::collections::is_empty" || callee == "std::collections::length") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        const auto *list = require_list_arg(0, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (callee == "std::collections::is_empty") {
            return append_diagnostics(EvalResult{make_bool(list->items.empty()), {}},
                                      std::move(diagnostics));
        }
        return append_diagnostics(EvalResult{make_int(static_cast<std::int64_t>(list->items.size())), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::collections::list_get" || callee == "std::collections::first" ||
        callee == "std::collections::last") {
        const std::size_t expected = callee == "std::collections::list_get" ? 2U : 1U;
        if (auto error = arity(expected)) {
            return std::move(*error);
        }
        const auto *list = require_list_arg(0, callee);
        const auto *index = callee == "std::collections::list_get" ? require_int_arg(1, callee)
                                                                    : nullptr;
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        std::int64_t item_index = 0;
        if (callee == "std::collections::last") {
            item_index = static_cast<std::int64_t>(list->items.size()) - 1;
        } else if (index != nullptr) {
            item_index = index->value;
        }
        if (item_index < 0 || static_cast<std::size_t>(item_index) >= list->items.size() ||
            list->items[static_cast<std::size_t>(item_index)] == nullptr) {
            return append_diagnostics(EvalResult{make_optional_none(), {}}, std::move(diagnostics));
        }
        return append_diagnostics(
            EvalResult{make_optional_some(clone_value(*list->items[static_cast<std::size_t>(item_index)])), {}},
            std::move(diagnostics));
    }
    if (callee == "std::collections::append") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *left = require_list_arg(0, callee);
        const auto *right = require_list_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        std::vector<Value> items;
        items.reserve(left->items.size() + right->items.size());
        for (const auto &item : left->items) {
            items.push_back(item ? clone_value(*item) : make_none());
        }
        for (const auto &item : right->items) {
            items.push_back(item ? clone_value(*item) : make_none());
        }
        return append_diagnostics(EvalResult{make_list(std::move(items)), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::map" || callee == "std::collections::filter") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *list = require_list_arg(0, callee);
        const auto *callable = require_callable_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        std::vector<Value> items;
        items.reserve(list->items.size());
        for (const auto &item : list->items) {
            Value item_value = item ? clone_value(*item) : make_none();
            EvalResult applied = invoke_unary_callable(*callable, item_value, call_eval);
            if (applied.has_errors()) {
                return append_diagnostics(std::move(applied), std::move(diagnostics));
            }
            if (callee == "std::collections::map") {
                items.push_back(std::move(applied.value));
                continue;
            }
            const auto *keep = std::get_if<BoolValue>(&applied.value.node);
            if (keep == nullptr) {
                return call_type_error(callee, "predicate must return Bool", std::move(diagnostics));
            }
            if (keep->value) {
                items.push_back(std::move(item_value));
            }
            diagnostics.append(std::move(applied.diagnostics));
        }
        return append_diagnostics(EvalResult{make_list(std::move(items)), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::fold") {
        if (auto error = arity(3)) {
            return std::move(*error);
        }
        const auto *list = require_list_arg(0, callee);
        const auto *callable = require_callable_arg(2, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        Value accumulator = clone_value(args[1]);
        for (const auto &item : list->items) {
            const Value item_value = item ? clone_value(*item) : make_none();
            EvalResult updated = invoke_binary_callable(*callable, accumulator, item_value, call_eval);
            if (updated.has_errors()) {
                return append_diagnostics(std::move(updated), std::move(diagnostics));
            }
            diagnostics.append(std::move(updated.diagnostics));
            accumulator = std::move(updated.value);
        }
        return append_diagnostics(EvalResult{std::move(accumulator), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::set_empty") {
        if (auto error = arity(0)) {
            return std::move(*error);
        }
        return append_diagnostics(EvalResult{make_set({}), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::set_singleton") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        std::vector<Value> items;
        items.push_back(clone_value(args[0]));
        return append_diagnostics(EvalResult{make_set(std::move(items)), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::collections::set_is_empty" || callee == "std::collections::contains") {
        const std::size_t expected = callee == "std::collections::contains" ? 2U : 1U;
        if (auto error = arity(expected)) {
            return std::move(*error);
        }
        const auto *set = require_set_arg(0, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (callee == "std::collections::set_is_empty") {
            return append_diagnostics(EvalResult{make_bool(set->items.empty()), {}},
                                      std::move(diagnostics));
        }
        for (const auto &item : set->items) {
            if (item && deep_equal(*item, args[1])) {
                return append_diagnostics(EvalResult{make_bool(true), {}}, std::move(diagnostics));
            }
        }
        return append_diagnostics(EvalResult{make_bool(false), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::map_empty") {
        if (auto error = arity(0)) {
            return std::move(*error);
        }
        return append_diagnostics(EvalResult{make_map({}), {}}, std::move(diagnostics));
    }
    if (callee == "std::collections::map_singleton") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        std::vector<std::pair<Value, Value>> entries;
        entries.emplace_back(clone_value(args[0]), clone_value(args[1]));
        return append_diagnostics(EvalResult{make_map(std::move(entries)), {}},
                                  std::move(diagnostics));
    }
    if (callee == "std::collections::map_is_empty" ||
        callee == "std::collections::contains_key" ||
        callee == "std::collections::map_get") {
        const std::size_t expected = callee == "std::collections::map_is_empty" ? 1U : 2U;
        if (auto error = arity(expected)) {
            return std::move(*error);
        }
        const auto *map = require_map_arg(0, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (callee == "std::collections::map_is_empty") {
            return append_diagnostics(EvalResult{make_bool(map->entries.empty()), {}},
                                      std::move(diagnostics));
        }
        for (const auto &entry : map->entries) {
            if (!entry.first || !deep_equal(*entry.first, args[1])) {
                continue;
            }
            if (callee == "std::collections::contains_key") {
                return append_diagnostics(EvalResult{make_bool(true), {}}, std::move(diagnostics));
            }
            if (!entry.second) {
                return call_type_error(callee, "map entry value is missing", std::move(diagnostics));
            }
            return append_diagnostics(EvalResult{make_optional_some(clone_value(*entry.second)), {}},
                                      std::move(diagnostics));
        }
        return append_diagnostics(
            EvalResult{callee == "std::collections::contains_key" ? make_bool(false)
                                                                  : make_optional_none(),
                       {}},
            std::move(diagnostics));
    }
    if (callee == "std::string::is_empty" || callee == "std::string::length") {
        if (auto error = arity(1)) {
            return std::move(*error);
        }
        const auto *string = require_string_arg(0, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        if (callee == "std::string::is_empty") {
            return append_diagnostics(EvalResult{make_bool(string->value.empty()), {}},
                                      std::move(diagnostics));
        }
        return append_diagnostics(
            EvalResult{make_int(static_cast<std::int64_t>(string->value.size())), {}},
            std::move(diagnostics));
    }
    if (callee == "std::string::concat") {
        if (auto error = arity(2)) {
            return std::move(*error);
        }
        const auto *left = require_string_arg(0, callee);
        const auto *right = require_string_arg(1, callee);
        if (auto error = fail_if_diagnostics()) {
            return std::move(*error);
        }
        return append_diagnostics(EvalResult{make_string(left->value + right->value), {}},
                                  std::move(diagnostics));
    }

    return call_type_error(callee, "unsupported stdlib wrapper", std::move(diagnostics));
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
                std::holds_alternative<EnumValue>(lnode) ||
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
                std::holds_alternative<EnumValue>(lnode) ||
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
    if (auto *lv = get_list_if(base.value)) {
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

    auto *lv = get_list_if(base.value);
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
// MatchExpr
// ============================================================================

EvalResult eval_match_expr(const ir::MatchExpr &expr,
                           const EvalContext &ctx,
                           const CallEvalFn *call_eval) {
    if (!expr.scrutinee) {
        return make_error("MatchExpr has null scrutinee");
    }

    auto scrutinee = eval_expr_impl(*expr.scrutinee, ctx, call_eval);
    if (scrutinee.has_errors()) {
        return scrutinee;
    }

    for (const auto &arm : expr.arms) {
        PatternBindings bindings;
        if (!match_pattern(arm.pattern, scrutinee.value, bindings)) {
            continue;
        }

        EvalContext arm_ctx = ctx;
        for (auto &[name, value] : bindings) {
            arm_ctx.bind_local(name, std::move(value));
        }

        if (arm.guard) {
            auto guard = eval_expr_impl(*arm.guard, arm_ctx, call_eval);
            if (guard.has_errors()) {
                return guard;
            }
            const auto *guard_value = std::get_if<BoolValue>(&guard.value.node);
            if (guard_value == nullptr) {
                return make_error("match guard must evaluate to Bool");
            }
            if (!guard_value->value) {
                continue;
            }
        }

        if (!arm.body) {
            return make_error("MatchExpr arm has null body");
        }
        return eval_expr_impl(*arm.body, arm_ctx, call_eval);
    }

    return make_error("non-exhaustive match expression");
}

// ============================================================================
// CallExpr - NOT SUPPORTED in v0.51
// ============================================================================

EvalResult eval_intrinsic_call(const ir::CallExpr &expr,
                               const EvalContext &ctx,
                               const CallEvalFn *call_eval) {
    if (expr.callee == "std::option::Option::None") {
        if (!expr.arguments.empty()) {
            return make_error("Option::None expects no arguments");
        }
        // Nominal pattern: EnumValue{"std::option::Option", "None", {}, nullptr}
        return EvalResult{make_enum("std::option::Option", "None"), {}};
    }
    if (expr.callee == "std::option::Option::Some") {
        if (expr.arguments.size() != 1) {
            return make_error("Option::Some expects one argument");
        }
        if (!expr.arguments.front()) {
            return make_error("Option::Some has null payload expression");
        }
        auto inner = eval_expr_impl(*expr.arguments.front(), ctx, call_eval);
        if (inner.has_errors()) {
            return inner;
        }
        // Nominal pattern: EnumValue with `associated` field set to the payload
        return EvalResult{make_enum("std::option::Option", "Some",
                                    std::make_unique<Value>(std::move(inner.value))),
                          std::move(inner.diagnostics)};
    }
    if (expr.callee == "std::result::Result::Ok" || expr.callee == "std::result::Result::Err") {
        if (expr.arguments.size() != 1) {
            return make_error(expr.callee + " expects one argument");
        }
        if (!expr.arguments.front()) {
            return make_error(expr.callee + " has null payload expression");
        }
        auto payload = eval_expr_impl(*expr.arguments.front(), ctx, call_eval);
        if (payload.has_errors()) {
            return payload;
        }
        std::vector<Value> values;
        values.push_back(std::move(payload.value));
        const auto variant =
            expr.callee == "std::result::Result::Ok" ? std::string{"Ok"} : std::string{"Err"};
        return EvalResult{make_enum("std::result::Result", std::move(variant), std::move(values)),
                          {}};
    }
    // ---- P5 Big Bang: nominal stdlib container constructors ----
    // std::collections::list_from_array<T...>(args...) -> ListValue
    if (expr.callee == "std::collections::list_from_array") {
        ListValue list;
        list.items.reserve(expr.arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : expr.arguments) {
            if (!arg_ref) {
                return make_error("list_from_array: null argument expression");
            }
            auto arg_result = eval_expr_impl(*arg_ref, ctx, call_eval);
            if (arg_result.has_errors()) {
                return arg_result;
            }
            diags.append(std::move(arg_result.diagnostics));
            list.items.emplace_back(std::make_unique<Value>(std::move(arg_result.value)));
        }
        return EvalResult{Value{.node = std::move(list)}, std::move(diags)};
    }
    // std::collections::set_from_array<T...>(args...) -> SetValue
    // (deduplicated + canonical-sorted so equality & set ordering tests pass)
    if (expr.callee == "std::collections::set_from_array") {
        std::vector<std::unique_ptr<Value>> items;
        items.reserve(expr.arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : expr.arguments) {
            if (!arg_ref) {
                return make_error("set_from_array: null argument expression");
            }
            auto arg_result = eval_expr_impl(*arg_ref, ctx, call_eval);
            if (arg_result.has_errors()) {
                return arg_result;
            }
            diags.append(std::move(arg_result.diagnostics));
            items.emplace_back(std::make_unique<Value>(std::move(arg_result.value)));
        }
        // Less-than helper using common printable representations (keeps
        // canonical ordering stable across identical insertions).
        auto value_less = [](const std::unique_ptr<Value> &a, const std::unique_ptr<Value> &b) -> bool {
            if (!a || !b) return static_cast<bool>(!b);
            if (auto *ai = std::get_if<IntValue>(&a->node)) {
                if (auto *bi = std::get_if<IntValue>(&b->node)) return ai->value < bi->value;
                return true;
            }
            if (auto *as = std::get_if<StringValue>(&a->node)) {
                if (auto *bs = std::get_if<StringValue>(&b->node)) return as->value < bs->value;
                return true;
            }
            if (auto *ab = std::get_if<BoolValue>(&a->node)) {
                if (auto *bb = std::get_if<BoolValue>(&b->node)) return !ab->value && bb->value;
                return true;
            }
            if (auto *ad = std::get_if<DecimalValue>(&a->node)) {
                if (auto *bd = std::get_if<DecimalValue>(&b->node)) return ad->spelling < bd->spelling;
                return true;
            }
            return false;
        };
        auto value_equal = [&value_less](const std::unique_ptr<Value> &a, const std::unique_ptr<Value> &b) -> bool {
            return !value_less(a, b) && !value_less(b, a);
        };
        // Canonical sort
        std::sort(items.begin(), items.end(), value_less);
        // Deduplicate adjacent entries
        auto last = std::unique(items.begin(), items.end(), value_equal);
        items.erase(last, items.end());
        SetValue set;
        set.items = std::move(items);
        return EvalResult{Value{.node = std::move(set)}, std::move(diags)};
    }
    // std::collections::map_entry_new<K, V>(k, v) -> StructValue(MapEntry{key,value})
    if (expr.callee == "std::collections::map_entry_new") {
        if (expr.arguments.size() != 2) {
            return make_error("map_entry_new expects two arguments");
        }
        if (!expr.arguments[0] || !expr.arguments[1]) {
            return make_error("map_entry_new has null argument expression");
        }
        auto key_result = eval_expr_impl(*expr.arguments[0], ctx, call_eval);
        if (key_result.has_errors()) {
            return key_result;
        }
        auto val_result = eval_expr_impl(*expr.arguments[1], ctx, call_eval);
        if (val_result.has_errors()) {
            return val_result;
        }
        DiagnosticBag diags;
        diags.append(std::move(key_result.diagnostics));
        diags.append(std::move(val_result.diagnostics));
        std::unordered_map<std::string, std::unique_ptr<Value>> fields;
        fields.emplace("key", std::make_unique<Value>(std::move(key_result.value)));
        fields.emplace("value", std::make_unique<Value>(std::move(val_result.value)));
        return EvalResult{Value{.node = StructValue{.type_name = "MapEntry", .fields = std::move(fields)}},
                          std::move(diags)};
    }
    // std::collections::map_from_entries<K, V>(entry_0, entry_1, ...) -> MapValue
    if (expr.callee == "std::collections::map_from_entries") {
        MapValue map;
        map.entries.reserve(expr.arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : expr.arguments) {
            if (!arg_ref) {
                return make_error("map_from_entries: null entry expression");
            }
            auto entry_result = eval_expr_impl(*arg_ref, ctx, call_eval);
            if (entry_result.has_errors()) {
                return entry_result;
            }
            diags.append(std::move(entry_result.diagnostics));
            // Entry is a StructValue with "key"/"value" fields
            auto *sv = std::get_if<StructValue>(&entry_result.value.node);
            if (sv != nullptr) {
                auto key_it = sv->fields.find("key");
                auto val_it = sv->fields.find("value");
                if (key_it == sv->fields.end() || val_it == sv->fields.end() ||
                    !key_it->second || !val_it->second) {
                    return make_error("map_from_entries: entry missing key/value fields");
                }
                // Move keys directly (entry struct is single-use); frontend
                // guarantees unique keys per construction site.
                map.entries.emplace_back(std::move(key_it->second),
                                         std::move(val_it->second));
            } else {
                return make_error("map_from_entries: entry is not a MapEntry struct");
            }
        }
        return EvalResult{Value{.node = std::move(map)}, std::move(diags)};
    }
    if (auto stdlib_result = eval_stdlib_wrapper_call(expr, ctx, call_eval);
        stdlib_result.has_value()) {
        return std::move(*stdlib_result);
    }
    // Fall back to builtin table — used for @builtin functions from stdlib
    // that are lowered directly into IR CallExpr nodes.
    const BuiltinTable &table = BuiltinTable::instance();
    const BuiltinFn *fn = table.find(expr.callee);
    if (fn != nullptr) {
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

EvalResult
eval_call_expr(const ir::CallExpr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    if (auto callee_value = ctx.get_local(expr.callee); callee_value.has_value()) {
        const auto *callable = std::get_if<CallableValue>(&callee_value->node);
        if (callable != nullptr) {
            std::vector<Value> args;
            args.reserve(expr.arguments.size());
            DiagnosticBag diags;
            for (const auto &arg_ref : expr.arguments) {
                if (!arg_ref) {
                    return make_error("callable call: null argument expression");
                }
                EvalResult arg_result = eval_expr_impl(*arg_ref, ctx, call_eval);
                if (arg_result.has_errors()) {
                    diags.append(std::move(arg_result.diagnostics));
                    return EvalResult{make_none(), std::move(diags)};
                }
                diags.append(std::move(arg_result.diagnostics));
                args.push_back(std::move(arg_result.value));
            }
            EvalResult result = invoke_callable_value(*callable, args, call_eval);
            result.diagnostics.append(std::move(diags));
            return result;
        }
    }
    if (call_eval != nullptr && *call_eval) {
        return (*call_eval)(expr, ctx);
    }
    return eval_intrinsic_call(expr, ctx, call_eval);
}

// ============================================================================
// Main eval_expr dispatcher
// ============================================================================

EvalResult
eval_expr_impl(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn *call_eval) {
    return std::visit(
        [&ctx, call_eval](const auto &node) -> EvalResult {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ir::BoolLiteralExpr>) {
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
            } else if constexpr (std::is_same_v<T, ir::PathExpr>) {
                return eval_path_expr(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::QualifiedValueExpr>) {
                return eval_qualified_value_expr(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::CallExpr>) {
                return eval_call_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::LambdaExpr>) {
                return eval_lambda_expr(node, ctx);
            } else if constexpr (std::is_same_v<T, ir::StructLiteralExpr>) {
                return eval_struct_literal(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::UnaryExpr>) {
                return eval_unary_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::BinaryExpr>) {
                return eval_binary_expr(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::MemberAccessExpr>) {
                return eval_member_access(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::IndexAccessExpr>) {
                return eval_index_access(node, ctx, call_eval);
            } else if constexpr (std::is_same_v<T, ir::MatchExpr>) {
                return eval_match_expr(node, ctx, call_eval);
            }
        },
        expr.node);
}

struct ProgramCallState : std::enable_shared_from_this<ProgramCallState> {
    const ir::Program *program{nullptr};

    explicit ProgramCallState(const ir::Program &p) noexcept : program(&p) {}

    [[nodiscard]] CallEvalFn recursive_call_eval() {
        auto self = shared_from_this();
        return [self](const ir::CallExpr &call, const EvalContext &ctx) -> EvalResult {
            return self->evaluate(call, ctx);
        };
    }

    [[nodiscard]] ExprEvalFn recursive_expr_eval() {
        auto self = shared_from_this();
        return [self](const ir::Expr &expr, const EvalContext &ctx) -> EvalResult {
            auto call_eval = self->recursive_call_eval();
            return eval_expr(expr, ctx, call_eval);
        };
    }

    [[nodiscard]] EvalResult evaluate(const ir::CallExpr &call, const EvalContext &ctx) {
        auto recursive = recursive_call_eval();
        const ir::FnDecl *fn = program == nullptr ? nullptr : find_function(*program, call.callee);
        if (fn == nullptr) {
            return eval_intrinsic_call(call, ctx, &recursive);
        }
        if (!fn->body) {
            return make_error("function has no executable body: " + call.callee);
        }
        if (fn->params.size() != call.arguments.size()) {
            return make_error("function '" + call.callee + "' expected " +
                              std::to_string(fn->params.size()) + " arguments, got " +
                              std::to_string(call.arguments.size()));
        }

        DiagnosticBag diags;
        std::vector<Value> args;
        args.reserve(call.arguments.size());
        for (const auto &arg_ref : call.arguments) {
            if (!arg_ref) {
                return make_error("function call: null argument expression");
            }
            EvalResult arg_result = eval_expr_impl(*arg_ref, ctx, &recursive);
            if (arg_result.has_errors()) {
                diags.append(std::move(arg_result.diagnostics));
                return EvalResult{make_none(), std::move(diags)};
            }
            diags.append(std::move(arg_result.diagnostics));
            args.push_back(std::move(arg_result.value));
        }

        ExecContext exec_ctx;
        exec_ctx.eval_ctx = ctx;
        exec_ctx.expr_eval = recursive_expr_eval();
        for (std::size_t index = 0; index < fn->params.size(); ++index) {
            exec_ctx.bind_local(fn->params[index].name, std::move(args[index]));
        }

        ExecResult exec_result = exec_block(*fn->body, exec_ctx);
        diags.append(std::move(exec_result.diagnostics));
        if (diags.has_error()) {
            return EvalResult{make_none(), std::move(diags)};
        }
        if (auto *ret = std::get_if<ExecReturn>(&exec_result.outcome); ret != nullptr) {
            return EvalResult{std::move(ret->value), std::move(diags)};
        }
        if (auto *assert_failed = std::get_if<ExecAssertFailed>(&exec_result.outcome);
            assert_failed != nullptr) {
            auto result = make_error("function assertion failed: " + assert_failed->message);
            result.diagnostics.append(std::move(diags));
            return result;
        }
        if (auto *go = std::get_if<ExecGoto>(&exec_result.outcome); go != nullptr) {
            auto result = make_error("function body cannot goto state: " + go->target_state);
            result.diagnostics.append(std::move(diags));
            return result;
        }
        return EvalResult{make_none(), std::move(diags)};
    }
};

} // anonymous namespace

EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx) {
    return eval_expr_impl(expr, ctx, nullptr);
}

EvalResult eval_expr(const ir::Expr &expr, const EvalContext &ctx, const CallEvalFn &call_eval) {
    return eval_expr_impl(expr, ctx, &call_eval);
}

EvalResult eval_intrinsic_with_args(const std::string &callee,
                                    std::vector<Value> args,
                                    const EvalContext &ctx) {
    // Direct intrinsic dispatch with PRE-EVALUATED arguments.  Used by the
    // capability bridge / program dispatch layers so that argument sub-trees
    // which may contain custom (capability / user-function) calls do not get
    // re-evaluated through a different dispatch path.

    if (callee == "std::option::Option::None") {
        if (!args.empty()) {
            return make_error("Option::None expects no arguments");
        }
        return EvalResult{make_enum("std::option::Option", "None"), {}};
    }
    if (callee == "std::option::Option::Some") {
        if (args.size() != 1) {
            return make_error("Option::Some expects one argument");
        }
        return EvalResult{make_enum("std::option::Option", "Some",
                                    std::make_unique<Value>(std::move(args.front()))),
                          {}};
    }
    if (callee == "std::result::Result::Ok" || callee == "std::result::Result::Err") {
        if (args.size() != 1) {
            return make_error(callee + " expects one argument");
        }
        const auto variant = callee == "std::result::Result::Ok" ? std::string{"Ok"} : std::string{"Err"};
        return EvalResult{make_enum("std::result::Result", std::move(variant), std::move(args)), {}};
    }
    if (callee == "std::collections::list_from_array") {
        ListValue list;
        list.items.reserve(args.size());
        for (auto &a : args) {
            list.items.emplace_back(std::make_unique<Value>(std::move(a)));
        }
        return EvalResult{Value{.node = std::move(list)}, {}};
    }
    if (callee == "std::collections::set_from_array") {
        std::vector<std::unique_ptr<Value>> items;
        items.reserve(args.size());
        for (auto &a : args) {
            items.emplace_back(std::make_unique<Value>(std::move(a)));
        }
        auto value_less = [](const std::unique_ptr<Value> &a, const std::unique_ptr<Value> &b) -> bool {
            if (!a || !b) return static_cast<bool>(!b);
            if (auto *ai = std::get_if<IntValue>(&a->node)) {
                if (auto *bi = std::get_if<IntValue>(&b->node)) return ai->value < bi->value;
                return true;
            }
            if (auto *as = std::get_if<StringValue>(&a->node)) {
                if (auto *bs = std::get_if<StringValue>(&b->node)) return as->value < bs->value;
                return true;
            }
            if (auto *ab = std::get_if<BoolValue>(&a->node)) {
                if (auto *bb = std::get_if<BoolValue>(&b->node)) return !ab->value && bb->value;
                return true;
            }
            if (auto *ad = std::get_if<DecimalValue>(&a->node)) {
                if (auto *bd = std::get_if<DecimalValue>(&b->node)) return ad->spelling < bd->spelling;
                return true;
            }
            return false;
        };
        auto value_equal = [&value_less](const std::unique_ptr<Value> &a, const std::unique_ptr<Value> &b) -> bool {
            return !value_less(a, b) && !value_less(b, a);
        };
        std::sort(items.begin(), items.end(), value_less);
        auto last = std::unique(items.begin(), items.end(), value_equal);
        items.erase(last, items.end());
        SetValue set;
        set.items = std::move(items);
        return EvalResult{Value{.node = std::move(set)}, {}};
    }
    if (callee == "std::collections::map_entry_new") {
        if (args.size() != 2) {
            return make_error("map_entry_new expects two arguments");
        }
        std::unordered_map<std::string, std::unique_ptr<Value>> fields;
        fields.emplace("key", std::make_unique<Value>(std::move(args[0])));
        fields.emplace("value", std::make_unique<Value>(std::move(args[1])));
        return EvalResult{Value{.node = StructValue{.type_name = "MapEntry", .fields = std::move(fields)}}, {}};
    }
    if (callee == "std::collections::map_from_entries") {
        MapValue map;
        map.entries.reserve(args.size());
        for (auto &entry_arg : args) {
            auto *sv = std::get_if<StructValue>(&entry_arg.node);
            if (sv == nullptr) {
                return make_error("map_from_entries: entry is not a MapEntry struct");
            }
            auto key_it = sv->fields.find("key");
            auto val_it = sv->fields.find("value");
            if (key_it == sv->fields.end() || val_it == sv->fields.end() ||
                !key_it->second || !val_it->second) {
                return make_error("map_from_entries: entry missing key/value fields");
            }
            map.entries.emplace_back(std::move(key_it->second), std::move(val_it->second));
        }
        return EvalResult{Value{.node = std::move(map)}, {}};
    }
    // Stdlib wrappers (std::option::is_some, std::collections::length, ...)
    // currently always recurse through expression evaluation.  Call sites that
    // combine wrapper dispatch with custom argument evaluation (capability
    // bridging, program-call evaluation) should either be handled by a
    // dedicated branch above or fall through to a dedicated re-entry that
    // replaces the outer `call_eval` with a no-op for the wrapper itself
    // while preserving the custom dispatcher for nested user-defined calls.
    // For now we just let unknown stdlib names fall through to the builtin
    // table below.
    // Fallback: builtin table — accepts pre-evaluated args already.
    const BuiltinTable &table = BuiltinTable::instance();
    const BuiltinFn *fn = table.find(callee);
    if (fn != nullptr) {
        return (*fn)(args, ctx);
    }
    return make_error("CallExpr not supported: unknown callee '" + callee + "'");
}

CallEvalFn make_program_call_eval(const ir::Program &program) {
    auto state = std::make_shared<ProgramCallState>(program);
    return [state](const ir::CallExpr &call, const EvalContext &ctx) -> EvalResult {
        return state->evaluate(call, ctx);
    };
}

} // namespace ahfl::evaluator
