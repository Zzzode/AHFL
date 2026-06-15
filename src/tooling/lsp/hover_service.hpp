#pragma once

#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/hover_markup.hpp"
#include "tooling/lsp/protocol_types.hpp"

#include <optional>

namespace ahfl::lsp {

class HoverService {
  public:
    [[nodiscard]] std::optional<HoverPayload> payload_at(const LspAnalysisSnapshot &snapshot,
                                                         const LspSourceSnapshot &source,
                                                         Position position) const;

    [[nodiscard]] std::optional<Hover> hover_at(const LspAnalysisSnapshot &snapshot,
                                                const LspSourceSnapshot &source,
                                                Position position) const;

  private:
    HoverMarkdownRenderer renderer_;
};

} // namespace ahfl::lsp
