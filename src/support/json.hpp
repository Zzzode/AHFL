#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

inline void write_escaped_json_string(std::ostream &out, std::string_view value) {
    constexpr char kHexDigits[] = "0123456789abcdef";

    out << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
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
            if (byte < 0x20U) {
                out << "\\u00" << kHexDigits[(byte >> 4U) & 0x0FU] << kHexDigits[byte & 0x0FU];
            } else {
                out << character;
            }
            break;
        }
    }
    out << '"';
}

class PrettyJsonWriter {
  protected:
    explicit PrettyJsonWriter(std::ostream &out, int indent_width = 2)
        : out_(out), indent_width_(indent_width) {}

    void write_indent(int indent_level) {
        out_ << std::string(
            static_cast<std::size_t>(indent_level) * static_cast<std::size_t>(indent_width_), ' ');
    }

    void newline_and_indent(int indent_level) {
        out_ << '\n';
        write_indent(indent_level);
    }

    void write_string(std::string_view value) {
        write_escaped_json_string(out_, value);
    }

    template <typename WriteFields> void print_object(int indent_level, WriteFields write_fields) {
        out_ << '{';
        bool wrote_any_field = false;

        const auto field = [&](std::string_view name, const auto &write_value) {
            if (wrote_any_field) {
                out_ << ',';
            }
            wrote_any_field = true;
            newline_and_indent(indent_level + 1);
            write_string(name);
            out_ << ": ";
            write_value();
        };

        write_fields(field);

        if (wrote_any_field) {
            newline_and_indent(indent_level);
        }
        out_ << '}';
    }

    template <typename WriteItems> void print_array(int indent_level, WriteItems write_items) {
        out_ << '[';
        bool wrote_any_item = false;

        const auto item = [&](const auto &write_value) {
            if (wrote_any_item) {
                out_ << ',';
            }
            wrote_any_item = true;
            newline_and_indent(indent_level + 1);
            write_value();
        };

        write_items(item);

        if (wrote_any_item) {
            newline_and_indent(indent_level);
        }
        out_ << ']';
    }

    std::ostream &out_;

  private:
    int indent_width_;
};

} // namespace ahfl
