#include "ahfl/compiler/semantics/const_sema.hpp"

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

[[nodiscard]] ConstValue make_const_value(ConstValueKind kind,
                                          std::string scalar = {},
                                          std::optional<SymbolId> symbol = std::nullopt) {
    return ConstValue{
        .kind = kind,
        .scalar = std::move(scalar),
        .symbol = symbol,
        .child_names = {},
        .children = {},
    };
}

[[nodiscard]] ConstValue make_bool_const(bool value) {
    return make_const_value(ConstValueKind::Bool, value ? "true" : "false");
}

[[nodiscard]] std::optional<std::int64_t> parse_int_const(std::string_view text) {
    std::int64_t value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::int64_t> parse_duration_milliseconds(std::string_view text) {
    std::int64_t multiplier = 0;
    if (text.ends_with("ms")) {
        multiplier = 1;
        text.remove_suffix(2);
    } else if (text.ends_with("s")) {
        multiplier = 1000;
        text.remove_suffix(1);
    } else if (text.ends_with("m")) {
        multiplier = 60 * 1000;
        text.remove_suffix(1);
    } else if (text.ends_with("h")) {
        multiplier = 60 * 60 * 1000;
        text.remove_suffix(1);
    } else {
        return std::nullopt;
    }

    const auto value = parse_int_const(text);
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    if (*value > std::numeric_limits<std::int64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return *value * multiplier;
}

struct DecimalConst {
    std::int64_t units{0};
    std::int64_t scale{0};
};

[[nodiscard]] std::optional<long double> parse_float_const(std::string_view text) {
    std::string buffer{text};
    char *end = nullptr;
    errno = 0;
    const auto value = std::strtold(buffer.c_str(), &end);
    if (end != buffer.c_str() + buffer.size() || errno == ERANGE || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::string render_float_const(long double value) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<long double>::digits10) << value;
    auto text = out.str();
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

[[nodiscard]] std::optional<std::int64_t> pow10_i64(std::int64_t exponent) {
    if (exponent < 0) {
        return std::nullopt;
    }
    std::int64_t result = 1;
    for (std::int64_t index = 0; index < exponent; ++index) {
        if (result > std::numeric_limits<std::int64_t>::max() / 10) {
            return std::nullopt;
        }
        result *= 10;
    }
    return result;
}

[[nodiscard]] std::optional<DecimalConst> parse_decimal_const(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    if (text.back() == 'd' || text.back() == 'D') {
        text.remove_suffix(1);
    }

    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    const auto dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }

    std::string digits;
    digits.reserve(text.size() - 1);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == dot) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return std::nullopt;
        }
        digits.push_back(text[index]);
    }

    std::int64_t units = 0;
    const auto *begin = digits.data();
    const auto *end = digits.data() + digits.size();
    const auto result = std::from_chars(begin, end, units);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    if (negative) {
        units = -units;
    }
    return DecimalConst{
        .units = units,
        .scale = static_cast<std::int64_t>(text.size() - dot - 1),
    };
}

[[nodiscard]] std::optional<DecimalConst> decimal_operand_at_scale(const ConstValue &value,
                                                                   std::int64_t scale) {
    if (value.kind == ConstValueKind::Decimal) {
        auto decimal = parse_decimal_const(value.scalar);
        if (!decimal.has_value() || decimal->scale != scale) {
            return std::nullopt;
        }
        return decimal;
    }
    if (value.kind == ConstValueKind::Integer) {
        auto integer = parse_int_const(value.scalar);
        auto multiplier = pow10_i64(scale);
        if (!integer.has_value() || !multiplier.has_value()) {
            return std::nullopt;
        }
        if ((*integer > 0 && *integer > std::numeric_limits<std::int64_t>::max() / *multiplier) ||
            (*integer < 0 && *integer < std::numeric_limits<std::int64_t>::min() / *multiplier)) {
            return std::nullopt;
        }
        return DecimalConst{
            .units = *integer * *multiplier,
            .scale = scale,
        };
    }
    return std::nullopt;
}

[[nodiscard]] std::string render_decimal_const(DecimalConst value) {
    const bool negative = value.units < 0;
    const auto units = negative ? static_cast<std::uint64_t>(-(value.units + 1)) + 1U
                                : static_cast<std::uint64_t>(value.units);
    const auto divisor = static_cast<std::uint64_t>(pow10_i64(value.scale).value_or(1));
    const auto whole = units / divisor;
    const auto fraction = units % divisor;

    std::ostringstream out;
    if (negative) {
        out << '-';
    }
    out << whole << '.';
    out << std::setw(static_cast<int>(value.scale)) << std::setfill('0') << fraction << 'd';
    return out.str();
}

[[nodiscard]] std::optional<std::string> concatenate_string_const(std::string_view lhs,
                                                                  std::string_view rhs) {
    if (lhs.size() < 2 || rhs.size() < 2 || lhs.front() != '"' || lhs.back() != '"' ||
        rhs.front() != '"' || rhs.back() != '"') {
        return std::nullopt;
    }
    std::string result;
    result.reserve(lhs.size() + rhs.size() - 2);
    result.append(lhs.substr(0, lhs.size() - 1));
    result.append(rhs.substr(1));
    return result;
}

[[nodiscard]] std::optional<bool> parse_bool_const(const ConstValue &value) {
    const auto &effective = effective_const_value(value);
    if (effective.kind != ConstValueKind::Bool) {
        return std::nullopt;
    }
    if (effective.scalar == "true") {
        return true;
    }
    if (effective.scalar == "false") {
        return false;
    }
    return std::nullopt;
}

void append_const_value_key_part(std::string &key, std::string_view part) {
    key += std::to_string(part.size());
    key += ':';
    key += part;
    key += ';';
}

[[nodiscard]] std::string_view const_binary_op_spelling(ast::ExprBinaryOp op) noexcept {
    switch (op) {
    case ast::ExprBinaryOp::Implies:
        return "implies";
    case ast::ExprBinaryOp::Or:
        return "||";
    case ast::ExprBinaryOp::And:
        return "&&";
    case ast::ExprBinaryOp::Equal:
        return "==";
    case ast::ExprBinaryOp::NotEqual:
        return "!=";
    case ast::ExprBinaryOp::Less:
        return "<";
    case ast::ExprBinaryOp::LessEqual:
        return "<=";
    case ast::ExprBinaryOp::Greater:
        return ">";
    case ast::ExprBinaryOp::GreaterEqual:
        return ">=";
    case ast::ExprBinaryOp::Add:
        return "+";
    case ast::ExprBinaryOp::Subtract:
        return "-";
    case ast::ExprBinaryOp::Multiply:
        return "*";
    case ast::ExprBinaryOp::Divide:
        return "/";
    case ast::ExprBinaryOp::Modulo:
        return "%";
    }
    return "?";
}

