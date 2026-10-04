// Unit tests for the capability wire-JSON frame serializer
// (src/runtime/engine/wire_value.cpp) and the strict/observation split in
// value_json. These moved out of the gRPC transcoding tests when the
// transcoding pass-through wrappers were deleted: the frame format is owned by
// wire_value, not by a transport.

#include "runtime/engine/wire_value.hpp"
#include "runtime/value/value_json.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace ahfl::runtime;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

void test_serialize_args_empty() {
    std::vector<Value> args;
    auto result = serialize_args_for_wire_json(args);
    check(result.has_value() && *result == "{}", "serialize_args.empty");
}

void test_serialize_args_single_struct() {
    Value v;
    StructValue sv;
    sv.type_name = "Request";
    sv.fields.set("name", std::make_unique<Value>(Value{StringValue{"alice"}}));
    v.node = std::move(sv);

    std::vector<Value> args;
    args.push_back(std::move(v));

    auto result = serialize_args_for_wire_json(args);
    check(result.has_value(), "serialize_args.single_struct.encoded");
    check(result.has_value() && result->find("\"name\"") != std::string::npos,
          "serialize_args.single_struct.has_field");
    check(result.has_value() && result->find("\"alice\"") != std::string::npos,
          "serialize_args.single_struct.has_value");
    check(result.has_value() && result->find("\"value\"") == std::string::npos,
          "serialize_args.single_struct.not_wrapped");
}

void test_serialize_args_single_non_struct() {
    std::vector<Value> args;
    args.push_back(make_int(42));

    auto result = serialize_args_for_wire_json(args);
    check(result.has_value() && *result == R"({"value":42})",
          "serialize_args.single_int");
}

void test_serialize_args_multiple() {
    std::vector<Value> args;
    args.push_back(make_int(1));
    args.push_back(make_string("hello"));

    auto result = serialize_args_for_wire_json(args);
    check(result.has_value() && *result == R"({"args":[1,"hello"]})",
          "serialize_args.multi");
}

void test_serialize_scalar_values() {
    auto string_result = serialize_value_for_wire_json(make_string("hello world"));
    check(string_result.has_value() && *string_result == "\"hello world\"",
          "serialize_value.string");

    auto int_result = serialize_value_for_wire_json(make_int(99));
    check(int_result.has_value() && *int_result == "99", "serialize_value.int");

    auto bool_result = serialize_value_for_wire_json(make_bool(true));
    check(bool_result.has_value() && *bool_result == "true", "serialize_value.bool");
}

// The strict hash is deterministic for normal wire values and length-delimits
// arguments (the original FNV contract is unchanged on encodable input).
void test_hash_stable() {
    std::vector<Value> one;
    one.emplace_back(IntValue{42});
    std::vector<Value> one_again;
    one_again.emplace_back(IntValue{42});
    std::vector<Value> two;
    two.emplace_back(IntValue{42});
    two.emplace_back(IntValue{7});

    const auto h1 = hash_values(one);
    const auto h2 = hash_values(one_again);
    const auto h3 = hash_values(two);
    check(h1.has_value() && h2.has_value() && h3.has_value(),
          "hash_values.encodable_present");
    check(h1 == h2, "hash_values.stable");
    check(h1 != h3, "hash_values.distinguishes_args");
}

// ==== P2-5: parse_args_from_wire_json (envelope SSOT) ====

// Build a wire schema with: node 0 = Int, node 1 = String, node 2 = Struct{name: String}.
ahfl::ir::core::CoreWireSchemaTable make_test_schema() {
    ahfl::ir::core::CoreWireSchemaTable wire;
    wire.nodes.push_back(
        ahfl::ir::core::CoreWireSchemaNode{ahfl::ir::core::CoreWireSchemaInt{}});
    wire.nodes.push_back(
        ahfl::ir::core::CoreWireSchemaNode{ahfl::ir::core::CoreWireSchemaString{}});
    ahfl::ir::core::CoreWireSchemaStruct struct_shape;
    struct_shape.wire_name = "Request";
    struct_shape.fields.push_back(ahfl::ir::core::CoreWireSchemaField{
        .wire_name = "name", .type = ahfl::ir::core::CoreWireSchemaNodeId{1}});
    wire.nodes.push_back(ahfl::ir::core::CoreWireSchemaNode{std::move(struct_shape)});
    return wire;
}

