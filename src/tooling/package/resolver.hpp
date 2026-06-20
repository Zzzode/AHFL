#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::package {

struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::string prerelease;
};

struct Dependency {
    std::string package_name;
    std::string version_constraint; // e.g., "^1.2.0", ">=1.0.0 <2.0.0", "~1.2"
};

struct ResolvedPackage {
    std::string name;
    SemVer version;
    std::vector<std::string> dependencies;
};

enum class ResolveError {
    VersionConflict,
    PackageNotFound,
    CircularDependency,
    NoSatisfyingVersion
};

struct ResolveResult {
    bool success;
    std::vector<ResolvedPackage> resolved;
    std::optional<ResolveError> error;
    std::string error_message;
};

class Resolver {
  public:
    void add_available(const std::string &name, const std::vector<SemVer> &versions);
    void add_dependency(Dependency dep);

    [[nodiscard]] ResolveResult resolve() const;

    [[nodiscard]] static SemVer parse_version(const std::string &version_str);
    [[nodiscard]] static bool satisfies(const SemVer &version, const std::string &constraint);
    [[nodiscard]] static int compare(const SemVer &a, const SemVer &b);
    [[nodiscard]] static std::string format_version(const SemVer &v);

  private:
    std::unordered_map<std::string, std::vector<SemVer>> available_;
    std::vector<Dependency> dependencies_;
};

} // namespace ahfl::package
