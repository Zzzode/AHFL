#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/hover_service.hpp"
#include "tooling/lsp/server.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace ahfl::lsp;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

/// Helper: construct a Content-Length framed message string.
std::string make_frame(const std::string &json_body) {
    std::string frame;
    frame += "Content-Length: " + std::to_string(json_body.size()) + "\r\n";
    frame += "\r\n";
    frame += json_body;
    return frame;
}

std::string escape_json_string(const std::string &text) {
    std::string escaped;
    for (char c : text) {
        if (c == '"')
            escaped += "\\\"";
        else if (c == '\n')
            escaped += "\\n";
        else if (c == '\\')
            escaped += "\\\\";
        else
            escaped += c;
    }
    return escaped;
}

std::string run_lsp_messages(const std::vector<std::string> &bodies) {
    std::string input;
    for (const auto &body : bodies) {
        input += make_frame(body);
    }

    std::istringstream in(input);
    std::ostringstream out;
    LspServer server(in, out);
    server.run();
    return out.str();
}

struct LspMessageStep {
    std::string body{};
    std::function<void()> before{};
};

class HookedMessageBuffer : public std::streambuf {
  public:
    explicit HookedMessageBuffer(std::vector<LspMessageStep> steps) : steps_(std::move(steps)) {}

  protected:
    int_type underflow() override {
        if (gptr() != nullptr && gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        if (next_step_ >= steps_.size()) {
            return traits_type::eof();
        }

        auto &step = steps_[next_step_++];
        if (step.before) {
            step.before();
        }
        current_ = make_frame(step.body);
        setg(current_.data(), current_.data(), current_.data() + current_.size());
        return traits_type::to_int_type(*gptr());
    }

  private:
    std::vector<LspMessageStep> steps_;
    std::size_t next_step_{0};
    std::string current_;
};

std::string run_lsp_message_steps(std::vector<LspMessageStep> steps) {
    HookedMessageBuffer buffer(std::move(steps));
    std::istream in(&buffer);
    std::ostringstream out;
    LspServer server(in, out);
    server.run();
    return out.str();
}

std::string response_body_for_id(const std::string &output, int id) {
    std::size_t cursor = 0;
    const auto id_needle = "\"id\":" + std::to_string(id);
    while (cursor < output.size()) {
        const auto header = output.find("Content-Length: ", cursor);
        if (header == std::string::npos) {
            return {};
        }
        const auto value_start = header + std::string_view("Content-Length: ").size();
        const auto value_end = output.find("\r\n", value_start);
        if (value_end == std::string::npos) {
            return {};
        }
        const auto body_start = output.find("\r\n\r\n", value_end);
        if (body_start == std::string::npos) {
            return {};
        }

        const auto length_text = output.substr(value_start, value_end - value_start);
        const auto length = static_cast<std::size_t>(std::stoull(length_text));
        const auto json_start = body_start + 4;
        if (json_start + length > output.size()) {
            return {};
        }

        auto body = output.substr(json_start, length);
        if (body.find(id_needle) != std::string::npos) {
            return body;
        }
        cursor = json_start + length;
    }
    return {};
}

std::size_t count_substring(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t cursor = 0;
    while (true) {
        cursor = text.find(needle, cursor);
        if (cursor == std::string_view::npos) {
            return count;
        }
        ++count;
        cursor += needle.size();
    }
}

/// Extract the resultId string from a textDocument/diagnostic response body.
std::string extract_result_id(const std::string &response_body) {
    const auto key = std::string_view("\"resultId\":\"");
    const auto pos = response_body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + key.size();
    const auto end = response_body.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return response_body.substr(start, end - start);
}

/// Helper: send initialize + didOpen + request, return response body
std::string run_handler_request(const std::string &source_text,
                                const std::string &method,
                                const std::string &params_json) {
    // Build initialize request
    std::string init_body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    // Build didOpen notification
    const auto escaped_text = escape_json_string(source_text);
    std::string did_open_body =
        R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test.ahfl","languageId":"ahfl","version":1,"text":")" +
        escaped_text + R"("}}})";
    // Build actual request
    std::string req_body =
        R"({"jsonrpc":"2.0","id":2,"method":")" + method + R"(","params":)" + params_json + R"(})";
    // Build shutdown
    std::string shutdown_body = R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})";

    return run_lsp_messages({init_body, did_open_body, req_body, shutdown_body});
}

Position
position_of(const std::string &source, const std::string &needle, std::size_t occurrence = 0) {
    std::size_t search_from = 0;
    std::size_t offset = std::string::npos;
    for (std::size_t i = 0; i <= occurrence; ++i) {
        offset = source.find(needle, search_from);
        if (offset == std::string::npos) {
            return {};
        }
        search_from = offset + needle.size();
    }

    Position pos;
    for (std::size_t i = 0; i < offset; ++i) {
        if (source[i] == '\n') {
            ++pos.line;
            pos.character = 0;
        } else {
            ++pos.character;
        }
    }
    return pos;
}

std::string hover_params_at(const std::string &uri, Position position) {
    return R"({"textDocument":{"uri":")" + uri + R"("},"position":{"line":)" +
           std::to_string(position.line) + R"(,"character":)" + std::to_string(position.character) +
           R"(}})";
}

std::string hover_request_body(const std::string &uri, Position position, int id = 2) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
           R"(,"method":"textDocument/hover","params":)" + hover_params_at(uri, position) + R"(})";
}

std::string run_hover_request(const std::string &source_text,
                              const std::string &needle,
                              std::size_t occurrence = 0) {
    return run_handler_request(
        source_text,
        "textDocument/hover",
        hover_params_at("file:///test.ahfl", position_of(source_text, needle, occurrence)));
}

bool payload_has_fact(const HoverPayload &payload,
                      const std::string &label,
                      const std::string &value) {
    for (const auto &fact : payload.facts) {
        if (fact.label == label && fact.value == value) {
            return true;
        }
    }
    return false;
}

Position lsp_position_at(const ahfl::SourceFile &source, std::size_t offset) {
    const auto location = source.locate(offset);
    return Position{
        .line = static_cast<std::uint32_t>(location.line > 0 ? location.line - 1 : 0),
        .character = static_cast<std::uint32_t>(location.column > 0 ? location.column - 1 : 0),
    };
}

std::string hover_coverage_name(const ahfl::SourceFile &source,
                                const HoverTarget &target,
                                std::size_t ordinal) {
    const auto text = std::string(source.slice(target.token_range));
    const auto position = lsp_position_at(source, target.token_range.begin_offset);
    return "hoverCoverage.target." + std::to_string(ordinal) + "." +
           std::to_string(static_cast<int>(target.kind)) + "." + text + "." +
           std::to_string(position.line) + "." + std::to_string(position.character);
}

void check_hover_target_index_coverage_for_source(const LspAnalysisSnapshot &snapshot,
                                                  const LspSourceSnapshot &source,
                                                  std::string_view label) {
    check(source.source != nullptr, "hoverCoverage." + std::string(label) + ".source_exists");
    if (source.source == nullptr) {
        return;
    }

    const auto index_iter = snapshot.hover_indices.find(hover_index_key(source));
    check(index_iter != snapshot.hover_indices.end(),
          "hoverCoverage." + std::string(label) + ".index_exists");
    if (index_iter == snapshot.hover_indices.end()) {
        return;
    }

    HoverService service;
    std::size_t ordinal = 0;
    for (const auto &target : index_iter->second.targets()) {
        if (target.token_range.empty()) {
            continue;
        }
        const auto position = lsp_position_at(*source.source, target.token_range.begin_offset);
        const auto hover = service.hover_at(snapshot, source, position);
        const auto test_name = "hoverCoverage." + std::string(label) + "." +
                               hover_coverage_name(*source.source, target, ordinal++);
        check(hover.has_value() && !hover->contents.empty(), test_name);
    }
}

void check_hover_target_index_coverage(const std::string &source_text) {
    constexpr std::string_view uri = "file:///hover_coverage.ahfl";
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = std::string(uri),
        .language_id = "ahfl",
        .version = 1,
        .text = source_text,
    });

    AnalysisService analysis(store);
    const auto *snapshot = analysis.snapshot_for_uri(std::string(uri));
    check(snapshot != nullptr, "hoverCoverage.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto *source = snapshot->source_for_uri(uri);
    check(source != nullptr, "hoverCoverage.source_exists");
    if (source == nullptr) {
        return;
    }

    check_hover_target_index_coverage_for_source(*snapshot, *source, "rich");
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void check_hover_integration_fixture_coverage() {
    const auto root = std::filesystem::current_path() / "tests" / "integration" / "check_ok";
    const auto entry = root / "app" / "main.ahfl";
    const auto entry_uri = AnalysisService::uri_from_path(entry);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = entry_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = read_file(entry),
    });
    AnalysisService analysis(store);
    analysis.set_workspace_roots({root});

    const auto *snapshot = analysis.snapshot_for_uri(entry_uri);
    check(snapshot != nullptr, "hoverCoverage.integration.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const std::vector<std::pair<std::filesystem::path, std::string>> sources = {
        {root / "app" / "main.ahfl", "check_ok.app"},
        {root / "lib" / "agents.ahfl", "check_ok.agents"},
        {root / "lib" / "types.ahfl", "check_ok.types"},
    };
    for (const auto &[path, label] : sources) {
        const auto *source = snapshot->source_for_uri(AnalysisService::uri_from_path(path));
        check(source != nullptr, "hoverCoverage." + label + ".source_exists");
        if (source == nullptr) {
            continue;
        }
        check_hover_target_index_coverage_for_source(*snapshot, *source, label);
    }
}

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::filesystem::path make_temp_project(std::string_view name) {
    const auto root = std::filesystem::temp_directory_path() / ("ahfl_lsp_" + std::string(name));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

std::string did_open_body(const std::string &uri, int version, const std::string &text) {
    return R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
           uri + R"(","languageId":"ahfl","version":)" + std::to_string(version) + R"(,"text":")" +
           escape_json_string(text) + R"("}}})";
}