[[nodiscard]] std::optional<SymbolId> const_struct_symbol_for(const ast::ExprSyntax &expr,
                                                              const ResolveResult &resolve_result,
                                                              std::optional<SourceId> source_id) {
    const auto *struct_literal = std::get_if<ast::StructLiteralExpr>(&expr.node);
    if (struct_literal == nullptr || struct_literal->type_name == nullptr) {
        return std::nullopt;
    }
    if (const auto reference = resolve_result.find_reference(
            ReferenceKind::TypeName, struct_literal->type_name->range, source_id);
        reference.has_value()) {
        return reference->get().target;
    }
    return std::nullopt;
}

[[nodiscard]] bool is_const_expr_syntax(const ast::ExprSyntax &expr, std::string_view &reason) {
    return std::visit(
        Overloaded{
            [](const ast::NoneLiteralExpr &) { return true; },
            [](const ast::BoolLiteralExpr &) { return true; },
            [](const ast::IntegerLiteralExpr &) { return true; },
            [](const ast::FloatLiteralExpr &) { return true; },
            [](const ast::DecimalLiteralExpr &) { return true; },
            [](const ast::StringLiteralExpr &) { return true; },
            [](const ast::DurationLiteralExpr &) { return true; },
            [](const ast::QualifiedValueExpr &) { return true; },
            [&reason](const ast::SomeExpr &e) { return is_const_expr_syntax(*e.value, reason); },
            [&reason](const ast::GroupExpr &e) { return is_const_expr_syntax(*e.inner, reason); },
            [&reason](const ast::StructLiteralExpr &e) {
                for (const auto &field : e.fields) {
                    if (!is_const_expr_syntax(*field->value, reason)) {
                        return false;
                    }
                }
                return true;
            },
            [&reason](const ast::ListLiteralExpr &e) {
                for (const auto &item : e.items) {
                    if (!is_const_expr_syntax(*item, reason)) {
                        return false;
                    }
                }
                return true;
            },
            [&reason](const ast::SetLiteralExpr &e) {
                for (const auto &item : e.items) {
                    if (!is_const_expr_syntax(*item, reason)) {
                        return false;
                    }
                }
                return true;
            },
            [&reason](const ast::MapLiteralExpr &e) {
                for (const auto &entry : e.entries) {
                    if (!is_const_expr_syntax(*entry->key, reason) ||
                        !is_const_expr_syntax(*entry->value, reason)) {
                        return false;
                    }
                }
                return true;
            },
            [&reason](const ast::MemberAccessExpr &e) {
                return is_const_expr_syntax(*e.base, reason);
            },
            [&reason](const ast::IndexAccessExpr &e) {
                return is_const_expr_syntax(*e.base, reason) &&
                       is_const_expr_syntax(*e.index, reason);
            },
            [&reason](const ast::UnaryExpr &e) {
                // Pure unary operators (!, -, +) over constant operands are themselves
                // constants. The full type-check pass still validates the operator
                // applies to the operand kind; we only relax the syntactic gate.
                return is_const_expr_syntax(*e.operand, reason);
            },
            [&reason](const ast::BinaryExpr &e) {
                // Same rationale as Unary: arithmetic/logical/comparison operators
                // applied to constant operands fold to a constant. Type errors are
                // still surfaced by check_binary_expr.
                return is_const_expr_syntax(*e.lhs, reason) && is_const_expr_syntax(*e.rhs, reason);
            },
            [&reason](const ast::PathExpr &) {
                reason = "runtime path references are not compile-time constants";
                return false;
            },
            [&reason](const ast::CallExpr &) {
                reason = "capability and predicate calls are not compile-time constants";
                return false;
            },
            [&reason](const ast::MethodCallExpr &) {
                reason = "method calls are not compile-time constants";
                return false;
            },
            [&reason](const ast::MatchExpr &) {
                // P1 (ADT): match expressions are not compile-time constants.
                reason = "match expressions are not compile-time constants";
                return false;
            },
            [&reason](const ast::LambdaExpr &) {
                // P2 (RFC §6): closures are runtime values, not compile-time
                // constants.
                reason = "lambda expressions are not compile-time constants";
                return false;
            },
        },
        expr.node);
}

[[nodiscard]] bool is_const_error_type(const Type &type) noexcept {
    return type.holds<types::ErrorT>();
}

} // namespace

ConstExprSyntaxResult classify_const_expr_syntax(const ast::ExprSyntax &expr) {
    std::string_view reason = "unsupported expression form";
    return ConstExprSyntaxResult{
        .is_const = is_const_expr_syntax(expr, reason),
        .reason = reason,
    };
}

ConstExprGateResult
classify_const_expr_gate(const ast::ExprSyntax &expr, bool is_pure, ExprEffect effect) {
    if (!is_pure) {
        return ConstExprGateResult{
            .status = ConstExprGateStatus::RuntimeEffect,
            .reason = "expression has runtime effect: " + std::string(to_string(effect)),
        };
    }

    const auto syntax = classify_const_expr_syntax(expr);
    if (!syntax.is_const) {
        return ConstExprGateResult{
            .status = ConstExprGateStatus::UnsupportedSyntax,
            .reason = std::string(syntax.reason),
        };
    }

    return ConstExprGateResult{
        .status = ConstExprGateStatus::Accepted,
        .reason = {},
    };
}

ConstEvalOutcome const_eval_known(std::optional<ConstValue> const_value) {
    return ConstEvalOutcome{
        .kind = ConstEvalKind::KnownConst,
        .const_value = std::move(const_value),
    };
}

ConstEvalOutcome const_eval_not_const() {
    return ConstEvalOutcome{
        .kind = ConstEvalKind::NotConst,
        .const_value = std::nullopt,
    };
}

ConstEvalOutcome const_eval_error() {
    return ConstEvalOutcome{
        .kind = ConstEvalKind::Error,
        .const_value = std::nullopt,
    };
}

ConstDiagnosticEmitter::ConstDiagnosticEmitter(DiagnosticBag &diagnostics, const SourceFile *source)
    : diagnostics_(diagnostics), source_(source) {}

void ConstDiagnosticEmitter::emit(ConstDiagnosticReport report) const {
    Diagnostic diagnostic{
        .severity = DiagnosticSeverity::Error,
        .message = std::move(report.diagnostic.message),
        .code = report.diagnostic.code.full_code(),
        .range = report.range,
        .source_name = std::nullopt,
        .position = std::nullopt,
        .related = std::move(report.diagnostic.related),
    };

    if (source_ != nullptr) {
        diagnostic.source_name = source_->display_name;
        diagnostic.position = source_->locate(report.range.begin_offset);
    }

    diagnostics_.add_diagnostic(std::move(diagnostic));
}

void ConstDiagnosticEmitter::emit_all(std::vector<ConstDiagnosticReport> reports) const {
    for (auto &report : reports) {
        emit(std::move(report));
    }
}

ConstTypeRelationValidator::ConstTypeRelationValidator(TypeRelationContext &relations,
                                                       ConstDiagnosticEmitter diagnostics)
    : relations_(relations), diagnostics_(diagnostics) {}

