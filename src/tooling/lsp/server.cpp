#include "tooling/lsp/server.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <variant>

namespace ahfl::lsp {

namespace {

/// AHFL keywords for completion.
constexpr std::string_view kKeywords[] = {
    "agent",   "workflow", "flow",     "contract",   "capability", "predicate",
    "struct",  "enum",     "const",    "import",     "module",     "state",
    "handler", "on",       "emit",     "transition", "always",     "eventually",
    "until",   "implies",  "requires", "ensures",    "invariant",  "after",
    "true",    "false",    "if",       "else",       "match",      "return",
};

LspSymbolKind to_lsp_symbol_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
        return LspSymbolKind::Struct;
    case SymbolKind::Enum:
        return LspSymbolKind::Enum;
    case SymbolKind::TypeAlias:
        return LspSymbolKind::Class;
    case SymbolKind::Const:
        return LspSymbolKind::Constant;
    case SymbolKind::Capability:
        return LspSymbolKind::Interface;
    case SymbolKind::Predicate:
        return LspSymbolKind::Function;
    case SymbolKind::Agent:
        return LspSymbolKind::Class;
    case SymbolKind::Workflow:
        return LspSymbolKind::Function;
    }
    return LspSymbolKind::Variable;
}

} // anonymous namespace

LspServer::LspServer(std::istream &in, std::ostream &out) : transport_(in, out) {}

void LspServer::run() {
    while (true) {
        auto msg = transport_.read_message();
        if (!msg.has_value()) {
            break; // EOF
        }

        std::visit(
            [this](auto &m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, JsonRpcRequest>) {
                    handle_request(m);
                } else {
                    handle_notification(m);
                }
            },
            *msg);

        if (shutdown_requested_) {
            break;
        }
    }
}

void LspServer::handle_request(const JsonRpcRequest &req) {
    if (req.method == "initialize") {
        handle_initialize(req);
    } else if (req.method == "shutdown") {
        handle_shutdown(req);
    } else if (req.method == "textDocument/completion") {
        handle_completion(req);
    } else if (req.method == "textDocument/definition") {
        handle_definition(req);
    } else if (req.method == "textDocument/hover") {
        handle_hover(req);
    } else if (req.method == "textDocument/references") {
        handle_references(req);
    } else if (req.method == "textDocument/rename") {
        handle_rename(req);
    } else if (req.method == "textDocument/documentSymbol") {
        handle_document_symbol(req);
    } else if (req.method == "workspace/symbol") {
        handle_workspace_symbol(req);
    } else {
        // Method not found
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.error = JsonRpcError{kMethodNotFound, "Method not found: " + req.method};
        transport_.send_response(resp);
    }
}

void LspServer::handle_notification(const JsonRpcNotification &notif) {
    if (notif.method == "initialized") {
        handle_initialized();
    } else if (notif.method == "textDocument/didOpen") {
        if (notif.params) {
            handle_did_open(*notif.params);
        }
    } else if (notif.method == "textDocument/didChange") {
        if (notif.params) {
            handle_did_change(*notif.params);
        }
    } else if (notif.method == "textDocument/didClose") {
        if (notif.params) {
            handle_did_close(*notif.params);
        }
    } else if (notif.method == "exit") {
        handle_exit();
    }
    // Ignore unknown notifications per LSP spec
}

