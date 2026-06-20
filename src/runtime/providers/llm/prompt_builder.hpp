#pragma once

#include <string>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::llm_provider {

// Build the LLM prompt from IR type information
class PromptBuilder {
  public:
    explicit PromptBuilder(const ir::Program &program);

    // Build the system prompt for the given capability (includes return type JSON schema)
    [[nodiscard]] std::string build_system_prompt(const std::string &capability_name) const;

    // Build the user prompt for the given argument values
    [[nodiscard]] std::string build_user_prompt(const std::string &capability_name,
                                                const std::vector<evaluator::Value> &args) const;

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;

    [[nodiscard]] const ir::CapabilityDecl *find_capability(const std::string &name) const;
    [[nodiscard]] const ir::StructDecl *find_struct(const std::string &name) const;
    [[nodiscard]] const ir::EnumDecl *find_enum(const std::string &name) const;

    // Generate a JSON schema description for a type
    [[nodiscard]] std::string describe_type_schema(const std::string &type_name) const;
    // Convert a Value into a human-readable string
    [[nodiscard]] std::string value_to_string(const evaluator::Value &val) const;
};

} // namespace ahfl::llm_provider
