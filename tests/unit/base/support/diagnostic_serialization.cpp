#include "ahfl/base/support/diagnostic_serialization.hpp"
#include "ahfl/base/support/diagnostics.hpp"

#include "base/json/json_value.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
// DiagnosticReport type tests
// ============================================================================

void test_report_defaults() {
    ahfl::DiagnosticReport report;
    check(report.result_id.empty(), "report.default.result_id_empty");
    check(report.kind == "full", "report.default.kind_full");
    check(report.diagnostics.empty(), "report.default.diagnostics_empty");
    check(!report.has_error(), "report.default.no_error");
    check(report.error_count() == 0, "report.default.error_count_0");
    check(report.warning_count() == 0, "report.default.warning_count_0");
    check(report.note_count() == 0, "report.default.note_count_0");
}

void test_report_counts() {
    ahfl::DiagnosticReport report;
    report.result_id = "test-123";
    report.kind = "incremental";

    ahfl::Diagnostic err;
    err.severity = ahfl::DiagnosticSeverity::Error;
    err.message = "an error";

    ahfl::Diagnostic warn;
    warn.severity = ahfl::DiagnosticSeverity::Warning;
    warn.message = "a warning";

    ahfl::Diagnostic note;
    note.severity = ahfl::DiagnosticSeverity::Note;
    note.message = "a note";

    report.diagnostics.push_back(err);
    report.diagnostics.push_back(warn);
    report.diagnostics.push_back(note);
    report.diagnostics.push_back(err); // second error

    check(report.result_id == "test-123", "report.counts.result_id");
    check(report.kind == "incremental", "report.counts.kind");
    check(report.has_error(), "report.counts.has_error");
    check(report.error_count() == 2, "report.counts.error_count");
    check(report.warning_count() == 1, "report.counts.warning_count");
    check(report.note_count() == 1, "report.counts.note_count");
    check(report.diagnostics.size() == 4, "report.counts.total");
}

void test_report_from_bag() {
    ahfl::DiagnosticBag bag;
    bag.error().message("first error").emit();
    bag.warning().message("a warning").emit();
    bag.note().message("a note").emit();
    bag.error().message("second error").emit();

    auto report = ahfl::DiagnosticReport::from_bag(bag, "bag-1", "snapshot");

    check(report.result_id == "bag-1", "report.from_bag.result_id");
    check(report.kind == "snapshot", "report.from_bag.kind");
    check(report.error_count() == 2, "report.from_bag.error_count");
    check(report.warning_count() == 1, "report.from_bag.warning_count");
    check(report.note_count() == 1, "report.from_bag.note_count");
    check(report.diagnostics.size() == 4, "report.from_bag.total");
}

void test_report_append() {
    ahfl::DiagnosticReport r1;
    r1.result_id = "r1";
    ahfl::Diagnostic err;
    err.severity = ahfl::DiagnosticSeverity::Error;
    err.message = "err1";
    r1.diagnostics.push_back(err);

    ahfl::DiagnosticReport r2;
    r2.result_id = "r2";
    ahfl::Diagnostic warn;
    warn.severity = ahfl::DiagnosticSeverity::Warning;
    warn.message = "warn1";
    r2.diagnostics.push_back(warn);

    r1.append(r2);

    check(r1.result_id == "r1", "report.append.preserves_result_id");
    check(r1.diagnostics.size() == 2, "report.append.size");
    check(r1.error_count() == 1, "report.append.error_count");
    check(r1.warning_count() == 1, "report.append.warning_count");
}

// ============================================================================
// Diagnostic serialization tests
// ============================================================================

void test_serialize_minimal_diagnostic() {
    ahfl::Diagnostic diag;
    diag.severity = ahfl::DiagnosticSeverity::Error;
    diag.message = "hello world";

    auto json = ahfl::serialize_diagnostic_json(diag);
    check(json != nullptr, "diag_ser.minimal.not_null");
    check(json->is_object(), "diag_ser.minimal.is_object");

    auto *sev = json->get("severity");
    check(sev != nullptr && sev->as_string() == "error", "diag_ser.minimal.severity");

    auto *msg = json->get("message");
    check(msg != nullptr && msg->as_string() == "hello world", "diag_ser.minimal.message");
}