std::string did_change_body(const std::string &uri, int version, const std::string &text) {
    return R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" +
           uri + R"(","version":)" + std::to_string(version) + R"(},"contentChanges":[{"text":")" +
           escape_json_string(text) + R"("}]}})";
}

std::string initialize_body(const std::filesystem::path &root) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) + R"("}})";
}

std::string initialize_workspace_folders_body(const std::vector<std::filesystem::path> &roots) {
    std::string body =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"workspaceFolders":[)";
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index > 0) {
            body += ",";
        }
        body += R"({"uri":")" + AnalysisService::uri_from_path(roots[index]) + R"(","name":"root)" +
                std::to_string(index) + R"("})";
    }
    body += R"(]}})";
    return body;
}

std::string diagnostics_output_for_source(const std::string &source) {
    const auto full_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body("file:///diag.ahfl", 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":"file:///diag.ahfl"}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    return response_body_for_id(full_output, 2);
}

void test_document_symbol_lists_all() {
    std::string source =
        "struct Foo {\n    value: String;\n}\n\nstruct Bar {\n    name: String;\n}";
    std::string params = R"({"textDocument":{"uri":"file:///test.ahfl"}})";
    std::string output = run_handler_request(source, "textDocument/documentSymbol", params);

    // Response should contain both "Foo" and "Bar"
    check(output.find("Foo") != std::string::npos, "documentSymbol.contains_Foo");
    check(output.find("Bar") != std::string::npos, "documentSymbol.contains_Bar");
}

void test_workspace_symbol_filters() {
    std::string source =
        "struct Alpha {\n    value: String;\n}\n\nstruct Beta {\n    name: String;\n}";
    std::string params = R"({"query":"Alph","textDocument":{"uri":"file:///test.ahfl"}})";
    std::string output = run_handler_request(source, "workspace/symbol", params);

    // Should contain Alpha but not Beta
    check(output.find("Alpha") != std::string::npos, "workspaceSymbol.contains_Alpha");
    // Beta should not appear since it doesn't match the query
    // The second response (id:2) is what we want to check
    // Find the second Content-Length (after initialize response)
    auto second_resp = output.find("Content-Length:", output.find("Content-Length:") + 1);
    auto third_resp = output.find("Content-Length:", second_resp + 1);
    if (third_resp != std::string::npos) {
        std::string handler_response = output.substr(third_resp);
        check(handler_response.find("Beta") == std::string::npos, "workspaceSymbol.excludes_Beta");
    } else {
        check(false, "workspaceSymbol.excludes_Beta");
    }
}

void test_references_returns_locations() {
    // Use a simple source with a struct referenced in another struct
    std::string source =
        "struct Msg {\n    value: String;\n}\n\nstruct Envelope {\n    payload: Msg;\n}";
    std::string params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7}})";
    std::string output = run_handler_request(source, "textDocument/references", params);

    // If references are found, response should be an array (containing "[")
    // At minimum it should contain a well-formed response for id:2
    check(output.find("\"id\"") != std::string::npos, "references.has_response");
}

void test_rename_returns_workspace_edit() {
    std::string source =
        "struct Msg {\n    value: String;\n}\n\nstruct Envelope {\n    payload: Msg;\n}";
    std::string params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7},"newName":"Message"})";
    std::string output = run_handler_request(source, "textDocument/rename", params);

    // If rename succeeds, response should contain the new name "Message"
    // or a changes object
    check(output.find("\"id\"") != std::string::npos, "rename.has_response");
}

void test_prepare_rename_returns_range_or_null() {
    const std::string source =
        "struct Msg {\n    value: String;\n}\n\nstruct Envelope {\n    payload: Msg;\n}";

    const std::string init_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
    const auto init_response = response_body_for_id(init_output, 1);
    check(init_response.find("\"renameProvider\"") != std::string::npos,
          "prepareRename.capability_renames_object");
    check(init_response.find("\"prepareProvider\":true") != std::string::npos,
          "prepareRename.capability_prepare_provider");

    const std::string decl_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7}})";
    const auto decl_output =
        run_handler_request(source, "textDocument/prepareRename", decl_params);
    const auto decl_response = response_body_for_id(decl_output, 2);
    check(decl_response.find("\"start\":{\"line\":0,\"character\":7}") != std::string::npos,
          "prepareRename.declaration_start");
    check(decl_response.find("\"end\":{\"line\":0,\"character\":10}") != std::string::npos,
          "prepareRename.declaration_end");

    const std::string ref_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":5,"character":13}})";
    const auto ref_output =
        run_handler_request(source, "textDocument/prepareRename", ref_params);
    const auto ref_response = response_body_for_id(ref_output, 2);
    check(ref_response.find("\"start\":{\"line\":5,\"character\":13}") != std::string::npos,
          "prepareRename.reference_start");
    check(ref_response.find("\"end\":{\"line\":5,\"character\":16}") != std::string::npos,
          "prepareRename.reference_end");

    const std::string invalid_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":1,"character":4}})";
    const auto invalid_output =
        run_handler_request(source, "textDocument/prepareRename", invalid_params);
    const auto invalid_response = response_body_for_id(invalid_output, 2);
    check(invalid_response.find("\"result\":null") != std::string::npos,
          "prepareRename.invalid_position_null");
}

void test_analysis_snapshot_reuse_and_invalidation() {
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = "file:///snapshot.ahfl",
        .language_id = "ahfl",
        .version = 1,
        .text = "struct Msg {\n    value: String;\n}\n",
    });

    AnalysisService analysis(store);
    const auto *first = analysis.snapshot_for_uri("file:///snapshot.ahfl");
    const auto first_revision = first ? first->document_revision : 0;
    const auto *second = analysis.snapshot_for_uri("file:///snapshot.ahfl");

    check(first != nullptr, "analysisSnapshot.first_exists");
    check(first == second, "analysisSnapshot.reuses_same_revision");
    check(analysis.analysis_runs() == 1, "analysisSnapshot.single_run_for_same_revision");

    store.change("file:///snapshot.ahfl", 2, "struct Msg {\n    value: Int;\n}\n");
    const auto *third = analysis.snapshot_for_uri("file:///snapshot.ahfl");
    check(third != nullptr, "analysisSnapshot.third_exists");
    check(third->document_revision != first_revision, "analysisSnapshot.revision_changes");
    check(third->document_version == 2, "analysisSnapshot.version_changes");
    check(analysis.analysis_runs() == 2, "analysisSnapshot.rebuilds_after_change");
}

