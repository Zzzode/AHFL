#include "tooling/incremental/cache_core.hpp"
#include "tooling/incremental/dependency_graph.hpp"
#include "tooling/incremental/import_graph_discovery.hpp"
#include "tooling/incremental/incremental_compiler.hpp"
#include "tooling/incremental/ir_cache.hpp"

#include "compiler/project_discovery/discovery.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

void print_usage(std::ostream &out) {
    out << "Usage: ahfl-incremental [--help] [--project <root>] [--cache-dir <dir>]"
           " <changed.ahfl>...\n\n"
        << "Runs the AHFL incremental compiler over the supplied changed source files.\n"
        << "When the changed files belong to a project (ahfl.toml), the import graph is\n"
        << "discovered automatically and a persistent cache is maintained under the\n"
        << "project's cache directory.\n";
}

std::string_view status_name(ahfl::incremental::CompileStatus status) {
    switch (status) {
    case ahfl::incremental::CompileStatus::UpToDate:
        return "up-to-date";
    case ahfl::incremental::CompileStatus::Recompiled:
        return "recompiled";
    case ahfl::incremental::CompileStatus::Failed:
        return "failed";
    }
    return "unknown";
}

// Default cache root per RFC 0016 (XDG base directory spec):
// $XDG_CACHE_HOME/ahfl, falling back to ~/.cache/ahfl.
[[nodiscard]] fs::path default_cache_root() {
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return fs::path(xdg) / "ahfl";
    }
    if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return fs::path(home) / ".cache" / "ahfl";
    }
    return fs::path{".ahfl-cache"};
}

struct CliOptions {
    std::vector<std::string> changed_paths;
    std::optional<fs::path> project_root;
    std::optional<fs::path> cache_dir;
};

[[nodiscard]] std::optional<CliOptions> parse_args(int argc, char **argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            if (argc == 2) {
                print_usage(std::cout);
                std::exit(0);
            }
            std::cerr << "error: --help cannot be combined with source files\n";
            print_usage(std::cerr);
            return std::nullopt;
        }
        if (argument == "--project") {
            if (index + 1 >= argc) {
                std::cerr << "error: --project requires a path argument\n";
                return std::nullopt;
            }
            options.project_root = fs::path(argv[++index]);
            continue;
        }
        if (argument == "--cache-dir") {
            if (index + 1 >= argc) {
                std::cerr << "error: --cache-dir requires a path argument\n";
                return std::nullopt;
            }
            options.cache_dir = fs::path(argv[++index]);
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "unknown option: " << argument << '\n';
            print_usage(std::cerr);
            return std::nullopt;
        }
        options.changed_paths.push_back(argument);
    }
    if (options.changed_paths.empty()) {
        print_usage(std::cerr);
        return std::nullopt;
    }
    return options;
}

// Resolves the project root and manifest for the changed paths. When
// --project is not supplied, walks up from the first changed path to find the
// nearest ahfl.toml (mirroring the LSP's project discovery).
struct ProjectLocation {
    fs::path root;
    fs::path manifest;
};

[[nodiscard]] std::optional<ProjectLocation>
resolve_project_location(const CliOptions &options) {
    if (options.project_root.has_value()) {
        const auto root = ahfl::project_discovery::normalize_project_path(
            *options.project_root);
        const auto manifest = root / "ahfl.toml";
        std::error_code error;
        if (!fs::exists(manifest, error) || error) {
            std::cerr << "warning: project manifest not found at '"
                      << manifest.generic_string() << "'\n";
            return std::nullopt;
        }
        return ProjectLocation{root, manifest};
    }
    const auto manifest =
        ahfl::project_discovery::find_package_manifest_for_document(
            fs::path(options.changed_paths.front()), {});
    if (!manifest.has_value()) {
        return std::nullopt;
    }
    return ProjectLocation{manifest->parent_path(), *manifest};
}

} // namespace

int main(int argc, char **argv) {
    const auto options = parse_args(argc, argv);
    if (!options.has_value()) {
        return 2;
    }

    ahfl::incremental::DependencyGraph graph;
    ahfl::incremental::IrCache cache;
    ahfl::incremental::IncrementalCompilerConfig config;

    // Project-aware mode: discover the import graph and wire the persistent
    // cache. Falls back to the legacy detached mode (empty graph, in-memory
    // cache only) when no project manifest is found.
    std::optional<ahfl::incremental::PersistentCache> persistent_cache;
    if (const auto location = resolve_project_location(*options)) {
        auto discovery = ahfl::incremental::discover_import_graph(location->root,
                                                                  location->manifest);
        for (const auto &diagnostic : discovery.diagnostics) {
            std::cerr << "warning: " << diagnostic << '\n';
        }
        if (discovery.discovered) {
            graph = std::move(discovery.graph);
            config.project_root = location->root;
            const auto cache_root =
                options->cache_dir.value_or(default_cache_root());
            const auto project_cache_dir =
                cache_root / ahfl::incremental::project_root_hash(location->root);
            persistent_cache.emplace(project_cache_dir);
            config.persistent_cache = &*persistent_cache;
        }
    }

    // Normalize changed paths to match the discovered graph's absolute keys,
    // and ensure every changed path is a node in the graph even when
    // discovery did not run (detached mode) or missed a file.
    std::vector<std::string> changed_paths;
    changed_paths.reserve(options->changed_paths.size());
    for (const auto &path : options->changed_paths) {
        const auto normalized =
            ahfl::project_discovery::normalize_project_path(fs::path(path)).string();
        changed_paths.push_back(normalized);
        if (!graph.has_module(normalized)) {
            ahfl::incremental::ModuleNode node;
            node.module_path = normalized;
            graph.add_module(std::move(node));
        }
    }

    ahfl::incremental::IncrementalCompiler compiler(graph, cache, std::move(config));
    const auto results = compiler.compile_changed(changed_paths);
    const auto stats = compiler.stats();

    std::cout << "ahfl.incremental.v1\n";
    bool has_failure = false;
    for (const auto &result : results) {
        std::cout << "module " << result.module_path << ' ' << status_name(result.status) << '\n';
        if (result.status == ahfl::incremental::CompileStatus::Failed) {
            has_failure = true;
            if (!result.error_message.empty()) {
                std::cerr << result.module_path << ": " << result.error_message << '\n';
            }
        }
    }

    std::cout << "stats modules_checked " << stats.modules_checked << '\n'
              << "stats modules_recompiled " << stats.modules_recompiled << '\n'
              << "stats cache_hits " << stats.cache_hits << '\n'
              << "stats cache_misses " << stats.cache_misses << '\n'
              << "stats persistent_cache_hits " << stats.persistent_cache_hits << '\n'
              << "stats fingerprint_unchanged " << stats.fingerprint_unchanged << '\n';

    return has_failure ? 1 : 0;
}
