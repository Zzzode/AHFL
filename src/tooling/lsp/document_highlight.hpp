#pragma once

#include <string>
#include <vector>

#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

/// Compute document highlights for the identifier at the given position.
///
/// Finds the word (identifier) under the cursor and returns all occurrences
/// of that word throughout the source document. All highlights use the
/// DocumentHighlightKind::Text kind.
[[nodiscard]] std::vector<DocumentHighlight> compute_document_highlights(const std::string &source,
                                                                         const Position &position);

} // namespace ahfl::lsp
