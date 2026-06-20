#pragma once

#include "ahfl/base/support/diagnostics.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::json {
struct JsonValue;
} // namespace ahfl::json

namespace ahfl {

/// Schema version for diagnostic report JSON serialization.
inline constexpr std::string_view kDiagnosticReportSchemaVersion = "AHFL_DIAGNOSTIC_REPORT_V1";

// ---------------------------------------------------------------------------
// Diagnostic <-> JSON
// ---------------------------------------------------------------------------

/// Serialize a single Diagnostic to a JSON value.
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_diagnostic_json(const Diagnostic &diagnostic);

/// Deserialize a single Diagnostic from a JSON value.
/// Returns std::nullopt if the JSON structure is invalid.
[[nodiscard]] std::optional<Diagnostic> deserialize_diagnostic_json(const json::JsonValue &value);

// ---------------------------------------------------------------------------
// DiagnosticReport <-> JSON
// ---------------------------------------------------------------------------

/// Serialize a DiagnosticReport to a JSON string.
/// The output includes the schema version and a summary block.
[[nodiscard]] std::string serialize_diagnostic_report_json(const DiagnosticReport &report);

/// Deserialize a DiagnosticReport from a JSON string.
/// Returns std::nullopt if the JSON structure is invalid or the schema
/// version does not match.
[[nodiscard]] std::optional<DiagnosticReport>
deserialize_diagnostic_report_json(std::string_view json_text);

/// Low-level: serialize a DiagnosticReport to a JsonValue tree.
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_diagnostic_report_value(const DiagnosticReport &report);

/// Low-level: deserialize a DiagnosticReport from a JsonValue tree.
[[nodiscard]] std::optional<DiagnosticReport>
deserialize_diagnostic_report_value(const json::JsonValue &value);

} // namespace ahfl
