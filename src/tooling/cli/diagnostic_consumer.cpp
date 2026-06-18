#include "tooling/cli/diagnostic_consumer.hpp"

#include <ostream>
#include <stdexcept>
#include <string>

namespace ahfl::cli {

// ---------------------------------------------------------------------------
// TextDiagnosticConsumer
// ---------------------------------------------------------------------------

TextDiagnosticConsumer::TextDiagnosticConsumer(std::ostream &out) : out_(out) {}

void TextDiagnosticConsumer::consume(
    const ahfl::DiagnosticBag &bag,
    std::optional<std::reference_wrapper<const ahfl::SourceFile>> source) {
    bag.render(out_, source, /*include_code=*/true);
}

// ---------------------------------------------------------------------------
// JsonDiagnosticConsumer
// ---------------------------------------------------------------------------

namespace {

void json_escape(std::ostream &out, std::string_view str) {
    for (const char ch : str) {
        switch (ch) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                // Control characters as \u00XX
                const char *hex = "0123456789abcdef";
                out << "\\u00" << hex[(ch >> 4) & 0xf] << hex[ch & 0xf];
            } else {
                out << ch;
            }
            break;
        }
    }
}

} // namespace

JsonDiagnosticConsumer::JsonDiagnosticConsumer(std::ostream &out) : out_(out) {}

void JsonDiagnosticConsumer::consume(
    const ahfl::DiagnosticBag &bag,
    std::optional<std::reference_wrapper<const ahfl::SourceFile>> source) {
    for (const auto &diagnostic : bag.entries()) {
        out_ << "{\"severity\":\"" << ahfl::to_string(diagnostic.severity) << "\",\"message\":\"";
        json_escape(out_, diagnostic.message);
        out_ << "\"";

        if (diagnostic.code.has_value()) {
            out_ << ",\"code\":\"";
            json_escape(out_, *diagnostic.code);
            out_ << "\"";
        }

        // Resolve file/line/column from either the diagnostic's own fields or the source context.
        if (diagnostic.source_name.has_value()) {
            out_ << ",\"file\":\"";
            json_escape(out_, *diagnostic.source_name);
            out_ << "\"";
            if (diagnostic.position.has_value()) {
                out_ << ",\"line\":" << diagnostic.position->line;
                out_ << ",\"column\":" << diagnostic.position->column;
            }
        } else if (source.has_value() && diagnostic.range.has_value()) {
            const auto &source_file = source->get();
            out_ << ",\"file\":\"";
            json_escape(out_, source_file.display_name);
            out_ << "\"";
            const auto position = source_file.locate(diagnostic.range->begin_offset);
            out_ << ",\"line\":" << position.line;
            out_ << ",\"column\":" << position.column;
        }

        out_ << "}\n";
        first_ = false;
    }
}

void JsonDiagnosticConsumer::finalize() {
    // JSON Lines format doesn't need closing — intentional no-op.
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<DiagnosticConsumer> make_diagnostic_consumer(std::string_view format,
                                                             std::ostream &out) {
    if (format == "json") {
        return std::make_unique<JsonDiagnosticConsumer>(out);
    }
    if (format == "text" || format.empty()) {
        return std::make_unique<TextDiagnosticConsumer>(out);
    }
    throw std::invalid_argument(std::string("unknown diagnostic format: '") + std::string(format) +
                                "'");
}

} // namespace ahfl::cli