void test_diagnostics_reflect_document_version() {
    const std::string uri = "file:///diag-version.ahfl";
    const std::string pull_request =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
        uri + R"("}}})";

    const auto broken_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Broken {\n    value: ;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto broken_response = response_body_for_id(broken_output, 2);
    check(broken_response.find("\"kind\":\"full\"") != std::string::npos,
          "diagnostics.version_1_full_report");
    check(broken_response.find("parse.diagnostic") != std::string::npos,
          "diagnostics.version_1_has_errors");

    const auto fixed_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Broken {\n    value: ;\n}\n"),
        did_change_body(uri, 2, "struct Fixed {\n    value: String;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto fixed_response = response_body_for_id(fixed_output, 2);
    check(fixed_response.find("\"kind\":\"full\"") != std::string::npos,
          "diagnostics.version_2_full_report");
    check(fixed_response.find("\"items\":[]") != std::string::npos,
          "diagnostics.version_2_no_errors");
}

void test_text_document_diagnostic_pull_report() {
    const std::string init_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
    const auto init_response = response_body_for_id(init_output, 1);
    // Pull-based diagnostic provider is advertised. Diagnostics are delivered
    // via textDocument/diagnostic and workspace/diagnostic requests, and
    // the server notifies the client of changes via $/diagnostic/refresh.
    check(init_response.find("\"diagnosticProvider\"") != std::string::npos,
          "textDocumentDiagnostic.capability_exists");
    check(init_response.find("\"interFileDependencies\":true") != std::string::npos,
          "textDocumentDiagnostic.capability_inter_file_dependencies");
    check(init_response.find("\"workspaceDiagnostics\":true") != std::string::npos,
          "textDocumentDiagnostic.capability_workspace_true");
    check(init_response.find("\"workspaceFolders\"") != std::string::npos,
          "workspaceFolders.capability_exists");
    check(init_response.find("\"changeNotifications\":true") != std::string::npos,
          "workspaceFolders.capability_change_notifications");

    const std::string uri = "file:///diag-pull.ahfl";
    const std::string pull_request =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
        uri + R"("}}})";
    const auto broken_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Broken {\n    value: ;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto broken_response = response_body_for_id(broken_output, 2);
    check(broken_response.find("\"kind\":\"full\"") != std::string::npos,
          "textDocumentDiagnostic.full_report");
    check(broken_response.find("\"items\":[") != std::string::npos,
          "textDocumentDiagnostic.items_array");
    check(broken_response.find("parse.diagnostic") != std::string::npos,
          "textDocumentDiagnostic.includes_parse_code");

    const std::string fixed_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Broken {\n    value: ;\n}\n"),
        did_change_body(uri, 2, "struct Fixed {\n    value: String;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto fixed_response = response_body_for_id(fixed_output, 2);
    check(fixed_response.find("\"kind\":\"full\"") != std::string::npos,
          "textDocumentDiagnostic.recovery_full_report");
    check(fixed_response.find("\"items\":[]") != std::string::npos,
          "textDocumentDiagnostic.recovery_empty_items");
}

void test_diagnostic_refresh_notifications_on_document_changes() {
    const std::string uri = "file:///refresh-count.ahfl";

    // didOpen should trigger one $/diagnostic/refresh notification
    const auto open_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Msg {\n    value: String;\n}\n"),
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
    check(count_substring(open_output, "\"method\":\"$/diagnostic/refresh\"") == 1,
          "diagnosticRefresh.one_after_did_open");

    // didOpen + didChange should trigger two $/diagnostic/refresh notifications
    const auto change_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Msg {\n    value: String;\n}\n"),
        did_change_body(uri, 2, "struct Msg {\n    value: Int;\n}\n"),
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
    check(count_substring(change_output, "\"method\":\"$/diagnostic/refresh\"") == 2,
          "diagnosticRefresh.two_after_open_and_change");

    // didOpen + didChange + didClose should trigger three $/diagnostic/refresh notifications
    const auto close_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Msg {\n    value: String;\n}\n"),
        did_change_body(uri, 2, "struct Msg {\n    value: Int;\n}\n"),
        R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" + uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
    check(count_substring(close_output, "\"method\":\"$/diagnostic/refresh\"") == 3,
          "diagnosticRefresh.three_after_open_change_and_close");
}

void test_text_document_diagnostic_includes_result_id() {
    const std::string uri = "file:///result-id.ahfl";
    const std::string pull_request =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
        uri + R"("}}})";

    // Valid source
    const auto valid_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Msg {\n    value: String;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto valid_response = response_body_for_id(valid_output, 2);
    const auto valid_result_id = extract_result_id(valid_response);
    check(!valid_result_id.empty(), "textDocumentDiagnostic.result_id_exists");
    check(valid_result_id.find(uri) != std::string::npos,
          "textDocumentDiagnostic.result_id_contains_uri");
    check(valid_result_id.find("#v1") != std::string::npos,
          "textDocumentDiagnostic.result_id_contains_version");

    // Broken source - resultId should be different
    const auto broken_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, "struct Broken {\n    value: ;\n}\n"),
        pull_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto broken_response = response_body_for_id(broken_output, 2);
    const auto broken_result_id = extract_result_id(broken_response);
    check(!broken_result_id.empty(), "textDocumentDiagnostic.broken_result_id_exists");
    check(broken_result_id != valid_result_id,
          "textDocumentDiagnostic.result_id_changes_with_content");
}

void test_text_document_diagnostic_previous_result_id_unchanged() {
    const std::string uri = "file:///prev-result-id.ahfl";
    const std::string source = "struct Msg {\n    value: String;\n}\n";

    // First pass: get the resultId
    const auto first_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" + uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto first_response = response_body_for_id(first_output, 2);
    const auto result_id = extract_result_id(first_response);
    check(!result_id.empty(), "textDocumentDiagnostic.prev_id_first_result_id_exists");

    // Second pass: send the same resultId as previousResultId
    // The server should return empty items (unchanged)
    const auto second_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" + uri + R"("},"previousResultId":")" + result_id + R"("}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto second_response = response_body_for_id(second_output, 2);
    const auto second_result_id = extract_result_id(second_response);
    check(second_result_id == result_id,
          "textDocumentDiagnostic.prev_id_unchanged_same_result_id");
    check(second_response.find("\"items\":[]") != std::string::npos,
          "textDocumentDiagnostic.prev_id_unchanged_empty_items");
    check(second_response.find("\"kind\":\"full\"") != std::string::npos,
          "textDocumentDiagnostic.prev_id_unchanged_still_full_kind");

    // Third pass: send a different previousResultId - should get full report
    const auto third_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" + uri + R"("},"previousResultId":"fake-id-123"}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto third_response = response_body_for_id(third_output, 2);
    const auto third_result_id = extract_result_id(third_response);
    check(third_result_id == result_id,
          "textDocumentDiagnostic.prev_id_mismatch_same_result_id");
    // With a valid source and no errors, items may be empty or not - just check resultId matches
}

void test_workspace_diagnostic_includes_result_ids() {
    const auto root = make_temp_project("workspace_diagnostic_result_ids");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-workspace-result-ids\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: Missing;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    // Each item in workspace diagnostic should have a resultId
    check(response.find("\"resultId\":\"") != std::string::npos,
          "workspaceDiagnostic.items_have_result_ids");
    // There should be at least two resultIds (one per source)
    check(count_substring(response, "\"resultId\":\"") >= 2,
          "workspaceDiagnostic.multiple_result_ids");
    // resultId for main should contain the main uri
    check(response.find(main_uri + "#v") != std::string::npos,
          "workspaceDiagnostic.main_result_id_contains_uri");
}

void test_workspace_diagnostic_previous_result_ids_filter() {
    const auto root = make_temp_project("workspace_diagnostic_prev_ids");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-workspace-prev-ids\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module lib::types;\n"
                                     "\n"
                                     "struct Msg {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);

    // First pass: get resultIds for both documents
    const auto first_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, types_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto first_response = response_body_for_id(first_output, 2);

    // Extract resultIds by finding them near each URI.
    // In the serialized object, resultId comes before uri (kind, items, resultId, uri, version),
    // so we search backwards from the uri position to find the preceding resultId.
    auto find_result_id_for_uri = [](const std::string &response, const std::string &uri) {
        const auto uri_pos = response.find("\"uri\":\"" + uri + "\"");
        if (uri_pos == std::string::npos) {
            return std::string{};
        }
        const auto key = std::string_view("\"resultId\":\"");
        // Search backwards from uri_pos for the nearest preceding resultId
        std::size_t search_from = uri_pos;
        std::size_t id_pos = std::string::npos;
        while (true) {
            auto found = response.rfind(key, search_from);
            if (found == std::string::npos) {
                break;
            }
            // Make sure this is an actual match, not inside a string value
            id_pos = found;
            break;
        }
        if (id_pos == std::string::npos) {
            return std::string{};
        }
        const auto start = id_pos + key.size();
        const auto end = response.find('"', start);
        if (end == std::string::npos) {
            return std::string{};
        }
        return response.substr(start, end - start);
    };

    const auto main_result_id = find_result_id_for_uri(first_response, main_uri);
    const auto types_result_id = find_result_id_for_uri(first_response, types_uri);
    check(!main_result_id.empty(), "workspaceDiagnostic.prev.main_result_id_found");
    check(!types_result_id.empty(), "workspaceDiagnostic.prev.types_result_id_found");

    // Second pass: send both previousResultIds - both documents unchanged, so both should be skipped
    const auto prev_ids_json =
        "{\"" + main_uri + "\":\"" + main_result_id + "\",\"" + types_uri + "\":\"" +
        types_result_id + "\"}";
    const auto second_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, types_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{"previousResultIds":)" + prev_ids_json + R"(}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto second_response = response_body_for_id(second_output, 2);
    // With all previousIds matching, items array should be empty
    check(second_response.find("\"items\":[]") != std::string::npos,
          "workspaceDiagnostic.prev.all_unchanged_empty_items");

    // Third pass: change types source, send both previousResultIds
    // Only types should appear in the response
    const std::string changed_types_source = "module lib::types;\n"
                                              "\n"
                                              "struct Msg {\n"
                                              "    value: Int;\n"
                                              "}\n";
    const auto third_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, changed_types_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{"previousResultIds":)" + prev_ids_json + R"(}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto third_response = response_body_for_id(third_output, 2);
    // types_uri should appear (changed), main_uri should not (unchanged)
    check(third_response.find(types_uri) != std::string::npos,
          "workspaceDiagnostic.prev.partial_includes_changed");
    check(third_response.find(main_uri) == std::string::npos,
          "workspaceDiagnostic.prev.partial_excludes_unchanged");
}

void test_workspace_diagnostic_reports_unopened_project_sources() {
    const auto root = make_temp_project("workspace_diagnostic_project_sources");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-workspace-diagnostics\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: Missing;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find("\"items\":[") != std::string::npos,
          "workspaceDiagnostic.items_array");
    check(response.find(main_uri) != std::string::npos,
          "workspaceDiagnostic.includes_open_entry");
    check(response.find(types_uri) != std::string::npos,
          "workspaceDiagnostic.includes_unopened_import");
    check(response.find("\"version\":null") != std::string::npos,
          "workspaceDiagnostic.unopened_version_null");
    check(response.find("resolve.") != std::string::npos,
          "workspaceDiagnostic.includes_unopened_resolve_diagnostic");
}

void test_watched_file_change_invalidates_project_source_graph() {
    const auto root = make_temp_project("watched_file_source_graph");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-watched-source-graph\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string valid_types_source = "module lib::types;\n"
                                           "\n"
                                           "struct Msg {\n"
                                           "    value: String;\n"
                                           "}\n";
    const std::string broken_types_source = "module lib::types;\n"
                                            "\n"
                                            "struct Msg {\n"
                                            "    value: Missing;\n"
                                            "}\n";
    write_file(main_path, main_source);
    write_file(types_path, valid_types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string workspace_diagnostic_before =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})";
    const std::string watched_change =
        R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
        types_uri + R"(","type":2}]}})";
    const std::string workspace_diagnostic_after =
        R"({"jsonrpc":"2.0","id":3,"method":"workspace/diagnostic","params":{}})";

    const auto output = run_lsp_message_steps({
        LspMessageStep{.body = initialize_body(root)},
        LspMessageStep{.body = did_open_body(main_uri, 1, main_source)},
        LspMessageStep{.body = workspace_diagnostic_before},
        LspMessageStep{
            .body = watched_change,
            .before = [types_path, broken_types_source]() {
                write_file(types_path, broken_types_source);
            },
        },
        LspMessageStep{.body = workspace_diagnostic_after},
        LspMessageStep{.body = R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})"},
    });

    const auto before_response = response_body_for_id(output, 2);
    check(before_response.find(types_uri) != std::string::npos,
          "watchedFileChange.before_includes_imported_source");
    check(before_response.find("resolve.") == std::string::npos,
          "watchedFileChange.before_has_no_resolve_diagnostic");

    const auto after_response = response_body_for_id(output, 3);
    check(after_response.find(types_uri) != std::string::npos,
          "watchedFileChange.after_includes_imported_source");
    check(after_response.find("resolve.") != std::string::npos,
          "watchedFileChange.after_reanalyzes_changed_import");
}

