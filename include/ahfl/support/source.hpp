#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
    // Cached line-start offsets for locate/offset_of hot paths.
    // Source content is treated as immutable after loading.
    mutable std::vector<std::size_t> line_starts_cache;
    mutable const char *line_starts_content_data{nullptr};
    mutable std::size_t line_starts_content_size{0};

    [[nodiscard]] SourcePosition locate(std::size_t offset) const {
        refresh_line_starts_cache();

        SourcePosition position{.offset = std::min(offset, content.size())};
        const auto it =
            std::upper_bound(line_starts_cache.begin(), line_starts_cache.end(), position.offset);
        const auto line_index = it == line_starts_cache.begin()
                                    ? 0
                                    : static_cast<std::size_t>((it - line_starts_cache.begin()) - 1);
        position.line = line_index + 1;
        position.column = position.offset - line_starts_cache[line_index] + 1;
        return position;
    }

    [[nodiscard]] std::size_t offset_of(std::size_t line, std::size_t column) const {
        if (line <= 1 && column <= 1) {
            return 0;
        }

        refresh_line_starts_cache();
        if (line == 0 || line > line_starts_cache.size()) {
            return content.size();
        }

        const std::size_t line_start = line_starts_cache[line - 1];
        if (column <= 1) {
            return line_start;
        }

        std::size_t current_column = 1;
        for (std::size_t index = line_start; index < content.size(); ++index) {
            if (current_column == column) {
                return index;
            }

            if (content[index] == '\n') {
                break;
            }

            ++current_column;
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

    void invalidate_line_starts_cache() const {
        line_starts_cache.clear();
        line_starts_content_data = nullptr;
        line_starts_content_size = 0;
    }

    void refresh_line_starts_cache() const {
        const auto *data = content.data();
        const auto size = content.size();
        if (line_starts_content_data == data && line_starts_content_size == size &&
            !line_starts_cache.empty()) {
            return;
        }

        line_starts_cache.clear();
        line_starts_cache.reserve(1 + (content.size() / 32));
        line_starts_cache.push_back(0);
        for (std::size_t index = 0; index < content.size(); ++index) {
            if (content[index] == '\n') {
                line_starts_cache.push_back(index + 1);
            }
        }

        line_starts_content_data = data;
        line_starts_content_size = size;
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