bool ConstTypeRelationValidator::check_assignable(const Type &source,
                                                  const Type &target,
                                                  SourceRange range,
                                                  std::string_view context_label,
                                                  const TypeExpectation &expectation) const {
    if (is_const_error_type(source) || is_const_error_type(target)) {
        return true;
    }

    if (ahfl::is_assignable_to(source, target, relations_)) {
        return true;
    }

    std::vector<ConstDiagnosticRelated> notes;
    notes.push_back(ConstDiagnosticRelated{
        .message = expected_type_note(target, expectation),
        .range = expectation.origin_range,
    });
    notes.push_back(ConstDiagnosticRelated{
        .message = actual_type_note(source),
        .range = range,
    });

    diagnostics_.emit(ConstDiagnosticReport{
        .diagnostic =
            ConstTypeCheckDiagnostic{
                .code = error_codes::typecheck::TypeMismatch,
                .message = messages::typecheck::TypeMismatch.format_with(
                    context_label, target.describe(), source.describe()),
                .related = std::move(notes),
            },
        .range = range,
    });
    return false;
}

bool ConstTypeRelationValidator::check_exact_schema_boundary(
    const Type &source,
    const Type &target,
    SchemaBoundaryKind boundary,
    SourceRange range,
    const TypeExpectation &expectation) const {
    if (is_const_error_type(source) || is_const_error_type(target)) {
        return true;
    }

    if (ahfl::is_exact_schema_match(source, target, relations_)) {
        return true;
    }

    std::vector<ConstDiagnosticRelated> notes;
    notes.push_back(ConstDiagnosticRelated{
        .message = std::string(to_string(boundary)) +
                   " requires an exact schema match (no width or depth subtyping)",
        .range = std::nullopt,
    });
    notes.push_back(ConstDiagnosticRelated{
        .message = expected_schema_note(target, expectation),
        .range = expectation.origin_range,
    });
    notes.push_back(ConstDiagnosticRelated{
        .message = actual_type_note(source),
        .range = range,
    });

    diagnostics_.emit(ConstDiagnosticReport{
        .diagnostic =
            ConstTypeCheckDiagnostic{
                .code = error_codes::typecheck::ExactSchemaMismatch,
                .message = messages::typecheck::ExactSchemaMismatch.format_with(
                    to_string(boundary), target.describe(), source.describe()),
                .related = std::move(notes),
            },
        .range = range,
    });
    return false;
}

bool ConstTypeRelationValidator::check_struct_default(
    const Type &source,
    const Type &target,
    SourceRange range,
    const ConstStructDefaultValidationPolicy &policy) const {
    switch (policy.kind) {
    case ConstStructDefaultValidationKind::ExactAgentContextSchema:
        return check_exact_schema_boundary(
            source,
            target,
            policy.schema_boundary.value_or(SchemaBoundaryKind::AgentContextDefault),
            range,
            policy.expectation);
    case ConstStructDefaultValidationKind::Assignable:
        return check_assignable(source, target, range, policy.context_label, policy.expectation);
    }

    return false;
}

ConstInitializerValidationPolicy describe_const_initializer_validation(
    const Type &declared_type, SourceRange annotation_range, std::string_view const_name) {
    return ConstInitializerValidationPolicy{
        .context_label = "const initializer",
        .expectation =
            TypeExpectation{
                .expected = declared_type.clone(),
                .origin_kind = TypeExpectationOriginKind::Annotation,
                .origin_range = annotation_range,
                .description = "declared type of const '" + std::string(const_name) + "'",
            },
    };
}

ConstStructDefaultValidationPolicy classify_const_struct_default_validation(
    const Type &field_type, SourceRange field_range, bool is_agent_context_struct) {
    if (is_agent_context_struct) {
        return ConstStructDefaultValidationPolicy{
            .kind = ConstStructDefaultValidationKind::ExactAgentContextSchema,
            .context_label = "struct field default",
            .expectation =
                TypeExpectation{
                    .expected = field_type.clone(),
                    .origin_kind = TypeExpectationOriginKind::SchemaBoundary,
                    .origin_range = field_range,
                    .description = std::string(to_string(SchemaBoundaryKind::AgentContextDefault)),
                },
            .schema_boundary = SchemaBoundaryKind::AgentContextDefault,
        };
    }

    return ConstStructDefaultValidationPolicy{
        .kind = ConstStructDefaultValidationKind::Assignable,
        .context_label = "struct field default",
        .expectation =
            TypeExpectation{
                .expected = field_type.clone(),
                .origin_kind = TypeExpectationOriginKind::Annotation,
                .origin_range = field_range,
                .description = "struct field default",
            },
        .schema_boundary = std::nullopt,
    };
}

ConstExprRequiredDiagnostic describe_const_expr_required(std::string_view context_label,
                                                         const ConstExprGateResult &gate) {
    return ConstExprRequiredDiagnostic{
        .code = error_codes::typecheck::ConstExprRequired,
        .message = messages::typecheck::ConstExprRequired.format_with(context_label, gate.reason),
        .related = {},
    };
}

ConstDependencyCycleDiagnostic describe_const_dependency_cycle(const Symbol *symbol) {
    const auto display_name = symbol != nullptr && !symbol->canonical_name.empty()
                                  ? symbol->canonical_name
                                  : std::string{"<unknown const>"};

    ConstDependencyCycleDiagnostic diagnostic{
        .code = error_codes::typecheck::ConstDependencyCycle,
        .message = messages::typecheck::ConstDependencyCycle.format_with(display_name),
        .related = {},
    };
    if (symbol != nullptr) {
        diagnostic.related.push_back(ConstDiagnosticRelated{
            .message = messages::typecheck::ConstDependencyCycleParticipant.format_with(),
            .range = symbol->declaration_range,
        });
    }
    return diagnostic;
}

void add_const_child(ConstValue &value, std::string name, ConstValue child) {
    value.child_names.push_back(std::move(name));
    value.children.push_back(std::move(child));
}

const ConstValue &effective_const_value(const ConstValue &value) {
    if (value.kind == ConstValueKind::ConstReference && value.children.size() == 1) {
        return effective_const_value(value.children.front());
    }
    return value;
}

ConstValue duration_const_value(std::string_view text) {
    if (const auto milliseconds = parse_duration_milliseconds(text); milliseconds.has_value()) {
        return make_const_value(ConstValueKind::Duration, std::to_string(*milliseconds) + "ms");
    }
    return make_const_value(ConstValueKind::Duration, std::string{text});
}

bool const_values_equal(const ConstValue &lhs, const ConstValue &rhs) {
    const auto &left = effective_const_value(lhs);
    const auto &right = effective_const_value(rhs);
    if (left.kind != right.kind || left.scalar != right.scalar || left.symbol != right.symbol ||
        left.child_names != right.child_names || left.children.size() != right.children.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.children.size(); ++index) {
        if (!const_values_equal(left.children[index], right.children[index])) {
            return false;
        }
    }
    return true;
}

