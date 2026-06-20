#include "base/json/json_value.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::json;

bool test_parse_null() {
    auto result = parse_json("null");
    if (!result)
        return false;
    return (*result)->kind == Kind::Null;
}

bool test_parse_bool() {
    auto t = parse_json("true");
    auto f = parse_json("false");
    if (!t || !f)
        return false;
    if ((*t)->kind != Kind::Bool || (*t)->bool_val != true)
        return false;
    if ((*f)->kind != Kind::Bool || (*f)->bool_val != false)
        return false;
    return true;
}

bool test_parse_int() {
    auto result = parse_json("42");
    if (!result)
        return false;
    if ((*result)->kind != Kind::Int)
        return false;
    if ((*result)->int_val != 42)
        return false;

    auto neg = parse_json("-100");
    if (!neg)
        return false;
    if ((*neg)->int_val != -100)
        return false;
    return true;
}

bool test_parse_float() {
    auto result = parse_json("3.14");
    if (!result)
        return false;
    if ((*result)->kind != Kind::Float)
        return false;
    if ((*result)->float_val < 3.13 || (*result)->float_val > 3.15)
        return false;
    return true;
}

bool test_parse_string() {
    auto result = parse_json("\"hello\"");
    if (!result)
        return false;
    if ((*result)->kind != Kind::String)
        return false;
    if ((*result)->string_val != "hello")
        return false;
    return true;
}

bool test_parse_array() {
    auto result = parse_json("[1, 2, 3]");
    if (!result)
        return false;
    if ((*result)->kind != Kind::Array)
        return false;
    if ((*result)->array_items.size() != 3)
        return false;
    if ((*result)->array_items[0]->int_val != 1)
        return false;
    if ((*result)->array_items[2]->int_val != 3)
        return false;
    return true;
}

bool test_parse_object() {
    auto result = parse_json(R"({"key":"value","num":10})");
    if (!result)
        return false;
    if ((*result)->kind != Kind::Object)
        return false;
    if ((*result)->object_fields.size() != 2)
        return false;
    return true;
}

bool test_duplicate_object_key_rejected() {
    auto result = parse_json(R"({"checksum":"good","checksum":"bad"})");
    return !result.has_value();
}

bool test_unescaped_control_character_rejected() {
    auto result = parse_json("\"\n\"");
    return !result.has_value();
}

bool test_source_spans() {
    auto result = parse_json(R"(  {"name":"Alice","scores":[1,2]}  )");
    if (!result)
        return false;

    const auto &root = **result;
    if (root.begin_offset != 2 || root.end_offset != 33)
        return false;

    const auto *name = root.get("name");
    if (name == nullptr || name->begin_offset != 10 || name->end_offset != 17)
        return false;

    const auto *scores = root.get("scores");
    if (scores == nullptr || scores->begin_offset != 27 || scores->end_offset != 32)
        return false;

    return true;
}

bool test_serialize_roundtrip() {
    // Null
    auto null_v = parse_json("null");
    if (!null_v || serialize_json(**null_v) != "null")
        return false;

    // Bool
    auto bool_v = parse_json("true");
    if (!bool_v || serialize_json(**bool_v) != "true")
        return false;

    // Int
    auto int_v = parse_json("42");
    if (!int_v || serialize_json(**int_v) != "42")
        return false;

    // String
    auto str_v = parse_json("\"hello\"");
    if (!str_v || serialize_json(**str_v) != "\"hello\"")
        return false;

    // Array
    auto arr_v = parse_json("[1,2,3]");
    if (!arr_v || serialize_json(**arr_v) != "[1,2,3]")
        return false;

    return true;
}

bool test_get_accessor() {
    auto result = parse_json(R"({"name":"Alice","age":30})");
    if (!result)
        return false;
    auto &obj = **result;

    auto *name_field = obj.get("name");
    if (!name_field)
        return false;
    if (name_field->kind != Kind::String || name_field->string_val != "Alice")
        return false;

    auto *age_field = obj.get("age");
    if (!age_field)
        return false;
    if (age_field->kind != Kind::Int || age_field->int_val != 30)
        return false;

    // Missing key returns nullptr
    if (obj.get("missing") != nullptr)
        return false;

    return true;
}

