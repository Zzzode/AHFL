#pragma once

#include <istream>
#include <ostream>

#include "ahfl/lsp/document_store.hpp"
#include "ahfl/lsp/json_rpc.hpp"

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
    bool initialized_{false};
    bool shutdown_requested_{false};

    // Request handlers
    void handle_request(const JsonRpcRequest &req);
    void handle_notification(const JsonRpcNotification &notif);

    void handle_initialize(const JsonRpcRequest &req);
    void handle_shutdown(const JsonRpcRequest &req);
    void handle_completion(const JsonRpcRequest &req);
    void handle_definition(const JsonRpcRequest &req);
    void handle_hover(const JsonRpcRequest &req);
    void handle_references(const JsonRpcRequest &req);
    void handle_rename(const JsonRpcRequest &req);
    void handle_document_symbol(const JsonRpcRequest &req);
    void handle_workspace_symbol(const JsonRpcRequest &req);

    void handle_initialized();
    void handle_did_open(const json::JsonValue &params);
    void handle_did_change(const json::JsonValue &params);
    void handle_did_close(const json::JsonValue &params);
    void handle_exit();

    // Analysis
    void publish_diagnostics(const std::string &uri);
};

} // namespace ahfl::lsp
