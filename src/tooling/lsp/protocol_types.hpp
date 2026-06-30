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

// ---------- Text Edits ----------

struct TextEdit {
    Range range;
    std::string new_text;
};

struct WorkspaceEdit {
    std::unordered_map<std::string, std::vector<TextEdit>> changes;
};

// ---------- Diagnostics ----------

enum class DiagnosticSeverity : int {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct LspDiagnostic {
    struct RelatedInformation {
        Location location;
        std::string message;
    };

    Range range;
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string code;
    std::string source{"ahfl"};
    std::string message;
    std::vector<RelatedInformation> related_information;
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
    Constant = 21,
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind{CompletionItemKind::Text};
    std::string detail;
};

// ---------- Hover ----------

enum class MarkupKind {
    Markdown,
    Plaintext,
};

struct Hover {
    std::string contents;
    MarkupKind contents_kind{MarkupKind::Markdown};
    std::optional<Range> range;
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
    std::vector<DocumentSymbol> children;
};

struct SymbolInformation {
    std::string name;
    LspSymbolKind kind{LspSymbolKind::Variable};
    Location location;
};

// ---------- Signature Help ----------

struct ParameterInformation {
    std::string label;
    std::string documentation;
};

struct SignatureInformation {
    std::string label;
    std::string documentation;
    std::vector<ParameterInformation> parameters;
};

struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    int active_signature{0};
    int active_parameter{0};
};

// ---------- Semantic Tokens ----------

enum class SemanticTokenType : uint32_t {
    Namespace = 0,
    Type = 1,
    Class = 2,
    Enum = 3,
    Interface = 4,
    Struct = 5,
    TypeParameter = 6,
    Parameter = 7,
    Variable = 8,
    Property = 9,
    EnumMember = 10,
    Event = 11,
    Function = 12,
    Method = 13,
    Macro = 14,
    Keyword = 15,
    Modifier = 16,
    Comment = 17,
    String = 18,
    Number = 19,
    Regexp = 20,
    Operator = 21,
    Decorator = 22,
};

enum class SemanticTokenModifier : uint32_t {
    Declaration = 1 << 0,
    Definition = 1 << 1,
    Readonly = 1 << 2,
    Static = 1 << 3,
    Deprecated = 1 << 4,
    Abstract = 1 << 5,
    Async = 1 << 6,
    Modification = 1 << 7,
    Documentation = 1 << 8,
    DefaultLibrary = 1 << 9,
};

struct SemanticToken {
    uint32_t delta_line{0};
    uint32_t delta_start{0};
    uint32_t length{0};
    uint32_t token_type{0};
    uint32_t token_modifiers{0};
};

struct SemanticTokens {
    std::string result_id;
    std::vector<SemanticToken> data;
};

// ---------- Code Action ----------

enum class CodeActionKind {
    QuickFix,
    Refactor,
    RefactorExtract,
    RefactorInline,
    RefactorRewrite,
    Source,
    SourceOrganizeImports,
    SourceFixAll,
};

struct CodeAction {
    std::string title;
    std::optional<CodeActionKind> kind;
    std::vector<LspDiagnostic> diagnostics;
    bool is_preferred{false};
    std::optional<WorkspaceEdit> edit;
    std::optional<std::string> command;
    std::vector<std::string> command_arguments;
};

// ---------- Folding Range ----------

enum class FoldingRangeKind {
    Comment,
    Imports,
    Region,
};

struct FoldingRange {
    uint32_t start_line{0};
    uint32_t start_character{0};
    uint32_t end_line{0};
    uint32_t end_character{0};
    std::optional<FoldingRangeKind> kind;
};

// ---------- Document Highlight ----------

enum class DocumentHighlightKind {
    Text = 1,
    Read = 2,
    Write = 3,
};

struct DocumentHighlight {
    Range range;
    DocumentHighlightKind kind{DocumentHighlightKind::Text};
};

// ---------- Selection Range ----------

struct SelectionRange {
    Range range;
    std::unique_ptr<SelectionRange> parent;
};

// ---------- CodeLens ----------

struct CodeLens {
    Range range;
    std::optional<std::string> command_title;
    std::optional<std::string> command;
    std::vector<std::string> command_arguments;
    std::optional<std::string> data;
};

// ---------- Server Capabilities ----------

struct ServerCapabilities {
    bool text_document_sync_full{true};
    bool completion_provider{true};
    bool definition_provider{true};
    bool hover_provider{true};
    bool references_provider{true};
    bool rename_provider{true};
    bool prepare_rename_provider{true};
    bool diagnostic_provider{true};
    std::string diagnostic_id{"ahfl-diagnostics"};
    bool document_formatting_provider{false};
    bool document_range_formatting_provider{false};
    bool document_symbol_provider{true};
    bool workspace_symbol_provider{true};
    bool workspace_folders_provider{true};
    bool signature_help_provider{true};
    bool semantic_tokens_provider{false};
    bool code_action_provider{false};
    bool folding_range_provider{false};
    bool document_highlight_provider{false};
    bool selection_range_provider{false};
    bool code_lens_provider{false};
};

// ---------- Serialization helpers ----------

[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_position(const Position &pos);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_range(const Range &range);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_location(const Location &loc);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_diagnostic(const LspDiagnostic &diag);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_completion_item(const CompletionItem &item);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_hover(const Hover &hover);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_server_capabilities(const ServerCapabilities &caps);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_document_symbol(const DocumentSymbol &sym);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_symbol_information(const SymbolInformation &sym);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_text_edit(const TextEdit &edit);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_workspace_edit(const WorkspaceEdit &edit);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_parameter_information(const ParameterInformation &param);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_signature_information(const SignatureInformation &sig);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_signature_help(const SignatureHelp &help);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_semantic_tokens(const SemanticTokens &tokens);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_code_action(const CodeAction &action);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_folding_range(const FoldingRange &range);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_document_highlight(const DocumentHighlight &hl);
[[nodiscard]] std::unique_ptr<json::JsonValue>
serialize_selection_range(const SelectionRange &range);
[[nodiscard]] std::unique_ptr<json::JsonValue> serialize_code_lens(const CodeLens &lens);

// ---------- Deserialization helpers ----------

[[nodiscard]] Position parse_position(const json::JsonValue &v);
[[nodiscard]] Range parse_range(const json::JsonValue &v);
[[nodiscard]] TextDocumentItem parse_text_document_item(const json::JsonValue &v);
[[nodiscard]] TextDocumentIdentifier parse_text_document_identifier(const json::JsonValue &v);
[[nodiscard]] VersionedTextDocumentIdentifier
parse_versioned_text_document_identifier(const json::JsonValue &v);

} // namespace ahfl::lsp
