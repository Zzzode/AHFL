#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
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
    std::vector<std::filesystem::path> workspace_folders_;
    project_discovery::ToolchainProfileSet initialization_toolchain_profiles_;
    std::optional<project_discovery::ToolchainProfileSet> configuration_toolchain_profiles_;
    std::optional<std::string> pending_configuration_request_id_;
    std::uint64_t next_server_request_id_{1};
    bool initialized_{false};
    bool shutdown_requested_{false};
    bool trace_enabled_{false};
    bool completion_snippet_support_{false};

    // Request handlers
    void handle_request(const JsonRpcRequest &req);
    void handle_notification(const JsonRpcNotification &notif);
    void handle_response(const JsonRpcResponse &resp);

    void handle_initialize(const JsonRpcRequest &req);
    void handle_shutdown(const JsonRpcRequest &req);
    void handle_completion(const JsonRpcRequest &req);
    void handle_definition(const JsonRpcRequest &req);
    void handle_type_definition(const JsonRpcRequest &req);
    void handle_implementation(const JsonRpcRequest &req);
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
    void handle_did_save(const json::JsonValue &params);
    void handle_did_close(const json::JsonValue &params);
    void handle_did_change_configuration(const json::JsonValue &params);
    void handle_workspace_folders_changed(const json::JsonValue &params);
    void handle_watched_files_changed(const json::JsonValue &params);
    void handle_exit();

    // Analysis
    void apply_active_toolchain_profiles();
    void request_workspace_configuration();
    void send_diagnostic_refresh();
    void trace(std::string_view message) const;
};

} // namespace ahfl::lsp