std::string const_value_sort_key(const ConstValue &value) {
    const auto &effective = effective_const_value(value);
    std::string key;
    key += std::to_string(static_cast<unsigned>(effective.kind));
    key += '|';
    append_const_value_key_part(key, effective.scalar);
    append_const_value_key_part(
        key, effective.symbol.has_value() ? std::to_string(effective.symbol->value) : "");
    for (std::size_t index = 0; index < effective.children.size(); ++index) {
        append_const_value_key_part(
            key, index < effective.child_names.size() ? effective.child_names[index] : "");
        append_const_value_key_part(key, const_value_sort_key(effective.children[index]));
    }
    return key;
}

void normalize_set_const_value(ConstValue &value) {
    if (value.kind != ConstValueKind::Set || value.children.empty()) {
        return;
    }
    std::sort(value.children.begin(),
              value.children.end(),
              [](const ConstValue &lhs, const ConstValue &rhs) {
                  return const_value_sort_key(lhs) < const_value_sort_key(rhs);
              });
    value.child_names.assign(value.children.size(), "");
}

void normalize_map_const_value(ConstValue &value) {
    if (value.kind != ConstValueKind::Map || value.children.size() < 4) {
        return;
    }

    struct Entry {
        ConstValue key;
        ConstValue value;
    };
    std::vector<Entry> entries;
    entries.reserve(value.children.size() / 2);
    for (std::size_t offset = 0; offset + 1 < value.children.size(); offset += 2) {
        entries.push_back(Entry{
            .key = std::move(value.children[offset]),
            .value = std::move(value.children[offset + 1]),
        });
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &lhs, const Entry &rhs) {
        const auto lhs_key = const_value_sort_key(lhs.key);
        const auto rhs_key = const_value_sort_key(rhs.key);
        if (lhs_key != rhs_key) {
            return lhs_key < rhs_key;
        }
        return const_value_sort_key(lhs.value) < const_value_sort_key(rhs.value);
    });

    value.children.clear();
    value.child_names.clear();
    value.children.reserve(entries.size() * 2);
    value.child_names.reserve(entries.size() * 2);
    for (auto &entry : entries) {
        add_const_child(value, "key", std::move(entry.key));
        add_const_child(value, "value", std::move(entry.value));
    }
}

std::optional<ConstValue> fold_unary_const(ast::ExprUnaryOp op, const ConstValue &operand) {
    const auto &value = effective_const_value(operand);
    switch (op) {
    case ast::ExprUnaryOp::Not:
        if (const auto bool_value = parse_bool_const(value); bool_value.has_value()) {
            return make_bool_const(!*bool_value);
        }
        break;
    case ast::ExprUnaryOp::Positive:
        if (value.kind == ConstValueKind::Integer || value.kind == ConstValueKind::Float ||
            value.kind == ConstValueKind::Decimal) {
            return value;
        }
        break;
    case ast::ExprUnaryOp::Negate:
        if (value.kind == ConstValueKind::Integer) {
            if (const auto int_value = parse_int_const(value.scalar); int_value.has_value()) {
                return make_const_value(ConstValueKind::Integer, std::to_string(-*int_value));
            }
        }
        if (value.kind == ConstValueKind::Float) {
            if (const auto float_value = parse_float_const(value.scalar); float_value.has_value()) {
                return make_const_value(ConstValueKind::Float, render_float_const(-*float_value));
            }
        }
        if (value.kind == ConstValueKind::Decimal) {
            if (auto decimal = parse_decimal_const(value.scalar); decimal.has_value()) {
                decimal->units = -decimal->units;
                return make_const_value(ConstValueKind::Decimal, render_decimal_const(*decimal));
            }
        }
        break;
    }
    return std::nullopt;
}

