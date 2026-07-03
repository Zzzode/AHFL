#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

namespace ahfl {

struct ProjectInput {
    struct ModuleRoot {
        std::string prefix;
        std::filesystem::path root;
        std::vector<std::string> exported_modules;
        std::vector<std::string> dependency_prefixes;
        std::optional<std::vector<std::string>> compiler_intrinsics_allow;
    };

    std::vector<std::filesystem::path> entry_files{};
    std::vector<std::filesystem::path> search_roots{};
    std::vector<ModuleRoot> module_roots{};
    bool inject_prelude{false};
    std::unordered_map<std::string, std::string> source_overlays{};
    bool enforce_package_dependencies{false};
};

struct ProjectParseResult {
    SourceGraph graph;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProjectParseResult parse_project(const Frontend &frontend, const ProjectInput &input);

} // namespace ahfl
