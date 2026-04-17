#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace ahfl {

struct SourceId {
    std::size_t value{0};

    [[nodiscard]] friend bool operator==(SourceId lhs, SourceId rhs) noexcept = default;
};

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

[[nodiscard]] inline std::string display_path(const std::filesystem::path &path) {
    const auto normalized = path.lexically_normal();
    if (!normalized.is_absolute()) {
        return normalized.generic_string();
    }

    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (error) {
        return normalized.generic_string();
    }

    const auto relative = std::filesystem::proximate(normalized, current, error);
    if (error || relative.empty()) {
        return normalized.generic_string();
    }

    return relative.lexically_normal().generic_string();
}

struct SourceArtifact {
    SourceId id;
    std::filesystem::path path;
    SourceFile source;
};

struct ImportRequest {
    std::string module_name;
    std::string alias;
    SourceRange range;
};

} // namespace ahfl
