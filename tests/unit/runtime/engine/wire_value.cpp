// Unit tests for the capability wire-JSON frame serializer
// (src/runtime/engine/wire_value.cpp) and the strict/observation split in
// value_json. These moved out of the gRPC transcoding tests when the
// transcoding pass-through wrappers were deleted: the frame format is owned by
// wire_value, not by a transport.
//
// The load-bearing cases are the closure rejection ones (WH-S review P0-1): a
// closure anywhere in an argument vector MUST fail closed at the wire
// boundary, while the observation projector keeps rendering it as valid JSON.

#include "runtime/engine/wire_value.hpp"
#include "runtime/evaluator/evaluator.hpp"
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

// An interpreter closure is interpreter state; building one needs the
// descriptor definition, which is why this test links the evaluator.
[[nodiscard]] Value make_closure_value() {
    ahfl::evaluator::EvalContext ctx;
    return make_interpreter_closure(std::make_shared<InterpreterClosure>(
        InterpreterClosure{.params = {"x"}, .body = nullptr, .captured_context = ctx}));
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

// P0-1: a closure is not a wire value at the top level.
void test_closure_rejected_top_level() {
    Value closure = make_closure_value();

    check(!try_value_to_json(closure).has_value(),
          "try_value_to_json.closure_rejected");

    std::vector<Value> args;
    args.push_back(clone_value(closure));
    check(!serialize_args_for_wire_json(args).has_value(),
          "serialize_args.closure_rejected");

    check(!hash_values(args).has_value(), "hash_values.closure_rejected");
}

// P0-1: a closure nested inside a composite must also reject — an arm that
// only checked the root would let `{"value":[<closure>]}` through.
void test_closure_rejected_nested() {
    std::vector<Value> list_items;
    list_items.push_back(make_int(1));
    list_items.push_back(make_closure_value());
    Value list = make_list(std::move(list_items));
    check(!try_value_to_json(list).has_value(),
          "try_value_to_json.closure_in_list_rejected");

    StructValue sv;
    sv.type_name = "Bag";
    sv.fields.set("f", std::make_unique<Value>(make_closure_value()));
    Value structured{std::move(sv)};
    check(!try_value_to_json(structured).has_value(),
          "try_value_to_json.closure_in_struct_rejected");

    std::vector<Value> args;
    args.push_back(clone_value(list));
    check(!serialize_args_for_wire_json(args).has_value(),
          "serialize_args.closure_nested_rejected");
}

// The observation projection must stay valid JSON and stay stable; two
// closures are distinct instances and still render the same opaque shape.
void test_closure_observation_spelling() {
    Value a = make_closure_value();
    Value b = make_closure_value();
    check(value_to_json(a) == R"({"_callable":"runtime"})",
          "value_to_json.closure_opaque_spelling");
    check(value_to_json(a) == value_to_json(b),
          "value_to_json.closure_opaque_uniform");

    std::vector<Value> list_items;
    list_items.push_back(make_int(1));
    list_items.push_back(clone_value(a));
    Value list = make_list(std::move(list_items));
    check(value_to_json(list) == R"([1,{"_callable":"runtime"}])",
          "value_to_json.closure_nested_opaque");
}

// The monotonic id is the closure's identity: clones compare equal, distinct
// evaluations compare different, and ordering is by id (stable), not address.
void test_closure_identity() {
    Value a = make_closure_value();
    Value a_clone = clone_value(a);
    Value b = make_closure_value();

    check(structurally_equal(a, a_clone), "closure.identity.clone_equal");
    check(!structurally_equal(a, b), "closure.identity.distinct_differ");
    check(compare_values(a, b) < 0, "closure.identity.ordered_by_id");
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

} // namespace

int main() {
    test_serialize_args_empty();
    test_serialize_args_single_struct();
    test_serialize_args_single_non_struct();
    test_serialize_args_multiple();
    test_serialize_scalar_values();
    test_closure_rejected_top_level();
    test_closure_rejected_nested();
    test_closure_observation_spelling();
    test_closure_identity();
    test_hash_stable();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