void test_serialize_full_diagnostic() {
    ahfl::Diagnostic diag;
    diag.severity = ahfl::DiagnosticSeverity::Warning;
    diag.message = "type mismatch";
    diag.code = "typecheck.TYPE_MISMATCH";
    diag.source_name = "test.ahfl";
    diag.range = ahfl::SourceRange{10, 25};
    diag.position = ahfl::SourcePosition{10, 2, 5};

    ahfl::Diagnostic::Related rel;
    rel.message = "declared here";
    rel.range = ahfl::SourceRange{5, 8};
    diag.related.push_back(rel);

    auto json = ahfl::serialize_diagnostic_json(diag);
    check(json != nullptr, "diag_ser.full.not_null");

    auto *code = json->get("code");
    check(code != nullptr && code->as_string() == "typecheck.TYPE_MISMATCH",
          "diag_ser.full.code");

    auto *source = json->get("source_name");
    check(source != nullptr && source->as_string() == "test.ahfl",
          "diag_ser.full.source_name");

    auto *range = json->get("range");
    check(range != nullptr && range->is_object(), "diag_ser.full.range_exists");
    auto *rbegin = range->get("begin");
    auto *rend = range->get("end");
    check(rbegin != nullptr && rbegin->as_int() == 10, "diag_ser.full.range_begin");
    check(rend != nullptr && rend->as_int() == 25, "diag_ser.full.range_end");

    auto *pos = json->get("position");
    check(pos != nullptr && pos->is_object(), "diag_ser.full.position_exists");
    auto *pline = pos->get("line");
    auto *pcol = pos->get("column");
    check(pline != nullptr && pline->as_int() == 2, "diag_ser.full.position_line");
    check(pcol != nullptr && pcol->as_int() == 5, "diag_ser.full.position_column");

    auto *related = json->get("related");
    check(related != nullptr && related->is_array(), "diag_ser.full.related_array");
    check(related->array_items.size() == 1, "diag_ser.full.related_count");
}

void test_diagnostic_roundtrip_basic() {
    ahfl::Diagnostic original;
    original.severity = ahfl::DiagnosticSeverity::Error;
    original.message = "test error";
    original.code = "parse.UNEXPECTED_TOKEN";

    auto json = ahfl::serialize_diagnostic_json(original);
    auto parsed = ahfl::deserialize_diagnostic_json(*json);

    check(parsed.has_value(), "diag_roundtrip.basic.parsed");
    check(parsed->severity == ahfl::DiagnosticSeverity::Error, "diag_roundtrip.basic.severity");
    check(parsed->message == "test error", "diag_roundtrip.basic.message");
    check(parsed->code == "parse.UNEXPECTED_TOKEN", "diag_roundtrip.basic.code");
}

void test_diagnostic_roundtrip_full() {
    ahfl::Diagnostic original;
    original.severity = ahfl::DiagnosticSeverity::Warning;
    original.message = "shadowed binding";
    original.code = "typecheck.SHADOWED_BINDING";
    original.source_name = "module.ahfl";
    original.range = ahfl::SourceRange{100, 115};
    original.position = ahfl::SourcePosition{100, 10, 3};

    ahfl::Diagnostic::Related r1;
    r1.message = "previous declaration";
    r1.range = ahfl::SourceRange{50, 60};
    original.related.push_back(r1);

    ahfl::Diagnostic::Related r2;
    r2.message = "another note";
    r2.range = std::nullopt;
    original.related.push_back(r2);

    auto json = ahfl::serialize_diagnostic_json(original);
    auto parsed = ahfl::deserialize_diagnostic_json(*json);

    check(parsed.has_value(), "diag_roundtrip.full.parsed");
    check(parsed->severity == ahfl::DiagnosticSeverity::Warning,
          "diag_roundtrip.full.severity");
    check(parsed->message == "shadowed binding", "diag_roundtrip.full.message");
    check(parsed->code == "typecheck.SHADOWED_BINDING", "diag_roundtrip.full.code");
    check(parsed->source_name == "module.ahfl", "diag_roundtrip.full.source_name");
    check(parsed->range.has_value(), "diag_roundtrip.full.has_range");
    check(parsed->range->begin_offset == 100, "diag_roundtrip.full.range_begin");
    check(parsed->range->end_offset == 115, "diag_roundtrip.full.range_end");
    check(parsed->position.has_value(), "diag_roundtrip.full.has_position");
    check(parsed->position->offset == 100, "diag_roundtrip.full.position_offset");
    check(parsed->position->line == 10, "diag_roundtrip.full.position_line");
    check(parsed->position->column == 3, "diag_roundtrip.full.position_column");
    check(parsed->related.size() == 2, "diag_roundtrip.full.related_count");
    check(parsed->related[0].message == "previous declaration",
          "diag_roundtrip.full.related_0_message");
    check(parsed->related[0].range.has_value(), "diag_roundtrip.full.related_0_has_range");
    check(parsed->related[1].message == "another note",
          "diag_roundtrip.full.related_1_message");
    check(!parsed->related[1].range.has_value(),
          "diag_roundtrip.full.related_1_no_range");
}