void test_parse_args_arity_zero() {
    auto wire = make_test_schema();
    // Arity-0 accepts ONLY "{}".
    auto ok = parse_args_from_wire_json("{}", wire, {});
    check(ok.has_value() && ok->args.empty(), "parse_args.arity0.accepts_empty_object");
    // Any other JSON is rejected.
    check(!parse_args_from_wire_json("null", wire, {}).has_value(),
          "parse_args.arity0.rejects_null");
    check(!parse_args_from_wire_json("42", wire, {}).has_value(),
          "parse_args.arity0.rejects_int");
    check(!parse_args_from_wire_json("[]", wire, {}).has_value(),
          "parse_args.arity0.rejects_array");
    check(!parse_args_from_wire_json("{\"value\":1}", wire, {}).has_value(),
          "parse_args.arity0.rejects_value_wrapper");
}

void test_parse_args_arity_one_non_struct() {
    auto wire = make_test_schema();
    const std::vector<ahfl::ir::core::CoreWireSchemaNodeId> params = {
        ahfl::ir::core::CoreWireSchemaNodeId{0}};
    // Non-Struct param: {"value":...}
    auto ok = parse_args_from_wire_json("{\"value\":42}", wire, params);
    check(ok.has_value() && ok->args.size() == 1, "parse_args.arity1_int.parses");
    if (ok.has_value() && ok->args.size() == 1) {
        auto val = ok->args[0]->as_int();
        check(val.has_value() && *val == 42, "parse_args.arity1_int.value_is_42");
    }
    // Missing "value" field is rejected.
    check(!parse_args_from_wire_json("{}", wire, params).has_value(),
          "parse_args.arity1_int.rejects_empty_object");
    // Bare int (not wrapped) is rejected.
    check(!parse_args_from_wire_json("42", wire, params).has_value(),
          "parse_args.arity1_int.rejects_bare_int");
}

void test_parse_args_arity_one_struct() {
    auto wire = make_test_schema();
    const std::vector<ahfl::ir::core::CoreWireSchemaNodeId> params = {
        ahfl::ir::core::CoreWireSchemaNodeId{2}};
    // Struct param: bare struct JSON.
    auto ok = parse_args_from_wire_json("{\"name\":\"alice\"}", wire, params);
    check(ok.has_value() && ok->args.size() == 1, "parse_args.arity1_struct.parses");
    if (ok.has_value() && ok->args.size() == 1) {
        const auto *field = ok->args[0]->get("name");
        check(field != nullptr, "parse_args.arity1_struct.has_name_field");
        if (field != nullptr) {
            auto name = field->as_string();
            check(name.has_value() && *name == "alice",
                  "parse_args.arity1_struct.name_is_alice");
        }
    }
    // A bare struct with a "value" field is still a valid bare struct for a
    // Struct param (the envelope parser accepts it; the schema-bound decoder
    // would reject the unknown field).
    auto value_field = parse_args_from_wire_json("{\"value\":{\"name\":\"alice\"}}", wire, params);
    check(value_field.has_value() && value_field->args.size() == 1,
          "parse_args.arity1_struct.bare_with_value_field");
}

void test_parse_args_invalid_json() {
    auto wire = make_test_schema();
    const std::vector<ahfl::ir::core::CoreWireSchemaNodeId> params = {
        ahfl::ir::core::CoreWireSchemaNodeId{0}};
    check(!parse_args_from_wire_json("{\"value\":", wire, params).has_value(),
          "parse_args.invalid_json.rejected");
    check(!parse_args_from_wire_json("", wire, params).has_value(),
          "parse_args.empty_json.rejected");
}

void test_parse_args_round_trip() {
    auto wire = make_test_schema();
    // Round-trip: serialize -> parse -> verify.
    std::vector<Value> args;
    args.push_back(make_int(42));
    auto serialized = serialize_args_for_wire_json(args);
    check(serialized.has_value() && *serialized == "{\"value\":42}",
          "parse_args.round_trip.serialized");
    if (serialized.has_value()) {
        const std::vector<ahfl::ir::core::CoreWireSchemaNodeId> params = {
            ahfl::ir::core::CoreWireSchemaNodeId{0}};
        auto parsed = parse_args_from_wire_json(*serialized, wire, params);
        check(parsed.has_value() && parsed->args.size() == 1,
              "parse_args.round_trip.parsed");
        if (parsed.has_value() && parsed->args.size() == 1) {
            auto val = parsed->args[0]->as_int();
            check(val.has_value() && *val == 42, "parse_args.round_trip.value_is_42");
        }
    }
}

} // namespace

int main() {
    test_serialize_args_empty();
    test_serialize_args_single_struct();
    test_serialize_args_single_non_struct();
    test_serialize_args_multiple();
    test_serialize_scalar_values();
    test_hash_stable();
    test_parse_args_arity_zero();
    test_parse_args_arity_one_non_struct();
    test_parse_args_arity_one_struct();
    test_parse_args_invalid_json();
    test_parse_args_round_trip();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
