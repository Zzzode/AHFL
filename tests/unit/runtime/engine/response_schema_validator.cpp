#include "runtime/engine/response_schema_validator.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

using namespace ahfl::runtime;
using namespace ahfl::evaluator;
using namespace ahfl::ir;

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

TypeRef make_type(TypeRefKind kind) {
    TypeRef t;
    t.kind = kind;
    return t;
}

TypeRef make_named_struct(const std::string &name) {
    TypeRef t;
    t.kind = TypeRefKind::Struct;
    t.display_name = name;
    return t;
}

TypeRef make_named_enum(const std::string &name) {
    TypeRef t;
    t.kind = TypeRefKind::Enum;
    t.display_name = name;
    return t;
}

TypeRef make_canonical_type(TypeRefKind kind, const std::string &canonical_name) {
    TypeRef t;
    t.kind = kind;
    t.canonical_name = canonical_name;
    return t;
}

TypeRef make_optional_of(TypeRefKind inner_kind) {
    TypeRef t;
    t.kind = TypeRefKind::Optional;
    t.first = std::make_unique<TypeRef>();
    t.first->kind = inner_kind;
    return t;
}

FieldDecl make_field(const std::string &name, TypeRef type_ref) {
    FieldDecl field;
    field.name = name;
    field.type_ref = std::move(type_ref);
    return field;
}

Program build_schema_program() {
    Program program;

    EnumDecl priority;
    priority.name = "Priority";
    priority.variants = {"Low", "High"};
    program.declarations.push_back(std::move(priority));

    StructDecl request;
    request.name = "Request";
    request.fields.push_back(make_field("message", make_type(TypeRefKind::String)));
    request.fields.push_back(make_field("priority", make_named_enum("Priority")));
    program.declarations.push_back(std::move(request));

    return program;
}

std::unordered_map<std::string, Value> make_request_fields(Value message,
                                                           std::string priority_variant) {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("message", std::move(message));
    fields.emplace("priority", make_enum("Priority", std::move(priority_variant)));
    return fields;
}

std::unordered_map<std::string, Value> make_missing_priority_fields() {
    std::unordered_map<std::string, Value> fields;
    fields.emplace("message", make_string("hello"));
    return fields;
}

std::unordered_map<std::string, Value> make_extra_request_fields() {
    auto fields = make_request_fields(make_string("hello"), "Low");
    fields.emplace("unexpected", make_string("extra"));
    return fields;
}

// ============================================================================
// Basic type validation
// ============================================================================

void test_any_accepts_anything() {
    auto schema = make_type(TypeRefKind::Any);
    check(validate_value_against_schema(make_int(42), schema).valid, "any.int");
    check(validate_value_against_schema(make_string("hi"), schema).valid, "any.string");
    check(validate_value_against_schema(make_none(), schema).valid, "any.none");
}

void test_bool_validation() {
    auto schema = make_type(TypeRefKind::Bool);
    check(validate_value_against_schema(make_bool(true), schema).valid, "bool.valid");
    check(!validate_value_against_schema(make_int(1), schema).valid, "bool.reject_int");
    check(!validate_value_against_schema(make_string("true"), schema).valid, "bool.reject_string");
}

void test_int_validation() {
    auto schema = make_type(TypeRefKind::Int);
    check(validate_value_against_schema(make_int(100), schema).valid, "int.valid");
    check(!validate_value_against_schema(make_float(1.5), schema).valid, "int.reject_float");
    check(!validate_value_against_schema(make_bool(true), schema).valid, "int.reject_bool");
}

void test_float_validation() {
    auto schema = make_type(TypeRefKind::Float);
    check(validate_value_against_schema(make_float(3.14), schema).valid, "float.valid_float");
    check(validate_value_against_schema(make_int(42), schema).valid, "float.accept_int");
    check(!validate_value_against_schema(make_string("3.14"), schema).valid, "float.reject_string");
}

void test_string_validation() {
    auto schema = make_type(TypeRefKind::String);
    check(validate_value_against_schema(make_string("hello"), schema).valid, "string.valid");
    check(!validate_value_against_schema(make_int(42), schema).valid, "string.reject_int");
}

void test_unit_validation() {
    auto schema = make_type(TypeRefKind::Unit);
    check(validate_value_against_schema(make_none(), schema).valid, "unit.none_valid");
    check(!validate_value_against_schema(make_int(0), schema).valid, "unit.reject_int");
}

// ============================================================================
// Struct/Enum validation
// ============================================================================

void test_struct_validation() {
    auto schema = make_named_struct("MyStruct");
    auto good = make_struct("MyStruct", {});
    auto bad_name = make_struct("OtherStruct", {});

    check(validate_value_against_schema(good, schema).valid, "struct.name_match");
    check(!validate_value_against_schema(bad_name, schema).valid, "struct.name_mismatch");
    check(!validate_value_against_schema(make_int(1), schema).valid, "struct.reject_int");
}