std::optional<ConstValue>
fold_binary_const(ast::ExprBinaryOp op, const ConstValue &lhs, const ConstValue &rhs) {
    const auto &left = effective_const_value(lhs);
    const auto &right = effective_const_value(rhs);
    switch (op) {
    case ast::ExprBinaryOp::And:
    case ast::ExprBinaryOp::Or:
    case ast::ExprBinaryOp::Implies: {
        const auto left_bool = parse_bool_const(left);
        const auto right_bool = parse_bool_const(right);
        if (!left_bool.has_value() || !right_bool.has_value()) {
            break;
        }
        bool result = false;
        if (op == ast::ExprBinaryOp::And) {
            result = *left_bool && *right_bool;
        } else if (op == ast::ExprBinaryOp::Or) {
            result = *left_bool || *right_bool;
        } else {
            result = !*left_bool || *right_bool;
        }
        return make_bool_const(result);
    }
    case ast::ExprBinaryOp::Equal:
    case ast::ExprBinaryOp::NotEqual: {
        bool equal = const_values_equal(left, right);
        if (left.kind == ConstValueKind::Duration && right.kind == ConstValueKind::Duration) {
            const auto left_duration = parse_duration_milliseconds(left.scalar);
            const auto right_duration = parse_duration_milliseconds(right.scalar);
            if (left_duration.has_value() && right_duration.has_value()) {
                equal = *left_duration == *right_duration;
            }
        }
        const bool result = op == ast::ExprBinaryOp::Equal ? equal : !equal;
        return make_bool_const(result);
    }
    case ast::ExprBinaryOp::Add:
    case ast::ExprBinaryOp::Subtract:
    case ast::ExprBinaryOp::Multiply:
    case ast::ExprBinaryOp::Divide:
    case ast::ExprBinaryOp::Modulo:
    case ast::ExprBinaryOp::Less:
    case ast::ExprBinaryOp::LessEqual:
    case ast::ExprBinaryOp::Greater:
    case ast::ExprBinaryOp::GreaterEqual: {
        if (left.kind == ConstValueKind::Duration && right.kind == ConstValueKind::Duration) {
            const auto left_duration = parse_duration_milliseconds(left.scalar);
            const auto right_duration = parse_duration_milliseconds(right.scalar);
            if (!left_duration.has_value() || !right_duration.has_value()) {
                break;
            }
            switch (op) {
            case ast::ExprBinaryOp::Less:
                return make_bool_const(*left_duration < *right_duration);
            case ast::ExprBinaryOp::LessEqual:
                return make_bool_const(*left_duration <= *right_duration);
            case ast::ExprBinaryOp::Greater:
                return make_bool_const(*left_duration > *right_duration);
            case ast::ExprBinaryOp::GreaterEqual:
                return make_bool_const(*left_duration >= *right_duration);
            case ast::ExprBinaryOp::And:
            case ast::ExprBinaryOp::Or:
            case ast::ExprBinaryOp::Implies:
            case ast::ExprBinaryOp::Equal:
            case ast::ExprBinaryOp::NotEqual:
            case ast::ExprBinaryOp::Add:
            case ast::ExprBinaryOp::Subtract:
            case ast::ExprBinaryOp::Multiply:
            case ast::ExprBinaryOp::Divide:
            case ast::ExprBinaryOp::Modulo:
                break;
            }
        }
        if (op == ast::ExprBinaryOp::Add && left.kind == ConstValueKind::String &&
            right.kind == ConstValueKind::String) {
            if (auto concatenated = concatenate_string_const(left.scalar, right.scalar);
                concatenated.has_value()) {
                return make_const_value(ConstValueKind::String, *concatenated);
            }
        }
        if (left.kind == ConstValueKind::Float && right.kind == ConstValueKind::Float &&
            op != ast::ExprBinaryOp::Modulo) {
            const auto left_float = parse_float_const(left.scalar);
            const auto right_float = parse_float_const(right.scalar);
            if (!left_float.has_value() || !right_float.has_value()) {
                break;
            }
            switch (op) {
            case ast::ExprBinaryOp::Add:
                return make_const_value(ConstValueKind::Float,
                                        render_float_const(*left_float + *right_float));
            case ast::ExprBinaryOp::Subtract:
                return make_const_value(ConstValueKind::Float,
                                        render_float_const(*left_float - *right_float));
            case ast::ExprBinaryOp::Multiply:
                return make_const_value(ConstValueKind::Float,
                                        render_float_const(*left_float * *right_float));
            case ast::ExprBinaryOp::Divide:
                if (*right_float == 0.0L) {
                    return std::nullopt;
                }
                return make_const_value(ConstValueKind::Float,
                                        render_float_const(*left_float / *right_float));
            case ast::ExprBinaryOp::Less:
                return make_bool_const(*left_float < *right_float);
            case ast::ExprBinaryOp::LessEqual:
                return make_bool_const(*left_float <= *right_float);
            case ast::ExprBinaryOp::Greater:
                return make_bool_const(*left_float > *right_float);
            case ast::ExprBinaryOp::GreaterEqual:
                return make_bool_const(*left_float >= *right_float);
            case ast::ExprBinaryOp::And:
            case ast::ExprBinaryOp::Or:
            case ast::ExprBinaryOp::Implies:
            case ast::ExprBinaryOp::Equal:
            case ast::ExprBinaryOp::NotEqual:
            case ast::ExprBinaryOp::Modulo:
                break;
            }
        }
        if (left.kind == ConstValueKind::Decimal && right.kind == ConstValueKind::Decimal) {
            const auto left_decimal = parse_decimal_const(left.scalar);
            if (!left_decimal.has_value()) {
                break;
            }
            const auto right_decimal = decimal_operand_at_scale(right, left_decimal->scale);
            if (!right_decimal.has_value()) {
                break;
            }
            switch (op) {
            case ast::ExprBinaryOp::Add:
                return make_const_value(ConstValueKind::Decimal,
                                        render_decimal_const(DecimalConst{
                                            .units = left_decimal->units + right_decimal->units,
                                            .scale = left_decimal->scale,
                                        }));
            case ast::ExprBinaryOp::Subtract:
                return make_const_value(ConstValueKind::Decimal,
                                        render_decimal_const(DecimalConst{
                                            .units = left_decimal->units - right_decimal->units,
                                            .scale = left_decimal->scale,
                                        }));
            case ast::ExprBinaryOp::Less:
                return make_bool_const(left_decimal->units < right_decimal->units);
            case ast::ExprBinaryOp::LessEqual:
                return make_bool_const(left_decimal->units <= right_decimal->units);
            case ast::ExprBinaryOp::Greater:
                return make_bool_const(left_decimal->units > right_decimal->units);
            case ast::ExprBinaryOp::GreaterEqual:
                return make_bool_const(left_decimal->units >= right_decimal->units);
            case ast::ExprBinaryOp::And:
            case ast::ExprBinaryOp::Or:
            case ast::ExprBinaryOp::Implies:
            case ast::ExprBinaryOp::Equal:
            case ast::ExprBinaryOp::NotEqual:
            case ast::ExprBinaryOp::Multiply:
            case ast::ExprBinaryOp::Divide:
            case ast::ExprBinaryOp::Modulo:
                break;
            }
        }
        if (left.kind != ConstValueKind::Integer || right.kind != ConstValueKind::Integer) {
            break;
        }
        const auto left_int = parse_int_const(left.scalar);
        const auto right_int = parse_int_const(right.scalar);
        if (!left_int.has_value() || !right_int.has_value()) {
            break;
        }
        switch (op) {
        case ast::ExprBinaryOp::Add:
            return make_const_value(ConstValueKind::Integer,
                                    std::to_string(*left_int + *right_int));
        case ast::ExprBinaryOp::Subtract:
            return make_const_value(ConstValueKind::Integer,
                                    std::to_string(*left_int - *right_int));
        case ast::ExprBinaryOp::Multiply:
            return make_const_value(ConstValueKind::Integer,
                                    std::to_string(*left_int * *right_int));
        case ast::ExprBinaryOp::Divide:
            if (*right_int == 0) {
                return std::nullopt;
            }
            return make_const_value(ConstValueKind::Integer,
                                    std::to_string(*left_int / *right_int));
        case ast::ExprBinaryOp::Modulo:
            if (*right_int == 0) {
                return std::nullopt;
            }
            return make_const_value(ConstValueKind::Integer,
                                    std::to_string(*left_int % *right_int));
        case ast::ExprBinaryOp::Less:
            return make_bool_const(*left_int < *right_int);
        case ast::ExprBinaryOp::LessEqual:
            return make_bool_const(*left_int <= *right_int);
        case ast::ExprBinaryOp::Greater:
            return make_bool_const(*left_int > *right_int);
        case ast::ExprBinaryOp::GreaterEqual:
            return make_bool_const(*left_int >= *right_int);
        case ast::ExprBinaryOp::And:
        case ast::ExprBinaryOp::Or:
        case ast::ExprBinaryOp::Implies:
        case ast::ExprBinaryOp::Equal:
        case ast::ExprBinaryOp::NotEqual:
            break;
        }
        break;
    }
    }
    return std::nullopt;
}

std::optional<ConstValue> fold_member_const(const ConstValue &base, std::string_view member) {
    const auto &value = effective_const_value(base);
    if (value.kind != ConstValueKind::Struct) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < value.children.size() && index < value.child_names.size();
         ++index) {
        if (value.child_names[index] == member) {
            return value.children[index];
        }
    }
    return std::nullopt;
}

std::optional<ConstValue> fold_index_const(const ConstValue &base, const ConstValue &index_value) {
    const auto &value = effective_const_value(base);
    const auto &index = effective_const_value(index_value);
    if (value.kind == ConstValueKind::List && index.kind == ConstValueKind::Integer) {
        const auto integer = parse_int_const(index.scalar);
        if (!integer.has_value() || *integer < 0) {
            return std::nullopt;
        }
        const auto offset = static_cast<std::size_t>(*integer);
        if (offset >= value.children.size()) {
            return std::nullopt;
        }
        return value.children[offset];
    }
    if (value.kind == ConstValueKind::Map) {
        for (std::size_t offset = 0; offset + 1 < value.children.size(); offset += 2) {
            if (value.child_names.size() <= offset + 1 || value.child_names[offset] != "key" ||
                value.child_names[offset + 1] != "value") {
                continue;
            }
            if (const_values_equal(value.children[offset], index)) {
                return value.children[offset + 1];
            }
        }
    }
    return std::nullopt;
}

