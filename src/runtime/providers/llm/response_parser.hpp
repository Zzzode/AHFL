#pragma once

#include <optional>
#include <string>

#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::llm_provider {

struct ResponseParseResult {
    std::optional<evaluator::Value> value;
    std::string error_message;

    [[nodiscard]] bool success() const noexcept {
        return value.has_value();
    }
};

// 将 LLM 的 JSON 响应解析为 AHFL Value
class ResponseParser {
  public:
    explicit ResponseParser(const ir::Program &program);

    // 解析 JSON 字符串为指定类型的 Value
    [[nodiscard]] std::optional<evaluator::Value> parse(const std::string &json_str,
                                                        const std::string &expected_type) const;
    [[nodiscard]] ResponseParseResult
    parse_with_diagnostics(const std::string &json_str, const std::string &expected_type) const;

  private:
    const ir::Program &program_;

    [[nodiscard]] const ir::StructDecl *find_struct(const std::string &name) const;
    [[nodiscard]] const ir::EnumDecl *find_enum(const std::string &name) const;

    // 解析 JSON object 为 struct Value
    [[nodiscard]] ResponseParseResult parse_struct(const std::string &json_obj,
                                                   const ir::StructDecl &decl) const;

    // 从 JSON object 中提取指定 key 的 value
    [[nodiscard]] std::string extract_json_value(const std::string &json_str,
                                                 const std::string &key) const;

    // 解析基本类型值
    [[nodiscard]] ResponseParseResult parse_primitive(const std::string &value_str,
                                                      const std::string &type_name) const;
};

} // namespace ahfl::llm_provider
