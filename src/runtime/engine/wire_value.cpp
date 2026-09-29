#include "runtime/engine/wire_value.hpp"

#include "runtime/value/value_json.hpp"

#include <sstream>
#include <variant>

namespace ahfl::runtime {

std::string serialize_value_for_wire_json(const runtime::Value &value) {
    return runtime::value_to_json(value);
}

std::string serialize_args_for_wire_json(const std::vector<runtime::Value> &args) {
    if (args.empty()) {
        return "{}";
    }
    if (args.size() == 1) {
        if (std::holds_alternative<runtime::StructValue>(args[0].node)) {
            return serialize_value_for_wire_json(args[0]);
        }
        return "{\"value\":" + serialize_value_for_wire_json(args[0]) + "}";
    }
    std::ostringstream out;
    out << "{\"args\":[";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << serialize_value_for_wire_json(args[i]);
    }
    out << "]}";
    return out.str();
}

std::optional<runtime::Value> parse_value_from_wire_json(std::string_view json) {
    return runtime::value_from_json(json);
}

} // namespace ahfl::runtime
