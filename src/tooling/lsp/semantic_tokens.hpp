#pragma once

#include <string>

#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

class AnalysisService;

/// Compute semantic tokens for the given document URI.
///
/// Walks the AST of the parsed document and generates semantic tokens
/// for declarations, parameters, local bindings, and type references.
/// Tokens are returned in delta-encoded form relative to the previous token.
[[nodiscard]] SemanticTokens compute_semantic_tokens(const std::string &source,
                                                     const AnalysisService &analysis);

} // namespace ahfl::lsp
