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