ConstEvaluator::ConstEvaluator(const ResolveResult &resolve_result,
                               std::optional<SourceId> source_id,
                               const std::unordered_map<std::size_t, ConstValue> &const_values)
    : resolve_result_(resolve_result), source_id_(source_id), const_values_(const_values) {}

std::optional<ConstValue> ConstEvaluator::evaluate(const ast::ExprSyntax &expr) const {
    return std::visit(
        Overloaded{
            [](const ast::NoneLiteralExpr &) -> std::optional<ConstValue> {
                return make_const_value(ConstValueKind::NoneLiteral, "none");
            },
            [](const ast::BoolLiteralExpr &e) -> std::optional<ConstValue> {
                return make_bool_const(e.value);
            },
            [](const ast::IntegerLiteralExpr &e) -> std::optional<ConstValue> {
                return make_const_value(ConstValueKind::Integer, e.literal->spelling);
            },
            [](const ast::FloatLiteralExpr &e) -> std::optional<ConstValue> {
                return make_const_value(ConstValueKind::Float, e.spelling);
            },
            [](const ast::DecimalLiteralExpr &e) -> std::optional<ConstValue> {
                return make_const_value(ConstValueKind::Decimal, e.spelling);
            },
            [](const ast::StringLiteralExpr &e) -> std::optional<ConstValue> {
                return make_const_value(ConstValueKind::String, e.spelling);
            },
            [](const ast::DurationLiteralExpr &e) -> std::optional<ConstValue> {
                return duration_const_value(e.literal->spelling);
            },
            [this](const ast::QualifiedValueExpr &e) -> std::optional<ConstValue> {
                if (e.name == nullptr) {
                    return std::nullopt;
                }
                if (const auto reference = resolve_result_.find_reference(
                        ReferenceKind::ConstValue, e.name->range, source_id_);
                    reference.has_value()) {
                    auto value = make_const_value(ConstValueKind::ConstReference,
                                                  e.name->spelling(),
                                                  reference->get().target);
                    if (const auto iter = const_values_.find(reference->get().target.value);
                        iter != const_values_.end()) {
                        add_const_child(value, "value", iter->second);
                    }
                    return value;
                }
                if (const auto reference = resolve_result_.find_reference(
                        ReferenceKind::QualifiedValueOwnerType, e.name->range, source_id_);
                    reference.has_value()) {
                    return make_const_value(
                        ConstValueKind::EnumVariant, e.name->spelling(), reference->get().target);
                }
                return std::nullopt;
            },
            [this](const ast::SomeExpr &e) -> std::optional<ConstValue> {
                if (!e.value) {
                    return std::nullopt;
                }
                auto child = evaluate(*e.value);
                if (!child.has_value()) {
                    return std::nullopt;
                }
                auto value = make_const_value(ConstValueKind::Some, "some");
                add_const_child(value, "value", std::move(*child));
                return value;
            },
            [this, &expr](const ast::StructLiteralExpr &e) -> std::optional<ConstValue> {
                auto value =
                    make_const_value(ConstValueKind::Struct,
                                     e.type_name ? e.type_name->spelling() : std::string{},
                                     const_struct_symbol_for(expr, resolve_result_, source_id_));
                for (const auto &field : e.fields) {
                    if (field == nullptr || field->value == nullptr) {
                        return std::nullopt;
                    }
                    auto child = evaluate(*field->value);
                    if (!child.has_value()) {
                        return std::nullopt;
                    }
                    add_const_child(value, field->field_name, std::move(*child));
                }
                return value;
            },
            [this](const ast::ListLiteralExpr &e) -> std::optional<ConstValue> {
                auto value = make_const_value(ConstValueKind::List);
                for (const auto &item : e.items) {
                    if (item == nullptr) {
                        return std::nullopt;
                    }
                    auto child = evaluate(*item);
                    if (!child.has_value()) {
                        return std::nullopt;
                    }
                    add_const_child(value, "", std::move(*child));
                }
                return value;
            },
            [this](const ast::SetLiteralExpr &e) -> std::optional<ConstValue> {
                auto value = make_const_value(ConstValueKind::Set);
                for (const auto &item : e.items) {
                    if (item == nullptr) {
                        return std::nullopt;
                    }
                    auto child = evaluate(*item);
                    if (!child.has_value()) {
                        return std::nullopt;
                    }
                    add_const_child(value, "", std::move(*child));
                }
                normalize_set_const_value(value);
                return value;
            },
            [this](const ast::MapLiteralExpr &e) -> std::optional<ConstValue> {
                auto value = make_const_value(ConstValueKind::Map);
                for (const auto &entry : e.entries) {
                    if (entry == nullptr || entry->key == nullptr || entry->value == nullptr) {
                        return std::nullopt;
                    }
                    auto key = evaluate(*entry->key);
                    auto map_value = evaluate(*entry->value);
                    if (!key.has_value() || !map_value.has_value()) {
                        return std::nullopt;
                    }
                    add_const_child(value, "key", std::move(*key));
                    add_const_child(value, "value", std::move(*map_value));
                }
                normalize_map_const_value(value);
                return value;
            },
            [this](const ast::UnaryExpr &e) -> std::optional<ConstValue> {
                if (!e.operand) {
                    return std::nullopt;
                }
                auto child = evaluate(*e.operand);
                if (!child.has_value()) {
                    return std::nullopt;
                }
                if (auto folded = fold_unary_const(e.op, *child); folded.has_value()) {
                    return folded;
                }
                auto value = make_const_value(ConstValueKind::Unary,
                                              e.op == ast::ExprUnaryOp::Not      ? "!"
                                              : e.op == ast::ExprUnaryOp::Negate ? "-"
                                                                                 : "+");
                add_const_child(value, "operand", std::move(*child));
                return value;
            },
            [this](const ast::BinaryExpr &e) -> std::optional<ConstValue> {
                if (!e.lhs || !e.rhs) {
                    return std::nullopt;
                }
                auto lhs = evaluate(*e.lhs);
                auto rhs = evaluate(*e.rhs);
                if (!lhs.has_value() || !rhs.has_value()) {
                    return std::nullopt;
                }
                if (auto folded = fold_binary_const(e.op, *lhs, *rhs); folded.has_value()) {
                    return folded;
                }
                auto value = make_const_value(ConstValueKind::Binary,
                                              std::string(const_binary_op_spelling(e.op)));
                add_const_child(value, "lhs", std::move(*lhs));
                add_const_child(value, "rhs", std::move(*rhs));
                return value;
            },
            [this](const ast::MemberAccessExpr &e) -> std::optional<ConstValue> {
                if (!e.base) {
                    return std::nullopt;
                }
                auto base = evaluate(*e.base);
                if (!base.has_value()) {
                    return std::nullopt;
                }
                if (auto folded = fold_member_const(*base, e.member); folded.has_value()) {
                    return folded;
                }
                auto value = make_const_value(ConstValueKind::MemberAccess, e.member);
                add_const_child(value, "base", std::move(*base));
                return value;
            },
            [this](const ast::IndexAccessExpr &e) -> std::optional<ConstValue> {
                if (!e.base || !e.index) {
                    return std::nullopt;
                }
                auto base = evaluate(*e.base);
                auto index = evaluate(*e.index);
                if (!base.has_value() || !index.has_value()) {
                    return std::nullopt;
                }
                if (auto folded = fold_index_const(*base, *index); folded.has_value()) {
                    return folded;
                }
                auto value = make_const_value(ConstValueKind::IndexAccess);
                add_const_child(value, "base", std::move(*base));
                add_const_child(value, "index", std::move(*index));
                return value;
            },
            [this](const ast::GroupExpr &e) -> std::optional<ConstValue> {
                if (!e.inner) {
                    return std::nullopt;
                }
                return evaluate(*e.inner);
            },
            [](const ast::PathExpr &) -> std::optional<ConstValue> { return std::nullopt; },
            [](const ast::CallExpr &) -> std::optional<ConstValue> { return std::nullopt; },
            [](const ast::MethodCallExpr &) -> std::optional<ConstValue> { return std::nullopt; },
            // P1 (ADT): match expressions are not foldable compile-time constants.
            [](const ast::MatchExpr &) -> std::optional<ConstValue> { return std::nullopt; },
            // P2 (RFC §6): closures are runtime values, not foldable constants.
            [](const ast::LambdaExpr &) -> std::optional<ConstValue> { return std::nullopt; },
        },
        expr.node);
}

