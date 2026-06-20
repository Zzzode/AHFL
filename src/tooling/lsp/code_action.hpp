#pragma once

#include "tooling/lsp/protocol_types.hpp"

#include <string>
#include <vector>

namespace ahfl::lsp {

/// Compute code actions available for the given source, range, and diagnostics.
///
/// @param source The full source text of the document.
/// @param range The selection range to compute actions for.
/// @param diagnostics Diagnostics associated with the document.
/// @return A list of code actions. The WorkspaceEdit entries use an empty string
///         as the URI key; the caller must replace it with the actual document URI.
[[nodiscard]] std::vector<CodeAction> compute_code_actions(
    const std::string &source,
    const Range &range,
    const std::vector<LspDiagnostic> &diagnostics);

} // namespace ahfl::lsp