void test_diagnostic_deserialize_invalid() {
    auto null_val = ahfl::json::JsonValue::make_null();
    auto parsed = ahfl::deserialize_diagnostic_json(*null_val);
    check(!parsed.has_value(), "diag_deser.invalid_null");

    auto obj = ahfl::json::JsonValue::make_object();
    obj->set("severity", ahfl::json::JsonValue::make_string("invalid-severity"));
    obj->set("message", ahfl::json::JsonValue::make_string("msg"));
    obj->set("related", ahfl::json::JsonValue::make_array());
    auto parsed2 = ahfl::deserialize_diagnostic_json(*obj);
    check(!parsed2.has_value(), "diag_deser.invalid_severity");

    auto obj2 = ahfl::json::JsonValue::make_object();
    obj2->set("severity", ahfl::json::JsonValue::make_string("error"));
    // missing message and related
    auto parsed3 = ahfl::deserialize_diagnostic_json(*obj2);
    check(!parsed3.has_value(), "diag_deser.missing_fields");
}

// ============================================================================
// DiagnosticReport serialization tests
// ============================================================================

void test_report_serialize_basic() {
    ahfl::DiagnosticReport report;
    report.result_id = "report-abc";
    report.kind = "full";

    ahfl::Diagnostic diag;
    diag.severity = ahfl::DiagnosticSeverity::Error;
    diag.message = "bad stuff";
    report.diagnostics.push_back(diag);

    std::string json_str = ahfl::serialize_diagnostic_report_json(report);
    check(!json_str.empty(), "report_ser.basic.non_empty");
    check(json_str.find("report-abc") != std::string::npos,
          "report_ser.basic.contains_result_id");
    check(json_str.find(ahfl::kDiagnosticReportSchemaVersion) != std::string::npos,
          "report_ser.basic.contains_schema_version");
}

void test_report_roundtrip_empty() {
    ahfl::DiagnosticReport original;
    original.result_id = "empty-report";
    original.kind = "snapshot";

    std::string json_str = ahfl::serialize_diagnostic_report_json(original);
    auto parsed = ahfl::deserialize_diagnostic_report_json(json_str);

    check(parsed.has_value(), "report_roundtrip.empty.parsed");
    check(parsed->result_id == "empty-report", "report_roundtrip.empty.result_id");
    check(parsed->kind == "snapshot", "report_roundtrip.empty.kind");
    check(parsed->diagnostics.empty(), "report_roundtrip.empty.no_diagnostics");
}

void test_report_roundtrip_multiple() {
    ahfl::DiagnosticReport original;
    original.result_id = "multi-42";
    original.kind = "incremental";

    ahfl::Diagnostic d1;
    d1.severity = ahfl::DiagnosticSeverity::Error;
    d1.message = "error one";
    d1.code = "typecheck.TYPE_MISMATCH";
    original.diagnostics.push_back(d1);

    ahfl::Diagnostic d2;
    d2.severity = ahfl::DiagnosticSeverity::Warning;
    d2.message = "warning two";
    d2.source_name = "file.ahfl";
    d2.range = ahfl::SourceRange{5, 10};
    original.diagnostics.push_back(d2);

    ahfl::Diagnostic d3;
    d3.severity = ahfl::DiagnosticSeverity::Note;
    d3.message = "just a note";
    original.diagnostics.push_back(d3);

    std::string json_str = ahfl::serialize_diagnostic_report_json(original);
    auto parsed = ahfl::deserialize_diagnostic_report_json(json_str);

    check(parsed.has_value(), "report_roundtrip.multi.parsed");
    check(parsed->result_id == "multi-42", "report_roundtrip.multi.result_id");
    check(parsed->kind == "incremental", "report_roundtrip.multi.kind");
    check(parsed->diagnostics.size() == 3, "report_roundtrip.multi.count");
    check(parsed->error_count() == 1, "report_roundtrip.multi.error_count");
    check(parsed->warning_count() == 1, "report_roundtrip.multi.warning_count");
    check(parsed->note_count() == 1, "report_roundtrip.multi.note_count");
    check(parsed->diagnostics[0].message == "error one",
          "report_roundtrip.multi.d0_message");
    check(parsed->diagnostics[1].source_name == "file.ahfl",
          "report_roundtrip.multi.d1_source");
    check(parsed->diagnostics[1].range->begin_offset == 5,
          "report_roundtrip.multi.d1_range_begin");
}

