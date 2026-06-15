#include "tooling/lsp/hover_markup.hpp"

#include <string>

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

} // namespace

std::string HoverMarkdownRenderer::render(const HoverPayload &payload) const {
    std::string out;
    if (!payload.ahfl_signature.empty()) {
        out += "```ahfl\n";
        out += payload.ahfl_signature;
        out += "\n```";
    }

    append_section(out, payload.summary);

    if (!payload.canonical_name.empty()) {
        append_section(out, "canonical `" + payload.canonical_name + "`");
    }
    if (!payload.declared_spelling.empty()) {
        append_section(out, "declared as `" + payload.declared_spelling + "`");
    }
    if (!payload.module_name.empty()) {
        append_section(out, "module `" + payload.module_name + "`");
    }
    if (!payload.source_label.empty()) {
        append_section(out, "source `" + payload.source_label + "`");
    }

    for (const auto &fact : payload.facts) {
        append_section(out, fact);
    }
    for (const auto &diagnostic : payload.diagnostics) {
        append_section(out, "diagnostic: " + diagnostic);
    }

    return out;
}

} // namespace ahfl::lsp
