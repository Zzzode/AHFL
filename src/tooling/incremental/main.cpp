#include "tooling/incremental/dependency_graph.hpp"
#include "tooling/incremental/incremental_compiler.hpp"
#include "tooling/incremental/ir_cache.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void print_usage(std::ostream &out) {
    out << "Usage: ahfl-incremental [--help] <changed.ahfl>...\n\n"
        << "Runs the AHFL incremental compiler over the supplied changed source files.\n";
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

} // namespace

int main(int argc, char **argv) {
    if (argc <= 1) {
        print_usage(std::cerr);
        return 2;
    }

    std::vector<std::string> changed_paths;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            if (argc == 2) {
                print_usage(std::cout);
                return 0;
            }
            std::cerr << "error: --help cannot be combined with source files\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "unknown option: " << argument << '\n';
            print_usage(std::cerr);
            return 2;
        }
        changed_paths.push_back(argument);
    }

    ahfl::incremental::DependencyGraph graph;
    for (const auto &path : changed_paths) {
        ahfl::incremental::ModuleNode node;
        node.module_path = path;
        graph.add_module(std::move(node));
    }

    ahfl::incremental::IrCache cache;
    ahfl::incremental::IncrementalCompiler compiler(graph, cache);
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
              << "stats fingerprint_unchanged " << stats.fingerprint_unchanged << '\n';

    return has_failure ? 1 : 0;
}
