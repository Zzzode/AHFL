#pragma once

#include <filesystem>
#include <istream>
#include <ostream>
#include <string_view>
#include <vector>

#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/document_store.hpp"
#include "tooling/lsp/hover_markup.hpp"
#include "tooling/lsp/json_rpc.hpp"

namespace ahfl::lsp {

/// AHFL Language Server — handles LSP protocol over stdin/stdout.
class LspServer {
  public:
    LspServer(std::istream &in, std::ostream &out);

    /// Blocking event loop. Returns when the transport reaches EOF or receives exit.
    void run();

  private:
    JsonRpcTransport transport_;
    DocumentStore store_;
    AnalysisService analysis_;
    HoverRenderOptions hover_options_;
    std::vector<std::filesystem::path> workspace_roots_;
    bool initialized_{false};
    bool shutdown_requested_{false};
    bool trace_enabled_{false};

    // Request handlers
    void handle_request(const JsonRpcRequest &req);
    void handle_notification(const JsonRpcNotification &notif);

    void handle_initialize(const JsonRpcRequest &req);
    void handle_shutdown(const JsonRpcRequest &req);
    void handle_completion(const JsonRpcRequest &req);
    void handle_definition(const JsonRpcRequest &req);
    void handle_hover(const JsonRpcRequest &req);
    void handle_references(const JsonRpcRequest &req);
    void handle_prepare_rename(const JsonRpcRequest &req);
    void handle_rename(const JsonRpcRequest &req);
    void handle_text_document_diagnostic(const JsonRpcRequest &req);
    void handle_workspace_diagnostic(const JsonRpcRequest &req);
    void handle_document_symbol(const JsonRpcRequest &req);
    void handle_workspace_symbol(const JsonRpcRequest &req);
    void handle_signature_help(const JsonRpcRequest &req);
    void handle_document_formatting(const JsonRpcRequest &req);
    void handle_semantic_tokens_full(const JsonRpcRequest &req);
    void handle_code_action(const JsonRpcRequest &req);
    void handle_folding_range(const JsonRpcRequest &req);
    void handle_document_highlight(const JsonRpcRequest &req);
    void handle_selection_range(const JsonRpcRequest &req);
    void handle_code_lens(const JsonRpcRequest &req);

    void handle_initialized();
    void handle_did_open(const json::JsonValue &params);
    void handle_did_change(const json::JsonValue &params);
    void handle_did_close(const json::JsonValue &params);
    void handle_workspace_folders_changed(const json::JsonValue &params);
    void handle_watched_files_changed(const json::JsonValue &params);
    void handle_exit();

    // Analysis
    void send_diagnostic_refresh();
    void trace(std::string_view message) const;
};

} // namespace ahfl::lsp