void LspServer::handle_initialize(const JsonRpcRequest &req) {
    initialized_ = true;

    ServerCapabilities caps;
    auto result = json::JsonValue::make_object();
    result->set("capabilities", serialize_server_capabilities(caps));

    auto info = json::JsonValue::make_object();
    info->set("name", json::JsonValue::make_string("ahfl-lsp"));
    info->set("version", json::JsonValue::make_string("0.1.0"));
    result->set("serverInfo", std::move(info));

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_shutdown(const JsonRpcRequest &req) {
    shutdown_requested_ = true;

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = json::JsonValue::make_null();
    transport_.send_response(resp);
}

void LspServer::handle_initialized() {
    // Nothing to do — server is ready
}

void LspServer::handle_did_open(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    auto item = parse_text_document_item(*td);
    store_.open(item);
    publish_diagnostics(item.uri);
}

void LspServer::handle_did_change(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    auto versioned = parse_versioned_text_document_identifier(*td);

    const auto *changes = params.get("contentChanges");
    if (changes == nullptr || !changes->is_array() || changes->array_items.empty()) {
        return;
    }

    // Full document sync — take last content change
    const auto &last_change = *changes->array_items.back();
    auto text_opt = last_change.get("text");
    if (text_opt == nullptr) {
        return;
    }
    auto text_sv = text_opt->as_string();
    if (!text_sv.has_value()) {
        return;
    }

    store_.change(versioned.uri, versioned.version, std::string(*text_sv));
    publish_diagnostics(versioned.uri);
}

void LspServer::handle_did_close(const json::JsonValue &params) {
    const auto *td = params.get("textDocument");
    if (td == nullptr) {
        return;
    }

    auto id = parse_text_document_identifier(*td);
    store_.close(id.uri);

    // Clear diagnostics for closed file
    auto diag_params = json::JsonValue::make_object();
    diag_params->set("uri", json::JsonValue::make_string(id.uri));
    diag_params->set("diagnostics", json::JsonValue::make_array());
    transport_.send_notification("textDocument/publishDiagnostics", std::move(diag_params));
}

void LspServer::handle_exit() {
    shutdown_requested_ = true;
}

void LspServer::handle_completion(const JsonRpcRequest &req) {
    std::vector<CompletionItem> items;

    // Keyword completions
    for (auto kw : kKeywords) {
        CompletionItem item;
        item.label = std::string(kw);
        item.kind = CompletionItemKind::Keyword;
        items.push_back(std::move(item));
    }

    // Symbol completions from current document
    if (req.params) {
        const auto *td = req.params->get("textDocument");
        if (td != nullptr) {
            auto id = parse_text_document_identifier(*td);
            const auto *doc = store_.get(id.uri);
            if (doc != nullptr) {
                Frontend frontend;
                auto parse_result = frontend.parse_text(doc->uri, doc->text);
                if (!parse_result.has_errors()) {
                    Resolver resolver;
                    auto resolve_result = resolver.resolve(*parse_result.program);
                    for (const auto &sym : resolve_result.symbol_table.symbols()) {
                        CompletionItem ci;
                        ci.label = sym.local_name;
                        switch (sym.kind) {
                        case SymbolKind::Struct:
                            ci.kind = CompletionItemKind::Struct;
                            ci.detail = "struct";
                            break;
                        case SymbolKind::Enum:
                            ci.kind = CompletionItemKind::Enum;
                            ci.detail = "enum";
                            break;
                        case SymbolKind::Capability:
                            ci.kind = CompletionItemKind::Interface;
                            ci.detail = "capability";
                            break;
                        case SymbolKind::Agent:
                            ci.kind = CompletionItemKind::Function;
                            ci.detail = "agent";
                            break;
                        case SymbolKind::Predicate:
                            ci.kind = CompletionItemKind::Function;
                            ci.detail = "predicate";
                            break;
                        default:
                            ci.kind = CompletionItemKind::Variable;
                            break;
                        }
                        items.push_back(std::move(ci));
                    }
                }
            }
        }
    }

    // Build response array
    auto result = json::JsonValue::make_array();
    for (const auto &item : items) {
        result->push(serialize_completion_item(item));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_definition(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    const auto *td = req.params->get("textDocument");
    const auto *pos_val = req.params->get("position");
    if (td == nullptr || pos_val == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    auto id = parse_text_document_identifier(*td);
    auto pos = parse_position(*pos_val);
    const auto *doc = store_.get(id.uri);

    if (doc == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);
    if (parse_result.has_errors()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);

    // Convert LSP position (0-based) to offset
    auto offset = parse_result.source.offset_of(pos.line + 1, pos.character + 1);

    // Find a reference at this offset
    for (const auto &ref : resolve_result.references()) {
        if (ref.range.begin_offset <= offset && offset <= ref.range.end_offset) {
            // Found a reference — get target symbol's declaration range
            auto sym_opt = resolve_result.symbol_table.get(ref.target);
            if (sym_opt.has_value()) {
                const auto &sym = sym_opt.value().get();
                auto start_pos = parse_result.source.locate(sym.declaration_range.begin_offset);
                auto end_pos = parse_result.source.locate(sym.declaration_range.end_offset);

                Location loc;
                loc.uri = doc->uri;
                loc.range.start = {static_cast<uint32_t>(start_pos.line - 1),
                                   static_cast<uint32_t>(start_pos.column - 1)};
                loc.range.end = {static_cast<uint32_t>(end_pos.line - 1),
                                 static_cast<uint32_t>(end_pos.column - 1)};

                JsonRpcResponse resp;
                resp.id = req.id;
                resp.result = serialize_location(loc);
                transport_.send_response(resp);
                return;
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = json::JsonValue::make_null();
    transport_.send_response(resp);
}

void LspServer::handle_hover(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    const auto *td = req.params->get("textDocument");
    const auto *pos_val = req.params->get("position");
    if (td == nullptr || pos_val == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    auto id = parse_text_document_identifier(*td);
    auto pos = parse_position(*pos_val);
    const auto *doc = store_.get(id.uri);

    if (doc == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);
    if (parse_result.has_errors()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);

    TypeChecker checker;
    auto tc_result = checker.check(*parse_result.program, resolve_result);

    // Convert LSP position to offset
    auto offset = parse_result.source.offset_of(pos.line + 1, pos.character + 1);

    // Try to find expression type at this offset
    const auto *hover_expr = tc_result.typed_program.find_expr_containing(offset, std::nullopt);
    if (hover_expr != nullptr && hover_expr->type != nullptr) {
        Hover hover;
        hover.contents = "```\n" + hover_expr->type->describe() + "\n```";

        auto start_pos = parse_result.source.locate(hover_expr->range.begin_offset);
        auto end_pos = parse_result.source.locate(hover_expr->range.end_offset);
        hover.range = Range{
            {static_cast<uint32_t>(start_pos.line - 1),
             static_cast<uint32_t>(start_pos.column - 1)},
            {static_cast<uint32_t>(end_pos.line - 1), static_cast<uint32_t>(end_pos.column - 1)},
        };

        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = serialize_hover(hover);
        transport_.send_response(resp);
        return;
    }

    // Fallback: check if hovering over a symbol reference
    for (const auto &ref : resolve_result.references()) {
        if (ref.range.begin_offset <= offset && offset <= ref.range.end_offset) {
            auto sym_opt = resolve_result.symbol_table.get(ref.target);
            if (sym_opt.has_value()) {
                const auto &sym = sym_opt.value().get();
                Hover hover;
                hover.contents = "**" + sym.local_name + "** (" + sym.canonical_name + ")";

                auto start_pos = parse_result.source.locate(ref.range.begin_offset);
                auto end_pos = parse_result.source.locate(ref.range.end_offset);
                hover.range = Range{
                    {static_cast<uint32_t>(start_pos.line - 1),
                     static_cast<uint32_t>(start_pos.column - 1)},
                    {static_cast<uint32_t>(end_pos.line - 1),
                     static_cast<uint32_t>(end_pos.column - 1)},
                };

                JsonRpcResponse resp;
                resp.id = req.id;
                resp.result = serialize_hover(hover);
                transport_.send_response(resp);
                return;
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = json::JsonValue::make_null();
    transport_.send_response(resp);
}

void LspServer::publish_diagnostics(const std::string &uri) {
    const auto *doc = store_.get(uri);
    if (doc == nullptr) {
        return;
    }

    std::vector<LspDiagnostic> lsp_diags;

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);

    auto collect = [&](const DiagnosticBag &bag) {
        for (const auto &d : bag.entries()) {
            LspDiagnostic ld;
            ld.message = d.message;
            if (d.code.has_value()) {
                ld.code = *d.code;
            }

            switch (d.severity) {
            case ahfl::DiagnosticSeverity::Error:
                ld.severity = DiagnosticSeverity::Error;
                break;
            case ahfl::DiagnosticSeverity::Warning:
                ld.severity = DiagnosticSeverity::Warning;
                break;
            case ahfl::DiagnosticSeverity::Note:
                ld.severity = DiagnosticSeverity::Information;
                break;
            }

            if (d.range.has_value()) {
                auto start = parse_result.source.locate(d.range->begin_offset);
                auto end = parse_result.source.locate(d.range->end_offset);
                ld.range.start = {static_cast<uint32_t>(start.line - 1),
                                  static_cast<uint32_t>(start.column - 1)};
                ld.range.end = {static_cast<uint32_t>(end.line - 1),
                                static_cast<uint32_t>(end.column - 1)};
            }

            lsp_diags.push_back(std::move(ld));
        }
    };

    collect(parse_result.diagnostics);

    if (!parse_result.has_errors()) {
        Resolver resolver;
        auto resolve_result = resolver.resolve(*parse_result.program);
        collect(resolve_result.diagnostics);

        if (!resolve_result.has_errors()) {
            TypeChecker checker;
            auto tc_result = checker.check(*parse_result.program, resolve_result);
            collect(tc_result.diagnostics);
        }
    }

    // Send notification
    auto params = json::JsonValue::make_object();
    params->set("uri", json::JsonValue::make_string(uri));

    auto diag_array = json::JsonValue::make_array();
    for (const auto &ld : lsp_diags) {
        diag_array->push(serialize_diagnostic(ld));
    }
    params->set("diagnostics", std::move(diag_array));

    transport_.send_notification("textDocument/publishDiagnostics", std::move(params));
}

void LspServer::handle_references(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    const auto *td = req.params->get("textDocument");
    const auto *pos_val = req.params->get("position");
    if (td == nullptr || pos_val == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    auto id = parse_text_document_identifier(*td);
    auto pos = parse_position(*pos_val);
    const auto *doc = store_.get(id.uri);

    if (doc == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);
    if (parse_result.has_errors()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);

    auto offset = parse_result.source.offset_of(pos.line + 1, pos.character + 1);

    // Find symbol at cursor
    SymbolId target_id{};
    bool found_target = false;

    for (const auto &ref : resolve_result.references()) {
        if (ref.range.begin_offset <= offset && offset <= ref.range.end_offset) {
            target_id = ref.target;
            found_target = true;
            break;
        }
    }

    // Also check if cursor is on a declaration
    if (!found_target) {
        for (const auto &sym : resolve_result.symbol_table.symbols()) {
            if (sym.declaration_range.begin_offset <= offset &&
                offset <= sym.declaration_range.end_offset) {
                target_id = sym.id;
                found_target = true;
                break;
            }
        }
    }

    auto result = json::JsonValue::make_array();

    if (found_target) {
        // Check includeDeclaration
        bool include_declaration = false;
        const auto *context = req.params->get("context");
        if (context != nullptr) {
            const auto *incl = context->get("includeDeclaration");
            if (incl != nullptr) {
                auto b = incl->as_bool();
                if (b.has_value()) {
                    include_declaration = *b;
                }
            }
        }

        // Include declaration location
        if (include_declaration) {
            auto sym_opt = resolve_result.symbol_table.get(target_id);
            if (sym_opt.has_value()) {
                const auto &sym = sym_opt.value().get();
                auto start = parse_result.source.locate(sym.declaration_range.begin_offset);
                auto end = parse_result.source.locate(sym.declaration_range.end_offset);
                Location loc;
                loc.uri = doc->uri;
                loc.range.start = {static_cast<uint32_t>(start.line - 1),
                                   static_cast<uint32_t>(start.column - 1)};
                loc.range.end = {static_cast<uint32_t>(end.line - 1),
                                 static_cast<uint32_t>(end.column - 1)};
                result->push(serialize_location(loc));
            }
        }

        // Collect all references to this symbol
        for (const auto &ref : resolve_result.references()) {
            if (ref.target == target_id) {
                auto start = parse_result.source.locate(ref.range.begin_offset);
                auto end = parse_result.source.locate(ref.range.end_offset);
                Location loc;
                loc.uri = doc->uri;
                loc.range.start = {static_cast<uint32_t>(start.line - 1),
                                   static_cast<uint32_t>(start.column - 1)};
                loc.range.end = {static_cast<uint32_t>(end.line - 1),
                                 static_cast<uint32_t>(end.column - 1)};
                result->push(serialize_location(loc));
            }
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_rename(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    const auto *td = req.params->get("textDocument");
    const auto *pos_val = req.params->get("position");
    const auto *new_name_val = req.params->get("newName");
    if (td == nullptr || pos_val == nullptr || new_name_val == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    auto new_name_sv = new_name_val->as_string();
    if (!new_name_sv.has_value()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }
    std::string new_name(*new_name_sv);

    auto id = parse_text_document_identifier(*td);
    auto pos = parse_position(*pos_val);
    const auto *doc = store_.get(id.uri);

    if (doc == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);
    if (parse_result.has_errors()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);

    auto offset = parse_result.source.offset_of(pos.line + 1, pos.character + 1);

    // Find symbol at cursor
    SymbolId target_id{};
    bool found_target = false;

    for (const auto &ref : resolve_result.references()) {
        if (ref.range.begin_offset <= offset && offset <= ref.range.end_offset) {
            target_id = ref.target;
            found_target = true;
            break;
        }
    }

    if (!found_target) {
        for (const auto &sym : resolve_result.symbol_table.symbols()) {
            if (sym.declaration_range.begin_offset <= offset &&
                offset <= sym.declaration_range.end_offset) {
                target_id = sym.id;
                found_target = true;
                break;
            }
        }
    }

    if (!found_target) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_null();
        transport_.send_response(resp);
        return;
    }

    WorkspaceEdit workspace_edit;
    auto &edits = workspace_edit.changes[doc->uri];

    // Add declaration rename
    auto sym_opt = resolve_result.symbol_table.get(target_id);
    if (sym_opt.has_value()) {
        const auto &sym = sym_opt.value().get();
        auto start = parse_result.source.locate(sym.declaration_range.begin_offset);
        auto end = parse_result.source.locate(sym.declaration_range.end_offset);
        TextEdit te;
        te.range.start = {static_cast<uint32_t>(start.line - 1),
                          static_cast<uint32_t>(start.column - 1)};
        te.range.end = {static_cast<uint32_t>(end.line - 1), static_cast<uint32_t>(end.column - 1)};
        te.new_text = new_name;
        edits.push_back(std::move(te));
    }

    // Add reference renames
    for (const auto &ref : resolve_result.references()) {
        if (ref.target == target_id) {
            auto start = parse_result.source.locate(ref.range.begin_offset);
            auto end = parse_result.source.locate(ref.range.end_offset);
            TextEdit te;
            te.range.start = {static_cast<uint32_t>(start.line - 1),
                              static_cast<uint32_t>(start.column - 1)};
            te.range.end = {static_cast<uint32_t>(end.line - 1),
                            static_cast<uint32_t>(end.column - 1)};
            te.new_text = new_name;
            edits.push_back(std::move(te));
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = serialize_workspace_edit(workspace_edit);
    transport_.send_response(resp);
}

void LspServer::handle_document_symbol(const JsonRpcRequest &req) {
    if (!req.params) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    const auto *td = req.params->get("textDocument");
    if (td == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    auto id = parse_text_document_identifier(*td);
    const auto *doc = store_.get(id.uri);

    if (doc == nullptr) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    Frontend frontend;
    auto parse_result = frontend.parse_text(doc->uri, doc->text);
    if (parse_result.has_errors()) {
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.result = json::JsonValue::make_array();
        transport_.send_response(resp);
        return;
    }

    Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);

    auto result = json::JsonValue::make_array();

    for (const auto &sym : resolve_result.symbol_table.symbols()) {
        auto start = parse_result.source.locate(sym.declaration_range.begin_offset);
        auto end = parse_result.source.locate(sym.declaration_range.end_offset);

        DocumentSymbol ds;
        ds.name = sym.local_name;
        ds.kind = to_lsp_symbol_kind(sym.kind);
        ds.range.start = {static_cast<uint32_t>(start.line - 1),
                          static_cast<uint32_t>(start.column - 1)};
        ds.range.end = {static_cast<uint32_t>(end.line - 1), static_cast<uint32_t>(end.column - 1)};
        ds.selection_range = ds.range;

        result->push(serialize_document_symbol(ds));
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

void LspServer::handle_workspace_symbol(const JsonRpcRequest &req) {
    std::string query;
    if (req.params) {
        const auto *q = req.params->get("query");
        if (q != nullptr) {
            auto sv = q->as_string();
            if (sv.has_value()) {
                query = std::string(*sv);
            }
        }
    }

    auto result = json::JsonValue::make_array();

    for (const auto &uri : store_.all_uris()) {
        const auto *doc = store_.get(uri);
        if (doc == nullptr) {
            continue;
        }

        Frontend frontend;
        auto parse_result = frontend.parse_text(doc->uri, doc->text);
        if (parse_result.has_errors()) {
            continue;
        }

        Resolver resolver;
        auto resolve_result = resolver.resolve(*parse_result.program);

        for (const auto &sym : resolve_result.symbol_table.symbols()) {
            // Filter by query (substring match)
            if (!query.empty() && sym.local_name.find(query) == std::string::npos) {
                continue;
            }

            auto start = parse_result.source.locate(sym.declaration_range.begin_offset);
            auto end = parse_result.source.locate(sym.declaration_range.end_offset);

            SymbolInformation si;
            si.name = sym.local_name;
            si.kind = to_lsp_symbol_kind(sym.kind);
            si.location.uri = doc->uri;
            si.location.range.start = {static_cast<uint32_t>(start.line - 1),
                                       static_cast<uint32_t>(start.column - 1)};
            si.location.range.end = {static_cast<uint32_t>(end.line - 1),
                                     static_cast<uint32_t>(end.column - 1)};

            result->push(serialize_symbol_information(si));
        }
    }

    JsonRpcResponse resp;
    resp.id = req.id;
    resp.result = std::move(result);
    transport_.send_response(resp);
}

} // namespace ahfl::lsp
