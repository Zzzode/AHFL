#include "base/support/version.hpp"

#include <charconv>
#include <cstdlib>
#include <stdexcept>

namespace ahfl::support {

bool SemanticVersion::is_compatible_with(const SemanticVersion &other) const {
    return major == other.major && other.minor >= minor;
}

std::string SemanticVersion::to_string() const {
    auto result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) {
        result += "-" + prerelease;
    }
    return result;
}

SemanticVersion parse_version(std::string_view input) {
    SemanticVersion version;

    // Extract prerelease suffix first
    auto dash_pos = input.find('-');
    std::string_view numeric_part = input;
    if (dash_pos != std::string_view::npos) {
        version.prerelease = std::string(input.substr(dash_pos + 1));
        numeric_part = input.substr(0, dash_pos);
    }

    // Parse major
    auto dot1 = numeric_part.find('.');
    if (dot1 == std::string_view::npos) {
        std::abort();
    }
    auto [ptr1, ec1] =
        std::from_chars(numeric_part.data(), numeric_part.data() + dot1, version.major);
    if (ec1 != std::errc{}) {
        std::abort();
    }

    // Parse minor
    auto rest = numeric_part.substr(dot1 + 1);
    auto dot2 = rest.find('.');
    if (dot2 == std::string_view::npos) {
        std::abort();
    }
    auto [ptr2, ec2] = std::from_chars(rest.data(), rest.data() + dot2, version.minor);
    if (ec2 != std::errc{}) {
        std::abort();
    }

    // Parse patch
    auto patch_str = rest.substr(dot2 + 1);
    auto [ptr3, ec3] =
        std::from_chars(patch_str.data(), patch_str.data() + patch_str.size(), version.patch);
    if (ec3 != std::errc{}) {
        std::abort();
    }

    return version;
}

} // namespace ahfl::support
