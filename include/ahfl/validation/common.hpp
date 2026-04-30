#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/handoff/package.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::validation {

namespace code {
inline constexpr std::string_view kVersionMismatch = "AHFL.VAL.VERSION_MISMATCH";
inline constexpr std::string_view kRequiredFieldEmpty = "AHFL.VAL.REQUIRED_FIELD_EMPTY";
inline constexpr std::string_view kOptionalFieldEmpty = "AHFL.VAL.OPTIONAL_FIELD_EMPTY";
inline constexpr std::string_view kFailureSummaryEmptyMessage =
    "AHFL.VAL.FAILURE_SUMMARY_EMPTY_MESSAGE";
inline constexpr std::string_view kFailureSummaryEmptyNodeName =
    "AHFL.VAL.FAILURE_SUMMARY_EMPTY_NODE_NAME";
} // namespace code

[[nodiscard]] inline std::string scoped_label(std::string_view scope, std::string_view field) {
    if (scope.empty()) {
        return std::string(field);
    }

    return std::string(scope) + " " + std::string(field);
}

[[nodiscard]] inline std::string scoped_message(std::string_view scope, std::string_view message) {
    if (scope.empty()) {
        return std::string(message);
    }

    return std::string(scope) + " " + std::string(message);
}

inline void
emit_validation_error(DiagnosticBag &diagnostics, std::string_view code, std::string message) {
    diagnostics.error()
        .legacy_code(DiagnosticCategory::Validation, code)
        .message(std::move(message))
        .emit();
}

inline void require_version(DiagnosticBag &diagnostics,
                            std::string_view scope,
                            std::string_view field,
                            std::string_view actual,
                            std::string_view expected) {
    if (actual == expected) {
        return;
    }

    emit_validation_error(diagnostics,
                          code::kVersionMismatch,
                          scoped_label(scope, field) + " must be '" + std::string(expected) + "'");
}

inline void require_non_empty(DiagnosticBag &diagnostics,
                              std::string_view scope,
                              std::string_view field,
                              std::string_view value) {
    if (!value.empty()) {
        return;
    }

    emit_validation_error(
        diagnostics, code::kRequiredFieldEmpty, scoped_label(scope, field) + " must not be empty");
}

inline void require_optional_non_empty(DiagnosticBag &diagnostics,
                                       std::string_view scope,
                                       std::string_view field,
                                       const std::optional<std::string> &value,
                                       std::string_view suffix = "must not be empty when present") {
    if (!value.has_value() || !value->empty()) {
        return;
    }

    emit_validation_error(diagnostics,
                          code::kOptionalFieldEmpty,
                          scoped_label(scope, field) + " " + std::string(suffix));
}

[[nodiscard]] inline bool
package_identity_equals(const std::optional<handoff::PackageIdentity> &lhs,
                        const std::optional<handoff::PackageIdentity> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->format_version == rhs->format_version && lhs->name == rhs->name &&
           lhs->version == rhs->version;
}

[[nodiscard]] inline bool
failure_summary_equals(const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
                       const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->kind == rhs->kind && lhs->node_name == rhs->node_name &&
           lhs->message == rhs->message;
}

[[nodiscard]] inline bool is_prefix(const std::vector<std::string> &prefix,
                                    const std::vector<std::string> &full) {
    if (prefix.size() > full.size()) {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (prefix[index] != full[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline bool vector_equals(const std::vector<std::string> &lhs,
                                        const std::vector<std::string> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }

    return true;
}

inline void validate_package_identity(const handoff::PackageIdentity &identity,
                                      DiagnosticBag &diagnostics,
                                      std::string_view scope,
                                      std::string_view field = "source_package_identity") {
    const std::string base(field);
    require_version(diagnostics,
                    scope,
                    base + " format_version",
                    identity.format_version,
                    handoff::kFormatVersion);
    require_non_empty(diagnostics, scope, base + " name", identity.name);
    require_non_empty(diagnostics, scope, base + " version", identity.version);
}

inline void validate_failure_summary_field(const runtime_session::RuntimeFailureSummary &summary,
                                           DiagnosticBag &diagnostics,
                                           std::string_view scope,
                                           std::string_view field = "workflow_failure_summary") {
    const std::string base(field);
    require_non_empty(diagnostics, scope, base + " message", summary.message);
    require_optional_non_empty(
        diagnostics, scope, base + " node_name", summary.node_name, "must not be empty");
}

inline void validate_failure_summary_owner(const runtime_session::RuntimeFailureSummary &summary,
                                           std::string_view owner_name,
                                           DiagnosticBag &diagnostics,
                                           std::string_view scope) {
    if (summary.message.empty()) {
        emit_validation_error(diagnostics,
                              code::kFailureSummaryEmptyMessage,
                              scoped_message(scope,
                                             std::string(owner_name) +
                                                 " contains failure summary with empty message"));
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        emit_validation_error(diagnostics,
                              code::kFailureSummaryEmptyNodeName,
                              scoped_message(scope,
                                             std::string(owner_name) +
                                                 " contains failure summary with empty node_name"));
    }
}

} // namespace ahfl::validation