ConstValueTreeRecorder::ConstValueTreeRecorder(
    const ResolveResult &resolve_result,
    std::optional<SourceId> source_id,
    const std::unordered_map<std::size_t, ConstValue> &const_values,
    RememberConstValueFn remember_const_value)
    : evaluator_(resolve_result, source_id, const_values),
      remember_const_value_(std::move(remember_const_value)) {}

ConstEvalOutcome ConstValueTreeRecorder::evaluate_and_record(const ast::ExprSyntax &expr) const {
    auto value = evaluator_.evaluate(expr);
    if (!value.has_value()) {
        return const_eval_known(std::nullopt);
    }
    remember_const_value_(expr, *value);
    record_children(expr);
    return const_eval_known(std::move(value));
}

void ConstValueTreeRecorder::record(const ast::ExprSyntax &expr) const {
    (void)evaluate_and_record(expr);
}

void ConstValueTreeRecorder::record_children(const ast::ExprSyntax &expr) const {
    std::visit(Overloaded{
                   [this](const ast::SomeExpr &e) {
                       if (e.value != nullptr) {
                           record(*e.value);
                       }
                   },
                   [this](const ast::UnaryExpr &e) {
                       if (e.operand != nullptr) {
                           record(*e.operand);
                       }
                   },
                   [this](const ast::MemberAccessExpr &e) {
                       if (e.base != nullptr) {
                           record(*e.base);
                       }
                   },
                   [this](const ast::GroupExpr &e) {
                       if (e.inner != nullptr) {
                           record(*e.inner);
                       }
                   },
                   [this](const ast::BinaryExpr &e) {
                       if (e.lhs != nullptr) {
                           record(*e.lhs);
                       }
                       if (e.rhs != nullptr) {
                           record(*e.rhs);
                       }
                   },
                   [this](const ast::IndexAccessExpr &e) {
                       if (e.base != nullptr) {
                           record(*e.base);
                       }
                       if (e.index != nullptr) {
                           record(*e.index);
                       }
                   },
                   [this](const ast::StructLiteralExpr &e) {
                       for (const auto &field : e.fields) {
                           if (field != nullptr && field->value != nullptr) {
                               record(*field->value);
                           }
                       }
                   },
                   [this](const ast::ListLiteralExpr &e) {
                       for (const auto &item : e.items) {
                           if (item != nullptr) {
                               record(*item);
                           }
                       }
                   },
                   [this](const ast::SetLiteralExpr &e) {
                       for (const auto &item : e.items) {
                           if (item != nullptr) {
                               record(*item);
                           }
                       }
                   },
                   [this](const ast::MapLiteralExpr &e) {
                       for (const auto &entry : e.entries) {
                           if (entry != nullptr && entry->key != nullptr) {
                               record(*entry->key);
                           }
                           if (entry != nullptr && entry->value != nullptr) {
                               record(*entry->value);
                           }
                       }
                   },
                   [](const auto &) {
                       // No children for leaf nodes
                   },
               },
               expr.node);
}

ConstValueResolutionState::ConstValueResolutionState(
    std::unordered_map<std::size_t, ConstValue> &const_values,
    std::unordered_set<std::size_t> &active_const_values,
    std::unordered_set<std::size_t> &failed_const_values)
    : const_values_(const_values), active_const_values_(active_const_values),
      failed_const_values_(failed_const_values) {}

ConstValueResolutionBeginResult
ConstValueResolutionState::begin(SymbolId id, const Symbol *symbol, SourceRange use_range) {
    if (const_values_.find(id.value) != const_values_.end()) {
        return ConstValueResolutionBeginResult{
            .status = ConstValueResolutionStatus::Known,
            .diagnostics = {},
        };
    }
    if (failed_const_values_.find(id.value) != failed_const_values_.end()) {
        return ConstValueResolutionBeginResult{
            .status = ConstValueResolutionStatus::Failed,
            .diagnostics = {},
        };
    }
    if (!active_const_values_.insert(id.value).second) {
        failed_const_values_.insert(id.value);
        return ConstValueResolutionBeginResult{
            .status = ConstValueResolutionStatus::Cycle,
            .diagnostics =
                {
                    ConstDiagnosticReport{
                        .diagnostic = describe_const_dependency_cycle(symbol),
                        .range = use_range,
                    },
                },
        };
    }
    return ConstValueResolutionBeginResult{
        .status = ConstValueResolutionStatus::Entered,
        .diagnostics = {},
    };
}

bool ConstValueResolutionState::finish(SymbolId id, bool ok) {
    active_const_values_.erase(id.value);
    if (!ok) {
        failed_const_values_.insert(id.value);
    }
    return ok;
}

bool ConstValueResolutionState::fail(SymbolId id) {
    return finish(id, false);
}

void ConstValueResolutionState::remember(SymbolId id, ConstValue value) {
    const_values_.insert_or_assign(id.value, std::move(value));
}

bool ConstValueResolutionState::finish_after_assignability(SymbolId id,
                                                           const ConstEvalOutcome &outcome,
                                                           bool type_assignable) {
    if (!type_assignable || outcome.kind != ConstEvalKind::KnownConst ||
        !outcome.const_value.has_value()) {
        return finish(id, false);
    }

    remember(id, *outcome.const_value);
    return finish(id, true);
}

ConstDependencyResolver::ConstDependencyResolver(const ResolveResult &resolve_result,
                                                 std::optional<SourceId> source_id)
    : resolve_result_(resolve_result), source_id_(source_id) {}

