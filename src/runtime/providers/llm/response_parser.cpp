// response_parser.cpp - LLM JSON 响应 → AHFL Value 解析

#include "runtime/providers/llm/response_parser.hpp"

#include "base/json/json_value.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ahfl::llm_provider {

namespace {

[[nodiscard]] ResponseParseResult parse_success(evaluator::Value value) {
    return ResponseParseResult{
        .value = std::move(value),
        .error_message = {},
    };
}

[[nodiscard]] ResponseParseResult parse_error(std::string message) {
    return ResponseParseResult{
        .value = std::nullopt,
        .error_message = std::move(message),
    };
}

} // namespace

ResponseParser::ResponseParser(const ir::Program &program) : program_(program) {}

const ir::StructDecl *ResponseParser::find_struct(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (auto *s = std::get_if<ir::StructDecl>(&decl)) {
            if (s->name == name) {
                return s;
            }
        }
    }
    return nullptr;
}

const ir::EnumDecl *ResponseParser::find_enum(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (auto *e = std::get_if<ir::EnumDecl>(&decl)) {
            if (e->name == name) {
                return e;
            }
        }
    }
    return nullptr;
}

std::string ResponseParser::extract_json_value(const std::string &json_str,
                                               const std::string &key) const {
    auto parsed = ahfl::json::parse_json(json_str);
    if (!parsed.has_value() || !*parsed) {
        return "";
    }
    const auto *field = (*parsed)->get(key);
    if (field == nullptr) {
        return "";
    }
    if (auto value = field->as_string(); value.has_value()) {
        return std::string(*value);
    }
    if (auto value = field->as_int(); value.has_value()) {
        return std::to_string(*value);
    }
    if (field->kind == ahfl::json::Kind::Float) {
        return ahfl::json::serialize_json(*field);
    }
    if (auto value = field->as_bool(); value.has_value()) {
        return *value ? "true" : "false";
    }
    if (field->is_object() || field->is_array()) {
        return ahfl::json::serialize_json(*field);
    }
    return "";
}

ResponseParseResult ResponseParser::parse_primitive(const std::string &value_str,
                                                    const std::string &type_name) const {
    if (type_name == "String") {
        return parse_success(evaluator::make_string(value_str));
    }
    if (type_name == "Int") {
        try {
            return parse_success(evaluator::make_int(std::stoll(value_str)));
        } catch (const std::exception &) {
            return parse_error("expected Int but got '" + value_str + "'");
        }
    }
    if (type_name == "Float") {
        try {
            return parse_success(evaluator::make_float(std::stod(value_str)));
        } catch (const std::exception &) {
            return parse_error("expected Float but got '" + value_str + "'");
        }
    }
    if (type_name == "Bool") {
        if (value_str == "true") {
            return parse_success(evaluator::make_bool(true));
        }
        if (value_str == "false") {
            return parse_success(evaluator::make_bool(false));
        }
        return parse_error("expected Bool but got '" + value_str + "'");
    }

    // 枚举类型
    if (const auto *enum_decl = find_enum(type_name)) {
        // 验证 variant 是否合法
        for (const auto &variant : enum_decl->variants) {
            if (variant == value_str) {
                return parse_success(evaluator::make_enum(type_name, value_str));
            }
        }
        return parse_error("unknown variant '" + value_str + "' for enum '" + type_name + "'");
    }

    return parse_error("unknown expected type: " + type_name);
}

ResponseParseResult ResponseParser::parse_struct(const std::string &json_obj,
                                                 const ir::StructDecl &decl) const {
    auto parsed = ahfl::json::parse_json(json_obj);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return parse_error("expected JSON object for struct '" + decl.name + "'");
    }

    std::unordered_map<std::string, evaluator::Value> fields;

    for (const auto &field : decl.fields) {
        const auto *field_json = (*parsed)->get(field.name);
        if (field_json == nullptr || field_json->is_null()) {
            return parse_error("missing required field '" + field.name + "' for struct '" +
                               decl.name + "'");
        }

        std::string raw_value;
        if (auto string_value = field_json->as_string(); string_value.has_value()) {
            raw_value = std::string(*string_value);
        } else if (auto int_value = field_json->as_int(); int_value.has_value()) {
            raw_value = std::to_string(*int_value);
        } else if (field_json->kind == ahfl::json::Kind::Float) {
            raw_value = ahfl::json::serialize_json(*field_json);
        } else if (auto bool_value = field_json->as_bool(); bool_value.has_value()) {
            raw_value = *bool_value ? "true" : "false";
        } else {
            raw_value = ahfl::json::serialize_json(*field_json);
        }

        // 检查是否是结构体类型
        if (const auto *nested_struct = find_struct(field.type)) {
            auto nested_val = parse_struct(raw_value, *nested_struct);
            if (nested_val.success()) {
                fields.emplace(field.name, std::move(*nested_val.value));
            } else {
                return parse_error("field '" + field.name + "': " + nested_val.error_message);
            }
            continue;
        }

        // 解析基本类型
        auto field_val = parse_primitive(raw_value, field.type);
        if (field_val.success()) {
            fields.emplace(field.name, std::move(*field_val.value));
        } else {
            return parse_error("field '" + field.name + "': " + field_val.error_message);
        }
    }

    return parse_success(evaluator::make_struct(decl.name, std::move(fields)));
}

std::optional<evaluator::Value> ResponseParser::parse(const std::string &json_str,
                                                      const std::string &expected_type) const {
    auto result = parse_with_diagnostics(json_str, expected_type);
    if (result.success()) {
        return std::move(result.value);
    }
    return std::nullopt;
}

ResponseParseResult ResponseParser::parse_with_diagnostics(const std::string &json_str,
                                                           const std::string &expected_type) const {
    // 基本类型
    if (expected_type == "String" || expected_type == "Int" || expected_type == "Float" ||
        expected_type == "Bool") {
        return parse_primitive(json_str, expected_type);
    }

    // 枚举类型
    if (find_enum(expected_type) != nullptr) {
        // 如果 json_str 是一个 JSON 对象中某个字段的值，直接作为枚举
        return parse_primitive(json_str, expected_type);
    }

    // 结构体类型
    if (const auto *struct_decl = find_struct(expected_type)) {
        return parse_struct(json_str, *struct_decl);
    }

    return parse_error("unknown expected type: " + expected_type);
}

} // namespace ahfl::llm_provider
