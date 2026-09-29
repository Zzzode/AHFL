#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_value.hpp"
#include "runtime/value/value.hpp"

namespace ahfl::runtime {

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

// Convert an already-parsed JSON DOM node into a Value directly (RFC 0026 C2b
// P0-10). Trust-boundary callers (durable resume, CLI tool catalog) that already
// hold a subtree DOM MUST use this instead of serialize_json + value_from_json:
// re-serializing then re-parsing discards the numeric provenance the DOM carries,
// which would let an ambiguous number (a high-bit unsigned magnitude or an
// integer-token fallback) silently degrade instead of failing closed. This is the
// same schema-free decode as the string overload, minus the parse step.
[[nodiscard]] std::optional<Value> value_from_json(const ahfl::json::JsonValue &json_value);

} // namespace ahfl::runtime
