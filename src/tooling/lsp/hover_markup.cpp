#include "tooling/lsp/hover_markup.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::lsp {

namespace {

void append_section(std::string &out, const std::string &text) {
    if (text.empty()) {
        return;
    }
    if (!out.empty()) {
        out += "\n\n";
    }
    out += text;
}

[[nodiscard]] bool show_primary_facts(const HoverRenderOptions &options) noexcept {
    return options.detail_level == HoverDetailLevel::Standard ||
           options.detail_level == HoverDetailLevel::Debug;
}

[[nodiscard]] bool show_debug_details(const HoverRenderOptions &options) noexcept {
    return options.detail_level == HoverDetailLevel::Debug;
}

[[nodiscard]] bool fact_visible(const HoverFact &fact, const HoverRenderOptions &options) noexcept {
    switch (fact.importance) {
    case HoverFactImportance::Primary:
        return show_primary_facts(options);
    case HoverFactImportance::Secondary:
        return options.detail_level == HoverDetailLevel::Debug;
    case HoverFactImportance::Debug:
        return show_debug_details(options);
    }
    return false;
}

[[nodiscard]] std::string render_fact_markdown(const HoverFact &fact) {
    if (fact.label.empty()) {
        return "- " + fact.value;
    }
    if (fact.value.empty()) {
        return "- " + fact.label;
    }
    return "- " + fact.label + ": " + fact.value;
}

[[nodiscard]] std::string strip_markdown(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '`') {
            continue;
        }
        out += text[index];
    }
    return out;
}

[[nodiscard]] std::string render_fact_plaintext(const HoverFact &fact) {
    if (fact.label.empty()) {
        return "- " + strip_markdown(fact.value);
    }
    if (fact.value.empty()) {
        return "- " + strip_markdown(fact.label);
    }
    return "- " + strip_markdown(fact.label) + ": " + strip_markdown(fact.value);
}

[[nodiscard]] std::string
render_facts(const HoverPayload &payload, const HoverRenderOptions &options, bool markdown) {
    if (!show_primary_facts(options)) {
        return {};
    }

    std::string out;
    std::size_t shown = 0;
    const std::size_t max_facts =
        options.detail_level == HoverDetailLevel::Debug ? payload.facts.size() : options.max_facts;
    for (const auto &fact : payload.facts) {
        if (!fact_visible(fact, options) || shown >= max_facts) {
            continue;
        }
        if (!out.empty()) {
            out += '\n';
        }
        out += markdown ? render_fact_markdown(fact) : render_fact_plaintext(fact);
        ++shown;
    }
    return out;
}

[[nodiscard]] std::string render_debug_details_markdown(const HoverPayload &payload,
                                                        const HoverRenderOptions &options) {
    if (!show_debug_details(options)) {
        return {};
    }

    std::string details;
    if (!payload.canonical_name.empty()) {
        details = "Details: `" + payload.canonical_name + "`";
        if (!payload.module_name.empty()) {
            details += " in `" + payload.module_name + "`";
        }
    } else if (!payload.module_name.empty()) {
        details = "Module: `" + payload.module_name + "`";
    }

    if (options.show_source && !payload.source_label.empty()) {
        if (!details.empty()) {
            details += "\n";
        }
        details += "Source: `" + payload.source_label + "`";
    }

    return details;
}

[[nodiscard]] std::string render_debug_details_plaintext(const HoverPayload &payload,
                                                         const HoverRenderOptions &options) {
    if (!show_debug_details(options)) {
        return {};
    }

    std::string details;
    if (!payload.canonical_name.empty()) {
        details = "Details: " + payload.canonical_name;
        if (!payload.module_name.empty()) {
            details += " in " + payload.module_name;
        }
    } else if (!payload.module_name.empty()) {
        details = "Module: " + payload.module_name;
    }

    if (options.show_source && !payload.source_label.empty()) {
        if (!details.empty()) {
            details += "\n";
        }
        details += "Source: " + payload.source_label;
    }

    return details;
}

[[nodiscard]] std::string render_diagnostics(const HoverPayload &payload, bool markdown) {
    std::string out;
    for (const auto &diagnostic : payload.diagnostics) {
        if (!out.empty()) {
            out += '\n';
        }
        out += markdown ? "- Diagnostic: " + diagnostic : "- Diagnostic: " + diagnostic;
    }
    return out;
}

[[nodiscard]] std::string render_markdown(const HoverPayload &payload,
                                          const HoverRenderOptions &options) {
    std::string out;
    if (!payload.signature.empty()) {
        out += "```ahfl\n";
        out += payload.signature;
        out += "\n```";
    }

    append_section(out, payload.headline);
    append_section(out, render_facts(payload, options, true));

    if (!payload.documentation.empty() && options.detail_level != HoverDetailLevel::Compact) {
        append_section(out, payload.documentation.front());
    }

    append_section(out, render_diagnostics(payload, true));
    append_section(out, render_debug_details_markdown(payload, options));
    return out;
}

[[nodiscard]] std::string render_plaintext(const HoverPayload &payload,
                                           const HoverRenderOptions &options) {
    std::string out;
    append_section(out, payload.signature);
    append_section(out, strip_markdown(payload.headline));
    append_section(out, render_facts(payload, options, false));

    if (!payload.documentation.empty() && options.detail_level != HoverDetailLevel::Compact) {
        append_section(out, strip_markdown(payload.documentation.front()));
    }

    append_section(out, render_diagnostics(payload, false));
    append_section(out, render_debug_details_plaintext(payload, options));
    return out;
}

} // namespace

std::string HoverRenderer::render(const HoverPayload &payload,
                                  const HoverRenderOptions &options) const {
    if (options.markup_kind == MarkupKind::Plaintext) {
        return render_plaintext(payload, options);
    }
    return render_markdown(payload, options);
}

void add_hover_fact(HoverPayload &payload,
                    HoverFactImportance importance,
                    std::string label,
                    std::string value) {
    if (label.empty() && value.empty()) {
        return;
    }
    payload.facts.push_back(HoverFact{
        .importance = importance,
        .label = std::move(label),
        .value = std::move(value),
    });
}

} // namespace ahfl::lsp
