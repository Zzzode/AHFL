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
        std::string prefix{};
        std::filesystem::path root{};
        std::vector<std::string> exported_modules{};
        std::vector<std::string> artifact_exports{};
        std::vector<std::string> dependency_prefixes{};
        std::optional<std::vector<std::string>> compiler_intrinsics_allow{};

        [[nodiscard]] friend bool operator==(const ModuleRoot &,
                                             const ModuleRoot &) noexcept = default;
    };

    std::vector<std::filesystem::path> entry_files{};
    std::vector<std::filesystem::path> search_roots{};
    std::vector<ModuleRoot> module_roots{};
    bool inject_prelude{false};
    std::unordered_map<std::string, std::string> source_overlays{};
    std::unordered_map<std::string, std::string> source_cache{};
    bool enforce_package_dependencies{false};
};

// One source file as the project parser sees it. `path` is the identity — the
// normalized path, matching the parser's own `normalize_path` key (index/key
// discipline; the display string is derived, never the key). The other two
// fields are exactly the two facts the parser reads from the filesystem:
//
//   text            the frozen bytes; nullopt = the path exists but its text
//                   could not be read (the parser's "failed to open source
//                   file" diagnostic, reproduced on load).
//   exists_on_disk  whether the parser's own `std::filesystem::exists` gate
//                   would pass for this path. True for disk files, false for an
//                   overlay / cached text whose path is not on disk — the direct
//                   pipeline never lets such a path satisfy import resolution,
//                   so preserving the distinction keeps the two routes identical.
struct ProjectSource {
    std::filesystem::path path;
    std::optional<std::string> text;
    bool exists_on_disk = false;

    [[nodiscard]] friend bool operator==(const ProjectSource &,
                                         const ProjectSource &) noexcept = default;
};

// Value-semantics form of a project to parse: the *resolved* configuration (the
// effective search / module roots and entry files, in the normal form the parser
// itself computes) plus the complete text snapshot of every source the parser
// can reach. This is what makes a project a query input. `parse_project` over a
// model is a pure function of the model, so two equal models share one parse —
// no owned-AST comparison (SourceUnit holds an Owned<ast::Program>, so a
// SourceGraph is neither copyable nor comparable) and no filesystem probing on
// the parse path. Overlay / cached texts enter as sources: they are inputs, not
// side effects of the filesystem.
//
// `resolve_project_input` is the one impure boundary: it reads the disk to
// discover the reachable sources and freezes them into this value.
struct ProjectInputModel {
    std::vector<std::filesystem::path> entry_files{};
    std::vector<std::filesystem::path> search_roots{};
    std::vector<ProjectInput::ModuleRoot> module_roots{};
    bool inject_prelude{false};
    bool enforce_package_dependencies{false};
    // Every source the parser may load, sorted by native path string so equality
    // is independent of discovery order.
    std::vector<ProjectSource> sources{};

    [[nodiscard]] friend bool operator==(const ProjectInputModel &,
                                         const ProjectInputModel &) noexcept = default;
};

struct ProjectParseResult {
    SourceGraph graph;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Freeze a ProjectInput into a value-semantics model by reading the disk: the
// entry files, every overlay / cached text, and every `.ahfl` file under the
// effective search and module roots. Roots that do not exist are skipped (the
// parser reports unresolvable imports itself, with source ranges); this function
// only builds the input, so it must not fail the compile.
[[nodiscard]] ProjectInputModel resolve_project_input(const ProjectInput &input);

[[nodiscard]] ProjectParseResult parse_project(const Frontend &frontend, const ProjectInput &input);

// The query path: a deterministic parse over the frozen model. Byte-equivalent
// to the ProjectInput overload for the input that model was resolved from (guarded
// by ahfl.query.frontend_equiv_all).
[[nodiscard]] ProjectParseResult parse_project(const Frontend &frontend,
                                               const ProjectInputModel &model);

} // namespace ahfl