void test_report_deserialize_schema_mismatch() {
    auto obj = ahfl::json::JsonValue::make_object();
    obj->set("schema_version", ahfl::json::JsonValue::make_string("INVALID_SCHEMA_V999"));
    obj->set("result_id", ahfl::json::JsonValue::make_string("x"));
    obj->set("kind", ahfl::json::JsonValue::make_string("full"));
    obj->set("diagnostics", ahfl::json::JsonValue::make_array());

    auto parsed = ahfl::deserialize_diagnostic_report_value(*obj);
    check(!parsed.has_value(), "report_deser.schema_mismatch");
}

void test_report_deserialize_invalid_json() {
    auto parsed = ahfl::deserialize_diagnostic_report_json("not valid json at all");
    check(!parsed.has_value(), "report_deser.invalid_json");
}

void test_report_value_roundtrip() {
    ahfl::DiagnosticReport original;
    original.result_id = "val-r1";
    original.kind = "snapshot";

    ahfl::Diagnostic diag;
    diag.severity = ahfl::DiagnosticSeverity::Note;
    diag.message = "note via value";
    original.diagnostics.push_back(diag);

    auto value = ahfl::serialize_diagnostic_report_value(original);
    auto parsed = ahfl::deserialize_diagnostic_report_value(*value);

    check(parsed.has_value(), "report_val_roundtrip.parsed");
    check(parsed->result_id == "val-r1", "report_val_roundtrip.result_id");
    check(parsed->diagnostics.size() == 1, "report_val_roundtrip.count");
    check(parsed->diagnostics[0].message == "note via value",
          "report_val_roundtrip.message");
}

void test_report_has_summary() {
    ahfl::DiagnosticReport report;
    report.result_id = "summary-test";

    ahfl::Diagnostic err;
    err.severity = ahfl::DiagnosticSeverity::Error;
    err.message = "e1";
    report.diagnostics.push_back(err);
    report.diagnostics.push_back(err);

    ahfl::Diagnostic warn;
    warn.severity = ahfl::DiagnosticSeverity::Warning;
    warn.message = "w1";
    report.diagnostics.push_back(warn);

    auto value = ahfl::serialize_diagnostic_report_value(report);
    auto *summary = value->get("summary");
    check(summary != nullptr && summary->is_object(), "report_summary.exists");

    auto *err_count = summary->get("error_count");
    auto *warn_count = summary->get("warning_count");
    auto *total_count = summary->get("total_count");

    check(err_count != nullptr && err_count->as_int() == 2, "report_summary.error_count");
    check(warn_count != nullptr && warn_count->as_int() == 1,
          "report_summary.warning_count");
    check(total_count != nullptr && total_count->as_int() == 3,
          "report_summary.total_count");
}

} // namespace

int main() {
    // DiagnosticReport type
    test_report_defaults();
    test_report_counts();
    test_report_from_bag();
    test_report_append();

    // Diagnostic serialization
    test_serialize_minimal_diagnostic();
    test_serialize_full_diagnostic();
    test_diagnostic_roundtrip_basic();
    test_diagnostic_roundtrip_full();
    test_diagnostic_deserialize_invalid();

    // DiagnosticReport serialization
    test_report_serialize_basic();
    test_report_roundtrip_empty();
    test_report_roundtrip_multiple();
    test_report_deserialize_schema_mismatch();
    test_report_deserialize_invalid_json();
    test_report_value_roundtrip();
    test_report_has_summary();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
