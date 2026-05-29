#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "runtime/evaluator/value.hpp"

namespace ahfl::evaluator {

// Serialize Value to compact JSON string.
[[nodiscard]] std::string value_to_json(const Value &v);

// Write Value as compact JSON to stream.
void write_value_json(const Value &v, std::ostream &out);

// Parse JSON string into Value. Returns nullopt on parse failure.
[[nodiscard]] std::optional<Value> value_from_json(std::string_view json);

} // namespace ahfl::evaluator
