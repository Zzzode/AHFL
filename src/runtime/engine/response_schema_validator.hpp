#pragma once

#include "ahfl/compiler/ir/types.hpp"
#include "runtime/evaluator/value.hpp"

#include <string>

namespace ahfl::runtime {

struct SchemaValidationResult {
    bool valid{true};
    std::string error;

    [[nodiscard]] static SchemaValidationResult ok() { return {true, {}}; }
    [[nodiscard]] static SchemaValidationResult fail(std::string msg) {
        return {false, std::move(msg)};
    }
};

[[nodiscard]] SchemaValidationResult
validate_value_against_schema(const evaluator::Value &value, const ir::TypeRef &expected);

} // namespace ahfl::runtime
