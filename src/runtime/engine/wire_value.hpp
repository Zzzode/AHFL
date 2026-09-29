#pragma once

#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/json/json_value.hpp"
#include "runtime/value/value.hpp"

#include <memory>
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

// The parsed wire-JSON argument envelope: `root` owns the parsed DOM; `args`
// are non-owning pointers into `root` (one per capability parameter). The
// caller MUST keep `root` alive while using `args`.
struct ParsedWireArgs {
    std::unique_ptr<json::JsonValue> root;
    std::vector<const json::JsonValue *> args;
};

// Parse the wire-JSON argument envelope (the SSOT inverse of
// serialize_args_for_wire_json). The envelope format is:
//   0 args -> "{}" (and ONLY "{}"; any other JSON is rejected)
//   1 Struct -> bare struct JSON
//   1 non-Struct -> {"value":...}
//   2+ -> {"args":[...]}
// The arity and param wire shapes come from the wire-schema capability record.
// Returns nullopt on any parse failure or shape mismatch.
[[nodiscard]] std::optional<ParsedWireArgs>
parse_args_from_wire_json(std::string_view json,
                          const ir::core::CoreWireSchemaTable &wire,
                          const std::vector<ir::core::CoreWireSchemaNodeId> &param_ids);

} // namespace ahfl::runtime
