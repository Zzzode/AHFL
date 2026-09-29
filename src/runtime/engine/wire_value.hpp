#pragma once

#include "runtime/value/value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::runtime {

// Serialize ONE value to a wire-JSON frame. Returns nullopt if the value is
// not wire-encodable (a closure, including one nested in a composite): the
// capability transports fail closed on nullopt rather than emitting malformed
// JSON. A bare StructValue is the arity-1 frame as-is.
[[nodiscard]] std::optional<std::string>
serialize_value_for_wire_json(const runtime::Value &value);

// Serialize a capability argument vector to its wire frame. Returns nullopt if
// any argument is not wire-encodable; callers turn that into a capability
// error, never into `{"value":}` or `{}`.
[[nodiscard]] std::optional<std::string>
serialize_args_for_wire_json(const std::vector<runtime::Value> &args);

[[nodiscard]] std::optional<runtime::Value> parse_value_from_wire_json(std::string_view json);

} // namespace ahfl::runtime