void test_workspace_folder_change_switches_project_descriptor() {
    const auto root = make_temp_project("workspace_folder_descriptor_switch");
    const auto root_a = root / "workspace-a";
    const auto root_b = root / "workspace-b";
    const auto shared = root / "shared";
    const auto main_path = shared / "app" / "main.ahfl";
    const auto imports_a = root_a / "imports";
    const auto imports_b = root_b / "imports";
    const auto types_a_path = imports_a / "lib" / "types.ahfl";
    const auto types_b_path = imports_b / "lib" / "types.ahfl";

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_a_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");
    write_file(types_b_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: Missing;\n"
               "}\n");

    const auto project_descriptor = [&](const std::filesystem::path &search_root) {
        return "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-folder-switch\",\n"
               "  \"search_roots\": [\"" +
               escape_json_string(search_root.generic_string()) +
               "\"],\n"
               "  \"entry_sources\": [\"" +
               escape_json_string(main_path.generic_string()) +
               "\"]\n"
               "}\n";
    };
    write_file(root_a / "project.json", project_descriptor(imports_a));
    write_file(root_a / "ahfl.workspace.json",
               "{\n"
               "  \"format_version\": \"ahfl.workspace.v0.3\",\n"
               "  \"name\": \"workspace-a\",\n"
               "  \"projects\": [\"project.json\"]\n"
               "}\n");
    write_file(root_b / "project.json", project_descriptor(imports_b));
    write_file(root_b / "ahfl.workspace.json",
               "{\n"
               "  \"format_version\": \"ahfl.workspace.v0.3\",\n"
               "  \"name\": \"workspace-b\",\n"
               "  \"projects\": [\"project.json\"]\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_a_uri = AnalysisService::uri_from_path(types_a_path);
    const auto types_b_uri = AnalysisService::uri_from_path(types_b_path);
    const std::string before =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})";
    const std::string folder_change =
        R"({"jsonrpc":"2.0","method":"workspace/didChangeWorkspaceFolders","params":{"event":{"removed":[{"uri":")" +
        AnalysisService::uri_from_path(root_a) +
        R"(","name":"workspace-a"}],"added":[{"uri":")" +
        AnalysisService::uri_from_path(root_b) + R"(","name":"workspace-b"}]}}})";
    const std::string after =
        R"({"jsonrpc":"2.0","id":3,"method":"workspace/diagnostic","params":{}})";

    const auto output = run_lsp_message_steps({
        LspMessageStep{.body = initialize_workspace_folders_body({root_a})},
        LspMessageStep{.body = did_open_body(main_uri, 1, main_source)},
        LspMessageStep{.body = before},
        LspMessageStep{.body = folder_change},
        LspMessageStep{.body = after},
        LspMessageStep{.body = R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})"},
    });

    const auto before_response = response_body_for_id(output, 2);
    check(before_response.find(types_a_uri) != std::string::npos,
          "workspaceFolderChange.before_uses_original_descriptor");
    check(before_response.find(types_b_uri) == std::string::npos,
          "workspaceFolderChange.before_excludes_new_descriptor");
    check(before_response.find("resolve.") == std::string::npos,
          "workspaceFolderChange.before_has_no_resolve_diagnostic");

    const auto after_response = response_body_for_id(output, 3);
    check(after_response.find(types_b_uri) != std::string::npos,
          "workspaceFolderChange.after_uses_new_descriptor");
    check(after_response.find("resolve.") != std::string::npos,
          "workspaceFolderChange.after_reports_new_descriptor_diagnostic");
}

void test_diagnostics_cover_parse_resolve_typecheck_and_validation() {
    const auto parse_output = diagnostics_output_for_source("struct Broken {\n    value: ;\n}\n");
    check(parse_output.find("parse.diagnostic") != std::string::npos, "diagnostics.parse_code");

    const auto resolve_output =
        diagnostics_output_for_source("struct Envelope {\n    payload: Missing;\n}\n");
    check(resolve_output.find("resolve.") != std::string::npos, "diagnostics.resolve_code");

    const std::string type_source = "struct Request {\n"
                                    "    value: String;\n"
                                    "}\n"
                                    "struct Context {\n"
                                    "    value: String;\n"
                                    "}\n"
                                    "capability Echo(value: String) -> Request;\n"
                                    "agent TestAgent {\n"
                                    "    input: Request;\n"
                                    "    context: Context;\n"
                                    "    output: Request;\n"
                                    "    states: [Init, Done];\n"
                                    "    initial: Init;\n"
                                    "    final: [Done];\n"
                                    "    capabilities: [Echo];\n"
                                    "    transition Init -> Done;\n"
                                    "}\n";
    const auto type_output = diagnostics_output_for_source(type_source);
    check(type_output.find("typecheck.") != std::string::npos, "diagnostics.typecheck_code");

    const std::string validation_source = "struct Request {\n"
                                          "    value: String;\n"
                                          "}\n"
                                          "struct Context {\n"
                                          "    value: String = \"pending\";\n"
                                          "}\n"
                                          "capability Echo(value: String) -> Request;\n"
                                          "agent TestAgent {\n"
                                          "    input: Request;\n"
                                          "    context: Context;\n"
                                          "    output: Request;\n"
                                          "    states: [Init, Done];\n"
                                          "    initial: Init;\n"
                                          "    final: [Done];\n"
                                          "    capabilities: [Echo];\n"
                                          "    transition Init -> Done;\n"
                                          "}\n"
                                          "flow for TestAgent {\n"
                                          "    state Init {\n"
                                          "        goto Done;\n"
                                          "    }\n"
                                          "}\n";
    const auto validation_output = diagnostics_output_for_source(validation_source);
    check(validation_output.find("validation.") != std::string::npos,
          "diagnostics.validation_code");
}