void test_program_struct_validation_rejects_missing_and_extra_fields() {
    const auto program = build_schema_program();
    const auto schema = make_named_struct("Request");

    auto good = make_struct("Request", make_request_fields(make_string("hello"), "Low"));
    check(validate_value_against_schema(good, schema, program).valid, "struct.program.valid");

    auto missing = make_struct("Request", make_missing_priority_fields());
    const auto missing_result = validate_value_against_schema(missing, schema, program);
    check(!missing_result.valid, "struct.program.reject_missing_field");
    check(missing_result.error.find("missing field 'priority'") != std::string::npos,
          "struct.program.missing_field_message");

    auto extra = make_struct("Request", make_extra_request_fields());
    const auto extra_result = validate_value_against_schema(extra, schema, program);
    check(!extra_result.valid, "struct.program.reject_extra_field");
    check(extra_result.error.find("unexpected field 'unexpected'") != std::string::npos,
          "struct.program.extra_field_message");
}

void test_program_struct_validation_rejects_nested_field_type_and_enum_variant() {
    const auto program = build_schema_program();
    const auto schema = make_named_struct("Request");

    auto wrong_field_type = make_struct("Request", make_request_fields(make_int(7), "Low"));
    const auto field_result = validate_value_against_schema(wrong_field_type, schema, program);
    check(!field_result.valid, "struct.program.reject_field_type");
    check(field_result.error.find("message: expected String but got Int") != std::string::npos,
          "struct.program.field_type_message");

    auto wrong_variant =
        make_struct("Request", make_request_fields(make_string("hello"), "Medium"));
    const auto variant_result = validate_value_against_schema(wrong_variant, schema, program);
    check(!variant_result.valid, "struct.program.reject_enum_variant");
    check(variant_result.error.find("unknown enum variant 'Medium'") != std::string::npos,
          "struct.program.enum_variant_message");
}

void test_enum_validation() {
    auto schema = make_named_enum("Status");
    auto good = make_enum("Status", "Active");
    auto bad_name = make_enum("Other", "Active");

    check(validate_value_against_schema(good, schema).valid, "enum.name_match");
    check(!validate_value_against_schema(bad_name, schema).valid, "enum.name_mismatch");
    check(!validate_value_against_schema(make_string("Active"), schema).valid,
          "enum.reject_string");
}

void test_canonical_name_validation() {
    auto struct_schema = make_canonical_type(TypeRefKind::Struct, "pkg::Request");
    check(validate_value_against_schema(make_struct("pkg::Request", {}), struct_schema).valid,
          "struct.canonical_name_match");
    check(!validate_value_against_schema(make_struct("Other", {}), struct_schema).valid,
          "struct.canonical_name_mismatch");

    auto enum_schema = make_canonical_type(TypeRefKind::Enum, "pkg::Status");
    check(validate_value_against_schema(make_enum("pkg::Status", "Active"), enum_schema).valid,
          "enum.canonical_name_match");
    check(!validate_value_against_schema(make_enum("Other", "Active"), enum_schema).valid,
          "enum.canonical_name_mismatch");
}

// ============================================================================
// Optional validation
// ============================================================================

void test_optional_validation() {
    auto schema = make_optional_of(TypeRefKind::Int);
    check(validate_value_against_schema(make_none(), schema).valid, "optional.none_valid");
    check(validate_value_against_schema(make_int(42), schema).valid, "optional.inner_valid");
    check(!validate_value_against_schema(make_string("x"), schema).valid, "optional.inner_invalid");
}

// ============================================================================
// List/Duration/Decimal
// ============================================================================

void test_list_validation() {
    auto schema = make_type(TypeRefKind::List);
    auto good = make_list({});
    check(validate_value_against_schema(good, schema).valid, "list.valid");
    check(!validate_value_against_schema(make_int(1), schema).valid, "list.reject_int");
}

void test_duration_validation() {
    auto schema = make_type(TypeRefKind::Duration);
    auto good = make_duration("5s");
    check(validate_value_against_schema(good, schema).valid, "duration.valid");
    check(!validate_value_against_schema(make_string("5s"), schema).valid,
          "duration.reject_string");
}

void test_decimal_validation() {
    auto schema = make_type(TypeRefKind::Decimal);
    auto good = make_decimal("3.14");
    check(validate_value_against_schema(good, schema).valid, "decimal.valid_decimal");
    check(validate_value_against_schema(make_float(3.14), schema).valid, "decimal.accept_float");
    check(validate_value_against_schema(make_int(3), schema).valid, "decimal.accept_int");
    check(!validate_value_against_schema(make_string("3"), schema).valid, "decimal.reject_string");
}

// ============================================================================
// Error message quality
// ============================================================================

void test_error_messages() {
    auto schema = make_type(TypeRefKind::Bool);
    auto result = validate_value_against_schema(make_int(1), schema);
    check(!result.valid, "error_msg.is_invalid");
    check(result.error.find("Bool") != std::string::npos, "error_msg.mentions_expected");
    check(result.error.find("Int") != std::string::npos, "error_msg.mentions_actual");
}

} // namespace

int main() {
    test_any_accepts_anything();
    test_bool_validation();
    test_int_validation();
    test_float_validation();
    test_string_validation();
    test_unit_validation();
    test_struct_validation();
    test_program_struct_validation_rejects_missing_and_extra_fields();
    test_program_struct_validation_rejects_nested_field_type_and_enum_variant();
    test_enum_validation();
    test_canonical_name_validation();
    test_optional_validation();
    test_list_validation();
    test_duration_validation();
    test_decimal_validation();
    test_error_messages();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
