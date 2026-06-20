#pragma once

#include "tooling/lsp/protocol_types.hpp"

#include <string>
#include <vector>

namespace ahfl::lsp {

/**
 * Compute selection ranges for each given position.
 *
 * For every position, a chain of progressively larger selection ranges is
 * returned, linked through the `parent` pointer. The levels from innermost
 * to outermost are:
 *   1. Identifier  - the word (identifier-like token) containing the position
 *   2. Expression  - the smallest parenthesized/bracketed expression enclosing the position
 *   3. Line        - the full line containing the position
 *   4. Block       - the enclosing braced block (`{ ... }`)
 *   5. Function    - the enclosing top-level declaration (capability, predicate, agent, etc.)
 *   6. File        - the entire source file
 *
 * Levels that cannot be determined are skipped.
 */
[[nodiscard]] std::vector<SelectionRange> compute_selection_ranges(
    const std::string &source,
    const std::vector<Position> &positions);

} // namespace ahfl::lsp
