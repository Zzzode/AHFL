#include "ahfl/base/support/diagnostic_serialization.hpp"

#include "base/json/json_value.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl {

namespace {

using Json = json::JsonValue;

[[nodiscard]] std::unique_ptr<Json> j_int(std::size_t value) {
    return Json::make_int(static_cast<std::int64_t>(value));
}

[[nodiscard]] std::optional<std::string> read_optional_string(const Json &value,
                                                              std::string_view key) {
    const auto *field = value.get(key);
    if (field == nullptr || field->is_null()) {
        return std::nullopt;
    }
    auto str = field->as_string();
    if (!str.has_value()) {
        return std::nullopt;
    }
    return std::string(*str);
}

[[nodiscard]] std::optional<std::size_t> read_size_t(const Json &value, std::string_view key) {
    const auto *field = value.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto v = field->as_int();
    if (!v.has_value() || *v < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*v);
}

[[nodiscard]] std::unique_ptr<Json> serialize_source_range(SourceRange range) {
    auto obj = Json::make_object();
    obj->set("begin", j_int(range.begin_offset));
    obj->set("end", j_int(range.end_offset));
    return obj;
}

[[nodiscard]] std::optional<SourceRange> deserialize_source_range(const Json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    auto begin = read_size_t(value, "begin");
    auto end = read_size_t(value, "end");
    if (!begin.has_value() || !end.has_value()) {
        return std::nullopt;
    }
    return SourceRange{
        .begin_offset = *begin,
        .end_offset = *end,
    };
}

[[nodiscard]] std::unique_ptr<Json> serialize_source_position(const SourcePosition &pos) {
    auto obj = Json::make_object();
    obj->set("offset", j_int(pos.offset));
    obj->set("line", j_int(pos.line));
    obj->set("column", j_int(pos.column));
    return obj;
}

[[nodiscard]] std::optional<SourcePosition> deserialize_source_position(const Json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    auto offset = read_size_t(value, "offset");
    auto line = read_size_t(value, "line");
    auto column = read_size_t(value, "column");
    if (!offset.has_value() || !line.has_value() || !column.has_value()) {
        return std::nullopt;
    }
    return SourcePosition{
        .offset = *offset,
        .line = *line,
        .column = *column,
    };
}

[[nodiscard]] std::unique_ptr<Json>
serialize_related(const std::vector<Diagnostic::Related> &related) {
    auto arr = Json::make_array();
    for (const auto &r : related) {
        auto obj = Json::make_object();
        obj->set("message", Json::make_string(r.message));
        if (r.range.has_value()) {
            obj->set("range", serialize_source_range(*r.range));
        } else {
            obj->set("range", Json::make_null());
        }
        arr->push(std::move(obj));
    }
    return arr;
}

[[nodiscard]] std::optional<std::vector<Diagnostic::Related>>
deserialize_related(const Json &value) {
    if (!value.is_array()) {
        return std::nullopt;
    }
    std::vector<Diagnostic::Related> result;
    for (const auto &item_ptr : value.array_items) {
        if (!item_ptr->is_object()) {
            return std::nullopt;
        }
        const auto &item = *item_ptr;

        auto message = item.get("message");
        if (message == nullptr) {
            return std::nullopt;
        }
        auto message_str = message->as_string();
        if (!message_str.has_value()) {
            return std::nullopt;
        }

        std::optional<SourceRange> range;
        auto *range_field = item.get("range");
        if (range_field != nullptr && !range_field->is_null()) {
            auto r = deserialize_source_range(*range_field);
            if (!r.has_value()) {
                return std::nullopt;
            }
            range = *r;
        }

        result.push_back(Diagnostic::Related{
            .message = std::string(*message_str),
            .range = range,
        });
    }
    return result;
}

[[nodiscard]] std::optional<DiagnosticSeverity> parse_severity(std::string_view s) {
    if (s == "error") {
        return DiagnosticSeverity::Error;
    }
    if (s == "warning") {
        return DiagnosticSeverity::Warning;
    }
    if (s == "note") {
        return DiagnosticSeverity::Note;
    }
    return std::nullopt;
}

} // namespace

// ============================================================================
// Diagnostic serialization
// ============================================================================

std::unique_ptr<json::JsonValue> serialize_diagnostic_json(const Diagnostic &diagnostic) {
    auto obj = Json::make_object();

    obj->set("severity", Json::make_string(std::string(to_string(diagnostic.severity))));
    obj->set("message", Json::make_string(diagnostic.message));

    if (diagnostic.code.has_value()) {
        obj->set("code", Json::make_string(*diagnostic.code));
    } else {
        obj->set("code", Json::make_null());
    }

    if (diagnostic.source_name.has_value()) {
        obj->set("source_name", Json::make_string(*diagnostic.source_name));
    } else {
        obj->set("source_name", Json::make_null());
    }

    if (diagnostic.range.has_value()) {
        obj->set("range", serialize_source_range(*diagnostic.range));
    } else {
        obj->set("range", Json::make_null());
    }

    if (diagnostic.position.has_value()) {
        obj->set("position", serialize_source_position(*diagnostic.position));
    } else {
        obj->set("position", Json::make_null());
    }

    obj->set("related", serialize_related(diagnostic.related));

    return obj;
}