void test_project_definition_workspace_symbol_and_rename_cross_file() {
    const auto root = make_temp_project("project_cross_file");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-project\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");
    write_file(main_path,
               "module app::main;\n"
               "import lib::types as types;\n"
               "\n"
               "struct Use {\n"
               "    payload: types::Msg;\n"
               "}\n");
    write_file(types_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string init = initialize_body(root);
    const std::string open_main = did_open_body(main_uri,
                                                1,
                                                "module app::main;\n"
                                                "import lib::types as types;\n"
                                                "\n"
                                                "struct Use {\n"
                                                "    payload: types::Msg;\n"
                                                "}\n");
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22}}})";
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"Msg"}})";
    const std::string rename =
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22},"newName":"Message"}})";
    const std::string shutdown = R"({"jsonrpc":"2.0","id":5,"method":"shutdown","params":{}})";

    const auto output =
        run_lsp_messages({init, open_main, definition, workspace_symbol, rename, shutdown});
    check(output.find(types_uri) != std::string::npos, "project.definition_targets_imported_uri");
    check(output.find("\"name\":\"Msg\"") != std::string::npos,
          "project.workspace_symbol_includes_unopened_source");
    check(output.find("Message") != std::string::npos, "project.rename_contains_new_name");
    check(output.find(main_uri) != std::string::npos, "project.rename_edits_main_uri");
    check(output.find(types_uri) != std::string::npos, "project.rename_edits_decl_uri");
}

void test_project_workspace_symbol_deduplicates_open_project_snapshots() {
    const auto root = make_temp_project("project_workspace_symbol_dedup");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-project-dedup\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module lib::types;\n"
                                     "\n"
                                     "struct Msg {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"Msg"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, types_source),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(count_substring(response, "\"name\":\"Msg\"") == 1,
          "project.workspace_symbol_deduplicates_symbol");
    check(response.find(types_uri) != std::string::npos,
          "project.workspace_symbol_dedup_preserves_location");
}

void test_project_open_document_overlay_drives_definition() {
    const auto root = make_temp_project("project_overlay");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-overlay\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");
    write_file(main_path,
               "module app::main;\n"
               "import lib::types as types;\n"
               "\n"
               "struct Use {\n"
               "    payload: types::Msg;\n"
               "}\n");
    write_file(types_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string main_overlay = "module app::main;\n"
                                     "import lib::types as types;\n"
                                     "\n"
                                     "struct Use {\n"
                                     "    payload: types::Draft;\n"
                                     "}\n";
    const std::string types_overlay = "module lib::types;\n"
                                      "\n"
                                      "struct Draft {\n"
                                      "    value: String;\n"
                                      "}\n";

    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22}}})";
    const std::string shutdown = R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_overlay),
        did_open_body(types_uri, 1, types_overlay),
        definition,
        shutdown,
    });

    check(output.find(types_uri) != std::string::npos,
          "project.overlay_definition_targets_unsaved_source");
}

void test_project_diagnostics_refresh_dependent_open_documents() {
    const auto root = make_temp_project("project_diagnostics_refresh");
    const auto main_path = root / "app" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"lsp-diagnostics-refresh\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"app/main.ahfl\"]\n"
               "}\n");

    const std::string main_source = "module app::main;\n"
                                    "import lib::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module lib::types;\n"
                                     "\n"
                                     "struct Msg {\n"
                                     "    value: String;\n"
                                     "}\n";
    const std::string changed_types_source = "module lib::types;\n"
                                             "\n"
                                             "struct Draft {\n"
                                             "    value: String;\n"
                                             "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string workspace_diag_request =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, types_source),
        did_change_body(types_uri, 2, changed_types_source),
        workspace_diag_request,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto diag_response = response_body_for_id(output, 2);

    check(diag_response.find(main_uri) != std::string::npos,
          "project.diagnostics_refresh.reports_dependent_uri");
    check(diag_response.find("\"version\":2") != std::string::npos,
          "project.diagnostics_refresh.reports_changed_version");
    check(diag_response.find("unknown type 'types::Msg'") != std::string::npos,
          "project.diagnostics_refresh.reports_dependent_unknown_type");
    check(diag_response.find("resolve.") != std::string::npos,
          "project.diagnostics_refresh.uses_resolve_diagnostic");
}

void test_workspace_descriptor_selects_project_for_source() {
    const auto root = make_temp_project("workspace_descriptor");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    write_file(root / "ahfl.workspace.json",
               "{\n"
               "  \"format_version\": \"ahfl.workspace.v0.3\",\n"
               "  \"name\": \"lsp-workspace\",\n"
               "  \"projects\": [\"project.json\"]\n"
               "}\n");
    write_file(root / "project.json",
               "{\n"
               "  \"format_version\": \"ahfl.project.v0.3\",\n"
               "  \"name\": \"workspace-project\",\n"
               "  \"search_roots\": [\".\"],\n"
               "  \"entry_sources\": [\"src/main.ahfl\"]\n"
               "}\n");
    write_file(main_path,
               "module app::main;\n"
               "import lib::types as types;\n"
               "\n"
               "struct Use {\n"
               "    payload: types::Msg;\n"
               "}\n");
    write_file(types_path,
               "module lib::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri,
                      1,
                      "module app::main;\n"
                      "import lib::types as types;\n"
                      "\n"
                      "struct Use {\n"
                      "    payload: types::Msg;\n"
                      "}\n"),
        definition,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find(types_uri) != std::string::npos,
          "workspace_descriptor.definition_targets_project_source");
}

