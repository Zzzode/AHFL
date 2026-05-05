#include "ahfl/evaluator/value.hpp"

#include <memory>
#include <ostream>

namespace ahfl::evaluator {

// ============================================================================
// value_kind implementation
// ============================================================================

ValueKind value_kind(const Value &v) {
    return std::visit([](const auto &inner) -> ValueKind {
        using T = std::decay_t<decltype(inner)>;
        if constexpr (std::is_same_v<T, NoneValue>) {
            return ValueKind::None;
        } else if constexpr (std::is_same_v<T, BoolValue>) {
            return ValueKind::Bool;
        } else if constexpr (std::is_same_v<T, IntValue>) {
            return ValueKind::Int;
        } else if constexpr (std::is_same_v<T, FloatValue>) {
            return ValueKind::Float;
        } else if constexpr (std::is_same_v<T, StringValue>) {
            return ValueKind::String;
        } else if constexpr (std::is_same_v<T, DecimalValue>) {
            return ValueKind::Decimal;
        } else if constexpr (std::is_same_v<T, DurationValue>) {
            return ValueKind::Duration;
        } else if constexpr (std::is_same_v<T, StructValue>) {
            return ValueKind::Struct;
        } else if constexpr (std::is_same_v<T, ListValue>) {
            return ValueKind::List;
        } else if constexpr (std::is_same_v<T, EnumValue>) {
            return ValueKind::Enum;
        } else if constexpr (std::is_same_v<T, OptionalValue>) {
            return ValueKind::Optional;
        }
    }, v.node);
}

// ============================================================================
// is_none implementation
// ============================================================================

bool is_none(const Value &v) {
    return std::holds_alternative<NoneValue>(v.node);
}

// ============================================================================
// print_value implementation
// ============================================================================

void print_value(const Value &v, std::ostream &out) {
    std::visit([&out](const auto &inner) {
        using T = std::decay_t<decltype(inner)>;
        if constexpr (std::is_same_v<T, NoneValue>) {
            out << "none";
        } else if constexpr (std::is_same_v<T, BoolValue>) {
            out << (inner.value ? "true" : "false");
        } else if constexpr (std::is_same_v<T, IntValue>) {
            out << inner.value;
        } else if constexpr (std::is_same_v<T, FloatValue>) {
            out << inner.value;
        } else if constexpr (std::is_same_v<T, StringValue>) {
            out << "\"" << inner.value << "\"";
        } else if constexpr (std::is_same_v<T, DecimalValue>) {
            out << inner.spelling;
        } else if constexpr (std::is_same_v<T, DurationValue>) {
            out << inner.spelling;
        } else if constexpr (std::is_same_v<T, StructValue>) {
            out << inner.type_name << " { ";
            bool first = true;
            for (const auto &[name, val] : inner.fields) {
                if (!first) out << ", ";
                first = false;
                out << name << ": ";
                if (val) {
                    print_value(*val, out);
                } else {
                    out << "<null>";
                }
            }
            out << " }";
        } else if constexpr (std::is_same_v<T, ListValue>) {
            out << "[";
            for (size_t i = 0; i < inner.items.size(); ++i) {
                if (i > 0) out << ", ";
                if (inner.items[i]) {
                    print_value(*inner.items[i], out);
                } else {
                    out << "<null>";
                }
            }
            out << "]";
        } else if constexpr (std::is_same_v<T, EnumValue>) {
            out << inner.enum_name << "::" << inner.variant;
        } else if constexpr (std::is_same_v<T, OptionalValue>) {
            if (inner.inner) {
                out << "some(";
                print_value(*inner.inner, out);
                out << ")";
            } else {
                out << "none";
            }
        }
    }, v.node);
}

// ============================================================================
// Convenience constructor implementations
// ============================================================================

Value make_optional_some(Value inner) {
    return Value{OptionalValue{std::make_unique<Value>(std::move(inner))}};
}

Value make_optional_none() {
    return Value{OptionalValue{nullptr}};
}

Value make_struct(std::string type_name,
                  std::unordered_map<std::string, Value> fields) {
    StructValue sv;
    sv.type_name = std::move(type_name);
    for (auto &[name, val] : fields) {
        sv.fields.emplace(name, std::make_unique<Value>(std::move(val)));
    }
    return Value{std::move(sv)};
}

Value make_list(std::vector<Value> items) {
    ListValue lv;
    for (auto &item : items) {
        lv.items.push_back(std::make_unique<Value>(std::move(item)));
    }
    return Value{std::move(lv)};
}

// ============================================================================
// clone_value implementation
// ============================================================================

Value clone_value(const Value &v) {
    return std::visit([](const auto &inner) -> Value {
        using T = std::decay_t<decltype(inner)>;
        if constexpr (std::is_same_v<T, NoneValue>) {
            return make_none();
        } else if constexpr (std::is_same_v<T, BoolValue>) {
            return make_bool(inner.value);
        } else if constexpr (std::is_same_v<T, IntValue>) {
            return make_int(inner.value);
        } else if constexpr (std::is_same_v<T, FloatValue>) {
            return make_float(inner.value);
        } else if constexpr (std::is_same_v<T, StringValue>) {
            return make_string(inner.value);
        } else if constexpr (std::is_same_v<T, DecimalValue>) {
            return make_decimal(inner.spelling);
        } else if constexpr (std::is_same_v<T, DurationValue>) {
            return make_duration(inner.spelling);
        } else if constexpr (std::is_same_v<T, StructValue>) {
            StructValue sv;
            sv.type_name = inner.type_name;
            for (const auto &[name, val] : inner.fields) {
                if (val) {
                    sv.fields.emplace(name, std::make_unique<Value>(clone_value(*val)));
                }
            }
            return Value{std::move(sv)};
        } else if constexpr (std::is_same_v<T, ListValue>) {
            ListValue lv;
            for (const auto &item : inner.items) {
                if (item) {
                    lv.items.push_back(std::make_unique<Value>(clone_value(*item)));
                }
            }
            return Value{std::move(lv)};
        } else if constexpr (std::is_same_v<T, EnumValue>) {
            return make_enum(inner.enum_name, inner.variant);
        } else if constexpr (std::is_same_v<T, OptionalValue>) {
            if (inner.inner) {
                return make_optional_some(clone_value(*inner.inner));
            }
            return make_optional_none();
        }
    }, v.node);
}

} // namespace ahfl::evaluator
