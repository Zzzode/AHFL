#include "ahfl/evaluator/value_json.hpp"
#include "ahfl/evaluator/value.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace ahfl::evaluator;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

// ============================================================================
// Serialization tests
// ============================================================================

void test_serialize_none() {
    auto json = value_to_json(make_none());
    check(json == "null", "serialize.none");
}

void test_serialize_bool() {
    check(value_to_json(make_bool(true)) == "true", "serialize.bool_true");
    check(value_to_json(make_bool(false)) == "false", "serialize.bool_false");
}

void test_serialize_int() {
    check(value_to_json(make_int(0)) == "0", "serialize.int_zero");
    check(value_to_json(make_int(42)) == "42", "serialize.int_42");
    check(value_to_json(make_int(-100)) == "-100", "serialize.int_neg");
}

void test_serialize_float() {
    auto json = value_to_json(make_float(3.14));
    // Should contain a decimal point
    check(json.find('.') != std::string::npos || json.find('e') != std::string::npos,
          "serialize.float_has_decimal");
}

void test_serialize_string() {
    check(value_to_json(make_string("hello")) == "\"hello\"", "serialize.string_simple");
    check(value_to_json(make_string("a\"b")) == "\"a\\\"b\"", "serialize.string_escaped_quote");
    check(value_to_json(make_string("a\\b")) == "\"a\\\\b\"", "serialize.string_escaped_backslash");
    check(value_to_json(make_string("line\nnew")) == "\"line\\nnew\"",
          "serialize.string_escaped_newline");
}

void test_serialize_decimal() {
    check(value_to_json(make_decimal("1.23456789")) == "\"1.23456789\"", "serialize.decimal");
}

void test_serialize_duration() {
    check(value_to_json(make_duration("5s")) == "\"5s\"", "serialize.duration");
    check(value_to_json(make_duration("100ms")) == "\"100ms\"", "serialize.duration_ms");
}

void test_serialize_enum() {
    auto json = value_to_json(make_enum("Color", "Red"));
    check(json.find("\"_enum\":\"Color\"") != std::string::npos, "serialize.enum_name");
    check(json.find("\"_variant\":\"Red\"") != std::string::npos, "serialize.enum_variant");
}

void test_serialize_list() {
    std::vector<Value> items;
    items.push_back(make_int(1));
    items.push_back(make_int(2));
    items.push_back(make_int(3));
    auto list = make_list(std::move(items));
    check(value_to_json(list) == "[1,2,3]", "serialize.list_ints");

    std::vector<Value> empty_items;
    auto empty_list = make_list(std::move(empty_items));
    check(value_to_json(empty_list) == "[]", "serialize.list_empty");
}

void test_serialize_struct() {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("name", make_string("test"));
    fields.emplace("age", make_int(30));
    auto sv = make_struct("Person", std::move(fields));
    auto json = value_to_json(sv);
    check(json.find("\"_type\":\"Person\"") != std::string::npos, "serialize.struct_type");
    check(json.find("\"name\":\"test\"") != std::string::npos, "serialize.struct_field_name");
    check(json.find("\"age\":30") != std::string::npos, "serialize.struct_field_age");
}

void test_serialize_optional() {
    auto some = make_optional_some(make_int(99));
    check(value_to_json(some) == "99", "serialize.optional_some");

    auto none = make_optional_none();
    check(value_to_json(none) == "null", "serialize.optional_none");
}

// ============================================================================
// Deserialization tests
// ============================================================================

void test_parse_null() {
    auto result = value_from_json("null");
    check(result.has_value(), "parse.null_has_value");
    if (result) {
        check(std::holds_alternative<NoneValue>(result->node), "parse.null_is_none");
    }
}

void test_parse_bool() {
    auto t = value_from_json("true");
    check(t.has_value() && std::get_if<BoolValue>(&t->node) &&
              std::get_if<BoolValue>(&t->node)->value == true,
          "parse.bool_true");

    auto f = value_from_json("false");
    check(f.has_value() && std::get_if<BoolValue>(&f->node) &&
              std::get_if<BoolValue>(&f->node)->value == false,
          "parse.bool_false");
}

void test_parse_int() {
    auto result = value_from_json("42");
    check(result.has_value(), "parse.int_has_value");
    if (result) {
        auto *iv = std::get_if<IntValue>(&result->node);
        check(iv != nullptr && iv->value == 42, "parse.int_value_42");
    }

    auto neg = value_from_json("-100");
    check(neg.has_value() && std::get_if<IntValue>(&neg->node) &&
              std::get_if<IntValue>(&neg->node)->value == -100,
          "parse.int_negative");
}

void test_parse_float() {
    auto result = value_from_json("3.14");
    check(result.has_value(), "parse.float_has_value");
    if (result) {
        auto *fv = std::get_if<FloatValue>(&result->node);
        check(fv != nullptr && fv->value > 3.13 && fv->value < 3.15, "parse.float_value");
    }

    auto sci = value_from_json("1.5e2");
    check(sci.has_value() && std::get_if<FloatValue>(&sci->node) &&
              std::get_if<FloatValue>(&sci->node)->value == 150.0,
          "parse.float_scientific");
}

