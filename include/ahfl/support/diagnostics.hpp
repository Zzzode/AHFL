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
    std::optional<std::string> source_name;
    std::optional<SourcePosition> position;
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

    void add_in_source(DiagnosticSeverity severity,
                       std::string message,
                       const SourceFile &source,
                       std::optional<SourceRange> range = std::nullopt) {
        Diagnostic diagnostic{
            .severity = severity,
            .message = std::move(message),
            .range = range,
            .source_name = source.display_name,
        };

        if (range.has_value()) {
            diagnostic.position = source.locate(range->begin_offset);
        }

        diagnostics_.push_back(std::move(diagnostic));
    }

    void note(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Note, std::move(message), range);
    }

    void note_in_source(std::string message,
                        const SourceFile &source,
                        std::optional<SourceRange> range = std::nullopt) {
        add_in_source(DiagnosticSeverity::Note, std::move(message), source, range);
    }

    void warning(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Warning, std::move(message), range);
    }

    void warning_in_source(std::string message,
                           const SourceFile &source,
                           std::optional<SourceRange> range = std::nullopt) {
        add_in_source(DiagnosticSeverity::Warning, std::move(message), source, range);
    }

    void error(std::string message, std::optional<SourceRange> range = std::nullopt) {
        add(DiagnosticSeverity::Error, std::move(message), range);
    }

    void error_in_source(std::string message,
                         const SourceFile &source,
                         std::optional<SourceRange> range = std::nullopt) {
        add_in_source(DiagnosticSeverity::Error, std::move(message), source, range);
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

    void append(const DiagnosticBag &other) {
        diagnostics_.insert(
            diagnostics_.end(), other.diagnostics_.begin(), other.diagnostics_.end());
    }

    void append_from_source(const DiagnosticBag &other, const SourceFile &source) {
        for (const auto &entry : other.diagnostics_) {
            auto diagnostic = entry;
            if (!diagnostic.source_name.has_value()) {
                diagnostic.source_name = source.display_name;
            }
            if (!diagnostic.position.has_value() && diagnostic.range.has_value()) {
                diagnostic.position = source.locate(diagnostic.range->begin_offset);
            }

            diagnostics_.push_back(std::move(diagnostic));
        }
    }

    void render(std::ostream &out, MaybeCRef<SourceFile> source = std::nullopt) const {
        for (const auto &diagnostic : diagnostics_) {
            out << to_string(diagnostic.severity) << ": " << diagnostic.message;

            if (diagnostic.source_name.has_value()) {
                out << " (" << *diagnostic.source_name;
                if (diagnostic.position.has_value()) {
                    out << ":" << diagnostic.position->line << ":" << diagnostic.position->column;
                }
                out << ")";
            } else if (source.has_value() && diagnostic.range.has_value()) {
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