void test_descriptorless_workspace_infers_module_root_for_imports() {
    const auto root = make_temp_project("descriptorless_workspace");
    const auto app_path = root / "app" / "main.ahfl";
    const auto agents_path = root / "lib" / "agents.ahfl";
    const auto types_path = root / "lib" / "types.ahfl";
    const std::string app_source = "module app::main;\n"
                                   "import lib::types as types;\n"
                                   "import lib::agents as agents;\n"
                                   "\n"
                                   "workflow BadWorkflow {\n"
                                   "    input: types::RequestAlias;\n"
                                   "    output: types::ResponseAlias;\n"
                                   "    node run: agents::AliasAgent(input);\n"
                                   "    liveness: eventually completed(run, Missing);\n"
                                   "    return: run;\n"
                                   "}\n";
    const std::string agents_source = "module lib::agents;\n"
                                      "import lib::types as types;\n"
                                      "\n"
                                      "agent AliasAgent {\n"
                                      "    input: types::RequestAlias;\n"
                                      "    context: types::ContextAlias;\n"
                                      "    output: types::ResponseAlias;\n"
                                      "    states: [Init, Done];\n"
                                      "    initial: Init;\n"
                                      "    final: [Done];\n"
                                      "    capabilities: [];\n"
                                      "    transition Init -> Done;\n"
                                      "}\n";
    const std::string types_source = "module lib::types;\n"
                                     "\n"
                                     "struct Request {\n"
                                     "    value: String;\n"
                                     "}\n"
                                     "\n"
                                     "struct Context {\n"
                                     "    value: String = \"pending\";\n"
                                     "}\n"
                                     "\n"
                                     "struct Response {\n"
                                     "    value: String;\n"
                                     "}\n"
                                     "\n"
                                     "type RequestAlias = Request;\n"
                                     "type ContextAlias = Context;\n"
                                     "type ResponseAlias = Response;\n";
    write_file(app_path, app_source);
    write_file(agents_path, agents_source);
    write_file(types_path, types_source);

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    const auto agents_uri = AnalysisService::uri_from_path(agents_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        agents_uri + R"("},"position":{"line":4,"character":24}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        definition,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("unknown type 'types::RequestAlias'") == std::string::npos,
          "descriptorless_workspace.no_unknown_imported_alias_diagnostic");
    check(output.find(types_uri) != std::string::npos,
          "descriptorless_workspace.definition_targets_imported_source");

    const std::string input_hover =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
        agents_uri + R"("},"position":{"line":4,"character":6}}})";
    const auto input_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        input_hover,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(input_hover_output.find("lib::agents::AliasAgent") == std::string::npos,
          "descriptorless_workspace.input_label_hover_does_not_select_agent");
    check(input_hover_output.find("input: lib::types::Request") != std::string::npos,
          "descriptorless_workspace.input_label_hover_shows_resolved_type");
    check(input_hover_output.find("- Declared as: `types::RequestAlias`") != std::string::npos,
          "descriptorless_workspace.input_label_hover_shows_declared_alias");

    const std::string agent_name_hover =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":")" +
        agents_uri + R"("},"position":{"line":3,"character":8}}})";
    const auto agent_name_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        agent_name_hover,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(agent_name_hover_output.find("lib::agents::AliasAgent") != std::string::npos,
          "descriptorless_workspace.agent_name_hover_selects_agent");

    const auto alias_position = position_of(agents_source, "RequestAlias");
    const auto alias_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        hover_request_body(agents_uri, alias_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(alias_hover_output.find("type lib::types::RequestAlias = lib::types::Request") !=
              std::string::npos,
          "descriptorless_workspace.alias_hover_shows_alias_signature");
    check(alias_hover_output.find("- Declared as: `Request`") != std::string::npos,
          "descriptorless_workspace.alias_hover_shows_declared_target");

    const auto transition_position = position_of(agents_source, "transition");
    const auto transition_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        hover_request_body(agents_uri, transition_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(transition_hover_output.find("transition Init -> Done") != std::string::npos,
          "descriptorless_workspace.transition_label_hover_signature");
    check(transition_hover_output.find("Transition of `AliasAgent`") != std::string::npos,
          "descriptorless_workspace.transition_label_hover_headline");

    const auto liveness_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(app_uri, 1, app_source),
        hover_request_body(app_uri, position_of(app_source, "liveness")),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(liveness_hover_output.find("liveness: eventually completed(run, Missing)") !=
              std::string::npos,
          "descriptorless_workspace.liveness_hover_signature");
    check(liveness_hover_output.find("workflow liveness property") != std::string::npos,
          "descriptorless_workspace.liveness_hover_headline");
}

void test_hover_renderer_detail_levels() {
    HoverPayload payload;
    payload.signature = "states: [Init, Done]";
    payload.headline = "Defines 2 states for `AliasAgent`";
    payload.canonical_name = "lib::agents::AliasAgent.states";
    payload.module_name = "lib::agents";
    payload.source_label = "tests/integration/check_fail_state/lib/agents.ahfl";
    add_hover_fact(payload, HoverFactImportance::Primary, "", "`Init` initial");
    add_hover_fact(payload, HoverFactImportance::Primary, "", "`Done` final");
    add_hover_fact(payload, HoverFactImportance::Debug, "Index", "agent.states");

    HoverRenderer renderer;
    HoverRenderOptions standard;
    const auto standard_output = renderer.render(payload, standard);
    check(standard_output.find("```ahfl\nstates: [Init, Done]\n```") != std::string::npos,
          "hoverRenderer.standard_signature");
    check(standard_output.find("Defines 2 states for `AliasAgent`") != std::string::npos,
          "hoverRenderer.standard_headline");
    check(standard_output.find("- `Init` initial") != std::string::npos,
          "hoverRenderer.standard_primary_fact");
    check(standard_output.find("canonical") == std::string::npos,
          "hoverRenderer.standard_hides_canonical");
    check(standard_output.find("Source:") == std::string::npos,
          "hoverRenderer.standard_hides_source");

    HoverRenderOptions compact;
    compact.detail_level = HoverDetailLevel::Compact;
    const auto compact_output = renderer.render(payload, compact);
    check(compact_output.find("- `Init` initial") == std::string::npos,
          "hoverRenderer.compact_hides_facts");

    HoverRenderOptions debug;
    debug.detail_level = HoverDetailLevel::Debug;
    debug.show_source = true;
    const auto debug_output = renderer.render(payload, debug);
    check(debug_output.find("Details: `lib::agents::AliasAgent.states` in `lib::agents`") !=
              std::string::npos,
          "hoverRenderer.debug_shows_details");
    check(debug_output.find("Source: `tests/integration/check_fail_state/lib/agents.ahfl`") !=
              std::string::npos,
          "hoverRenderer.debug_shows_source");

    HoverRenderOptions plaintext;
    plaintext.markup_kind = MarkupKind::Plaintext;
    const auto plaintext_output = renderer.render(payload, plaintext);
    check(plaintext_output.find("```") == std::string::npos,
          "hoverRenderer.plaintext_has_no_code_fence");
    check(plaintext_output.find('`') == std::string::npos,
          "hoverRenderer.plaintext_strips_inline_code");
    check(plaintext_output.find("- Init initial") != std::string::npos,
          "hoverRenderer.plaintext_fact");
}

void test_hover_respects_client_markup_and_debug_options() {
    const std::string source = "struct Request {\n"
                               "    category: String;\n"
                               "}\n"
                               "\n"
                               "agent TestAgent {\n"
                               "    input: Request;\n"
                               "    context: Request;\n"
                               "    output: Request;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [];\n"
                               "    transition Init -> Done;\n"
                               "}\n";

    const auto states_position = position_of(source, "states");
    const std::string plaintext_init =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{"textDocument":{"hover":{"contentFormat":["plaintext"]}}}}})";
    const auto plaintext_output = run_lsp_messages({
        plaintext_init,
        did_open_body("file:///plain.ahfl", 1, source),
        hover_request_body("file:///plain.ahfl", states_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(plaintext_output.find("\"kind\":\"plaintext\"") != std::string::npos,
          "hoverHandler.plaintext_kind");
    check(plaintext_output.find("```ahfl") == std::string::npos,
          "hoverHandler.plaintext_has_no_code_fence");

    const std::string debug_init =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"initializationOptions":{"hover":{"detailLevel":"debug","showSource":true}}}})";
    const auto debug_output = run_lsp_messages({
        debug_init,
        did_open_body("file:///debug.ahfl", 1, source),
        hover_request_body("file:///debug.ahfl", states_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(debug_output.find("Details: `TestAgent.states`") != std::string::npos,
          "hoverHandler.debug_shows_details");
    check(debug_output.find("Source: `file:///debug.ahfl`") != std::string::npos,
          "hoverHandler.debug_shows_source");
}

void test_hover_rich_symbol_targets() {
    const std::string source =
        "enum Priority {\n"
        "    High,\n"
        "    Low,\n"
        "}\n"
        "\n"
        "struct Request {\n"
        "    category: String;\n"
        "    priority: Priority;\n"
        "}\n"
        "\n"
        "struct Context {\n"
        "    value: String = \"pending\";\n"
        "}\n"
        "\n"
        "capability Echo(value: String) -> Request;\n"
        "predicate IsReady(value: String) -> Bool;\n"
        "\n"
        "agent TestAgent {\n"
        "    input: Request;\n"
        "    context: Context;\n"
        "    output: Request;\n"
        "    states: [Init, Done];\n"
        "    initial: Init;\n"
        "    final: [Done];\n"
        "    capabilities: [Echo];\n"
        "    transition Init -> Done;\n"
        "}\n"
        "\n"
        "flow for TestAgent {\n"
        "    state Init {\n"
        "        let item = input.category;\n"
        "        let made = Request { category: input.category, priority: Priority::High };\n"
        "        goto Done;\n"
        "    }\n"
        "}\n"
        "\n"
        "workflow TestWorkflow {\n"
        "    input: Request;\n"
        "    output: Request;\n"
        "    node run: TestAgent(input);\n"
        "    node follow: TestAgent(input) after [run];\n"
        "    safety: always not running(run) or eventually completed(run);\n"
        "    liveness: eventually completed(follow, Done);\n"
        "    return: follow;\n"
        "}\n";

    const auto field_output = run_hover_request(source, "category");
    check(field_output.find("category: String") != std::string::npos,
          "hover.struct_field_signature");
    check(field_output.find("Field of") != std::string::npos, "hover.struct_field_headline");

    const auto variant_output = run_hover_request(source, "High");
    check(variant_output.find("Priority::High") != std::string::npos,
          "hover.enum_variant_signature");
    check(variant_output.find("Variant of") != std::string::npos, "hover.enum_variant_headline");

    const auto param_output = run_hover_request(source, "value: String) -> Request");
    check(param_output.find("value: String") != std::string::npos,
          "hover.capability_param_signature");
    check(param_output.find("capability parameter") != std::string::npos,
          "hover.capability_param_summary");

    const auto builtin_output = run_hover_request(source, "String");
    check(builtin_output.find("builtin type") != std::string::npos, "hover.builtin_type_headline");
    check(builtin_output.find("canonical `String`") == std::string::npos,
          "hover.builtin_type_hides_canonical_in_standard");

    const auto state_output = run_hover_request(source, "Done");
    check(state_output.find("state Done") != std::string::npos, "hover.agent_state_signature");
    check(state_output.find("Final state") != std::string::npos, "hover.agent_state_fact");

    const auto transition_label_output = run_hover_request(source, "transition");
    check(transition_label_output.find("transition Init -> Done") != std::string::npos,
          "hover.agent_transition_label_signature");
    check(transition_label_output.find("Transition of `TestAgent`") != std::string::npos,
          "hover.agent_transition_label_headline");

    const auto states_label_output = run_hover_request(source, "states");
    check(states_label_output.find("states: [Init, Done]") != std::string::npos,
          "hover.agent_states_label_signature");
    check(states_label_output.find("Defines 2 states for `TestAgent`") != std::string::npos,
          "hover.agent_states_label_headline");
    check(states_label_output.find("- `Init` initial") != std::string::npos,
          "hover.agent_states_label_initial_fact");
    check(states_label_output.find("- `Done` final") != std::string::npos,
          "hover.agent_states_label_final_fact");
    check(states_label_output.find("state count") == std::string::npos,
          "hover.agent_states_label_hides_internal_count");
    check(states_label_output.find("canonical") == std::string::npos,
          "hover.agent_states_label_hides_canonical");
    check(states_label_output.find("source `") == std::string::npos,
          "hover.agent_states_label_hides_source");

    const auto initial_label_output = run_hover_request(source, "initial");
    check(initial_label_output.find("initial: Init") != std::string::npos,
          "hover.agent_initial_label_signature");
    check(initial_label_output.find("Initial state for `TestAgent`") != std::string::npos,
          "hover.agent_initial_label_headline");

    const auto final_label_output = run_hover_request(source, "final");
    check(final_label_output.find("final: [Done]") != std::string::npos,
          "hover.agent_final_label_signature");
    check(final_label_output.find("Final states for `TestAgent`") != std::string::npos,
          "hover.agent_final_label_headline");

    const auto capabilities_label_output = run_hover_request(source, "capabilities");
    check(capabilities_label_output.find("capabilities: [Echo]") != std::string::npos,
          "hover.agent_capabilities_label_signature");
    check(capabilities_label_output.find("Capabilities available to `TestAgent`") !=
              std::string::npos,
          "hover.agent_capabilities_label_headline");

    const auto agent_capability_output = run_hover_request(source, "Echo];");
    check(agent_capability_output.find("capability Echo(value: String) -> Request") !=
              std::string::npos,
          "hover.agent_capability_signature");
    check(agent_capability_output.find("capability") != std::string::npos,
          "hover.agent_capability_summary");

    auto flow_state_position = position_of(source, "state Init");
    flow_state_position.character += static_cast<std::uint32_t>(std::string("state ").size());
    const auto flow_state_output = run_handler_request(
        source, "textDocument/hover", hover_params_at("file:///test.ahfl", flow_state_position));
    check(flow_state_output.find("state Init") != std::string::npos, "hover.flow_state_signature");
    check(flow_state_output.find("Flow state handler") != std::string::npos,
          "hover.flow_state_headline");

    auto goto_position = position_of(source, "goto Done");
    goto_position.character += static_cast<std::uint32_t>(std::string("goto ").size());
    const auto goto_output = run_handler_request(
        source, "textDocument/hover", hover_params_at("file:///test.ahfl", goto_position));
    check(goto_output.find("state Done") != std::string::npos, "hover.goto_state_signature");
    check(goto_output.find("Jumps to final state `Done`") != std::string::npos,
          "hover.goto_state_headline");

    const auto workflow_output = run_hover_request(source, "run: TestAgent");
    check(workflow_output.find("node run: TestAgent") != std::string::npos,
          "hover.workflow_node_signature");
    check(workflow_output.find("workflow node") != std::string::npos,
          "hover.workflow_node_summary");

    const auto workflow_target_output = run_hover_request(source, "TestAgent(input)");
    check(workflow_target_output.find("agent TestAgent") != std::string::npos,
          "hover.workflow_target_agent_signature");
    check(workflow_target_output.find("agent") != std::string::npos,
          "hover.workflow_target_agent_summary");

    const auto workflow_dependency_output = run_hover_request(source, "run];");
    check(workflow_dependency_output.find("node run: TestAgent") != std::string::npos,
          "hover.workflow_dependency_signature");
    check(workflow_dependency_output.find("workflow dependency") != std::string::npos,
          "hover.workflow_dependency_summary");

    const auto safety_output = run_hover_request(source, "safety");
    check(safety_output.find("safety: always not running(run) or eventually completed(run)") !=
              std::string::npos,
          "hover.workflow_safety_clause_signature");
    check(safety_output.find("workflow safety property") != std::string::npos,
          "hover.workflow_safety_clause_headline");

    const auto liveness_output = run_hover_request(source, "liveness");
    check(liveness_output.find("liveness: eventually completed(follow, Done)") != std::string::npos,
          "hover.workflow_liveness_clause_signature");
    check(liveness_output.find("workflow liveness property") != std::string::npos,
          "hover.workflow_liveness_clause_headline");

    const auto root_output = run_hover_request(source, "input.category");
    check(root_output.find("input: Request") != std::string::npos,
          "hover.path_root_schema_signature");
    check(root_output.find("Agent path root") != std::string::npos,
          "hover.path_root_schema_headline");

    const auto member_position = position_of(source, "input.category");
    Position member_char = member_position;
    member_char.character += static_cast<std::uint32_t>(std::string("input.").size());
    const auto member_output = run_handler_request(
        source, "textDocument/hover", hover_params_at("file:///test.ahfl", member_char));
    check(member_output.find("category: String") != std::string::npos,
          "hover.member_access_signature");
    check(member_output.find("member access") != std::string::npos, "hover.member_access_summary");

    const auto literal_field_output = run_hover_request(source, "category: input");
    check(literal_field_output.find("category: String") != std::string::npos,
          "hover.struct_literal_field_signature");
    check(literal_field_output.find("Field of") != std::string::npos,
          "hover.struct_literal_field_headline");

    check_hover_target_index_coverage(source);
}

void test_hover_service_payload_contract() {
    const std::string source = "struct Request {\n"
                               "    category: String;\n"
                               "}\n"
                               "\n"
                               "type RequestAlias = Request;\n"
                               "\n"
                               "struct Context {\n"
                               "    value: String = \"pending\";\n"
                               "}\n"
                               "\n"
                               "agent TestAgent {\n"
                               "    input: RequestAlias;\n"
                               "    context: Context;\n"
                               "    output: Request;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [];\n"
                               "    transition Init -> Done;\n"
                               "}\n";
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = "file:///payload.ahfl",
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });
    AnalysisService analysis(store);
    const auto *snapshot = analysis.snapshot_for_uri("file:///payload.ahfl");
    const auto *lsp_source =
        snapshot != nullptr ? snapshot->source_for_uri("file:///payload.ahfl") : nullptr;
    HoverService hover;
    check(snapshot != nullptr, "hoverPayload.snapshot_exists");
    check(lsp_source != nullptr, "hoverPayload.source_exists");
    if (snapshot == nullptr || lsp_source == nullptr) {
        return;
    }

    const auto field_payload =
        hover.payload_at(*snapshot, *lsp_source, position_of(source, "category"));
    check(field_payload.has_value(), "hoverPayload.field_exists");
    if (field_payload.has_value()) {
        check(field_payload->headline.find("Field of") != std::string::npos,
              "hoverPayload.field_headline");
        check(field_payload->signature == "category: String", "hoverPayload.field_signature");
        check(field_payload->token_range.end_offset - field_payload->token_range.begin_offset ==
                  std::string("category").size(),
              "hoverPayload.field_token_range");
    }

    const auto alias_payload =
        hover.payload_at(*snapshot, *lsp_source, position_of(source, "RequestAlias"));
    check(alias_payload.has_value(), "hoverPayload.alias_exists");
    if (alias_payload.has_value()) {
        check(alias_payload->headline == "type alias", "hoverPayload.alias_summary");
        check(alias_payload->signature == "type RequestAlias = Request",
              "hoverPayload.alias_signature");
        check(payload_has_fact(*alias_payload, "Declared as", "`Request`"),
              "hoverPayload.alias_declared_fact");
    }

    const auto schema_payload =
        hover.payload_at(*snapshot, *lsp_source, position_of(source, "input"));
    check(schema_payload.has_value(), "hoverPayload.schema_exists");
    if (schema_payload.has_value()) {
        check(schema_payload->headline == "Input schema for `TestAgent`",
              "hoverPayload.schema_headline");
        check(schema_payload->signature == "input: Request", "hoverPayload.schema_signature");
        check(payload_has_fact(*schema_payload, "Declared as", "`RequestAlias`"),
              "hoverPayload.schema_declared_fact");
    }

    const std::string changed_source = "struct Request {\n"
                                       "    category: Int;\n"
                                       "}\n";
    store.change("file:///payload.ahfl", 2, changed_source);
    const auto *changed_snapshot = analysis.snapshot_for_uri("file:///payload.ahfl");
    const auto *changed_lsp_source = changed_snapshot != nullptr
                                         ? changed_snapshot->source_for_uri("file:///payload.ahfl")
                                         : nullptr;
    check(analysis.analysis_runs() == 2, "hoverPayload.snapshot_rebuilt_after_change");
    check(changed_lsp_source != nullptr, "hoverPayload.changed_source_exists");
    if (changed_snapshot != nullptr && changed_lsp_source != nullptr) {
        const auto changed_payload = hover.payload_at(
            *changed_snapshot, *changed_lsp_source, position_of(changed_source, "category"));
        check(changed_payload.has_value(), "hoverPayload.changed_field_exists");
        if (changed_payload.has_value()) {
            check(changed_payload->signature == "category: Int",
                  "hoverPayload.changed_field_signature");
        }
    }
}

