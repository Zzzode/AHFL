#pragma once

#include <optional>
#include <string>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::llm_provider {

struct ResponseParseResult {
    std::optional<evaluator::Value> value;
    std::string error_message;

    [[nodiscard]] bool success() const noexcept {
        return value.has_value();
    }
};

// Parse an LLM's JSON response into an AHFL Value
class ResponseParser {
  public:
    explicit ResponseParser(const ir::Program &program);

    // Parse a JSON string into a Value of the specified type
    [[nodiscard]] std::optional<evaluator::Value> parse(const std::string &json_str,
                                                        const std::string &expected_type) const;
    [[nodiscard]] ResponseParseResult
    parse_with_diagnostics(const std::string &json_str, const std::string &expected_type) const;

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;

    [[nodiscard]] const ir::StructDecl *find_struct(const std::string &name) const;
    [[nodiscard]] const ir::EnumDecl *find_enum(const std::string &name) const;

    // Parse a JSON object into a struct Value
    [[nodiscard]] ResponseParseResult parse_struct(const std::string &json_obj,
                                                   const ir::StructDecl &decl) const;

    // Extract the value of a given key from a JSON object
    [[nodiscard]] std::string extract_json_value(const std::string &json_str,
                                                 const std::string &key) const;

    // Parse a primitive type value
    [[nodiscard]] ResponseParseResult parse_primitive(const std::string &value_str,
                                                      const std::string &type_name) const;
};

} // namespace ahfl::llm_provider
