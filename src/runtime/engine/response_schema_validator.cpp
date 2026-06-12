#include "runtime/engine/response_schema_validator.hpp"

#include <variant>

namespace ahfl::runtime {
namespace {

[[nodiscard]] const char *kind_label(const evaluator::Value &v) {
    using namespace ahfl::evaluator;
    return std::visit(
        [](const auto &inner) -> const char * {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) return "None";
            else if constexpr (std::is_same_v<T, BoolValue>) return "Bool";
            else if constexpr (std::is_same_v<T, IntValue>) return "Int";
            else if constexpr (std::is_same_v<T, FloatValue>) return "Float";
            else if constexpr (std::is_same_v<T, StringValue>) return "String";
            else if constexpr (std::is_same_v<T, DecimalValue>) return "Decimal";
            else if constexpr (std::is_same_v<T, DurationValue>) return "Duration";
            else if constexpr (std::is_same_v<T, StructValue>) return "Struct";
            else if constexpr (std::is_same_v<T, ListValue>) return "List";
            else if constexpr (std::is_same_v<T, EnumValue>) return "Enum";
            else if constexpr (std::is_same_v<T, OptionalValue>) return "Optional";
            else return "Unknown";
        },
        v.node);
}

[[nodiscard]] SchemaValidationResult check(const evaluator::Value &value,
                                           const ir::TypeRef &expected) {
    using namespace ahfl::evaluator;
    using Kind = ir::TypeRefKind;

    if (expected.kind == Kind::Any) {
        return SchemaValidationResult::ok();
    }

    if (expected.kind == Kind::Unit) {
        if (is_none(value)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Unit but got a value");
    }

    if (expected.kind == Kind::Optional) {
        if (is_none(value)) {
            return SchemaValidationResult::ok();
        }
        if (auto *opt = std::get_if<OptionalValue>(&value.node)) {
            if (!opt->inner) {
                return SchemaValidationResult::ok();
            }
            if (expected.first) {
                return check(*opt->inner, *expected.first);
            }
            return SchemaValidationResult::ok();
        }
        if (expected.first) {
            return check(value, *expected.first);
        }
        return SchemaValidationResult::ok();
    }

    if (expected.kind == Kind::Bool) {
        if (std::holds_alternative<BoolValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Bool but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Int) {
        if (std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Int but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Float) {
        if (std::holds_alternative<FloatValue>(value.node) ||
            std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Float but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::String || expected.kind == Kind::BoundedString) {
        if (std::holds_alternative<StringValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected String but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Decimal) {
        if (std::holds_alternative<DecimalValue>(value.node) ||
            std::holds_alternative<FloatValue>(value.node) ||
            std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Decimal but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Duration) {
        if (std::holds_alternative<DurationValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Duration but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Struct) {
        if (auto *sv = std::get_if<StructValue>(&value.node)) {
            if (!expected.display_name.empty() && sv->type_name != expected.display_name) {
                return SchemaValidationResult::fail("expected struct '" + expected.display_name +
                                                    "' but got '" + sv->type_name + "'");
            }
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Struct but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Enum) {
        if (auto *ev = std::get_if<EnumValue>(&value.node)) {
            if (!expected.display_name.empty() && ev->enum_name != expected.display_name) {
                return SchemaValidationResult::fail("expected enum '" + expected.display_name +
                                                    "' but got '" + ev->enum_name + "'");
            }
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Enum but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::List || expected.kind == Kind::Set) {
        if (std::holds_alternative<ListValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected List/Set but got " +
                                            std::string(kind_label(value)));
    }

    if (expected.kind == Kind::Map) {
        if (std::holds_alternative<StructValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail("expected Map but got " +
                                            std::string(kind_label(value)));
    }

    return SchemaValidationResult::ok();
}

} // namespace

SchemaValidationResult validate_value_against_schema(const evaluator::Value &value,
                                                     const ir::TypeRef &expected) {
    return check(value, expected);
}

} // namespace ahfl::runtime
