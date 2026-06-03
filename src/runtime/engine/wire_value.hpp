#pragma once

#include "runtime/evaluator/value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime {

[[nodiscard]] std::string serialize_value_for_wire_json(const evaluator::Value &value);

[[nodiscard]] std::string serialize_args_for_wire_json(const std::vector<evaluator::Value> &args);

[[nodiscard]] std::optional<evaluator::Value> parse_value_from_wire_json(std::string_view json);

} // namespace ahfl::runtime
