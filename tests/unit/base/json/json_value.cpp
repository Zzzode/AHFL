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

    auto exponent = parse_json("1.5e2");
    if (!exponent)
        return false;
    if ((*exponent)->kind != Kind::Float)
        return false;
    if ((*exponent)->float_val < 149.9 || (*exponent)->float_val > 150.1)
        return false;

    // An integer above INT64_MAX but within uint64 range now parses as an
    // exact Int (readable losslessly via as_uint()), not a lossy Float — this
    // lets a size_t identity with the high bit set round-trip through JSON.
    auto oversized_int = parse_json("9223372036854775808");
    if (!oversized_int)
        return false;
    if ((*oversized_int)->kind != Kind::Int)
        return false;
    const auto oversized_uint = (*oversized_int)->as_uint();
    if (!oversized_uint.has_value() || *oversized_uint != 9223372036854775808ULL)
        return false;

    // A magnitude beyond uint64 range still falls back to Float.
    auto huge = parse_json("99999999999999999999999");
    if (!huge)
        return false;
    if ((*huge)->kind != Kind::Float)
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

// The parser is recursive descent, so it is an untrusted-input admission
// boundary: a whitespace-cheap document of deeply nested brackets must fail
// closed rather than overflow the native stack. `kMaxJsonNestingDepth` bounds the
// descent. One level UNDER the bound parses; one level OVER is a parse error.
bool test_nesting_depth_bound() {
    const std::string at_bound(kMaxJsonNestingDepth, '[');
    const std::string closed = at_bound + std::string(kMaxJsonNestingDepth, ']');
    auto ok = parse_json(closed);
    if (!ok)
        return false;

    const std::string over(kMaxJsonNestingDepth + 1, '[');
    auto rejected = parse_json(over);
    if (rejected)
        return false;

    // The historical crash shape: a large bracket-only document (used to
    // segfault the process) is now a clean parse error.
    const std::string huge(100000, '[');
    if (parse_json(huge))
        return false;

    // An OBJECT-nested document is bounded the same way.
    const std::string nested_objects = std::string(kMaxJsonNestingDepth + 1, '{') +
                                       std::string(kMaxJsonNestingDepth + 1, '}');
    if (parse_json(nested_objects))
        return false;

    return true;
}

// RFC 0026 C2b P0-10: numeric provenance classification, accessor behavior,
// serializer round-trip, and invalid-combination fail-closed.
bool test_provenance_classification() {
    auto s = parse_json("42");
    if (!s || (*s)->kind != Kind::Int ||
        (*s)->number_provenance != NumberProvenance::SignedInteger) {
        return false;
    }
    auto neg = parse_json("-100");
    if (!neg || (*neg)->number_provenance != NumberProvenance::SignedInteger) {
        return false;
    }
    auto u = parse_json("18446744073709551615"); // UINT64_MAX
    if (!u || (*u)->kind != Kind::Int ||
        (*u)->number_provenance != NumberProvenance::UnsignedInteger ||
        (*u)->uint_val != 18446744073709551615ULL) {
        return false;
    }
    auto big = parse_json("99999999999999999999999"); // > uint64
    if (!big || (*big)->kind != Kind::Float ||
        (*big)->number_provenance != NumberProvenance::IntegerFallback) {
        return false;
    }
    auto bigneg = parse_json("-99999999999999999999999");
    if (!bigneg || (*bigneg)->number_provenance != NumberProvenance::IntegerFallback) {
        return false;
    }
    auto f = parse_json("1.5");
    if (!f || (*f)->kind != Kind::Float ||
        (*f)->number_provenance != NumberProvenance::FloatSyntax) {
        return false;
    }
    auto fe = parse_json("1e3");
    if (!fe || (*fe)->number_provenance != NumberProvenance::FloatSyntax) {
        return false;
    }
    auto str = parse_json("\"hi\"");
    if (!str || (*str)->number_provenance != NumberProvenance::NotNumeric) {
        return false;
    }
    return true;
}

bool test_provenance_accessors() {
    auto s = parse_json("42");
    if (!s || (*s)->as_int() != 42 || (*s)->as_uint() != 42ULL || (*s)->as_float() != 42.0) {
        return false;
    }
    auto neg = parse_json("-5");
    if (!neg || (*neg)->as_int() != -5 || (*neg)->as_uint().has_value()) {
        return false;
    }
    auto u = parse_json("18446744073709551615");
    if (!u || (*u)->as_int().has_value() || (*u)->as_uint() != 18446744073709551615ULL ||
        !(*u)->as_float().has_value()) {
        return false;
    }
    auto big = parse_json("99999999999999999999999");
    if (!big || (*big)->as_int().has_value() || (*big)->as_uint().has_value() ||
        !(*big)->as_float().has_value()) {
        return false;
    }
    auto f = parse_json("1.5");
    if (!f || (*f)->as_int().has_value() || (*f)->as_uint().has_value() ||
        (*f)->as_float() != 1.5) {
        return false;
    }
    return true;
}

