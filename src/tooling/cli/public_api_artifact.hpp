#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::cli {

struct PublicApiPackageContext {
    std::string name;
    std::string version;
    std::string module_prefix;
    std::filesystem::path package_root;
    std::filesystem::path manifest_path;
};

struct PublicApiSemVerGate {
    std::string from_version;
    std::string to_version;
};

[[nodiscard]] int emit_public_api_snapshot(const ahfl::SourceGraph &graph,
                                           const ahfl::ResolveResult &resolve_result,
                                           const ahfl::TypeCheckResult &type_check_result,
                                           const PublicApiPackageContext &package,
                                           std::ostream &out,
                                           std::ostream &err);

[[nodiscard]] int emit_public_api_docs(const ahfl::SourceGraph &graph,
                                       const ahfl::ResolveResult &resolve_result,
                                       const ahfl::TypeCheckResult &type_check_result,
                                       const PublicApiPackageContext &package,
                                       std::ostream &out,
                                       std::ostream &err);

[[nodiscard]] int emit_public_api_diff(const std::filesystem::path &old_snapshot,
                                       const std::filesystem::path &new_snapshot,
                                       std::optional<PublicApiSemVerGate> semver_gate,
                                       std::ostream &out,
                                       std::ostream &err);

[[nodiscard]] int
emit_public_api_diff_from_snapshots(std::string_view old_snapshot,
                                    std::string_view old_snapshot_label,
                                    std::string_view new_snapshot,
                                    std::string_view new_snapshot_label,
                                    std::optional<PublicApiSemVerGate> semver_gate,
                                    std::ostream &out,
                                    std::ostream &err);

} // namespace ahfl::cli
