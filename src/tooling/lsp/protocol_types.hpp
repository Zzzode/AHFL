#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/json/json_value.hpp"

namespace ahfl::lsp {

// ---------- Basic types (0-based) ----------

struct Position {
    uint32_t line{0};
    uint32_t character{0};
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range range;
};

// ---------- Text Documents ----------

struct TextDocumentIdentifier {
    std::string uri;
};

struct TextDocumentItem {
    std::string uri;
    std::string language_id;
    int version{0};
    std::string text;
};

struct VersionedTextDocumentIdentifier {
    std::string uri;
    int version{0};
};

struct TextDocumentContentChangeEvent {
    std::string text; // full content replacement
};

// ---------- Diagnostics ----------

enum class DiagnosticSeverity : int {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct LspDiagnostic {
    Range range;
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string code;
    std::string source{"ahfl"};
    std::string message;
};

// ---------- Completion ----------

enum class CompletionItemKind : int {
    Text = 1,
    Keyword = 14,
    Struct = 22,
    Enum = 13,
    Interface = 8, // used for Capability
    Function = 3,
    Variable = 6,
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind{CompletionItemKind::Text};
    std::string detail;
};

// ---------- Hover ----------

struct Hover {
    std::string contents; // markdown
    std::optional<Range> range;
};

// ---------- Server Capabilities ----------

struct ServerCapabilities {
    bool text_document_sync_full{true};
    bool completion_provider{true};
    bool definition_provider{true};
    bool hover_provider{true};
    bool references_provider{true};
    bool rename_provider{true};
    bool document_symbol_provider{true};
    bool workspace_symbol_provider{true};
};

// ---------- Symbol Information ----------

enum class LspSymbolKind : int {
    File = 1,
    Module = 2,
    Namespace = 3,
    Class = 5,
    Method = 6,
    Property = 7,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    Struct = 23,
};

struct DocumentSymbol {
    std::string name;
    LspSymbolKind kind{LspSymbolKind::Variable};
    Range range;
    Range selection_range;
};

struct SymbolInformation {
    std::string name;
    LspSymbolKind kind{LspSymbolKind::Variable};
    Location location;
};

// ---------- Text Edits ----------

struct TextEdit {
    Range range;
    std::string new_text;
};

struct WorkspaceEdit {
    std::unordered_map<std::string, std::vector<TextEdit>> changes;
};

// ---------- Serialization helpers ----------

[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_position(const Position &pos);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_range(const Range &range);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_location(const Location &loc);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_diagnostic(const LspDiagnostic &diag);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_completion_item(const CompletionItem &item);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_hover(const Hover &hover);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_server_capabilities(const ServerCapabilities &caps);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_document_symbol(const DocumentSymbol &sym);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_symbol_information(const SymbolInformation &sym);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_text_edit(const TextEdit &edit);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_workspace_edit(const WorkspaceEdit &edit);

// ---------- Deserialization helpers ----------

[[nodiscard]] Position parse_position(const json::JsonValue &v);
[[nodiscard]] Range parse_range(const json::JsonValue &v);
[[nodiscard]] TextDocumentItem parse_text_document_item(const json::JsonValue &v);
[[nodiscard]] TextDocumentIdentifier parse_text_document_identifier(const json::JsonValue &v);
[[nodiscard]] VersionedTextDocumentIdentifier parse_versioned_text_document_identifier(const json::JsonValue &v);

} // namespace ahfl::lsp
