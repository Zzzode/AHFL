#pragma once

#include <cstddef>
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
        write_indent();
        buf_ += "- ";
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
    }

  private:
    void write_indent() {
        for (int i = 0; i < indent_ * 2; ++i) {
            buf_ += ' ';
        }
    }

    std::string buf_;
    int indent_ = 0;
};

class HclWriter {
  public:
    HclWriter() = default;

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
        buf_ += value;
        buf_ += "\"\n";
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
    void write_indent() {
        for (int i = 0; i < indent_ * 2; ++i) {
            buf_ += ' ';
        }
    }

    std::string buf_;
    int indent_ = 0;
};

} // namespace ahfl
