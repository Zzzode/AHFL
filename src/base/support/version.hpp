#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ahfl::support {

struct SemanticVersion {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t patch{0};
    std::string prerelease;

    [[nodiscard]] bool is_compatible_with(const SemanticVersion &other) const;
    [[nodiscard]] std::string to_string() const;

    bool operator==(const SemanticVersion &) const = default;
    auto operator<=>(const SemanticVersion &other) const {
        if (auto c = major <=> other.major; c != 0) return c;
        if (auto c = minor <=> other.minor; c != 0) return c;
        return patch <=> other.patch;
    }
};

[[nodiscard]] SemanticVersion parse_version(std::string_view input);

} // namespace ahfl::support
