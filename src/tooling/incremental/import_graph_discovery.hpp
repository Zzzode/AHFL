#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <tooling/incremental/dependency_graph.hpp>

namespace ahfl::incremental {

struct ImportGraphDiscoveryResult {
    DependencyGraph graph;
    std::vector<std::string> diagnostics;
    // True when the manifest was found and the SourceGraph parsed (even if the
    // graph is empty or the parse produced diagnostics).
    bool discovered{false};
};

// Discovers the import graph for a project by parsing the ahfl.toml manifest
// and the project's SourceGraph (RFC 0016 "Project-Aware Import Graph
// Discovery"). Every SourceUnit becomes a ModuleNode and every ImportEdge
// becomes an import entry on the importer node. Module paths are absolute,
// normalized filesystem paths so they can be read directly by the
// IncrementalCompiler.
//
// `project_root` is the project root directory; `manifest_path` is the
// ahfl.toml manifest to discover from.
[[nodiscard]] ImportGraphDiscoveryResult
discover_import_graph(const std::filesystem::path &project_root,
                      const std::filesystem::path &manifest_path);

} // namespace ahfl::incremental
