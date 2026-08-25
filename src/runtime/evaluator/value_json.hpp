#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/evaluator/value.hpp"

namespace ahfl::evaluator {

// Serialize Value to compact JSON string.
[[nodiscard]] std::string value_to_json(const Value &v);

// RFC 0022 (durable resume): a canonical, stable hash of a capability call's
// resolved argument list, used as a memo integrity cross-check on replay. Built
// on the deterministic `value_to_json` serialization (FieldMap is name-sorted,
// floats go through `format_double`), so the same logical arguments always hash
// identically across process restarts / cold starts. FNV-1a over the JSON of
// each argument, length-delimited so [a,b] and [ab] never collide.
[[nodiscard]] std::uint64_t hash_values(const std::vector<Value> &values);

// Write Value as compact JSON to stream.
void write_value_json(const Value &v, std::ostream &out);

// Parse JSON string into Value. Returns nullopt on parse failure.
[[nodiscard]] std::optional<Value> value_from_json(std::string_view json);

} // namespace ahfl::evaluator
