#pragma once

#include <filesystem>
#include <initializer_list>
#include <string_view>

namespace ahfl::formal {

/// Path to a solver binary shipped alongside the running executable, or an
/// empty path if none of the candidates exist in that directory.
///
/// RFC 0028: the default AHFL install layout ships the model checker in the
/// same bin/ directory as ahflc, so a default installation always has a
/// solver without PATH or environment-variable configuration. Candidates are
/// tried in order (e.g. nuXmv before NuSMV).
[[nodiscard]] std::filesystem::path
bundled_solver_path(std::initializer_list<std::string_view> candidates);

} // namespace ahfl::formal