std::vector<ConstDependencyReference>
ConstDependencyResolver::collect(const ast::ExprSyntax &expr) const {
    std::vector<ConstDependencyReference> references;
    collect_into(expr, references);
    return references;
}

void ConstDependencyResolver::collect_into(
    const ast::ExprSyntax &expr, std::vector<ConstDependencyReference> &references) const {
    std::visit(Overloaded{
                   [this, &expr, &references](const ast::QualifiedValueExpr &e) {
                       if (e.name != nullptr) {
                           if (const auto reference = resolve_result_.find_reference(
                                   ReferenceKind::ConstValue, e.name->range, source_id_);
                               reference.has_value()) {
                               references.push_back(ConstDependencyReference{
                                   .target = reference->get().target,
                                   .use_range = expr.range,
                                   .reference_range = reference->get().range,
                                   .source_id = reference->get().source_id,
                               });
                           }
                       }
                   },
                   [this, &references](const ast::SomeExpr &e) {
                       if (e.value != nullptr) {
                           collect_into(*e.value, references);
                       }
                   },
                   [this, &references](const ast::UnaryExpr &e) {
                       if (e.operand != nullptr) {
                           collect_into(*e.operand, references);
                       }
                   },
                   [this, &references](const ast::MemberAccessExpr &e) {
                       if (e.base != nullptr) {
                           collect_into(*e.base, references);
                       }
                   },
                   [this, &references](const ast::GroupExpr &e) {
                       if (e.inner != nullptr) {
                           collect_into(*e.inner, references);
                       }
                   },
                   [this, &references](const ast::BinaryExpr &e) {
                       if (e.lhs != nullptr) {
                           collect_into(*e.lhs, references);
                       }
                       if (e.rhs != nullptr) {
                           collect_into(*e.rhs, references);
                       }
                   },
                   [this, &references](const ast::IndexAccessExpr &e) {
                       if (e.base != nullptr) {
                           collect_into(*e.base, references);
                       }
                       if (e.index != nullptr) {
                           collect_into(*e.index, references);
                       }
                   },
                   [this, &references](const ast::StructLiteralExpr &e) {
                       for (const auto &field : e.fields) {
                           if (field != nullptr && field->value != nullptr) {
                               collect_into(*field->value, references);
                           }
                       }
                   },
                   [this, &references](const ast::ListLiteralExpr &e) {
                       for (const auto &item : e.items) {
                           if (item != nullptr) {
                               collect_into(*item, references);
                           }
                       }
                   },
                   [this, &references](const ast::SetLiteralExpr &e) {
                       for (const auto &item : e.items) {
                           if (item != nullptr) {
                               collect_into(*item, references);
                           }
                       }
                   },
                   [this, &references](const ast::MapLiteralExpr &e) {
                       for (const auto &entry : e.entries) {
                           if (entry != nullptr && entry->key != nullptr) {
                               collect_into(*entry->key, references);
                           }
                           if (entry != nullptr && entry->value != nullptr) {
                               collect_into(*entry->value, references);
                           }
                       }
                   },
                   [](const auto &) {
                       // Leaf nodes carry no const dependencies.
                   },
               },
               expr.node);
}

ConstDependencyScheduler::ConstDependencyScheduler(
    const ResolveResult &resolve_result,
    std::optional<SourceId> source_id,
    std::vector<ConstDependencyEdge> &dependency_edges,
    EnsureConstValueFn ensure_const_value)
    : resolve_result_(resolve_result), source_id_(source_id), dependency_edges_(dependency_edges),
      ensure_const_value_(std::move(ensure_const_value)) {}

bool ConstDependencyScheduler::ensure(std::optional<SymbolId> source_const,
                                      const ast::ExprSyntax &expr) const {
    bool ok = true;
    const ConstDependencyResolver resolver{resolve_result_, source_id_};
    for (const auto &dependency : resolver.collect(expr)) {
        if (source_const.has_value()) {
            dependency_edges_.push_back(ConstDependencyEdge{
                .source = *source_const,
                .target = dependency.target,
                .reference_range = dependency.reference_range,
                .source_id = dependency.source_id,
            });
        }
        ok = ensure_const_value_(dependency.target, dependency.use_range) && ok;
    }
    return ok;
}

ConstEvalPipeline::ConstEvalPipeline(
    const ResolveResult &resolve_result,
    std::optional<SourceId> source_id,
    std::vector<ConstDependencyEdge> &dependency_edges,
    const std::unordered_map<std::size_t, ConstValue> &const_values,
    EnsureConstValueFn ensure_const_value,
    RememberConstValueFn remember_const_value)
    : resolve_result_(resolve_result), source_id_(source_id), dependency_edges_(dependency_edges),
      const_values_(const_values), ensure_const_value_(std::move(ensure_const_value)),
      remember_const_value_(std::move(remember_const_value)) {}

ConstEvalPipelineResult ConstEvalPipeline::evaluate(const ast::ExprSyntax &expr,
                                                    const ConstCheckedExpression &checked_expr,
                                                    std::string_view context_label,
                                                    std::optional<SymbolId> source_const) const {
    auto gate = classify_const_expr_gate(expr, checked_expr.is_pure, checked_expr.effect);
    if (gate.status != ConstExprGateStatus::Accepted) {
        return ConstEvalPipelineResult{
            .outcome = const_eval_not_const(),
            .diagnostics =
                {
                    ConstDiagnosticReport{
                        .diagnostic = describe_const_expr_required(context_label, gate),
                        .range = expr.range,
                    },
                },
        };
    }

    ConstDependencyScheduler dependencies{
        resolve_result_,
        source_id_,
        dependency_edges_,
        ensure_const_value_,
    };
    if (!dependencies.ensure(source_const, expr)) {
        return ConstEvalPipelineResult{
            .outcome = const_eval_error(),
            .diagnostics = {},
        };
    }

    const ConstValueTreeRecorder recorder{
        resolve_result_,
        source_id_,
        const_values_,
        remember_const_value_,
    };
    return ConstEvalPipelineResult{
        .outcome = recorder.evaluate_and_record(expr),
        .diagnostics = {},
    };
}

ConstExpressionDriver::ConstExpressionDriver(ConstEvalPipeline const_eval,
                                             ConstDiagnosticEmitter diagnostics,
                                             CheckExpressionFn check_expression)
    : const_eval_(std::move(const_eval)), diagnostics_(diagnostics),
      check_expression_(std::move(check_expression)) {}

ConstExpressionResult ConstExpressionDriver::evaluate(const ast::ExprSyntax &expr,
                                                      MaybeCRef<Type> expected_type,
                                                      std::string_view context_label,
                                                      std::optional<SymbolId> source_const) const {
    auto checked_expr = check_expression_(expr, expected_type);
    auto const_result = const_eval_.evaluate(expr, checked_expr, context_label, source_const);
    diagnostics_.emit_all(std::move(const_result.diagnostics));

    return ConstExpressionResult{
        .outcome = std::move(const_result.outcome),
        .checked_expr = std::move(checked_expr),
    };
}

} // namespace ahfl
