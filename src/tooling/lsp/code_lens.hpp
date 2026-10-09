#pragma once

#include <string>
#include <vector>

#include "tooling/lsp/protocol_types.hpp"
#include "tooling/lsp/workspace_index.hpp"

namespace ahfl::lsp {

[[nodiscard]] std::vector<CodeLens> compute_code_lens(const std::string &source,
                                                      std::string_view uri);
[[nodiscard]] std::vector<CodeLens> compute_code_lens(const LspWorkspaceIndex &index,
                                                      std::string_view uri);

} // namespace ahfl::lsp
