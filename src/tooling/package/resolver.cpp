#include "tooling/package/resolver.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace ahfl::package {

void Resolver::add_available(const std::string& name, const std::vector<SemVer>& versions) {
    available_[name] = versions;
}

void Resolver::add_dependency(Dependency dep) { dependencies_.push_back(std::move(dep)); }

SemVer Resolver::parse_version(const std::string& version_str) {
    SemVer result{};
    std::string input = version_str;

    // Strip leading 'v' if present
    if (!input.empty() && input[0] == 'v') {
        input = input.substr(1);
    }

    // Split on '-' for prerelease
    auto dash_pos = input.find('-');
    std::string version_part = input;
    if (dash_pos != std::string::npos) {
        result.prerelease = input.substr(dash_pos + 1);
        version_part = input.substr(0, dash_pos);
    }

    // Parse major.minor.patch
    std::istringstream stream(version_part);
    std::string segment;

    if (std::getline(stream, segment, '.')) {
        auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(),
                                         result.major);
        (void)ptr;
        (void)ec;
    }
    if (std::getline(stream, segment, '.')) {
        auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(),
                                         result.minor);
        (void)ptr;
        (void)ec;
    }
    if (std::getline(stream, segment, '.')) {
        auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(),
                                         result.patch);
        (void)ptr;
        (void)ec;
    }

    return result;
}

int Resolver::compare(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

    // Prerelease has lower precedence than release
    if (a.prerelease.empty() && !b.prerelease.empty()) return 1;
    if (!a.prerelease.empty() && b.prerelease.empty()) return -1;
    if (a.prerelease < b.prerelease) return -1;
    if (a.prerelease > b.prerelease) return 1;

    return 0;
}

std::string Resolver::format_version(const SemVer& v) {
    std::string result =
        std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    if (!v.prerelease.empty()) {
        result += "-" + v.prerelease;
    }
    return result;
}

bool Resolver::satisfies(const SemVer& version, const std::string& constraint) {
    if (constraint.empty()) return true;

    // Handle caret (^) - compatible with version
    // ^1.2.3 means >=1.2.3 and <2.0.0
    // ^0.2.3 means >=0.2.3 and <0.3.0
    // ^0.0.3 means >=0.0.3 and <0.0.4
    if (constraint[0] == '^') {
        auto base = parse_version(constraint.substr(1));
        if (compare(version, base) < 0) return false;

        SemVer upper{};
        if (base.major != 0) {
            upper.major = base.major + 1;
        } else if (base.minor != 0) {
            upper.minor = base.minor + 1;
        } else {
            upper.patch = base.patch + 1;
        }
        return compare(version, upper) < 0;
    }

    // Handle tilde (~) - patch-level changes
    // ~1.2.3 means >=1.2.3 and <1.3.0
    // ~1.2 means >=1.2.0 and <1.3.0
    if (constraint[0] == '~') {
        auto base = parse_version(constraint.substr(1));
        if (compare(version, base) < 0) return false;

        SemVer upper{};
        upper.major = base.major;
        upper.minor = base.minor + 1;
        upper.patch = 0;
        return compare(version, upper) < 0;
    }

    // Handle >= prefix
    if (constraint.size() >= 2 && constraint[0] == '>' && constraint[1] == '=') {
        auto base = parse_version(constraint.substr(2));
        return compare(version, base) >= 0;
    }

    // Handle > prefix
    if (constraint[0] == '>' && (constraint.size() < 2 || constraint[1] != '=')) {
        auto base = parse_version(constraint.substr(1));
        return compare(version, base) > 0;
    }

    // Handle <= prefix
    if (constraint.size() >= 2 && constraint[0] == '<' && constraint[1] == '=') {
        auto base = parse_version(constraint.substr(2));
        return compare(version, base) <= 0;
    }

    // Handle < prefix
    if (constraint[0] == '<' && (constraint.size() < 2 || constraint[1] != '=')) {
        auto base = parse_version(constraint.substr(1));
        return compare(version, base) < 0;
    }

    // Handle exact match
    auto target = parse_version(constraint);
    return compare(version, target) == 0;
}

ResolveResult Resolver::resolve() const {
    ResolveResult result;
    result.success = true;

    for (const auto& dep : dependencies_) {
        auto it = available_.find(dep.package_name);
        if (it == available_.end()) {
            result.success = false;
            result.error = ResolveError::PackageNotFound;
            result.error_message = "Package not found: " + dep.package_name;
            return result;
        }

        const auto& versions = it->second;

        // Find the highest version that satisfies the constraint
        const SemVer* best = nullptr;
        for (const auto& v : versions) {
            if (satisfies(v, dep.version_constraint)) {
                if (best == nullptr || compare(v, *best) > 0) {
                    best = &v;
                }
            }
        }

        if (best == nullptr) {
            result.success = false;
            result.error = ResolveError::NoSatisfyingVersion;
            result.error_message = "No version satisfies constraint '" + dep.version_constraint +
                                   "' for package '" + dep.package_name + "'";
            return result;
        }

        ResolvedPackage resolved_pkg;
        resolved_pkg.name = dep.package_name;
        resolved_pkg.version = *best;
        result.resolved.push_back(std::move(resolved_pkg));
    }

    // Check for conflicts: same package resolved to different versions
    for (std::size_t i = 0; i < result.resolved.size(); ++i) {
        for (std::size_t j = i + 1; j < result.resolved.size(); ++j) {
            if (result.resolved[i].name == result.resolved[j].name) {
                if (compare(result.resolved[i].version, result.resolved[j].version) != 0) {
                    result.success = false;
                    result.error = ResolveError::VersionConflict;
                    result.error_message =
                        "Version conflict for '" + result.resolved[i].name + "': " +
                        format_version(result.resolved[i].version) + " vs " +
                        format_version(result.resolved[j].version);
                    return result;
                }
            }
        }
    }

    return result;
}

} // namespace ahfl::package
