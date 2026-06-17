#include "runtime/engine/response_schema_validator.hpp"

#include "ahfl/compiler/ir/program_view.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace ahfl::runtime {
namespace {

[[nodiscard]] const char *kind_label(const evaluator::Value &v) {
    using namespace ahfl::evaluator;
    return std::visit(
        [](const auto &inner) -> const char * {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>)
                return "None";
            else if constexpr (std::is_same_v<T, BoolValue>)
                return "Bool";
            else if constexpr (std::is_same_v<T, IntValue>)
                return "Int";
            else if constexpr (std::is_same_v<T, FloatValue>)
                return "Float";
            else if constexpr (std::is_same_v<T, StringValue>)
                return "String";
            else if constexpr (std::is_same_v<T, DecimalValue>)
                return "Decimal";
            else if constexpr (std::is_same_v<T, DurationValue>)
                return "Duration";
            else if constexpr (std::is_same_v<T, StructValue>)
                return "Struct";
            else if constexpr (std::is_same_v<T, ListValue>)
                return "List";
            else if constexpr (std::is_same_v<T, EnumValue>)
                return "Enum";
            else if constexpr (std::is_same_v<T, OptionalValue>)
                return "Optional";
            else
                return "Unknown";
        },
        v.node);
}

[[nodiscard]] std::string type_name(const ir::TypeRef &type) {
    if (!type.canonical_name.empty()) {
        return type.canonical_name;
    }
    return type.display_name;
}

[[nodiscard]] bool type_name_matches(std::string_view actual, const ir::TypeRef &expected) {
    if (!expected.canonical_name.empty() && actual == expected.canonical_name) {
        return true;
    }
    if (!expected.display_name.empty() && actual == expected.display_name) {
        return true;
    }
    return expected.canonical_name.empty() && expected.display_name.empty();
}