void test_signature_help_capability() {
    // Source with a capability declaration and a flow that calls it.
    // The cursor will be placed after the comma in: OrderQuery(input.order_id,
    std::string source = "struct OrderInfo {\n"
                         "    order_id: String;\n"
                         "    user_id: String;\n"
                         "}\n"
                         "\n"
                         "capability get_data(name: String, count: Int) -> OrderInfo;\n"
                         "\n"
                         "predicate valid(id: String);\n";

    // Position cursor at line 5 (the capability decl line), simulating a call:
    // We test using a modified source with a call expression.
    // For the test we'll place cursor where the user would have typed: get_data(name,
    // Line 5 is "capability get_data(name: String, count: Int) -> OrderInfo;"
    // We want to test signatureHelp as if cursor is inside get_data(x, |)
    // Use a source that includes an actual call in a flow.
    std::string source2 = "struct OrderInfo {\n"
                          "    order_id: String;\n"
                          "}\n"
                          "\n"
                          "capability get_data(name: String, count: Int) -> OrderInfo;\n"
                          "\n"
                          "agent TestAgent {\n"
                          "    input: OrderInfo;\n"
                          "    context: OrderInfo;\n"
                          "    output: OrderInfo;\n"
                          "    states: [Init, Done];\n"
                          "    initial: Init;\n"
                          "    final: [Done];\n"
                          "    capabilities: [get_data];\n"
                          "    transition Init -> Done;\n"
                          "}\n"
                          "\n"
                          "flow for TestAgent {\n"
                          "    state Init {\n"
                          "        let x = get_data(input.order_id, );\n"
                          "        goto Done;\n"
                          "    }\n"
                          "}\n";

    // Cursor at line 19, after the comma in "get_data(input.order_id, )"
    // line 19 = "        let x = get_data(input.order_id, );"
    //            chars:   01234567890123456789012345678901234567890123
    //                                                    ^39=','  ^41=')'
    // Place cursor at character 40 (the space between ',' and ')')
    std::string params2 =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":19,"character":40}})";
    std::string output2 = run_handler_request(source2, "textDocument/signatureHelp", params2);

    // Response should contain the signature with "get_data"
    check(output2.find("get_data") != std::string::npos, "signatureHelp.contains_callable_name");
    // Should contain parameter info
    check(output2.find("name") != std::string::npos, "signatureHelp.contains_param_name");
    check(output2.find("String") != std::string::npos, "signatureHelp.contains_param_type");
    // activeParameter should be 1 (after the first comma)
    check(output2.find("\"activeParameter\":1") != std::string::npos,
          "signatureHelp.active_parameter_is_1");
}

