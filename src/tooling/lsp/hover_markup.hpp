#pragma once

#include "ahfl/base/support/source.hpp"
#include "tooling/lsp/protocol_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ahfl::lsp {

enum class HoverFactImportance {
    Primary,
    Secondary,
    Debug,
};

struct HoverFact {
    HoverFactImportance importance{HoverFactImportance::Primary};
    std::string label;
    std::string value;
};

enum class HoverDetailLevel {
    Compact,
    Standard,
    Debug,
};

struct HoverRenderOptions {
    HoverDetailLevel detail_level{HoverDetailLevel::Standard};
    bool show_source{false};
    std::size_t max_facts{3};
    MarkupKind markup_kind{MarkupKind::Markdown};
};

struct HoverPayload {
    std::string signature;
    std::string headline;
    std::vector<HoverFact> facts;
    std::vector<std::string> documentation;
    std::vector<std::string> diagnostics;
    std::string canonical_name;
    std::string module_name;
    std::string source_label;
    SourceRange token_range;
};

class HoverRenderer {
  public:
    [[nodiscard]] std::string render(const HoverPayload &payload,
                                     const HoverRenderOptions &options) const;
};

void add_hover_fact(HoverPayload &payload,
                    HoverFactImportance importance,
                    std::string label,
                    std::string value);

} // namespace ahfl::lsp