bool test_as_string() {
    auto result = parse_json("\"world\"");
    if (!result)
        return false;
    auto val = (*result)->as_string();
    if (!val || *val != "world")
        return false;

    // Non-string returns nullopt
    auto int_v = parse_json("42");
    if (!int_v)
        return false;
    if ((*int_v)->as_string().has_value())
        return false;

    return true;
}

bool test_as_int() {
    auto result = parse_json("99");
    if (!result)
        return false;
    auto val = (*result)->as_int();
    if (!val || *val != 99)
        return false;

    // Non-int returns nullopt
    auto str_v = parse_json("\"hello\"");
    if (!str_v)
        return false;
    if ((*str_v)->as_int().has_value())
        return false;

    return true;
}

bool test_as_bool() {
    auto result = parse_json("true");
    if (!result)
        return false;
    auto val = (*result)->as_bool();
    if (!val || *val != true)
        return false;

    auto fv = parse_json("false");
    if (!fv)
        return false;
    auto fval = (*fv)->as_bool();
    if (!fval || *fval != false)
        return false;

    // Non-bool returns nullopt
    auto int_v = parse_json("42");
    if (!int_v)
        return false;
    if ((*int_v)->as_bool().has_value())
        return false;

    return true;
}

bool test_unicode_escape() {
    // \u0041 is 'A'
    auto result = parse_json("\"\\u0041\"");
    if (!result)
        return false;
    if ((*result)->kind != Kind::String)
        return false;
    if ((*result)->string_val != "A")
        return false;

    // The escape \u4e16 yields the CJK character 'world'
    auto cn = parse_json("\"\\u4e16\"");
    if (!cn)
        return false;
    if ((*cn)->string_val.empty())
        return false;

    return true;
}

bool test_nested_structures() {
    auto input = R"({"users":[{"name":"Bob","active":true},{"name":"Eve","active":false}]})";
    auto result = parse_json(input);
    if (!result)
        return false;
    auto &root = **result;
    if (root.kind != Kind::Object)
        return false;

    auto *users = root.get("users");
    if (!users || users->kind != Kind::Array)
        return false;
    if (users->array_items.size() != 2)
        return false;

    auto &first = *users->array_items[0];
    if (first.kind != Kind::Object)
        return false;
    auto *name = first.get("name");
    if (!name || name->string_val != "Bob")
        return false;
    auto *active = first.get("active");
    if (!active || active->bool_val != true)
        return false;

    // Roundtrip: serialize then re-parse
    auto serialized = serialize_json(root);
    auto reparsed = parse_json(serialized);
    if (!reparsed)
        return false;
    auto *reparsed_users = (*reparsed)->get("users");
    if (!reparsed_users || reparsed_users->array_items.size() != 2)
        return false;

    return true;
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](bool (*fn)(), const char *name) {
        if (!fn()) {
            std::cerr << "FAIL: " << name << "\n";
            ++failures;
        }
    };

    run(test_parse_null, "test_parse_null");
    run(test_parse_bool, "test_parse_bool");
    run(test_parse_int, "test_parse_int");
    run(test_parse_float, "test_parse_float");
    run(test_parse_string, "test_parse_string");
    run(test_parse_array, "test_parse_array");
    run(test_parse_object, "test_parse_object");
    run(test_duplicate_object_key_rejected, "test_duplicate_object_key_rejected");
    run(test_unescaped_control_character_rejected, "test_unescaped_control_character_rejected");
    run(test_source_spans, "test_source_spans");
    run(test_serialize_roundtrip, "test_serialize_roundtrip");
    run(test_get_accessor, "test_get_accessor");
    run(test_as_string, "test_as_string");
    run(test_as_int, "test_as_int");
    run(test_as_bool, "test_as_bool");
    run(test_unicode_escape, "test_unicode_escape");
    run(test_nested_structures, "test_nested_structures");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
