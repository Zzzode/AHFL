#include "runtime/engine/wire_value.hpp"

#include "runtime/value/value_json.hpp"

#include <sstream>
#include <variant>

namespace ahfl::runtime {

std::optional<std::string> serialize_value_for_wire_json(const runtime::Value &value) {
    return runtime::try_value_to_json(value);
}

std::optional<std::string>
serialize_args_for_wire_json(const std::vector<runtime::Value> &args) {
    if (args.empty()) {
        return std::string("{}");
    }
    if (args.size() == 1) {
        if (std::holds_alternative<runtime::StructValue>(args[0].node)) {
            return serialize_value_for_wire_json(args[0]);
        }
        auto inner = serialize_value_for_wire_json(args[0]);
        if (!inner.has_value()) {
            return std::nullopt;
        }
        return "{\"value\":" + *inner + "}";
    }
    std::ostringstream out;
    out << "{\"args\":[";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        auto item = serialize_value_for_wire_json(args[i]);
        if (!item.has_value()) {
            return std::nullopt;
        }
        out << *item;
    }
    out << "]}";
    return out.str();
}

std::optional<runtime::Value> parse_value_from_wire_json(std::string_view json) {
    return runtime::value_from_json(json);
}

std::optional<ParsedWireArgs>
parse_args_from_wire_json(std::string_view json,
                          const ir::core::CoreWireSchemaTable &wire,
                          const std::vector<ir::core::CoreWireSchemaNodeId> &param_ids) {
    auto parsed = json::parse_json(json);
    if (!parsed.has_value() || !*parsed) {
        return std::nullopt;
    }
    ParsedWireArgs result;
    result.root = std::move(*parsed);
    const json::JsonValue &root = *result.root;

    const std::size_t arity = param_ids.size();

    if (arity == 0) {
        // Arity-0 accepts ONLY "{}": an empty object.
        if (!root.is_object() || !root.object_fields.empty()) {
            return std::nullopt;
        }
        return result;
    }

    if (arity == 1) {
        const auto *param_node =
            (param_ids[0].value < wire.nodes.size()) ? &wire.nodes[param_ids[0].value]
                                                     : nullptr;
        if (param_node == nullptr) {
            return std::nullopt;
        }
        const bool is_struct =
            std::holds_alternative<ir::core::CoreWireSchemaStruct>(param_node->shape);
        if (is_struct) {
            // Bare struct: the root IS the arg.
            result.args.push_back(&root);
            return result;
        }
        // {"value":...}: extract the field.
        const json::JsonValue *field = root.get("value");
        if (field == nullptr) {
            return std::nullopt;
        }
        result.args.push_back(field);
        return result;
    }

    // arity >= 2: {"args":[...]}
    const json::JsonValue *args_field = root.get("args");
    if (args_field == nullptr || !args_field->is_array()) {
        return std::nullopt;
    }
    if (args_field->array_items.size() != arity) {
        return std::nullopt;
    }
    for (const auto &elem : args_field->array_items) {
        if (!elem) {
            return std::nullopt;
        }
        result.args.push_back(elem.get());
    }
    return result;
}

} // namespace ahfl::runtime