std::optional<Diagnostic> deserialize_diagnostic_json(const json::JsonValue &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    // severity (required)
    auto *severity_field = value.get("severity");
    if (severity_field == nullptr) {
        return std::nullopt;
    }
    auto severity_str = severity_field->as_string();
    if (!severity_str.has_value()) {
        return std::nullopt;
    }
    auto severity = parse_severity(*severity_str);
    if (!severity.has_value()) {
        return std::nullopt;
    }

    // message (required)
    auto *message_field = value.get("message");
    if (message_field == nullptr) {
        return std::nullopt;
    }
    auto message_str = message_field->as_string();
    if (!message_str.has_value()) {
        return std::nullopt;
    }

    Diagnostic diag;
    diag.severity = *severity;
    diag.message = std::string(*message_str);

    // code (optional)
    diag.code = read_optional_string(value, "code");

    // source_name (optional)
    diag.source_name = read_optional_string(value, "source_name");

    // range (optional)
    auto *range_field = value.get("range");
    if (range_field != nullptr && !range_field->is_null()) {
        auto range = deserialize_source_range(*range_field);
        if (!range.has_value()) {
            return std::nullopt;
        }
        diag.range = *range;
    }

    // position (optional)
    auto *position_field = value.get("position");
    if (position_field != nullptr && !position_field->is_null()) {
        auto pos = deserialize_source_position(*position_field);
        if (!pos.has_value()) {
            return std::nullopt;
        }
        diag.position = *pos;
    }

    // related (required, may be empty)
    auto *related_field = value.get("related");
    if (related_field == nullptr) {
        return std::nullopt;
    }
    auto related = deserialize_related(*related_field);
    if (!related.has_value()) {
        return std::nullopt;
    }
    diag.related = std::move(*related);

    return diag;
}

// ============================================================================
// DiagnosticReport serialization
// ============================================================================

std::unique_ptr<json::JsonValue> serialize_diagnostic_report_value(const DiagnosticReport &report) {
    auto obj = Json::make_object();

    obj->set("schema_version", Json::make_string(std::string(kDiagnosticReportSchemaVersion)));
    obj->set("result_id", Json::make_string(report.result_id));
    obj->set("kind", Json::make_string(report.kind));

    // Summary block
    auto summary = Json::make_object();
    summary->set("error_count", j_int(report.error_count()));
    summary->set("warning_count", j_int(report.warning_count()));
    summary->set("note_count", j_int(report.note_count()));
    summary->set("total_count", j_int(report.diagnostics.size()));
    obj->set("summary", std::move(summary));

    // Diagnostics array
    auto diagnostics = Json::make_array();
    for (const auto &d : report.diagnostics) {
        diagnostics->push(serialize_diagnostic_json(d));
    }
    obj->set("diagnostics", std::move(diagnostics));

    return obj;
}

std::string serialize_diagnostic_report_json(const DiagnosticReport &report) {
    auto value = serialize_diagnostic_report_value(report);
    return json::serialize_json(*value);
}

std::optional<DiagnosticReport> deserialize_diagnostic_report_value(const json::JsonValue &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    // schema_version (required)
    auto *schema_field = value.get("schema_version");
    if (schema_field == nullptr) {
        return std::nullopt;
    }
    auto schema_str = schema_field->as_string();
    if (!schema_str.has_value() || *schema_str != kDiagnosticReportSchemaVersion) {
        return std::nullopt;
    }

    // result_id (required, may be empty)
    auto *result_id_field = value.get("result_id");
    if (result_id_field == nullptr) {
        return std::nullopt;
    }
    auto result_id_str = result_id_field->as_string();
    if (!result_id_str.has_value()) {
        return std::nullopt;
    }

    // kind (required)
    auto *kind_field = value.get("kind");
    if (kind_field == nullptr) {
        return std::nullopt;
    }
    auto kind_str = kind_field->as_string();
    if (!kind_str.has_value()) {
        return std::nullopt;
    }

    DiagnosticReport report;
    report.result_id = std::string(*result_id_str);
    report.kind = std::string(*kind_str);

    // diagnostics (required, may be empty)
    auto *diagnostics_field = value.get("diagnostics");
    if (diagnostics_field == nullptr || !diagnostics_field->is_array()) {
        return std::nullopt;
    }
    for (const auto &item_ptr : diagnostics_field->array_items) {
        auto diag = deserialize_diagnostic_json(*item_ptr);
        if (!diag.has_value()) {
            return std::nullopt;
        }
        report.diagnostics.push_back(std::move(*diag));
    }

    return report;
}

std::optional<DiagnosticReport> deserialize_diagnostic_report_json(std::string_view json_text) {
    auto parsed = json::parse_json(json_text);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return deserialize_diagnostic_report_value(**parsed);
}

} // namespace ahfl