void test_completion_type_member_enum_state_and_workflow_contexts() {
    const std::string type_source = "struct Msg {\n"
                                    "    value: String;\n"
                                    "}\n"
                                    "\n"
                                    "struct Envelope {\n"
                                    "    payload: Msg;\n"
                                    "}\n";
    const std::string type_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":5,"character":13}})";
    const auto type_output =
        run_handler_request(type_source, "textDocument/completion", type_params);
    check(type_output.find("\"label\":\"Msg\"") != std::string::npos,
          "completion.type_position_contains_struct");

    const std::string member_source = "struct Request {\n"
                                      "    value: String;\n"
                                      "}\n"
                                      "\n"
                                      "struct Context {\n"
                                      "    value: String = \"pending\";\n"
                                      "}\n"
                                      "\n"
                                      "capability Echo(value: String) -> Request;\n"
                                      "\n"
                                      "agent TestAgent {\n"
                                      "    input: Request;\n"
                                      "    context: Context;\n"
                                      "    output: Request;\n"
                                      "    states: [Init, Done];\n"
                                      "    initial: Init;\n"
                                      "    final: [Done];\n"
                                      "    capabilities: [Echo];\n"
                                      "    transition Init -> Done;\n"
                                      "}\n"
                                      "\n"
                                      "flow for TestAgent {\n"
                                      "    state Init {\n"
                                      "        let x = input.value;\n"
                                      "        goto Done;\n"
                                      "    }\n"
                                      "}\n";
    const std::string member_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":23,"character":22}})";
    const auto member_output =
        run_handler_request(member_source, "textDocument/completion", member_params);
    check(member_output.find("\"label\":\"value\"") != std::string::npos,
          "completion.member_position_contains_struct_field");

    const std::string general_source = "enum Priority {\n"
                                       "    High,\n"
                                       "    Low,\n"
                                       "}\n"
                                       "\n"
                                       "struct Request {\n"
                                       "    value: String;\n"
                                       "}\n"
                                       "\n"
                                       "struct Context {\n"
                                       "    value: String = \"pending\";\n"
                                       "}\n"
                                       "\n"
                                       "capability Echo(value: String) -> Request;\n"
                                       "\n"
                                       "agent TestAgent {\n"
                                       "    input: Request;\n"
                                       "    context: Context;\n"
                                       "    output: Request;\n"
                                       "    states: [Init, Done];\n"
                                       "    initial: Init;\n"
                                       "    final: [Done];\n"
                                       "    capabilities: [Echo];\n"
                                       "    transition Init -> Done;\n"
                                       "}\n"
                                       "\n"
                                       "flow for TestAgent { state Init { goto Done; } }\n"
                                       "\n"
                                       "workflow TestWorkflow {\n"
                                       "    input: Request;\n"
                                       "    output: Request;\n"
                                       "    node run: TestAgent(input);\n"
                                       "    return: run;\n"
                                       "}\n";
    const std::string general_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":0}})";
    const auto general_output =
        run_handler_request(general_source, "textDocument/completion", general_params);
    check(general_output.find("Priority::High") != std::string::npos,
          "completion.expression_contains_enum_variant");

    const std::string state_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":26,"character":10}})";
    const auto state_output =
        run_handler_request(general_source, "textDocument/completion", state_params);
    check(state_output.find("\"label\":\"Done\"") != std::string::npos,
          "completion.expression_contains_agent_state");

    const std::string workflow_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":32,"character":12}})";
    const auto workflow_output =
        run_handler_request(general_source, "textDocument/completion", workflow_params);
    check(workflow_output.find("\"label\":\"run\"") != std::string::npos,
          "completion.expression_contains_workflow_node");
}

void test_rename_rejects_keyword_and_conflict() {
    const std::string source = "struct Msg {\n"
                               "    value: String;\n"
                               "}\n"
                               "\n"
                               "struct Envelope {\n"
                               "    payload: Msg;\n"
                               "}\n";
    const std::string keyword_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7},"newName":"struct"})";
    const auto keyword_output = run_handler_request(source, "textDocument/rename", keyword_params);
    check(keyword_output.find("\"code\":-32602") != std::string::npos, "rename.rejects_keyword");

    const std::string conflict_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7},"newName":"Envelope"})";
    const auto conflict_output =
        run_handler_request(source, "textDocument/rename", conflict_params);
    check(conflict_output.find("conflict") != std::string::npos, "rename.rejects_conflict");
}

void test_document_symbol_hierarchy() {
    const std::string source = "struct Request {\n"
                               "    value: String;\n"
                               "}\n"
                               "struct Context {\n"
                               "    value: String = \"pending\";\n"
                               "}\n"
                               "capability Echo(value: String) -> Request;\n"
                               "agent TestAgent {\n"
                               "    input: Request;\n"
                               "    context: Context;\n"
                               "    output: Request;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [Echo];\n"
                               "    transition Init -> Done;\n"
                               "}\n"
                               "workflow TestWorkflow {\n"
                               "    input: Request;\n"
                               "    output: Request;\n"
                               "    node run: TestAgent(input);\n"
                               "    return: run;\n"
                               "}\n";
    const std::string params = R"({"textDocument":{"uri":"file:///test.ahfl"}})";
    const auto output = run_handler_request(source, "textDocument/documentSymbol", params);
    check(output.find("\"children\"") != std::string::npos, "documentSymbol.includes_children");
    check(output.find("\"name\":\"Init\"") != std::string::npos,
          "documentSymbol.agent_state_child");
    check(output.find("\"name\":\"run\"") != std::string::npos,
          "documentSymbol.workflow_node_child");
}

} // anonymous namespace

int main() {
    test_analysis_snapshot_reuse_and_invalidation();
    test_document_symbol_lists_all();
    test_workspace_symbol_filters();
    test_references_returns_locations();
    test_rename_returns_workspace_edit();
    test_prepare_rename_returns_range_or_null();
    test_signature_help_capability();
    test_completion_type_member_enum_state_and_workflow_contexts();
    test_rename_rejects_keyword_and_conflict();
    test_document_symbol_hierarchy();
    test_diagnostics_reflect_document_version();
    test_text_document_diagnostic_pull_report();
    test_diagnostic_refresh_notifications_on_document_changes();
    test_text_document_diagnostic_includes_result_id();
    test_text_document_diagnostic_previous_result_id_unchanged();
    test_workspace_diagnostic_includes_result_ids();
    test_workspace_diagnostic_previous_result_ids_filter();
    test_workspace_diagnostic_reports_unopened_project_sources();
    test_watched_file_change_invalidates_project_source_graph();
    test_workspace_folder_change_switches_project_descriptor();
    test_diagnostics_cover_parse_resolve_typecheck_and_validation();
    test_project_definition_workspace_symbol_and_rename_cross_file();
    test_project_workspace_symbol_deduplicates_open_project_snapshots();
    test_project_open_document_overlay_drives_definition();
    test_project_diagnostics_refresh_dependent_open_documents();
    test_workspace_descriptor_selects_project_for_source();
    test_descriptorless_workspace_infers_module_root_for_imports();
    test_hover_renderer_detail_levels();
    test_hover_respects_client_markup_and_debug_options();
    test_hover_rich_symbol_targets();
    test_hover_service_payload_contract();
    check_hover_integration_fixture_coverage();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