[[nodiscard]] const ir::StructDecl *find_struct(const ir::ProgramIndex *index,
                                                const ir::TypeRef &expected) {
    if (index == nullptr) {
        return nullptr;
    }
    if (!expected.canonical_name.empty()) {
        if (const auto *decl = index->find_struct(expected.canonical_name); decl != nullptr) {
            return decl;
        }
    }
    if (!expected.display_name.empty()) {
        if (const auto *decl = index->find_struct(expected.display_name); decl != nullptr) {
            return decl;
        }
        for (const auto *decl : index->structs()) {
            if (decl != nullptr && decl->name == expected.display_name) {
                return decl;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const ir::EnumDecl *find_enum(const ir::ProgramIndex *index,
                                            const ir::TypeRef &expected) {
    if (index == nullptr) {
        return nullptr;
    }
    if (!expected.canonical_name.empty()) {
        if (const auto *decl = index->find_enum(expected.canonical_name); decl != nullptr) {
            return decl;
        }
    }
    if (!expected.display_name.empty()) {
        if (const auto *decl = index->find_enum(expected.display_name); decl != nullptr) {
            return decl;
        }
        for (const auto *decl : index->enums()) {
            if (decl != nullptr && decl->name == expected.display_name) {
                return decl;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::string at_path(std::string_view path, std::string message) {
    if (path.empty()) {
        return message;
    }
    return std::string(path) + ": " + message;
}

[[nodiscard]] SchemaValidationResult check(const evaluator::Value &value,
                                           const ir::TypeRef &expected,
                                           const ir::ProgramIndex *index,
                                           std::string_view path) {
    using namespace ahfl::evaluator;
    using Kind = ir::TypeRefKind;

    if (expected.kind == Kind::Any) {
        return SchemaValidationResult::ok();
    }

    if (expected.kind == Kind::Unit) {
        if (is_none(value)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(at_path(path, "expected Unit but got a value"));
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
                return check(*opt->inner, *expected.first, index, path);
            }
            return SchemaValidationResult::ok();
        }
        if (expected.first) {
            return check(value, *expected.first, index, path);
        }
        return SchemaValidationResult::ok();
    }

    if (expected.kind == Kind::Bool) {
        if (std::holds_alternative<BoolValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Bool but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Int) {
        if (std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Int but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Float) {
        if (std::holds_alternative<FloatValue>(value.node) ||
            std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Float but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::String || expected.kind == Kind::BoundedString) {
        if (std::holds_alternative<StringValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected String but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Decimal) {
        if (std::holds_alternative<DecimalValue>(value.node) ||
            std::holds_alternative<FloatValue>(value.node) ||
            std::holds_alternative<IntValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Decimal but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Duration) {
        if (std::holds_alternative<DurationValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Duration but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Struct) {
        if (auto *sv = std::get_if<StructValue>(&value.node)) {
            if (!type_name_matches(sv->type_name, expected)) {
                return SchemaValidationResult::fail(
                    at_path(path,
                            "expected struct '" + type_name(expected) + "' but got '" +
                                sv->type_name + "'"));
            }

            const auto *decl = find_struct(index, expected);
            if (decl == nullptr) {
                return SchemaValidationResult::ok();
            }

            std::unordered_set<std::string> expected_fields;
            expected_fields.reserve(decl->fields.size());
            for (const auto &field : decl->fields) {
                expected_fields.insert(field.name);
                const auto found = sv->fields.find(field.name);
                if (found == sv->fields.end() || found->second == nullptr) {
                    return SchemaValidationResult::fail(at_path(
                        path,
                        "missing field '" + field.name + "' for struct '" + decl->name + "'"));
                }
                const auto field_path =
                    path.empty() ? field.name : std::string(path) + "." + field.name;
                auto field_result = check(*found->second, field.type_ref, index, field_path);
                if (!field_result.valid) {
                    return field_result;
                }
            }

            for (const auto &[name, field_value] : sv->fields) {
                (void)field_value;
                if (!expected_fields.contains(name)) {
                    return SchemaValidationResult::fail(at_path(
                        path, "unexpected field '" + name + "' for struct '" + decl->name + "'"));
                }
            }
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Struct but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Enum) {
        if (auto *ev = std::get_if<EnumValue>(&value.node)) {
            if (!type_name_matches(ev->enum_name, expected)) {
                return SchemaValidationResult::fail(at_path(
                    path,
                    "expected enum '" + type_name(expected) + "' but got '" + ev->enum_name + "'"));
            }

            const auto *decl = find_enum(index, expected);
            if (decl == nullptr) {
                return SchemaValidationResult::ok();
            }
            if (std::ranges::find(decl->variants, ev->variant) == decl->variants.end()) {
                return SchemaValidationResult::fail(at_path(path,
                                                            "unknown enum variant '" + ev->variant +
                                                                "' for enum '" + decl->name + "'"));
            }
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Enum but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::List || expected.kind == Kind::Set) {
        if (const auto *list = std::get_if<ListValue>(&value.node)) {
            if (expected.first) {
                for (std::size_t index_value = 0; index_value < list->items.size(); ++index_value) {
                    if (!list->items[index_value]) {
                        return SchemaValidationResult::fail(
                            at_path(path, "list item " + std::to_string(index_value) + " is null"));
                    }
                    const auto item_path =
                        std::string(path) + "[" + std::to_string(index_value) + "]";
                    auto item_result =
                        check(*list->items[index_value], *expected.first, index, item_path);
                    if (!item_result.valid) {
                        return item_result;
                    }
                }
            }
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected List/Set but got " + std::string(kind_label(value))));
    }

    if (expected.kind == Kind::Map) {
        if (std::holds_alternative<StructValue>(value.node)) {
            return SchemaValidationResult::ok();
        }
        return SchemaValidationResult::fail(
            at_path(path, "expected Map but got " + std::string(kind_label(value))));
    }

    return SchemaValidationResult::ok();
}

} // namespace

SchemaValidationResult validate_value_against_schema(const evaluator::Value &value,
                                                     const ir::TypeRef &expected) {
    return check(value, expected, nullptr, "");
}

SchemaValidationResult validate_value_against_schema(const evaluator::Value &value,
                                                     const ir::TypeRef &expected,
                                                     const ir::Program &program) {
    const ir::ProgramIndex index{program};
    return check(value, expected, &index, "");
}

} // namespace ahfl::runtime
