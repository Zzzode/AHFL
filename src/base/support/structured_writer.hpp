#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl {

class YamlWriter {
  public:
    YamlWriter() = default;

    YamlWriter &key_value(std::string_view key, std::string_view value) {
        write_indent();
        buf_ += key;
        buf_ += ": ";
        buf_ += value;
        buf_ += '\n';
        return *this;
    }

    YamlWriter &key_value_bool(std::string_view key, bool value) {
        write_indent();
        buf_ += key;
        buf_ += ": ";
        buf_ += (value ? "true" : "false");
        buf_ += '\n';
        return *this;
    }

    // Emit a double-quoted YAML scalar. Use for free-form values that may
    // contain ': ' or other YAML-significant bytes.
    YamlWriter &key_value_quoted(std::string_view key, std::string_view value) {
        write_indent();
        buf_ += key;
        buf_ += ": \"";
        append_escaped_yaml(value);
        buf_ += "\"\n";
        return *this;
    }

    YamlWriter &begin_mapping(std::string_view key) {
        write_indent();
        buf_ += key;
        buf_ += ":\n";
        indent_++;
        return *this;
    }

    YamlWriter &end_mapping() {
        indent_--;
        return *this;
    }

    YamlWriter &begin_list_item() {
        // Defer the "- " marker to the next emitted line so the first key of
        // the item sits inline with the dash (e.g. "- name: v1alpha1") and
        // continuation keys align one level deeper. Emitting the dash eagerly
        // here produced a dangling "- " line with misaligned keys, which is
        // not valid YAML.
        pending_dash_ = true;
        indent_++;
        return *this;
    }

    YamlWriter &end_list_item() {
        indent_--;
        return *this;
    }

    YamlWriter &key_inline_list(std::string_view key, const std::vector<std::string> &items) {
        write_indent();
        buf_ += key;
        buf_ += ": [";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0)
                buf_ += ", ";
            buf_ += items[i];
        }
        buf_ += "]\n";
        return *this;
    }

    [[nodiscard]] const std::string &str() const {
        return buf_;
    }
    void clear() {
        buf_.clear();
        indent_ = 0;
        pending_dash_ = false;
    }

  private:
    void append_escaped_yaml(std::string_view value) {
        for (const char ch : value) {
            switch (ch) {
            case '\\':
                buf_ += "\\\\";
                break;
            case '"':
                buf_ += "\\\"";
                break;
            case '\n':
                buf_ += "\\n";
                break;
            default:
                buf_ += ch;
                break;
            }
        }
    }

    void write_indent() {
        if (pending_dash_) {
            // The list marker replaces the last indent level: emit
            // (indent_ - 1) levels of spaces, then "- ". The following key
            // therefore starts at the same column as a plain indent_ line,
            // keeping it aligned with the item's continuation keys.
            const int lead = (indent_ > 0 ? indent_ - 1 : 0) * 2;
            for (int i = 0; i < lead; ++i) {
                buf_ += ' ';
            }
            buf_ += "- ";
            pending_dash_ = false;
            return;
        }
        for (int i = 0; i < indent_ * 2; ++i) {
            buf_ += ' ';
        }
    }

    std::string buf_;
    int indent_ = 0;
    bool pending_dash_ = false;
};

class HclWriter {
  public:
    HclWriter() = default;

    HclWriter &begin_block(std::string_view type) {
        write_indent();
        buf_ += type;
        buf_ += " {\n";
        indent_++;
        return *this;
    }

    HclWriter &begin_block(std::string_view type, std::string_view name) {
        write_indent();
        buf_ += type;
        buf_ += " \"";
        buf_ += name;
        buf_ += "\" {\n";
        indent_++;
        return *this;
    }

    HclWriter &
    begin_block(std::string_view type, std::string_view label1, std::string_view label2) {
        write_indent();
        buf_ += type;
        buf_ += " \"";
        buf_ += label1;
        buf_ += "\" \"";
        buf_ += label2;
        buf_ += "\" {\n";
        indent_++;
        return *this;
    }

    // Begin an inline object assignment: `key = {`.
    HclWriter &begin_object(std::string_view key) {
        write_indent();
        buf_ += key;
        buf_ += " = {\n";
        indent_++;
        return *this;
    }

    HclWriter &end_block() {
        indent_--;
        write_indent();
        buf_ += "}\n\n";
        return *this;
    }

    HclWriter &attribute(std::string_view key, std::string_view value) {
        write_indent();
        buf_ += key;
        buf_ += " = \"";
        append_escaped_hcl(value);
        buf_ += "\"\n";
        return *this;
    }

    HclWriter &attribute_number(std::string_view key, std::int64_t value) {
        write_indent();
        buf_ += key;
        buf_ += " = ";
        buf_ += std::to_string(value);
        buf_ += '\n';
        return *this;
    }

    HclWriter &attribute_bool(std::string_view key, bool value) {
        write_indent();
        buf_ += key;
        buf_ += value ? " = true\n" : " = false\n";
        return *this;
    }

    // Emit a bracketed list of quoted strings: ["a", "b"].
    HclWriter &attribute_string_list(std::string_view key, const std::vector<std::string> &values) {
        write_indent();
        buf_ += key;
        buf_ += " = [";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0)
                buf_ += ", ";
            buf_ += '"';
            append_escaped_hcl(values[i]);
            buf_ += '"';
        }
        buf_ += "]\n";
        return *this;
    }

    // Emit a bracketed list of unquoted HCL references: [a.b, c.d].
    HclWriter &reference_list(std::string_view key, const std::vector<std::string> &refs) {
        write_indent();
        buf_ += key;
        buf_ += " = [";
        for (std::size_t i = 0; i < refs.size(); ++i) {
            if (i > 0)
                buf_ += ", ";
            buf_ += refs[i];
        }
        buf_ += "]\n";
        return *this;
    }

    [[nodiscard]] const std::string &str() const {
        return buf_;
    }
    void clear() {
        buf_.clear();
        indent_ = 0;
    }

  private:
    void append_escaped_hcl(std::string_view value) {
        for (const char ch : value) {
            switch (ch) {
            case '\\':
                buf_ += "\\\\";
                break;
            case '"':
                buf_ += "\\\"";
                break;
            case '\n':
                buf_ += "\\n";
                break;
            default:
                buf_ += ch;
                break;
            }
        }
    }

    void write_indent() {
        for (int i = 0; i < indent_ * 2; ++i) {
            buf_ += ' ';
        }
    }

    std::string buf_;
    int indent_ = 0;
};

} // namespace ahfl
