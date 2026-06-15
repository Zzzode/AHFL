#pragma once

#include "ahfl/base/support/source.hpp"

#include <string>
#include <vector>

namespace ahfl::lsp {

struct HoverPayload {
    std::string ahfl_signature;
    std::string summary;
    std::string canonical_name;
    std::string declared_spelling;
    std::string module_name;
    std::string source_label;
    std::vector<std::string> facts;
    std::vector<std::string> diagnostics;
    SourceRange token_range;
};

class HoverMarkdownRenderer {
  public:
    [[nodiscard]] std::string render(const HoverPayload &payload) const;
};

} // namespace ahfl::lsp
