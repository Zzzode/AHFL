#include "tooling/incremental/import_graph_discovery.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "compiler/package_graph/package_graph.hpp"
#include "compiler/project_discovery/discovery.hpp"
#include "compiler/syntax/frontend/project.hpp"
#include "tooling/incremental/cache_core.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace ahfl::incremental {

namespace {

namespace fs = std::filesystem;

// SourceId is a struct (not an enum), so std::hash is not provided. Mirrors
// the LSP's SourceIdHash in analysis_service.cpp.
struct SourceIdHash {
    [[nodiscard]] std::size_t operator()(SourceId id) const noexcept {
        return std::hash<std::size_t>{}(id.value);
    }
};

[[nodiscard]] std::vector<std::string>
dependency_prefixes_for_package(const package_graph::PackageGraph &graph,
                                package_graph::PackageId package_id) {
    std::vector<std::string> prefixes;
    for (const auto &dependency : graph.dependencies) {
        if (dependency.from != package_id) {
            continue;
        }
        const auto *target = graph.find_package(dependency.to);
        if (target != nullptr) {
            prefixes.push_back(target->module_prefix);
        }
    }
    std::sort(prefixes.begin(), prefixes.end());
    prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
    return prefixes;
}

[[nodiscard]] std::vector<std::string>
artifact_exports_for_package(const package_graph::PackageGraph &graph,
                             package_graph::PackageId package_id) {
    std::vector<std::string> exports;
    const auto *package = graph.find_package(package_id);
    if (package == nullptr) {
        return exports;
    }
    for (const auto &target : package->targets) {
        for (const auto &export_item : target.exports) {
            exports.push_back(export_item.name);
        }
    }
    std::sort(exports.begin(), exports.end());
    exports.erase(std::unique(exports.begin(), exports.end()), exports.end());
    return exports;
}

// Builds a ProjectInput from a discovered PackageGraph. Module roots mirror
// the LSP's project_input_from_package_graph (without the navigation-scope
// bookkeeping); entry files are seeded from every known source unit so the
// parsed SourceGraph covers the full project, not just one target's closure.
[[nodiscard]] ProjectInput project_input_from_package_graph(
    const package_graph::PackageGraph &graph) {
    ProjectInput input;
    input.inject_prelude = false;
    input.enforce_package_dependencies = true;

    input.module_roots.reserve(graph.packages.size());
    for (const auto &package : graph.packages) {
        input.module_roots.push_back(ProjectInput::ModuleRoot{
            .prefix = package.module_prefix,
            .root = package.module_root,
            .exported_modules = package.exported_modules,
            .artifact_exports = artifact_exports_for_package(graph, package.id),
            .dependency_prefixes = dependency_prefixes_for_package(graph, package.id),
            .compiler_intrinsics_allow = package.compiler_intrinsics_allow,
        });
    }

    for (const auto &unit : graph.source_units) {
        const auto normalized = project_discovery::normalize_project_path(unit.path);
        if (std::find(input.entry_files.begin(), input.entry_files.end(), normalized) ==
            input.entry_files.end()) {
            input.entry_files.push_back(normalized);
        }
    }
    return input;
}

[[nodiscard]] DependencyGraph dependency_graph_from_source_graph(const SourceGraph &source_graph) {
    DependencyGraph graph;

    std::unordered_map<SourceId, std::string, SourceIdHash> path_by_id;
    path_by_id.reserve(source_graph.sources.size());
    for (const auto &source : source_graph.sources) {
        path_by_id.emplace(source.id, source.path.string());
    }

    std::unordered_map<SourceId, std::vector<std::string>, SourceIdHash> imports_by_importer;
    imports_by_importer.reserve(source_graph.sources.size());
    for (const auto &edge : source_graph.import_edges) {
        if (edge.importer == edge.imported) {
            continue; // self-import carries no dependency edge
        }
        const auto imported_it = path_by_id.find(edge.imported);
        if (imported_it == path_by_id.end()) {
            continue;
        }
        auto &imports = imports_by_importer[edge.importer];
        if (std::find(imports.begin(), imports.end(), imported_it->second) == imports.end()) {
            imports.push_back(imported_it->second);
        }
    }

    for (const auto &source : source_graph.sources) {
        ModuleNode node;
        node.module_path = source.path.string();
        node.content_hash = fnv1a64(source.source.content);
        auto imports_it = imports_by_importer.find(source.id);
        if (imports_it != imports_by_importer.end()) {
            node.imports = std::move(imports_it->second);
        }
        graph.add_module(std::move(node));
    }
    return graph;
}

} // namespace

ImportGraphDiscoveryResult
discover_import_graph(const fs::path &project_root, const fs::path &manifest_path) {
    ImportGraphDiscoveryResult result;

    project_discovery::ProjectDiscoveryInput input;
    input.document_path = project_root;
    input.explicit_manifest_path = manifest_path;
    input.toolchains.allow_compile_default = true;

    const auto discovery = project_discovery::discover_project_context(input);
    for (const auto &diagnostic : discovery.diagnostics) {
        result.diagnostics.push_back(diagnostic.message);
    }
    if (!discovery.context.has_value()) {
        return result;
    }

    const auto project_input =
        project_input_from_package_graph(discovery.context->graph);
    if (project_input.entry_files.empty()) {
        result.diagnostics.emplace_back(
            "project discovery yielded no entry files; cannot build import graph");
        return result;
    }

    const Frontend frontend;
    auto project_result = parse_project(frontend, project_input);
    for (const auto &diagnostic : project_result.diagnostics.entries()) {
        result.diagnostics.push_back(diagnostic.message);
    }

    result.graph = dependency_graph_from_source_graph(project_result.graph);
    result.discovered = true;
    return result;
}

} // namespace ahfl::incremental
