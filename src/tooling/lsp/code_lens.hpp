#pragma once

#include <string>
#include <vector>

#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

[[nodiscard]] std::vector<CodeLens> compute_code_lens(const std::string &source);

} // namespace ahfl::lsp
