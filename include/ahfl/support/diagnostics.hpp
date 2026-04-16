#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl {

enum class DiagnosticSeverity {
    Note,
    Warning,
    Error,
};

[[nodiscard]] inline std::string_view to_string(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Note:
        return "note";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }

    return "unknown";
}

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string message;
    std::optional<SourceRange> range;
};

class DiagnosticBag {
  public:
    void add(DiagnosticSeverity severity,
             std::string message,
             std::optional<SourceRange> range = std::nullopt) {
        diagnostics_.push_back(Diagnostic{
            .severity = severity,
            .message = std::move(message),
            .range = range,
        });
    }

    void note(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Note, std::move(message), range);
    }

    void warning(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Warning, std::move(message), range);
    }

    void error(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Error, std::move(message), range);
    }

    [[nodiscard]] bool has_error() const noexcept {
        for (const auto &diagnostic : diagnostics_) {
            if (diagnostic.severity == DiagnosticSeverity::Error) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] const std::vector<Diagnostic> &entries() const noexcept {
        return diagnostics_;
    }

    void render(std::ostream &out, MaybeCRef<SourceFile> source = std::nullopt) const {
        for (const auto &diagnostic : diagnostics_) {
            out << to_string(diagnostic.severity) << ": " << diagnostic.message;

            if (source.has_value() && diagnostic.range.has_value()) {
                const auto &source_file = source->get();
                const auto position = source_file.locate(diagnostic.range->begin_offset);
                out << " (" << source_file.display_name << ":" << position.line << ":"
                    << position.column << ")";
            }

            out << '\n';
        }
    }

  private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace ahfl
