#pragma once

#include <string>
#include <vector>

#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl::llm_provider {

// 从 IR 类型信息构建 LLM prompt
class PromptBuilder {
  public:
    explicit PromptBuilder(const ir::Program &program);

    // 为指定 capability 构建 system prompt（含返回类型 JSON schema）
    [[nodiscard]] std::string build_system_prompt(const std::string &capability_name) const;

    // 为指定参数值构建 user prompt
    [[nodiscard]] std::string build_user_prompt(const std::string &capability_name,
                                                const std::vector<evaluator::Value> &args) const;

  private:
    const ir::Program &program_;

    [[nodiscard]] const ir::CapabilityDecl *find_capability(const std::string &name) const;
    [[nodiscard]] const ir::StructDecl *find_struct(const std::string &name) const;
    [[nodiscard]] const ir::EnumDecl *find_enum(const std::string &name) const;

    // 生成类型的 JSON schema 描述
    [[nodiscard]] std::string describe_type_schema(const std::string &type_name) const;
    // 将 Value 转为可读字符串
    [[nodiscard]] std::string value_to_string(const evaluator::Value &val) const;
};

} // namespace ahfl::llm_provider
