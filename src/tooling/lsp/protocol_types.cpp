#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

namespace {

[[nodiscard]] const char *markup_kind_name(MarkupKind kind) noexcept {
    switch (kind) {
    case MarkupKind::Markdown:
        return "markdown";
    case MarkupKind::Plaintext:
        return "plaintext";
    }
    return "markdown";
}

} // namespace

// ---------- Serialization ----------

std::unique_ptr<json::JsonValue> serialize_position(const Position &pos) {
    auto obj = json::JsonValue::make_object();
    obj->set("line", json::JsonValue::make_int(static_cast<int64_t>(pos.line)));
    obj->set("character", json::JsonValue::make_int(static_cast<int64_t>(pos.character)));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_range(const Range &range) {
    auto obj = json::JsonValue::make_object();
    obj->set("start", serialize_position(range.start));
    obj->set("end", serialize_position(range.end));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_location(const Location &loc) {
    auto obj = json::JsonValue::make_object();
    obj->set("uri", json::JsonValue::make_string(loc.uri));
    obj->set("range", serialize_range(loc.range));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_diagnostic(const LspDiagnostic &diag) {
    auto obj = json::JsonValue::make_object();
    obj->set("range", serialize_range(diag.range));
    obj->set("severity", json::JsonValue::make_int(static_cast<int64_t>(diag.severity)));
    obj->set("source", json::JsonValue::make_string(diag.source));
    obj->set("message", json::JsonValue::make_string(diag.message));
    if (!diag.code.empty()) {
        obj->set("code", json::JsonValue::make_string(diag.code));
    }
    if (!diag.related_information.empty()) {
        auto related = json::JsonValue::make_array();
        for (const auto &item : diag.related_information) {
            auto related_item = json::JsonValue::make_object();
            related_item->set("location", serialize_location(item.location));
            related_item->set("message", json::JsonValue::make_string(item.message));
            related->push(std::move(related_item));
        }
        obj->set("relatedInformation", std::move(related));
    }
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_completion_item(const CompletionItem &item) {
    auto obj = json::JsonValue::make_object();
    obj->set("label", json::JsonValue::make_string(item.label));
    obj->set("kind", json::JsonValue::make_int(static_cast<int64_t>(item.kind)));
    if (!item.detail.empty()) {
        obj->set("detail", json::JsonValue::make_string(item.detail));
    }
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_hover(const Hover &hover) {
    auto obj = json::JsonValue::make_object();

    auto contents = json::JsonValue::make_object();
    contents->set("kind", json::JsonValue::make_string(markup_kind_name(hover.contents_kind)));
    contents->set("value", json::JsonValue::make_string(hover.contents));
    obj->set("contents", std::move(contents));

    if (hover.range.has_value()) {
        obj->set("range", serialize_range(*hover.range));
    }
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_server_capabilities(const ServerCapabilities &caps) {
    auto obj = json::JsonValue::make_object();

    // textDocumentSync: 1 = Full
    if (caps.text_document_sync_full) {
        obj->set("textDocumentSync", json::JsonValue::make_int(1));
    }

    if (caps.completion_provider) {
        auto comp = json::JsonValue::make_object();
        auto triggers = json::JsonValue::make_array();
        triggers->push(json::JsonValue::make_string("."));
        triggers->push(json::JsonValue::make_string(":"));
        comp->set("triggerCharacters", std::move(triggers));
        obj->set("completionProvider", std::move(comp));
    }

    if (caps.definition_provider) {
        obj->set("definitionProvider", json::JsonValue::make_bool(true));
    }

    if (caps.hover_provider) {
        obj->set("hoverProvider", json::JsonValue::make_bool(true));
    }

    if (caps.references_provider) {
        obj->set("referencesProvider", json::JsonValue::make_bool(true));
    }

    if (caps.rename_provider) {
        auto rename = json::JsonValue::make_object();
        if (caps.prepare_rename_provider) {
            rename->set("prepareProvider", json::JsonValue::make_bool(true));
        }
        obj->set("renameProvider", std::move(rename));
    }

    if (caps.diagnostic_provider) {
        auto diagnostic = json::JsonValue::make_object();
        if (!caps.diagnostic_id.empty()) {
            diagnostic->set("id", json::JsonValue::make_string(caps.diagnostic_id));
        }
        diagnostic->set("interFileDependencies", json::JsonValue::make_bool(true));
        diagnostic->set("workspaceDiagnostics", json::JsonValue::make_bool(true));
        obj->set("diagnosticProvider", std::move(diagnostic));
    }

    if (caps.document_symbol_provider) {
        obj->set("documentSymbolProvider", json::JsonValue::make_bool(true));
    }

    if (caps.workspace_symbol_provider) {
        obj->set("workspaceSymbolProvider", json::JsonValue::make_bool(true));
    }

    if (caps.workspace_folders_provider) {
        auto workspace = json::JsonValue::make_object();
        auto workspace_folders = json::JsonValue::make_object();
        workspace_folders->set("supported", json::JsonValue::make_bool(true));
        workspace_folders->set("changeNotifications", json::JsonValue::make_bool(true));
        workspace->set("workspaceFolders", std::move(workspace_folders));
        obj->set("workspace", std::move(workspace));
    }

    if (caps.signature_help_provider) {
        auto sig = json::JsonValue::make_object();
        auto triggers = json::JsonValue::make_array();
        triggers->push(json::JsonValue::make_string("("));
        triggers->push(json::JsonValue::make_string(","));
        sig->set("triggerCharacters", std::move(triggers));
        obj->set("signatureHelpProvider", std::move(sig));
    }

    if (caps.document_formatting_provider) {
        obj->set("documentFormattingProvider", json::JsonValue::make_bool(true));
    }
    if (caps.document_range_formatting_provider) {
        obj->set("documentRangeFormattingProvider", json::JsonValue::make_bool(true));
    }

    if (caps.semantic_tokens_provider) {
        auto st = json::JsonValue::make_object();
        auto legend = json::JsonValue::make_object();
        auto token_types = json::JsonValue::make_array();
        token_types->push(json::JsonValue::make_string("namespace"));
        token_types->push(json::JsonValue::make_string("type"));
        token_types->push(json::JsonValue::make_string("class"));
        token_types->push(json::JsonValue::make_string("enum"));
        token_types->push(json::JsonValue::make_string("interface"));
        token_types->push(json::JsonValue::make_string("struct"));
        token_types->push(json::JsonValue::make_string("typeParameter"));
        token_types->push(json::JsonValue::make_string("parameter"));
        token_types->push(json::JsonValue::make_string("variable"));
        token_types->push(json::JsonValue::make_string("property"));
        token_types->push(json::JsonValue::make_string("enumMember"));
        token_types->push(json::JsonValue::make_string("event"));
        token_types->push(json::JsonValue::make_string("function"));
        token_types->push(json::JsonValue::make_string("method"));
        token_types->push(json::JsonValue::make_string("macro"));
        token_types->push(json::JsonValue::make_string("keyword"));
        token_types->push(json::JsonValue::make_string("modifier"));
        token_types->push(json::JsonValue::make_string("comment"));
        token_types->push(json::JsonValue::make_string("string"));
        token_types->push(json::JsonValue::make_string("number"));
        token_types->push(json::JsonValue::make_string("regexp"));
        token_types->push(json::JsonValue::make_string("operator"));
        token_types->push(json::JsonValue::make_string("decorator"));
        legend->set("tokenTypes", std::move(token_types));
        auto token_modifiers = json::JsonValue::make_array();
        token_modifiers->push(json::JsonValue::make_string("declaration"));
        token_modifiers->push(json::JsonValue::make_string("definition"));
        token_modifiers->push(json::JsonValue::make_string("readonly"));
        token_modifiers->push(json::JsonValue::make_string("static"));
        token_modifiers->push(json::JsonValue::make_string("deprecated"));
        token_modifiers->push(json::JsonValue::make_string("abstract"));
        token_modifiers->push(json::JsonValue::make_string("async"));
        token_modifiers->push(json::JsonValue::make_string("modification"));
        token_modifiers->push(json::JsonValue::make_string("documentation"));
        token_modifiers->push(json::JsonValue::make_string("defaultLibrary"));
        legend->set("tokenModifiers", std::move(token_modifiers));
        st->set("legend", std::move(legend));
        st->set("full", json::JsonValue::make_bool(true));
        st->set("range", json::JsonValue::make_bool(false));
        obj->set("semanticTokensProvider", std::move(st));
    }

    if (caps.code_action_provider) {
        auto ca = json::JsonValue::make_object();
        ca->set("codeActionLiteralSupport", json::JsonValue::make_bool(true));
        obj->set("codeActionProvider", std::move(ca));
    }

    if (caps.folding_range_provider) {
        obj->set("foldingRangeProvider", json::JsonValue::make_bool(true));
    }

    if (caps.document_highlight_provider) {
        obj->set("documentHighlightProvider", json::JsonValue::make_bool(true));
    }

    if (caps.selection_range_provider) {
        obj->set("selectionRangeProvider", json::JsonValue::make_bool(true));
    }

    if (caps.code_lens_provider) {
        auto cl = json::JsonValue::make_object();
        cl->set("resolveProvider", json::JsonValue::make_bool(false));
        obj->set("codeLensProvider", std::move(cl));
    }

    return obj;
}

std::unique_ptr<json::JsonValue> serialize_document_symbol(const DocumentSymbol &sym) {
    auto obj = json::JsonValue::make_object();
    obj->set("name", json::JsonValue::make_string(sym.name));
    obj->set("kind", json::JsonValue::make_int(static_cast<int64_t>(sym.kind)));
    obj->set("range", serialize_range(sym.range));
    obj->set("selectionRange", serialize_range(sym.selection_range));
    if (!sym.children.empty()) {
        auto children = json::JsonValue::make_array();
        for (const auto &child : sym.children) {
            children->push(serialize_document_symbol(child));
        }
        obj->set("children", std::move(children));
    }
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_symbol_information(const SymbolInformation &sym) {
    auto obj = json::JsonValue::make_object();
    obj->set("name", json::JsonValue::make_string(sym.name));
    obj->set("kind", json::JsonValue::make_int(static_cast<int64_t>(sym.kind)));
    obj->set("location", serialize_location(sym.location));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_text_edit(const TextEdit &edit) {
    auto obj = json::JsonValue::make_object();
    obj->set("range", serialize_range(edit.range));
    obj->set("newText", json::JsonValue::make_string(edit.new_text));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_workspace_edit(const WorkspaceEdit &edit) {
    auto obj = json::JsonValue::make_object();
    auto changes = json::JsonValue::make_object();
    for (const auto &[uri, edits] : edit.changes) {
        auto arr = json::JsonValue::make_array();
        for (const auto &e : edits) {
            arr->push(serialize_text_edit(e));
        }
        changes->set(uri, std::move(arr));
    }
    obj->set("changes", std::move(changes));
    return obj;
}

std::unique_ptr<json::JsonValue>
serialize_parameter_information(const ParameterInformation &param) {
    auto obj = json::JsonValue::make_object();
    obj->set("label", json::JsonValue::make_string(param.label));
    if (!param.documentation.empty()) {
        obj->set("documentation", json::JsonValue::make_string(param.documentation));
    }
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_signature_information(const SignatureInformation &sig) {
    auto obj = json::JsonValue::make_object();
    obj->set("label", json::JsonValue::make_string(sig.label));
    if (!sig.documentation.empty()) {
        obj->set("documentation", json::JsonValue::make_string(sig.documentation));
    }
    auto params = json::JsonValue::make_array();
    for (const auto &p : sig.parameters) {
        params->push(serialize_parameter_information(p));
    }
    obj->set("parameters", std::move(params));
    return obj;
}

std::unique_ptr<json::JsonValue> serialize_signature_help(const SignatureHelp &help) {
    auto obj = json::JsonValue::make_object();
    auto sigs = json::JsonValue::make_array();
    for (const auto &s : help.signatures) {
        sigs->push(serialize_signature_information(s));
    }
    obj->set("signatures", std::move(sigs));
    obj->set("activeSignature",
             json::JsonValue::make_int(static_cast<int64_t>(help.active_signature)));
    obj->set("activeParameter",
             json::JsonValue::make_int(static_cast<int64_t>(help.active_parameter)));
    return obj;
}

// ---------- Semantic Tokens ----------

std::unique_ptr<json::JsonValue> serialize_semantic_tokens(const SemanticTokens &tokens) {
    auto obj = json::JsonValue::make_object();
    if (!tokens.result_id.empty()) {
        obj->set("resultId", json::JsonValue::make_string(tokens.result_id));
    }
    auto data = json::JsonValue::make_array();
    for (const auto &t : tokens.data) {
        data->push(json::JsonValue::make_int(static_cast<int64_t>(t.delta_line)));
        data->push(json::JsonValue::make_int(static_cast<int64_t>(t.delta_start)));
        data->push(json::JsonValue::make_int(static_cast<int64_t>(t.length)));
        data->push(json::JsonValue::make_int(static_cast<int64_t>(t.token_type)));
        data->push(json::JsonValue::make_int(static_cast<int64_t>(t.token_modifiers)));
    }
    obj->set("data", std::move(data));
    return obj;
}

// ---------- Code Action ----------

namespace {
const char *code_action_kind_name(CodeActionKind kind) noexcept {
    switch (kind) {
    case CodeActionKind::QuickFix:
        return "quickfix";
    case CodeActionKind::Refactor:
        return "refactor";
    case CodeActionKind::RefactorExtract:
        return "refactor.extract";
    case CodeActionKind::RefactorInline:
        return "refactor.inline";
    case CodeActionKind::RefactorRewrite:
        return "refactor.rewrite";
    case CodeActionKind::Source:
        return "source";
    case CodeActionKind::SourceOrganizeImports:
        return "source.organizeImports";
    case CodeActionKind::SourceFixAll:
        return "source.fixAll";
    }
    return "";
}
} // namespace

std::unique_ptr<json::JsonValue> serialize_code_action(const CodeAction &action) {
    auto obj = json::JsonValue::make_object();
    obj->set("title", json::JsonValue::make_string(action.title));
    if (action.kind) {
        obj->set("kind", json::JsonValue::make_string(code_action_kind_name(*action.kind)));
    }
    if (!action.diagnostics.empty()) {
        auto diags = json::JsonValue::make_array();
        for (const auto &d : action.diagnostics) {
            diags->push(serialize_diagnostic(d));
        }
        obj->set("diagnostics", std::move(diags));
    }
    if (action.is_preferred) {
        obj->set("isPreferred", json::JsonValue::make_bool(true));
    }
    if (action.edit) {
        obj->set("edit", serialize_workspace_edit(*action.edit));
    }
    if (action.command) {
        obj->set("command", json::JsonValue::make_string(*action.command));
    }
    return obj;
}

// ---------- Folding Range ----------

namespace {
const char *folding_range_kind_name(FoldingRangeKind kind) noexcept {
    switch (kind) {
    case FoldingRangeKind::Comment:
        return "comment";
    case FoldingRangeKind::Imports:
        return "imports";
    case FoldingRangeKind::Region:
        return "region";
    }
    return "";
}
} // namespace

std::unique_ptr<json::JsonValue> serialize_folding_range(const FoldingRange &range) {
    auto obj = json::JsonValue::make_object();
    obj->set("startLine", json::JsonValue::make_int(static_cast<int64_t>(range.start_line)));
    if (range.start_character > 0) {
        obj->set("startCharacter",
                 json::JsonValue::make_int(static_cast<int64_t>(range.start_character)));
    }
    obj->set("endLine", json::JsonValue::make_int(static_cast<int64_t>(range.end_line)));
    if (range.end_character > 0) {
        obj->set("endCharacter",
                 json::JsonValue::make_int(static_cast<int64_t>(range.end_character)));
    }
    if (range.kind) {
        obj->set("kind", json::JsonValue::make_string(folding_range_kind_name(*range.kind)));
    }
    return obj;
}

// ---------- Document Highlight ----------

std::unique_ptr<json::JsonValue> serialize_document_highlight(const DocumentHighlight &hl) {
    auto obj = json::JsonValue::make_object();
    obj->set("range", serialize_range(hl.range));
    obj->set("kind", json::JsonValue::make_int(static_cast<int64_t>(hl.kind)));
    return obj;
}

// ---------- Selection Range ----------

std::unique_ptr<json::JsonValue> serialize_selection_range(const SelectionRange &range) {
    auto obj = json::JsonValue::make_object();
    obj->set("range", serialize_range(range.range));
    if (range.parent) {
        obj->set("parent", serialize_selection_range(*range.parent));
    }
    return obj;
}

// ---------- CodeLens ----------

std::unique_ptr<json::JsonValue> serialize_code_lens(const CodeLens &lens) {
    auto obj = json::JsonValue::make_object();
    obj->set("range", serialize_range(lens.range));
    if (lens.command) {
        auto cmd = json::JsonValue::make_object();
        if (lens.command_title) {
            cmd->set("title", json::JsonValue::make_string(*lens.command_title));
        }
        cmd->set("command", json::JsonValue::make_string(*lens.command));
        if (!lens.command_arguments.empty()) {
            auto args = json::JsonValue::make_array();
            for (const auto &arg : lens.command_arguments) {
                args->push(json::JsonValue::make_string(arg));
            }
            cmd->set("arguments", std::move(args));
        }
        obj->set("command", std::move(cmd));
    }
    if (lens.data) {
        obj->set("data", json::JsonValue::make_string(*lens.data));
    }
    return obj;
}

// ---------- Deserialization ----------

Position parse_position(const json::JsonValue &v) {
    Position pos;
    if (auto line = v.get("line"); line != nullptr) {
        if (auto i = line->as_int(); i.has_value()) {
            pos.line = static_cast<uint32_t>(*i);
        }
    }
    if (auto ch = v.get("character"); ch != nullptr) {
        if (auto i = ch->as_int(); i.has_value()) {
            pos.character = static_cast<uint32_t>(*i);
        }
    }
    return pos;
}

Range parse_range(const json::JsonValue &v) {
    Range range;
    if (auto s = v.get("start"); s != nullptr) {
        range.start = parse_position(*s);
    }
    if (auto e = v.get("end"); e != nullptr) {
        range.end = parse_position(*e);
    }
    return range;
}

TextDocumentItem parse_text_document_item(const json::JsonValue &v) {
    TextDocumentItem item;
    if (auto uri = v.get("uri"); uri != nullptr) {
        if (auto s = uri->as_string(); s.has_value()) {
            item.uri = std::string(*s);
        }
    }
    if (auto lang = v.get("languageId"); lang != nullptr) {
        if (auto s = lang->as_string(); s.has_value()) {
            item.language_id = std::string(*s);
        }
    }
    if (auto ver = v.get("version"); ver != nullptr) {
        if (auto i = ver->as_int(); i.has_value()) {
            item.version = static_cast<int>(*i);
        }
    }
    if (auto text = v.get("text"); text != nullptr) {
        if (auto s = text->as_string(); s.has_value()) {
            item.text = std::string(*s);
        }
    }
    return item;
}

TextDocumentIdentifier parse_text_document_identifier(const json::JsonValue &v) {
    TextDocumentIdentifier id;
    if (auto uri = v.get("uri"); uri != nullptr) {
        if (auto s = uri->as_string(); s.has_value()) {
            id.uri = std::string(*s);
        }
    }
    return id;
}

VersionedTextDocumentIdentifier parse_versioned_text_document_identifier(const json::JsonValue &v) {
    VersionedTextDocumentIdentifier id;
    if (auto uri = v.get("uri"); uri != nullptr) {
        if (auto s = uri->as_string(); s.has_value()) {
            id.uri = std::string(*s);
        }
    }
    if (auto ver = v.get("version"); ver != nullptr) {
        if (auto i = ver->as_int(); i.has_value()) {
            id.version = static_cast<int>(*i);
        }
    }
    return id;
}

} // namespace ahfl::lsp
