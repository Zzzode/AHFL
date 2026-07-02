#pragma once

#include "compiler/package_graph/package_graph.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::package_graph {

struct LockfilePackage {
    PackageId id;
    std::string name;
    std::string version;
    std::string source;
    std::string manifest;
    std::string checksum;
};

struct LockfileEdge {
    PackageId from;
    std::string dependency;
    PackageId to;
    std::string source;
};

struct Lockfile {
    std::string format_version{"ahfl.lock.v1"};
    int resolver_version{1};
    std::string root_package;
    std::vector<LockfilePackage> packages;
    std::vector<LockfileEdge> edges;
};

struct LockfileResult {
    std::optional<Lockfile> lockfile;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const {
        return !diagnostics.empty();
    }
};

[[nodiscard]] Lockfile make_lockfile(const PackageGraph &graph);
[[nodiscard]] Lockfile make_lockfile(const PackageGraph &graph,
                                     const std::filesystem::path &lockfile_directory);
[[nodiscard]] std::string serialize_lockfile(const Lockfile &lockfile);
[[nodiscard]] LockfileResult parse_lockfile_json(std::string_view input);
[[nodiscard]] std::vector<Diagnostic> check_lockfile_drift(const PackageGraph &graph,
                                                           const Lockfile &lockfile);
[[nodiscard]] std::vector<Diagnostic>
check_lockfile_drift(const PackageGraph &graph,
                     const Lockfile &lockfile,
                     const std::filesystem::path &lockfile_directory);

} // namespace ahfl::package_graph
