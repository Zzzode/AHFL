// response_parser.cpp - LLM JSON 响应 → AHFL Value 解析

#include "ahfl/llm_provider/response_parser.hpp"

#include <algorithm>
#include <string>
#include <variant>

namespace ahfl::llm_provider {

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
    // 查找 "key"
    std::string search = "\"" + key + "\"";
    auto pos = json_str.find(search);
    if (pos == std::string::npos) {
        return "";
    }

    // 跳过 key 和冒号
    pos += search.size();
    pos = json_str.find(':', pos);
    if (pos == std::string::npos) {
        return "";
    }
    ++pos;

    // 跳过空白
    while (pos < json_str.size() && (json_str[pos] == ' ' || json_str[pos] == '\t' ||
                                     json_str[pos] == '\n' || json_str[pos] == '\r')) {
        ++pos;
    }

    if (pos >= json_str.size()) {
        return "";
    }

    // 字符串值
    if (json_str[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < json_str.size() && json_str[pos] != '"') {
            if (json_str[pos] == '\\' && pos + 1 < json_str.size()) {
                ++pos;
                switch (json_str[pos]) {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                default:
                    value += json_str[pos];
                    break;
                }
            } else {
                value += json_str[pos];
            }
            ++pos;
        }
        return value;
    }

    // 嵌套对象
    if (json_str[pos] == '{') {
        int depth = 1;
        std::string value;
        value += json_str[pos];
        ++pos;
        while (pos < json_str.size() && depth > 0) {
            if (json_str[pos] == '{') {
                ++depth;
            } else if (json_str[pos] == '}') {
                --depth;
            }
            value += json_str[pos];
            ++pos;
        }
        return value;
    }

    // 数字、布尔值或 null
    std::string value;
    while (pos < json_str.size() && json_str[pos] != ',' && json_str[pos] != '}' &&
           json_str[pos] != ']' && json_str[pos] != ' ' && json_str[pos] != '\n' &&
           json_str[pos] != '\r') {
        value += json_str[pos];
        ++pos;
    }
    return value;
}

std::optional<evaluator::Value>
ResponseParser::parse_primitive(const std::string &value_str, const std::string &type_name) const {
    if (type_name == "String") {
        return evaluator::make_string(value_str);
    }
    if (type_name == "Int") {
        try {
            return evaluator::make_int(std::stoll(value_str));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (type_name == "Float") {
        try {
            return evaluator::make_float(std::stod(value_str));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (type_name == "Bool") {
        return evaluator::make_bool(value_str == "true");
    }

    // 枚举类型
    if (const auto *enum_decl = find_enum(type_name)) {
        // 验证 variant 是否合法
        for (const auto &variant : enum_decl->variants) {
            if (variant == value_str) {
                return evaluator::make_enum(type_name, value_str);
            }
        }
        // 不匹配则使用原值（容错）
        return evaluator::make_enum(type_name, value_str);
    }

    return std::nullopt;
}

std::optional<evaluator::Value> ResponseParser::parse_struct(const std::string &json_obj,
                                                             const ir::StructDecl &decl) const {
    std::unordered_map<std::string, evaluator::Value> fields;

    for (const auto &field : decl.fields) {
        std::string raw_value = extract_json_value(json_obj, field.name);
        if (raw_value.empty()) {
            // 字段缺失，使用默认值
            fields.emplace(field.name, evaluator::make_none());
            continue;
        }

        // 检查是否是结构体类型
        if (const auto *nested_struct = find_struct(field.type)) {
            auto nested_val = parse_struct(raw_value, *nested_struct);
            if (nested_val.has_value()) {
                fields.emplace(field.name, std::move(*nested_val));
            } else {
                fields.emplace(field.name, evaluator::make_none());
            }
            continue;
        }

        // 解析基本类型
        auto field_val = parse_primitive(raw_value, field.type);
        if (field_val.has_value()) {
            fields.emplace(field.name, std::move(*field_val));
        } else {
            // 如果类型解析失败，作为字符串保存
            fields.emplace(field.name, evaluator::make_string(raw_value));
        }
    }

    return evaluator::make_struct(decl.name, std::move(fields));
}

std::optional<evaluator::Value> ResponseParser::parse(const std::string &json_str,
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

    return std::nullopt;
}

} // namespace ahfl::llm_provider