void test_parse_string() {
    auto result = value_from_json("\"hello world\"");
    check(result.has_value(), "parse.string_has_value");
    if (result) {
        auto *sv = std::get_if<StringValue>(&result->node);
        check(sv != nullptr && sv->value == "hello world", "parse.string_value");
    }

    auto escaped = value_from_json("\"a\\\"b\\\\c\"");
    if (escaped) {
        auto *sv = std::get_if<StringValue>(&escaped->node);
        check(sv != nullptr && sv->value == "a\"b\\c", "parse.string_escaped");
    }
}

void test_parse_array() {
    auto result = value_from_json("[1, 2, 3]");
    check(result.has_value(), "parse.array_has_value");
    if (result) {
        auto *lv = std::get_if<ListValue>(&result->node);
        check(lv != nullptr && lv->items.size() == 3, "parse.array_size_3");
        if (lv && lv->items.size() == 3) {
            auto *first = std::get_if<IntValue>(&lv->items[0]->node);
            check(first != nullptr && first->value == 1, "parse.array_first_is_1");
        }
    }

    auto empty = value_from_json("[]");
    check(empty.has_value(), "parse.array_empty_has_value");
    if (empty) {
        auto *lv = std::get_if<ListValue>(&empty->node);
        check(lv != nullptr && lv->items.empty(), "parse.array_empty");
    }
}

void test_parse_enum() {
    auto result = value_from_json(R"({"_enum":"Status","_variant":"Active"})");
    check(result.has_value(), "parse.enum_has_value");
    if (result) {
        auto *ev = std::get_if<EnumValue>(&result->node);
        check(ev != nullptr, "parse.enum_is_enum");
        if (ev) {
            check(ev->enum_name == "Status", "parse.enum_name");
            check(ev->variant == "Active", "parse.enum_variant");
        }
    }
}

void test_parse_struct() {
    auto result = value_from_json(R"({"_type":"Point","x":10,"y":20})");
    check(result.has_value(), "parse.struct_has_value");
    if (result) {
        auto *sv = std::get_if<StructValue>(&result->node);
        check(sv != nullptr, "parse.struct_is_struct");
        if (sv) {
            check(sv->type_name == "Point", "parse.struct_type_name");
            check(sv->fields.size() == 2, "parse.struct_field_count");
            auto it_x = sv->fields.find("x");
            if (it_x != sv->fields.end() && it_x->second) {
                auto *xv = std::get_if<IntValue>(&it_x->second->node);
                check(xv != nullptr && xv->value == 10, "parse.struct_field_x");
            }
        }
    }
}

// ============================================================================
// Roundtrip tests
// ============================================================================

void test_roundtrip_int() {
    auto original = make_int(12345);
    auto json = value_to_json(original);
    auto parsed = value_from_json(json);
    check(parsed.has_value(), "roundtrip.int_parsed");
    if (parsed) {
        auto *iv = std::get_if<IntValue>(&parsed->node);
        check(iv != nullptr && iv->value == 12345, "roundtrip.int_value");
    }
}

void test_roundtrip_struct() {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("msg", make_string("hello"));
    fields.emplace("count", make_int(7));
    auto original = make_struct("Record", std::move(fields));
    auto json = value_to_json(original);
    auto parsed = value_from_json(json);
    check(parsed.has_value(), "roundtrip.struct_parsed");
    if (parsed) {
        auto *sv = std::get_if<StructValue>(&parsed->node);
        check(sv != nullptr && sv->type_name == "Record", "roundtrip.struct_type");
        if (sv) {
            check(sv->fields.size() == 2, "roundtrip.struct_fields");
        }
    }
}

void test_roundtrip_nested_list() {
    std::vector<Value> inner_items;
    inner_items.push_back(make_int(1));
    inner_items.push_back(make_int(2));
    auto inner = make_list(std::move(inner_items));

    std::vector<Value> outer_items;
    outer_items.push_back(std::move(inner));
    outer_items.push_back(make_string("end"));
    auto outer = make_list(std::move(outer_items));

    auto json = value_to_json(outer);
    auto parsed = value_from_json(json);
    check(parsed.has_value(), "roundtrip.nested_list_parsed");
    if (parsed) {
        auto *lv = std::get_if<ListValue>(&parsed->node);
        check(lv != nullptr && lv->items.size() == 2, "roundtrip.nested_list_size");
    }
}

// ============================================================================
// Error cases
// ============================================================================

void test_parse_errors() {
    check(!value_from_json("").has_value(), "error.empty");
    check(!value_from_json("{").has_value(), "error.unclosed_object");
    check(!value_from_json("[1,").has_value(), "error.unclosed_array");
    check(!value_from_json("undefined").has_value(), "error.undefined");
    check(!value_from_json("123abc").has_value(), "error.trailing_garbage");
}

} // anonymous namespace

int main() {
    test_serialize_none();
    test_serialize_bool();
    test_serialize_int();
    test_serialize_float();
    test_serialize_string();
    test_serialize_decimal();
    test_serialize_duration();
    test_serialize_enum();
    test_serialize_list();
    test_serialize_struct();
    test_serialize_optional();

    test_parse_null();
    test_parse_bool();
    test_parse_int();
    test_parse_float();
    test_parse_string();
    test_parse_array();
    test_parse_enum();
    test_parse_struct();

    test_roundtrip_int();
    test_roundtrip_struct();
    test_roundtrip_nested_list();

    test_parse_errors();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