bool test_high_uint_roundtrip() {
    const std::string src = "18446744073709551615";
    auto v = parse_json(src);
    if (!v) {
        return false;
    }
    auto text = serialize_json(**v);
    if (text != src) { // exact decimal, NOT a negative
        return false;
    }
    auto again = parse_json(text);
    if (!again || (*again)->number_provenance != NumberProvenance::UnsignedInteger ||
        (*again)->as_uint() != 18446744073709551615ULL) {
        return false;
    }
    return true;
}

bool test_floatsyntax_serializer_unchanged() {
    // A FloatSyntax integral value still serializes as bare `1` (A does NOT add
    // `.0`; v1/v2 snapshot integral-Float byte baseline preserved).
    auto v = parse_json("1.0");
    if (!v || (*v)->number_provenance != NumberProvenance::FloatSyntax) {
        return false;
    }
    return serialize_json(**v) == "1";
}

bool test_invalid_combo_serialize_failclosed() {
    JsonValue bad_string;
    bad_string.kind = Kind::String;
    bad_string.string_val = "x";
    bad_string.number_provenance = NumberProvenance::SignedInteger; // invalid
    if (!serialize_json(bad_string).empty()) {
        return false;
    }
    JsonValue bad_int;
    bad_int.kind = Kind::Int;
    bad_int.int_val = 1;
    bad_int.number_provenance = NumberProvenance::FloatSyntax; // invalid on Int
    if (!serialize_json(bad_int).empty()) {
        return false;
    }
    JsonValue ok_string; // NotNumeric default still serializes fine
    ok_string.kind = Kind::String;
    ok_string.string_val = "x";
    return serialize_json(ok_string) == "\"x\"";
}

bool test_invalid_combo_accessors_nullopt() {
    // Numeric accessors return nullopt for invalid (Kind, provenance) pairings.
    JsonValue float_signed; // Float carrying SignedInteger provenance
    float_signed.kind = Kind::Float;
    float_signed.float_val = 1.5;
    float_signed.number_provenance = NumberProvenance::SignedInteger;
    if (float_signed.as_int().has_value() || float_signed.as_uint().has_value() ||
        float_signed.as_float().has_value()) {
        return false;
    }
    JsonValue int_notnumeric; // Int left at the default NotNumeric provenance
    int_notnumeric.kind = Kind::Int;
    int_notnumeric.int_val = 7;
    if (int_notnumeric.as_int().has_value() || int_notnumeric.as_uint().has_value() ||
        int_notnumeric.as_float().has_value()) {
        return false;
    }
    return true;
}

bool test_generic_serialize_loses_float_provenance() {
    // A DOCUMENTED boundary of plan A: FloatSyntax/IntegerFallback provenance is
    // NOT preserved across a generic serialize -> parse round-trip. The generic
    // Float branch and legal-producer bytes are unchanged (the only byte the
    // serializer now changes is a formerly-corrupt high-uint Int, which emits its
    // correct unsigned decimal instead of a negative). Trust paths therefore decode
    // the DOM directly instead of serialize->reparse. A FloatSyntax `1.0` serializes
    // to `1` and re-parses as SignedInteger; an IntegerFallback serializes via the
    // float formatter and re-parses as FloatSyntax.
    auto f = parse_json("1.0");
    if (!f || (*f)->number_provenance != NumberProvenance::FloatSyntax) {
        return false;
    }
    auto reparsed_f = parse_json(serialize_json(**f)); // "1"
    if (!reparsed_f ||
        (*reparsed_f)->number_provenance != NumberProvenance::SignedInteger) {
        return false;
    }
    auto big = parse_json("99999999999999999999999");
    if (!big || (*big)->number_provenance != NumberProvenance::IntegerFallback) {
        return false;
    }
    auto reparsed_big = parse_json(serialize_json(**big));
    if (!reparsed_big ||
        (*reparsed_big)->number_provenance != NumberProvenance::FloatSyntax) {
        return false;
    }
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
    run(test_nesting_depth_bound, "test_nesting_depth_bound");
    run(test_provenance_classification, "test_provenance_classification");
    run(test_provenance_accessors, "test_provenance_accessors");
    run(test_high_uint_roundtrip, "test_high_uint_roundtrip");
    run(test_floatsyntax_serializer_unchanged, "test_floatsyntax_serializer_unchanged");
    run(test_invalid_combo_serialize_failclosed, "test_invalid_combo_serialize_failclosed");
    run(test_invalid_combo_accessors_nullopt, "test_invalid_combo_accessors_nullopt");
    run(test_generic_serialize_loses_float_provenance,
        "test_generic_serialize_loses_float_provenance");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
