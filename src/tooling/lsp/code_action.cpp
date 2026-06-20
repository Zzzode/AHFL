#include "tooling/lsp/code_action.hpp"

#include "tooling/formatter/formatter.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::lsp {

namespace {

[[nodiscard]] Position end_of_document(const std::string &source) {
    if (source.empty()) {
        return Position{0, 0};
    }

    uint32_t line = 0;
    uint32_t last_line_start = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++line;
            last_line_start = static_cast<uint32_t>(i + 1);
        }
    }
    const uint32_t character = static_cast<uint32_t>(source.size() - last_line_start);
    return Position{line, character};
}

[[nodiscard]] bool range_intersects_imports(const std::string &source, const Range &range) {
    // Simple heuristic: check if the range starts within the first section of the file
    // where imports typically live (before the first non-import, non-comment top-level item).
    // For simplicity, we always offer organize imports as a source-level action.
    (void)source;
    (void)range;
    return true;
}

[[nodiscard]] std::optional<CodeAction> make_organize_imports_action(const std::string &source) {
    formatter::FormatOptions options;
    options.sort_imports = true;

    auto result = formatter::format_source(source, options);
    if (!result.success || result.formatted == source) {
        return std::nullopt;
    }

    TextEdit full_edit;
    full_edit.range.start = Position{0, 0};
    full_edit.range.end = end_of_document(source);
    full_edit.new_text = result.formatted;

    WorkspaceEdit edit;
    // Caller must replace the empty key with the actual document URI.
    edit.changes.emplace("", std::vector<TextEdit>{std::move(full_edit)});

    CodeAction action;
    action.title = "Organize Imports";
    action.kind = CodeActionKind::SourceOrganizeImports;
    action.is_preferred = false;
    action.edit = std::move(edit);

    return action;
}

} // namespace

std::vector<CodeAction> compute_code_actions(const std::string &source,
                                             const Range &range,
                                             const std::vector<LspDiagnostic> &diagnostics) {
    std::vector<CodeAction> actions;

    if (range_intersects_imports(source, range)) {
        if (auto organize = make_organize_imports_action(source)) {
            actions.push_back(std::move(*organize));
        }
    }

    // TODO: add diagnostic-based quick fixes (e.g. spelling suggestions)
    (void)diagnostics;

    return actions;
}

} // namespace ahfl::lsp
