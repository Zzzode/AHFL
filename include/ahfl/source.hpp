#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace ahfl {

struct SourcePosition {
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};
};

struct SourceRange {
    std::size_t begin_offset{0};
    std::size_t end_offset{0};

    [[nodiscard]] bool empty() const noexcept {
        return begin_offset == end_offset;
    }
};

struct SourceFile {
    std::string display_name;
    std::string content;

    [[nodiscard]] SourcePosition locate(std::size_t offset) const {
        SourcePosition position{.offset = std::min(offset, content.size())};

        for (std::size_t index = 0; index < position.offset; ++index) {
            if (content[index] == '\n') {
                ++position.line;
                position.column = 1;
            } else {
                ++position.column;
            }
        }

        return position;
    }

    [[nodiscard]] std::size_t offset_of(std::size_t line, std::size_t column) const {
        if (line <= 1 && column <= 1) {
            return 0;
        }

        std::size_t current_line = 1;
        std::size_t current_column = 1;

        for (std::size_t index = 0; index < content.size(); ++index) {
            if (current_line == line && current_column == column) {
                return index;
            }

            if (content[index] == '\n') {
                ++current_line;
                current_column = 1;
            } else {
                ++current_column;
            }
        }

        return content.size();
    }

    [[nodiscard]] std::string_view slice(SourceRange range) const {
        if (range.begin_offset >= content.size() || range.end_offset < range.begin_offset) {
            return {};
        }

        const auto length = std::min(range.end_offset, content.size()) - range.begin_offset;
        return std::string_view{content}.substr(range.begin_offset, length);
    }
};

} // namespace ahfl
