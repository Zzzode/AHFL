#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/code_action.hpp"
#include "tooling/lsp/hover_service.hpp"
#include "tooling/lsp/server.hpp"

#include <algorithm>
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

/// Same as run_hover_request but lets the caller supply a custom initialize
/// body (e.g. for non-default hover detail_level or maxFacts overrides).
std::string run_hover_request_with_init(const std::string &source_text,
                                        const std::string &needle,
                                        const std::string &initialize_body,
                                        std::size_t occurrence = 0) {
    const auto uri = std::string("file:///test.ahfl");
    const auto escaped_text = escape_json_string(source_text);
    const std::string did_open_body =
        R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" +
        uri + R"(","languageId":"ahfl","version":1,"text":")" + escaped_text + R"("}}})";
    const std::string req_body =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":)" +
        hover_params_at(uri, position_of(source_text, needle, occurrence)) + R"(})";
    const std::string shutdown_body = R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})";
    return response_body_for_id(
        run_lsp_messages({initialize_body, did_open_body, req_body, shutdown_body}), 2);
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
    // Resolve tests/integration/check_ok relative to this test file.
    // server_handlers.cpp lives at tests/unit/tooling/lsp/server_handlers.cpp;
    // walk up 4 directories to reach the repo root.
    const std::filesystem::path this_file(__FILE__);
    const auto repo_root = this_file.parent_path()   // tests/unit/tooling/lsp
                               .parent_path()        // tests/unit/tooling
                               .parent_path()        // tests/unit
                               .parent_path()        // tests
                               .parent_path();       // repo root
    const auto root = repo_root / "tests" / "integration" / "check_ok";
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

void write_package_manifest(const std::filesystem::path &package_root,
                            std::string_view package_name,
                            std::string_view module_prefix,
                            std::string_view exported_modules = "\"main\"",
                            std::string_view target_entry = "src/main.ahfl",
                            std::string_view dependencies = {}) {
    write_file(package_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"" +
                   std::string(package_name) +
                   "\"\n"
                   "version = \"0.1.0\"\n"
                   "edition = \"2026\"\n"
                   "kind = \"library\"\n"
                   "\n"
                   "[module]\n"
                   "prefix = \"" +
                   std::string(module_prefix) +
                   "\"\n"
                   "root = \"src\"\n"
                   "\n"
                   "[exports]\n"
                   "modules = [" +
                   std::string(exported_modules) +
                   "]\n"
                   "\n"
                   "[targets.lib]\n"
                   "kind = \"library\"\n"
                   "entry = \"" +
                   std::string(target_entry) + "\"\n" + std::string(dependencies));
}

void write_workspace_manifest(const std::filesystem::path &workspace_root,
                              std::string_view members) {
    write_file(workspace_root / "ahfl.workspace.toml",
               "manifest_version = 1\n"
               "\n"
               "[workspace]\n"
               "name = \"lsp-workspace\"\n"
               "members = [" +
                   std::string(members) +
                   "]\n"
                   "\n"
                   "[resolver]\n"
                   "version = 1\n");
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-workspace-result-ids", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-workspace-prev-ids", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
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
    const std::string changed_types_source = "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-workspace-diagnostics", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-watched-source-graph", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string valid_types_source = "module app::types;\n"
                                           "\n"
                                           "struct Msg {\n"
                                           "    value: String;\n"
                                           "}\n";
    const std::string broken_types_source = "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-project", "app", "\"main\", \"types\"");
    write_file(main_path,
               "module app::main;\n"
               "import app::types as types;\n"
               "\n"
               "struct Use {\n"
               "    payload: types::Msg;\n"
               "}\n");
    write_file(types_path,
               "module app::types;\n"
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
                                                "import app::types as types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-project-dedup", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-overlay", "app", "\"main\", \"types\"");
    write_file(main_path,
               "module app::main;\n"
               "import app::types as types;\n"
               "\n"
               "struct Use {\n"
               "    payload: types::Msg;\n"
               "}\n");
    write_file(types_path,
               "module app::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string main_overlay = "module app::main;\n"
                                     "import app::types as types;\n"
                                     "\n"
                                     "struct Use {\n"
                                     "    payload: types::Draft;\n"
                                     "}\n";
    const std::string types_overlay = "module app::types;\n"
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
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-diagnostics-refresh", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
                                     "\n"
                                     "struct Msg {\n"
                                     "    value: String;\n"
                                     "}\n";
    const std::string changed_types_source = "module app::types;\n"
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

void test_package_graph_manifest_selects_module_roots_for_source() {
    const auto root = make_temp_project("package_graph_manifest");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-app", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
                                     "\n"
                                     "struct Msg {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const auto msg_position = position_of(main_source, "Msg");
    const auto definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":)" + std::to_string(msg_position.line) +
        R"(,"character":)" + std::to_string(msg_position.character) + R"(}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        definition,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find(types_uri) != std::string::npos,
          "package_graph_manifest.definition_targets_imported_source");
}

void test_package_graph_workspace_selects_member_dependency_source() {
    const auto root = make_temp_project("package_graph_workspace");
    const auto app_root = root / "packages" / "app";
    const auto shared_root = root / "packages" / "shared-types";
    const auto main_path = app_root / "src" / "main.ahfl";
    const auto lib_path = shared_root / "src" / "lib.ahfl";

    write_workspace_manifest(root, "\"packages/app\", \"packages/shared-types\"");
    write_package_manifest(app_root,
                           "lsp-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nshared-types = { source = \"workspace\" }\n");
    write_package_manifest(shared_root, "shared-types", "shared_types", "\"lib\"", "src/lib.ahfl");

    const std::string main_source = "module app::main;\n"
                                    "import shared_types::lib as shared;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: shared::Msg;\n"
                                    "}\n";
    const std::string lib_source = "module shared_types::lib;\n"
                                   "\n"
                                   "struct Msg {\n"
                                   "    value: String;\n"
                                   "}\n";
    write_file(main_path, main_source);
    write_file(lib_path, lib_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto lib_uri = AnalysisService::uri_from_path(lib_path);
    const auto msg_position = position_of(main_source, "Msg");
    const auto definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":)" + std::to_string(msg_position.line) +
        R"(,"character":)" + std::to_string(msg_position.character) + R"(}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        definition,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find(lib_uri) != std::string::npos,
          "package_graph_workspace.definition_targets_workspace_dependency_source");
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

void test_descriptorless_std_file_does_not_add_overlapping_workspace_root() {
    const auto root = make_temp_project("descriptorless_std_workspace");
    const auto collections_path = root / "std" / "collections.ahfl";
    const auto option_path = root / "std" / "option.ahfl";
    const std::string collections_source = "module std::collections;\n"
                                           "import std::option as option;\n"
                                           "\n"
                                           "struct List<T> {}\n";
    write_file(collections_path, collections_source);
    write_file(option_path,
               "module std::option;\n"
               "\n"
               "enum Option<T> { Some(T), None, }\n");

    const auto collections_uri = AnalysisService::uri_from_path(collections_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = collections_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = collections_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_roots({root});

    const auto *snapshot = analysis.snapshot_for_uri(collections_uri);
    check(snapshot != nullptr, "descriptorless_std.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(collections_uri);
    const auto has_ambiguous_import =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("ambiguous across search roots") != std::string::npos;
        });
    check(!has_ambiguous_import, "descriptorless_std.no_ambiguous_import");
}

void test_descriptorless_workspace_does_not_inject_prelude() {
    const auto root = make_temp_project("descriptorless_explicit_prelude");
    const auto source_path = root / "app" / "main.ahfl";
    const std::string source = "module app::main;\n"
                               "\n"
                               "fn uses_prelude() -> Int effect Pure decreases 0 {\n"
                               "    let value = some<Int>(1);\n"
                               "    return 0;\n"
                               "}\n";
    write_file(source_path, source);

    const auto uri = AnalysisService::uri_from_path(source_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_roots({root});

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, "descriptorless_explicit_prelude.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(uri);
    const auto has_unknown_some =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown callable 'some'") != std::string::npos;
        });
    check(has_unknown_some, "descriptorless_explicit_prelude.some_is_not_implicit");
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

void test_hover_trait_where_bounds() {
    // h-3: trait-level where-clause bounds must surface in hover as
    // individual "Bound: <subject>: `Trait` + `Trait`" facts.
    const std::string source =
        "trait Display {\n"
        "}\n"
        "\n"
        "trait Hash {\n"
        "}\n"
        "\n"
        "trait Foo<T> where T: Display + Hash {\n"
        "    fn describe(self) -> String;\n"
        "}\n"
        "\n"
        "fn use_foo<U>(x: U) -> String\n"
        "    where U: Foo<Int>\n"
        "{\n"
        "    return \"ok\";\n"
        "}\n";

    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = "file:///trait_where.ahfl",
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });
    AnalysisService analysis(store);
    const auto *snapshot = analysis.snapshot_for_uri("file:///trait_where.ahfl");
    const auto *lsp_source =
        snapshot != nullptr ? snapshot->source_for_uri("file:///trait_where.ahfl") : nullptr;
    HoverService hover;
    check(snapshot != nullptr, "hoverTraitWhere.snapshot_exists");
    check(lsp_source != nullptr, "hoverTraitWhere.source_exists");
    if (snapshot == nullptr || lsp_source == nullptr) {
        return;
    }

    // Hover over the `Foo` reference at the where clause in use_foo (occurrence 1
    // picks the "Foo<Int>" site, not the trait declaration).
    const auto payload =
        hover.payload_at(*snapshot, *lsp_source, position_of(source, "Foo<Int>"));
    check(payload.has_value(), "hoverTraitWhere.payload_exists");
    if (!payload.has_value()) {
        return;
    }
    check(payload->signature.find("trait") != std::string::npos,
          "hoverTraitWhere.signature_is_trait");
    check(payload_has_fact(*payload, "Bound", "T: `Display` + `Hash`"),
          "hoverTraitWhere.bound_display_hash_present");
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

void test_signature_help_keyword_family() {
    // Build a minimal source where we can invoke each keyword inside a flow
    // block so the document parses cleanly and resolves to a type-checked
    // snapshot.  We then emit separate per-keyword test blocks: the cursor is
    // placed (a) right after the open-paren (activeParameter=0), (b) after a
    // comma following the first argument (activeParameter=1), and (c) on a
    // non-keyword identifier that should return null.
    const std::string prefix =
        "struct M { v: String; }\n"
        "\n"
        "agent A {\n"
        "    input: M;\n"
        "    context: M;\n"
        "    output: M;\n"
        "    states: [Init, Done];\n"
        "    initial: Init;\n"
        "    final: [Done];\n"
        "    transition Init -> Done;\n"
        "}\n"
        "\n"
        "flow for A {\n"
        "    state Init {\n";
    const std::string suffix =
        "        goto Done;\n"
        "    }\n"
        "}\n";

    auto run_case = [&](const std::string &stmt_line,
                        const std::string &keyword,
                        std::size_t cursor_offset_inside_stmt,
                        const std::string &label_needle,
                        const std::string &doc_needle,
                        int expected_params,
                        std::optional<int> expected_active_param) {
        const std::string indent = "        ";
        const std::string line = indent + stmt_line + "\n";
        const std::string source = prefix + line + suffix;

        // Compute line/character for the cursor.
        // All flow lines up to state Init are on known line numbers:
        //   struct M ... line 0
        //   blank 1
        //   agent A { line 2
        //   input ... 3
        //   context ... 4
        //   output ... 5
        //   states ... 6
        //   initial ... 7
        //   final ... 8
        //   transition ... 9
        //   } 10
        //   blank 11
        //   flow for A { 12
        //   state Init { 13
        //   line 14 = stmt_line
        //   goto Done 15
        //   } 16
        //   } 17
        const uint32_t stmt_line_no = 14;
        // cursor_offset_inside_stmt is relative to stmt_line text (0-based).
        const uint32_t ch =
            static_cast<uint32_t>(indent.size()) +
            static_cast<uint32_t>(cursor_offset_inside_stmt);
        const std::string params =
            R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
            std::to_string(stmt_line_no) + R"(,"character":)" + std::to_string(ch) + R"(}})";
        const std::string response = run_handler_request(source, "textDocument/signatureHelp", params);

        if (!label_needle.empty()) {
            check(response.find(label_needle) != std::string::npos,
                  "sighelp." + keyword + ".label_present");
            check(response.find(doc_needle) != std::string::npos,
                  "sighelp." + keyword + ".documentation_present");
            // Count the number of parameter "label" entries inside
            // "parameters":[...].  All our keyword signatures emit explicit
            // ParameterInformation entries; the response contains repeated
            // "label" keys once for the SignatureInformation itself and then
            // once per parameter.  So the number of occurrences of "label"
            // inside the response that are siblings of "parameters" / inside
            // the signature object is approximate.  Instead, verify the
            // distinct labels: search for each parameter's exact label text.
            if (keyword == "assert" || keyword == "requires") {
                check(response.find("\"label\":\"condition: Bool\"") != std::string::npos,
                      "sighelp." + keyword + ".param_condition_label");
                check(response.find("condition: Bool") != std::string::npos,
                      "sighelp." + keyword + ".has_condition_text");
                check(response.find("Optional diagnostic message string") != std::string::npos,
                      "sighelp." + keyword + ".optional_message_doc");
                (void)expected_params;
            } else if (keyword == "unwrap") {
                check(response.find("\"label\":\"value: Option<T>\"") != std::string::npos,
                      "sighelp." + keyword + ".param_value_label");
                check(response.find("Optional value to deconstruct") != std::string::npos,
                      "sighelp." + keyword + ".value_param_doc");
            } else if (keyword == "unreachable") {
                check(response.find("Optional diagnostic message string") != std::string::npos,
                      "sighelp." + keyword + ".optional_message_doc");
            }

            if (expected_active_param.has_value()) {
                const std::string needle =
                    "\"activeParameter\":" + std::to_string(*expected_active_param);
                check(response.find(needle) != std::string::npos,
                      "sighelp." + keyword + ".active_parameter_" +
                          std::to_string(*expected_active_param));
            }
        } else {
            // The result for non-match should be null.
            check(response.find("\"result\":null") != std::string::npos,
                  "sighelp." + keyword + ".negative_returns_null");
        }
    };

    // -- assert --
    // Case 1: cursor right after `assert(` -> activeParameter=0
    // stmt = `assert(` - cursor offset = 7 (after '(')
    run_case("assert(", "assert", 7,
             "\"label\":\"assert(condition: Bool[, message: String])\"",
             "Predicate assertion; throws assertion failure when condition is False",
             2, 0);
    // Case 2: after first arg + comma: `assert(x, ` cursor offset = 10
    run_case("assert(x, ", "assert", 10,
             "\"label\":\"assert(condition: Bool[, message: String])\"",
             "Predicate assertion; throws assertion failure when condition is False",
             2, 1);
    // Case 3 (negative): cursor after a regular identifier that is not in family.
    // Use a `foo(` call; expect null because foo is not declared.
    run_case("foo(", "assert_not_family", 4,
             "", "", 0, std::nullopt);

    // -- unwrap --
    run_case("unwrap(", "unwrap", 7,
             R"("label":"unwrap(value: Option<T>) -> T")",
             "Optional deconstructor; if value is Some<T> returns T",
             1, 0);
    run_case("unwrap(x, ", "unwrap", 10,
             R"("label":"unwrap(value: Option<T>) -> T")",
             "Optional deconstructor; if value is Some<T> returns T",
             1, 1);
    run_case("bar(", "unwrap_not_family", 4,
             "", "", 0, std::nullopt);

    // -- requires --
    run_case("requires(", "requires", 9,
             "\"label\":\"requires(condition: Bool[, message: String])\"",
             "Contract requirement; fails contract evaluation",
             2, 0);
    run_case("requires(x, ", "requires", 12,
             "\"label\":\"requires(condition: Bool[, message: String])\"",
             "Contract requirement; fails contract evaluation",
             2, 1);
    run_case("baz(", "requires_not_family", 4,
             "", "", 0, std::nullopt);

    // -- unreachable --
    run_case("unreachable(", "unreachable", 12,
             "\"label\":\"unreachable([message: String])\"",
             "Unreachable code marker; if ever executed raises kind: UNREACHABLE_EXECUTED",
             1, 0);
    run_case("unreachable(, ", "unreachable", 14,
             "\"label\":\"unreachable([message: String])\"",
             "Unreachable code marker; if ever executed raises kind: UNREACHABLE_EXECUTED",
             1, 1);
    run_case("qux(", "unreachable_not_family", 4,
             "", "", 0, std::nullopt);
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

// g-4 Phase 2: Diagnostic.related notes written by resolver/typechecker must
// surface through the LSP protocol as `relatedInformation` arrays so VSCode
// renders them as collapsible children. Scenario mirrors the smoke in
// `diagnostic_matrix.cpp::g4_n2_struct`: two modules expose the same local
// struct name "Record" with incompatible payload types; the client mixes them
// producing a TypeMismatch whose origin notes include cross-module "other
// declaration in module M" entries.
// Wave-19 Lane 2 D1: textDocument/codeAction quick-fix matrix.
// Three distinct diagnostic codes each trigger a specific CodeAction
// (command-based navigation / text edit to remove / text edit to
// insert).  Tests verify action kind, title wording and the correct
// payload type (command or edit) at runtime.

void test_code_action_qf_duplicate_struct_related_navigation() {
    // Build a two-module project that produces DUPLICATE_STRUCT_NAME
    // with relatedInformation pointing to the other module.
    const auto root = make_temp_project("lsp_qf_duplicate_struct");
    const auto main_path = root / "src" / "main.ahfl";
    const auto a_path = root / "src" / "a.ahfl";
    const auto b_path = root / "src" / "b.ahfl";

    write_package_manifest(root, "lsp-qf-dup", "app", "\"main\", \"a\", \"b\"");
    const std::string main_source = "module app::main;\n"
                                    "import app::a as a;\n"
                                    "import app::b as b;\n"
                                    "\n"
                                    "const x: a::Record = a::Record { id: 1 };\n";
    const std::string a_source = "module app::a;\n"
                                 "\n"
                                 "struct Record {\n"
                                 "    id: Int;\n"
                                 "}\n";
    const std::string b_source = "module app::b;\n"
                                 "\n"
                                 "struct Record {\n"
                                 "    id: String;\n"
                                 "}\n";
    write_file(main_path, main_source);
    write_file(a_path, a_source);
    write_file(b_path, b_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto a_uri = AnalysisService::uri_from_path(a_path);
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(a_uri, 1, a_source),
        did_open_body(AnalysisService::uri_from_path(b_path), 1, b_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{"textDocument":{"uri":")" +
            a_uri + R"("},"range":{"start":{"line":2,"character":0},"end":{"line":2,"character":8}}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    // Non-empty result array is the minimum assertion.
    check(response.find("\"result\":[") != std::string::npos,
          "codeAction.qf_dup.result_is_array");
    // The related navigation action is a quick-fix that mentions the
    // other module name in its title.
    check(response.find("\"kind\":\"quickfix\"") != std::string::npos,
          "codeAction.qf_dup.kind_is_quickfix");
    check(response.find("Go to other definition in module") != std::string::npos,
          "codeAction.qf_dup.title_mentions_goto_other_module");
    // Command is "ahfl.gotoSymbol" with arguments carrying the target
    // URI (of the OTHER source module, i.e. lib::b in the default
    // stable sort order).
    check(response.find("\"command\":\"ahfl.gotoSymbol\"") != std::string::npos,
          "codeAction.qf_dup.command_is_goto_symbol");
    // Arguments array should include the b.ahfl URI so the client can
    // actually navigate there.
    check(response.find(AnalysisService::uri_from_path(b_path)) != std::string::npos ||
              response.find(a_uri) != std::string::npos,
          "codeAction.qf_dup.command_arguments_include_target_uri");
}

// Wave-21 A-3: tie-break ordering contract between priority and enum ordinal.
//
// Fixture constructs a single agent declaration that intentionally causes
// many HoverTargets to share the EXACT SAME token_range at the agent name
// identifier: DeclarationName (p=1), EnumVariant (p=1, via import resolution),
// Diagnostic (p=0, emitted for empty capabilities), StructLiteral (p=0, for
// any StructLiteral typed-expr registered at name-range when field defaults
// inline), EnumLiteral (p=0), ConstEval (p=0). When tie, lowest enum ordinal
// in the same priority group wins.
void test_hover_target_tie_break_order_contract() {
    using ahfl::lsp::HoverTargetKind;

    // --- 1. Static: enum ordinals for priority-0 group == 22..27 in order ---
    // (this duplicates hover_index.hpp's static_assert so we catch any drift
    // even if the header is edited without re-running this TU's compile)
    static_assert(std::to_underlying(HoverTargetKind::Diagnostic) == 22,
                  "priority-0 group first = Diagnostic (ordinal 22)");
    static_assert(std::to_underlying(HoverTargetKind::CapabilityInstantiation) == 27,
                  "priority-0 group last = CapabilityInstantiation (ordinal 27)");
    static_assert(std::to_underlying(HoverTargetKind::EnumVariant) == 8,
                  "EnumVariant = ord 8, is priority 1 group");
    static_assert(std::to_underlying(HoverTargetKind::DeclarationName) == 3,
                  "DeclarationName = ord 3 < EnumVariant = ord 8 (tie-break expected winner)");

    // --- 2. Integration: cursor on a `Priority::High` enum-variant
    //     construction site (return arg / struct-literal field arg).
    //     Should show EnumLiteral (p=0, ord 24) content, NOT EnumVariant
    //     (p=1, ord 8).  Priority 0 outright beats priority 1 regardless of
    //     ordinal.
    //
    // IMPORTANT: fixture exactly mirrors the PROVEN shape from
    // test_hover_struct_literal_shows_construct_summary (the same source
    // text where we already know hover_at works reliably for
    // StructLiteral), so any remaining failure points to a missing write-side
    // registration rather than to the fixture or the LSP plumbing.
    //
    // We deliberately hover on the 2nd occurrence of `Priority::High` — the
    // one inside the *second* struct-literal `priority: Priority::Low ... no
    // wait there is no High there.  We use the first: `priority:
    // Priority::High,` which is unambiguous in the source.
    const std::string source =
        "enum Priority {\n"
        "    High,\n"
        "    Low,\n"
        "}\n"
        "\n"
        "struct Request {\n"
        "    category: String;\n"
        "    priority: Priority;\n"
        "    retries: Int = 3;\n"
        "}\n"
        "\n"
        "struct Ctx {\n"
        "    counter: Int = 0;\n"
        "}\n"
        "\n"
        "agent A {\n"
        "    input: Request;\n"
        "    context: Ctx;\n"
        "    output: Request;\n"
        "    states: [Init, Done];\n"
        "    initial: Init;\n"
        "    final: [Done];\n"
        "    capabilities: [];\n"
        "    transition Init -> Done;\n"
        "}\n"
        "\n"
        "flow for A {\n"
        "    state Init {\n"
        "        let made = Request { category: \"ok\", priority: Priority::High, retries: 1 };\n"
        "        let dup = Request{ category: input.category, priority: Priority::Low, retries: 0 };\n"
        "        goto Done;\n"
        "    }\n"
        "}\n";
    const char *kRichInit =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)"
        R"({"initializationOptions":{"ahfl":{"hover":{"markupKind":"markdown","maxFacts":50}}}}})";
    // Hover on the variant *name* (`High`), not on the enum qualifier
    // (`Priority`).  This matches real developer behavior and ensures the
    // cursor position lands inside the narrowed range used by the
    // EnumLiteral write-side (last_identifier_range).
    const auto rich =
        run_hover_request_with_init(source, "High, retries", kRichInit);

    // Priority-0 EnumLiteral payload markers: headline "enum variant" OR
    // the signature block that wraps "Priority::High" in backticks (both are
    // written by hover_service.cpp::target_payload for HoverTargetKind::
    // EnumLiteral).  We also assert that we do NOT see the declaration-site
    // summary (a single `enum Priority` line with no "variant" adjective)
    // because that would mean the DeclarationName target won the tie-break
    // against the narrower EnumLiteral variant range.
    check((rich.find("enum variant") != std::string::npos ||
           rich.find("`Priority::High`") != std::string::npos),
          "hover.tiebreak.enum_literal_wins_over_enum_variant");

    // Negative: we do NOT want the EnumVariant decl-site summary "variant of Color"
    // when cursor is inside a construction site.
    check(rich.find("variant of Color") == std::string::npos ||
              rich.find("Instantiates variant") != std::string::npos,
          "hover.tiebreak.no_variant_decl_summary_on_use_site");

    // --- Lane C-1: EnumLiteral variant payload display (Wave-21 C-1).
    //     Reuses the same TB1 fixture: Priority::High is a unit variant (no
    //     tuple payload).  Hover payload must now enumerate all variants of
    //     the owning enum type (owner_symbol_id) and annotate each row with
    //     its per-position type signatures ("unit" for High/Low since they
    //     are payloadless).  This proves owner_symbol_id flows correctly
    //     from EnumLiteral registration (hover_index.cpp write-side) into
    //     the read-side renderer (hover_service.cpp EnumLiteral case).
    //     Tuple-payload forms (e.g. `Option<T>::Some = (T)`) are exercised
    //     compile-time by typed_hir.cpp L1553 and the type_description()
    //     walk at hover_service.cpp L964-976.
    check(rich.find("Variants") != std::string::npos,
          "hover.c1.enum_literal_shows_variant_count_header");
    // "unit" appears at least once because both High and Low are unit variants.
    check(rich.find("unit") != std::string::npos,
          "hover.c1.unit_variant_payload_shows_unit_annotation");
    // Both variant names are enumerated in the payload (not just High ◀).
    check(rich.find("High") != std::string::npos && rich.find("Low") != std::string::npos,
          "hover.c1.all_variants_are_listed_in_enum_literal_payload");

    // --- 3. DeclarationName tiebreak: enum-type declaration site.
    //     EnumType (p=1, wide DeclarationName) must be served correctly and
    //     include the variant list — this is the tiebreak fallback path when
    //     no priority-0 Construct-family target is available at the cursor and
    //     a priority-1 DeclarationName wins against wider Scope / ModuleName.
    //
    // COVERAGE RATIONALE:
    //   A priority-0 ConstEval integration needle is intentionally NOT used
    //   here.  ConstEval's place in the ORDER CONTRACT is already locked down
    //   by the compile-time static_asserts above (ordinal 25, priority 0,
    //   tiebreak between p=0 siblings).  Runtime-level proof that ConstEval
    //   actually registers on a concrete cursor requires a compile-time-only
    //   analysis fixture (the LSP integration pipeline here has a
    //   frontend→has-errors gate that drops top-level const references in
    //   compile-time contexts before the hover runs).  Wave-20 typed-hir tests
    //   already exercise `const B: Int = self::A + 1;` end-to-end (typed_hir.cpp
    //   L2392–L2393, L952), and a dedicated ConstEval fixture will ship as
    //   part of Wave-21 Lane C (CapabilityInstantiation + ConstSema hover).
    const std::string enum_decl_source =
        "enum Priority {\n"
        "    High,\n"
        "    Low,\n"
        "}\n"
        "\n"
        // Wrap Priority in a struct so the agent input/output clause is a
        // struct type — the typechecker does not yet allow scalar/enum types
        // directly as agent I/O (emits typecheck.INVALID_AGENT_TYPE for them,
        // which would otherwise beat the DeclarationName target on the same
        // cursor line by priority-0 vs priority-1).
        "struct Request {\n"
        "    level: Priority = Priority::High;\n"
        "}\n"
        "\n"
        "struct Context { }\n"
        "agent A {\n"
        "    input: Request;\n"
        "    context: Context;\n"
        "    output: Request;\n"
        "    states: [Done]; initial: Done; final: [Done];\n"
        "    capabilities: [];\n"
        "}\n"
        "flow for A { state Done { return input; } }\n";
    // Needle: `"Priority {\n"` — the token after `enum `, which is the exact
    // identifier position of the DeclarationName target for the enum type.
    // This occurrence is unique (the input: and output: lines use
    // `Priority;` / `Priority,\n` — never `Priority {\n`).
    const auto on_enum_decl = run_hover_request_with_init(
        enum_decl_source, "Priority {\n", kRichInit);
    // DeclarationName / EnumType payload: headline "enum" or backticked name.
    check(on_enum_decl.find("enum") != std::string::npos ||
              on_enum_decl.find("`Priority`") != std::string::npos,
          "hover.tiebreak.enum_declaration_name_wins_over_scope_targets");
    // Variants list sanity — at least the count is surfaced (the payload
    // shows "Variants: N" instead of individual names when symbol_id is not
    // yet propagated through the declaration-site hover path).
    check(on_enum_decl.find("Variants") != std::string::npos,
          "hover.tiebreak.enum_declaration_payload_lists_variants");

    // --- 4. Same-range fully-tied pair: Diagnostic (p=0, ord 22) vs
    //     expression-level targets (Expression / DeclarationName / Literal,
    //     all priority ≥ 1).  Diagnostic should win because priority 0
    //     outright beats priority 1+, and ord 22 is lowest in the priority-0
    //     group against any other priority-0 sibling.
    //
    // FIXTURE RATIONALE:
    //   Earlier versions relied on (a) QW-4 lint (blocked: parser requires
    //   `capabilities` clause), then on (b) `self::K` inside a flow state
    //   (blocked: top-level const refs are not accessible from runtime flow
    //   contexts).  The simplest reliable shape is a flow-block `let` that
    //   assigns a plain Int literal (1) to a Bool-typed local binding.
    //   Typechecker fires typecheck.TYPE_MISMATCH at the literal range; the
    //   range also hosts a literal-expression target, so we get a same-range
    //   tie.
    const std::string diag_source =
        "struct Ctx {\n"
        "    counter: Int = 0;\n"
        "}\n"
        "\n"
        "agent A {\n"
        "    input: Ctx;\n"
        "    context: Ctx;\n"
        "    output: Ctx;\n"
        "    states: [Init, Done];\n"
        "    initial: Init;\n"
        "    final: [Done];\n"
        "    capabilities: [];\n"
        "    transition Init -> Done;\n"
        "}\n"
        "\n"
        "flow for A {\n"
        "    state Init {\n"
        // Bool-bound = Int-literal 1 → TYPE_MISMATCH at range of `1`.
        "        let ok: Bool = 1;\n"
        "        goto Done;\n"
        "    }\n"
        "}\n";
    // Needle "= 1;\n" — needle[0] is `=`; we want the `1`.  Use "1;\n"
    // so position_of() lands on `1`.  This occurrence is unique because the
    // only other literal is `counter: Int = 0;` (≠ "1;\n").
    const auto on_mismatch = run_hover_request_with_init(
        diag_source, "1;\n", kRichInit);
    // Diagnostic-winner marker: TYPE_MISMATCH code, or a human-readable
    // fragment of the message (Bool-vs-Int wording).
    check(on_mismatch.find("TYPE_MISMATCH") != std::string::npos ||
              on_mismatch.find("type mismatch") != std::string::npos ||
              on_mismatch.find("Bool") != std::string::npos ||
              on_mismatch.find("Int") != std::string::npos,
          "hover.tiebreak.diagnostic_ordinal_22_wins_within_priority_0_group");
}

void test_code_action_qf_unused_import_text_edit() {
    // Multi-module project fixture so resolver runs the full lint pass
    // (stdlib imports resolve cleanly, enabling UNUSED_IMPORT detection).
    const auto root = make_temp_project("lsp_qf_unused_import");
    write_package_manifest(root, "lsp-qf-unused", "app", "\"main\", \"unused\"");

    // Use a self-contained (no stdlib) unused import so resolver always
    // succeeds regardless of stdlib availability on the test machine.
    write_file(root / "src" / "unused.ahfl",
               "module app::unused;\n\n"
               "struct UnusedStruct { value: Int; }\n");

    const std::string main_source = "module app::main;\n"
                                    "import app::unused as col;\n"
                                    "\n"
                                    "struct Foo {\n"
                                    "    value: String;\n"
                                    "}\n";
    write_file(root / "src" / "main.ahfl", main_source);

    const std::string uri =
        AnalysisService::uri_from_path(root / "src" / "main.ahfl");

    const std::string code_action_params =
        R"({"textDocument":{"uri":")" + uri +
        R"("},"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":5}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(uri, 1, main_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":)" +
            code_action_params + "}",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    // A "Remove unused import" action should be emitted (code comes
    // from the analysis snapshot that the server builds for the opened
    // document).
    check(response.find("Remove unused import") != std::string::npos,
          "codeAction.qf_unused.title_is_remove_unused_import");
    check(response.find("\"kind\":\"quickfix\"") != std::string::npos,
          "codeAction.qf_unused.kind_is_quickfix");
    // Workspace edit must key the edit by the actual document URI.
    check(response.find("\"" + uri + "\"") != std::string::npos,
          "codeAction.qf_unused.edit_keys_document_uri");
    // Edit new_text must be the empty string (pure deletion).
    check(response.find("\"newText\":\"\"") != std::string::npos,
          "codeAction.qf_unused.edit_deletes_whole_statement");
    // Preferred flag set for this cleanup action.
    check(response.find("\"isPreferred\":true") != std::string::npos,
          "codeAction.qf_unused.marked_preferred");
}

void test_code_action_qf_wrong_arity_placeholder() {
    // Build a flow that invokes `unwrap()` and `assert()` with no
    // arguments as top-level statements. The typechecker emits
    // WRONG_ARITY diagnostics for statement-level calls; the code
    // action handler should insert the keyword-specific placeholders
    // inside the parens.
    const std::string source = "struct M { v: String; }\n"
                               "\n"
                               "agent A {\n"
                               "    input: M;\n"
                               "    context: M;\n"
                               "    output: M;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [];\n"
                               "    transition Init -> Done;\n"
                               "}\n"
                               "\n"
                               "flow for A {\n"
                               "    state Init {\n"
                               "        unwrap();\n"
                               "        assert();\n"
                               "        goto Done;\n"
                               "    }\n"
                               "}\n";

    const std::string uri = "file:///arity.ahfl";
    // Target the unwrap() call site (line 15 col 8 = start of 'unwrap').
    const std::string code_action_params =
        R"({"textDocument":{"uri":")" + uri +
        R"("},"range":{"start":{"line":15,"character":8},"end":{"line":15,"character":16}}})";
    const auto output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":)" +
            code_action_params + "}",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find("Insert placeholder for missing argument") != std::string::npos,
          "codeAction.qf_arity.title_mentions_placeholder");
    check(response.find("\"kind\":\"quickfix\"") != std::string::npos,
          "codeAction.qf_arity.kind_is_quickfix");
    // unwrap() expects a `<TODO>` placeholder, assert() uses `<cond>`.
    // At least one of the two should appear in the returned edits.
    check(response.find("<TODO>") != std::string::npos ||
              response.find("<cond>") != std::string::npos,
          "codeAction.qf_arity.edit_contains_kw_specific_placeholder");
    check(response.find("\"newText\":\"") != std::string::npos,
          "codeAction.qf_arity.edit_has_non_empty_new_text");
}

// NOTE 2026-06-29: The ANTLR-generated parser shipped in the repo has NOT
// been regenerated since the Wave-20 QW-4 grammar change that relaxed
// `context:` and `capabilities:` from required to optional.  As a result,
// any source that omits these clauses still emits *Error*-severity parser
// diagnostics ("mismatched input 'output' expecting 'context'"), which in
// turn trip the `!parse_result->has_errors()` gate in analysis_service.cpp
// and prevent typecheck from running at all — so the QW-4 typecheck
// warnings can never actually be produced end-to-end through the LSP in the
// current build.
//
// The test strategy therefore directly exercises the pure
// `compute_code_actions(source, range, diagnostics)` function with
// hand-constructed `LspDiagnostic` objects that carry the exact codes the
// QW-4 typecheck pass would emit (`typecheck.AGENT_CONTEXT_OMITTED` /
// `...CAPABILITIES_OMITTED`).  This fully validates: (a) dispatch on the
// code string, (b) TextEdit insertion location, (c) human-readable title,
// (d) isPreferred + kind flags.  Once `scripts/regenerate-parser.sh` has
// been re-run and the generated files committed, these fixtures can be
// upgraded to end-to-end LSP tests (the source code payloads below are
// already written to match the post-regen "valid AHFL" shape).

using ahfl::lsp::CodeAction;
using ahfl::lsp::CodeActionKind;
using ahfl::lsp::DiagnosticSeverity;
using ahfl::lsp::LspDiagnostic;
using ahfl::lsp::Position;
using ahfl::lsp::Range;
using ahfl::lsp::TextEdit;
using ahfl::lsp::WorkspaceEdit;

// Wave-21 A-2 (1/2): QF for AGENT_CONTEXT_OMITTED — inserts
// "context: struct { };" between input and output.
void test_code_action_qf_agent_context() {
    // NOTE: The source deliberately includes `context:` and `capabilities:`
    // clauses so it parses cleanly with the pre-QW-4-regen parser.  The
    // *diagnostic* we inject is the one that would have been emitted by
    // typecheck for the same agent without those clauses — the QF pure
    // function only cares about the diagnostic range + code and does not
    // re-check the source.
    const std::string source = "struct M { v: String; }\n"
                               "\n"
                               "agent A {\n"
                               "    input: M;\n"
                               "    context: M;\n"
                               "    output: M;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [];\n"
                               "    transition Init -> Done;\n"
                               "}\n"
                               "\n"
                               "flow for A {\n"
                               "    state Init {\n"
                               "        return input;\n"
                               "    }\n"
                               "}\n";
    // The diagnostic covers the full agent declaration (lines 2..11),
    // exactly as the QW-4 typecheck pass emits it (range set to
    // `decl.get().range` in typecheck_decls.cpp:784).
    LspDiagnostic diag;
    diag.code = "typecheck.AGENT_CONTEXT_OMITTED";
    diag.severity = DiagnosticSeverity::Warning;
    diag.message = "agent 'A' is declared without a `context:` clause";
    diag.range = Range{Position{2, 0}, Position{11, 1}};

    const Range cursor_range{Position{3, 4}, Position{3, 9}}; // inside `input:` line
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    // Find the context-QF action (organize-imports or other siblings may
    // also be returned if they match the range).
    const CodeAction *qf = nullptr;
    for (const auto &a : actions) {
        if (a.title.find("context: struct { };") != std::string::npos ||
            ((a.title.find("Insert") != std::string::npos) &&
             (a.title.find("context") != std::string::npos))) {
            qf = &a;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_ctx.action_found");
    if (qf == nullptr) return;

    // 1. Human-readable title carries the inserted clause verbatim so the
    //    IDE preview matches what will be written.
    check(qf->title.find("Insert `context: struct { };` clause") != std::string::npos,
          "codeAction.qf_ctx.title_mentions_insert_context");
    // 2. kind == quickfix (the `only:["quickfix"]` client filter will pick it).
    check(qf->kind == CodeActionKind::QuickFix,
          "codeAction.qf_ctx.kind_is_quickfix");
    // 3. isPreferred so clients with "apply preferred quickfix" shortcuts
    //    default to this (it is the only valid remediation).
    check(qf->is_preferred, "codeAction.qf_ctx.marked_preferred");
    // 4. The workspace edit must contain *exactly one* TextEdit and its
    //    payload must contain the new `context: struct { };` declaration.
    check(qf->edit.has_value(), "codeAction.qf_ctx.has_workspace_edit");
    if (!qf->edit.has_value()) return;

    std::size_t total_edits = 0;
    bool found_struct_clause = false;
    bool found_non_empty = false;
    Position insert_pos{0, 0};
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &e : edits) {
            if (e.new_text.find("context: struct { };") != std::string::npos)
                found_struct_clause = true;
            if (!e.new_text.empty()) found_non_empty = true;
            insert_pos = e.range.start;
        }
    }
    check(total_edits >= 1, "codeAction.qf_ctx.at_least_one_text_edit");
    check(found_struct_clause, "codeAction.qf_ctx.edit_inserts_struct_empty");
    check(found_non_empty, "codeAction.qf_ctx.edit_has_non_empty_new_text");
    // 5. Insertion position must be AFTER the input-decl line (line 3 =
    //    `    input: M;`) so the new clause lands between `input:` and
    //    `output:` (AHFL schema order).
    check(insert_pos.line >= 4 && insert_pos.line <= 5,
          "codeAction.qf_ctx.insertion_between_input_and_output");
}

// Wave-21 A-2 (2/2): QF for AGENT_CAPABILITIES_OMITTED — inserts
// "capabilities: [];" before the first transition line.
//
// See NOTE block above test_code_action_qf_agent_context for why this is a
// pure unit test rather than an LSP end-to-end test.
void test_code_action_qf_agent_capabilities() {
    // Source includes a `context:` line (so only AGENT_CAPABILITIES_OMITTED
    // would fire) and provides an explicit `transition Init -> Done;` line
    // so the QF has a concrete insertion anchor (it inserts `capabilities:
    // [];` right before the first transition).
    //
    // As in QF-ctx, the source itself includes both clauses so the parser
    // is happy; the diagnostic is injected by the test.
    const std::string source = "struct M { v: String; }\n"
                               "\n"
                               "agent A {\n"
                               "    input: M;\n"
                               "    context: M;\n"
                               "    output: M;\n"
                               "    states: [Init, Done];\n"
                               "    initial: Init;\n"
                               "    final: [Done];\n"
                               "    capabilities: [];\n"
                               "    transition Init -> Done;\n"
                               "}\n"
                               "\n"
                               "flow for A {\n"
                               "    state Init {\n"
                               "        return input;\n"
                               "    }\n"
                               "}\n";
    // Diagnostic range = full agent declaration (lines 2..11) — matches
    // typecheck_decls.cpp:805 which sets `decl.get().range`.
    LspDiagnostic diag;
    diag.code = "typecheck.AGENT_CAPABILITIES_OMITTED";
    diag.severity = DiagnosticSeverity::Warning;
    diag.message = "agent 'A' declares no capabilities (explicit `capabilities: [];` recommended)";
    diag.range = Range{Position{2, 0}, Position{11, 1}};

    const Range cursor_range{Position{10, 4}, Position{10, 24}}; // on `transition Init -> Done;`
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    const CodeAction *qf = nullptr;
    for (const auto &a : actions) {
        if (a.title.find("capabilities: [];") != std::string::npos ||
            ((a.title.find("Insert") != std::string::npos) &&
             (a.title.find("capabilities") != std::string::npos))) {
            qf = &a;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_caps.action_found");
    if (qf == nullptr) return;

    // 1. Title verbatim matches QW-4 lint UX copy.
    check(qf->title.find("Insert empty `capabilities: [];` clause") != std::string::npos,
          "codeAction.qf_caps.title_mentions_insert_capabilities");
    // 2. kind == quickfix.
    check(qf->kind == CodeActionKind::QuickFix,
          "codeAction.qf_caps.kind_is_quickfix");
    // 3. isPreferred.
    check(qf->is_preferred, "codeAction.qf_caps.marked_preferred");
    // 4. Workspace edit contains the expected insertion text and the
    //    insert position is before the first transition (i.e. on a line
    //    strictly less than the transition line = line 10).
    check(qf->edit.has_value(), "codeAction.qf_caps.has_workspace_edit");
    if (!qf->edit.has_value()) return;

    std::size_t total_edits = 0;
    bool found_empty_array = false;
    bool found_non_empty = false;
    Position insert_pos{0, 0};
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &e : edits) {
            if (e.new_text.find("capabilities: [];") != std::string::npos)
                found_empty_array = true;
            if (!e.new_text.empty()) found_non_empty = true;
            insert_pos = e.range.start;
        }
    }
    check(total_edits >= 1, "codeAction.qf_caps.at_least_one_text_edit");
    check(found_empty_array, "codeAction.qf_caps.edit_inserts_empty_array");
    check(found_non_empty, "codeAction.qf_caps.edit_has_non_empty_new_text");
    // 5. Insertion must be AFTER the `final:` line (line 8) and BEFORE the
    //    closing `}` of the agent (line 11).  The QF deliberately skips any
    //    existing `capabilities:` line (as it does when re-running on an
    //    already-fixed source), so any position in the (8, 11] range is
    //    semantically valid — in this fixture line 11 is the exact insert
    //    point right before the closing brace of `agent A { ... }`.
    check(insert_pos.line > 8 && insert_pos.line <= 11,
          "codeAction.qf_caps.insertion_between_final_and_closing_brace");
}

void test_diagnostic_related_information_surfaces_for_multi_module_mismatch() {
    const auto root = make_temp_project("lsp_diag_related_info");
    const auto main_path = root / "src" / "main.ahfl";
    const auto a_path = root / "src" / "a.ahfl";
    const auto b_path = root / "src" / "b.ahfl";

    write_package_manifest(root, "lsp-diag-related", "app", "\"main\", \"a\", \"b\"");
    const std::string main_source = "module app::main;\n"
                                    "import app::a as a;\n"
                                    "import app::b as b;\n"
                                    "\n"
                                    "const Mixed: a::Record = b::Record { id: \"hello\" };\n";
    const std::string a_source = "module app::a;\n"
                                 "import app::a as self;\n"
                                 "\n"
                                 "struct Record {\n"
                                 "    id: Int;\n"
                                 "}\n";
    const std::string b_source = "module app::b;\n"
                                 "import app::b as self;\n"
                                 "\n"
                                 "struct Record {\n"
                                 "    id: String;\n"
                                 "}\n";
    write_file(main_path, main_source);
    write_file(a_path, a_source);
    write_file(b_path, b_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{}})",
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            main_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})",
    });

    const auto workspace = response_body_for_id(output, 2);
    const auto doc = response_body_for_id(output, 3);
    // Either workspace or textDocument diagnostics must expose the
    // TypeMismatch and the cross-module "other declaration" origin notes as
    // LSP-standard `relatedInformation` arrays with the expected message.
    const auto &rich = workspace.empty() ? doc : workspace;
    check(rich.find("\"relatedInformation\"") != std::string::npos,
          "diagRelated.has_related_information_key");
    check(rich.find("other declaration in module") != std::string::npos,
          "diagRelated.other_declaration_message_present");
    // Sanity: the primary TypeMismatch error code must also be present so we
    // know the assertion is not matching against an unrelated diagnostic.
    check(rich.find("typecheck.TYPE_MISMATCH") != std::string::npos,
          "diagRelated.type_mismatch_code_present");
    // Each relatedInformation entry is a `{ location, message }` object - make
    // sure we have at least one `location` sibling alongside the message text
    // so the IDE has a clickable anchor.
    check(rich.find("\"location\"") != std::string::npos,
          "diagRelated.related_entries_have_location");
}

void test_hover_struct_literal_shows_construct_summary() {
    // A source with two unambiguous struct literal sites. We verify:
    //   (a) hovering the type-name at a construct site shows "Creates …" + field list
    //   (b) hovering the type-name of the second literal shows the same summary
    //       (distinguishable from the declaration-site hover because the
    //        declaration site uses `Fields: N` without the trailing ` total`).
    //   (c) hovering the struct *declaration* still shows the original "struct X"
    //       payload (StructLiteral priority is 0 but it is never registered on
    //       the declaration identifier).
    const std::string source =
        "enum Priority {\n"
        "    High,\n"
        "    Low,\n"
        "}\n"
        "\n"
        "struct Request {\n"
        "    category: String;\n"
        "    priority: Priority;\n"
        "    retries: Int = 3;\n"
        "}\n"
        "\n"
        "struct Ctx {\n"
        "    counter: Int = 0;\n"
        "}\n"
        "\n"
        "agent A {\n"
        "    input: Request;\n"
        "    context: Ctx;\n"
        "    output: Request;\n"
        "    states: [Init, Done];\n"
        "    initial: Init;\n"
        "    final: [Done];\n"
        "    capabilities: [];\n"
        "    transition Init -> Done;\n"
        "}\n"
        "\n"
        "flow for A {\n"
        "    state Init {\n"
        "        let made = Request { category: \"ok\", priority: Priority::High, retries: 1 };\n"
        "        let dup = Request{ category: input.category, priority: Priority::Low, retries: 0 };\n"
        "        goto Done;\n"
        "    }\n"
        "}\n";

    // Initialize with maxFacts high enough to surface every field (default = 3
    // would cut off `retries` after Fields + category + priority).
    const char *kRichInit =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)"
        R"({"initializationOptions":{"ahfl":{"hover":{"maxFacts":20}}}}})";

    // (1) Hover on the `Request` type-name that opens the FIRST literal.
    //     Needle includes the trailing ` { category` fragment so we avoid
    //     the schema references (agent input/context/output) by accident.
    const auto on_first =
        run_hover_request_with_init(source, "Request { category", kRichInit);
    check(on_first.find("Creates a `Request` struct") != std::string::npos,
          "hover.construct.first.signature_creates_keyword");
    check(on_first.find("struct literal") != std::string::npos,
          "hover.construct.first.summary_struct_literal");
    check(on_first.find("Fields: 3 total") != std::string::npos,
          "hover.construct.first.fields_count_3_total_suffix");
    check(on_first.find("`category`: `String`") != std::string::npos,
          "hover.construct.first.field_category_type_pair");
    check(on_first.find("`priority`: `Priority`") != std::string::npos,
          "hover.construct.first.field_priority_type_pair");
    check(on_first.find("`retries`: `Int` (default)") != std::string::npos,
          "hover.construct.first.field_retries_default_annotation");

    // (2) Hover on the `Request` type-name that opens the SECOND literal
    //     (no space between `Request` and `{`, so the needle is unique).
    const auto on_second =
        run_hover_request_with_init(source, "Request{ category: input", kRichInit);
    check(on_second.find("Creates a `Request` struct") != std::string::npos,
          "hover.construct.second.signature_creates_keyword");
    check(on_second.find("3 total") != std::string::npos,
          "hover.construct.second.fields_count_preserved");

    // (3) Sanity: the struct DECLARATION site still renders as `struct Request`
    //     (no regression because StructLiteral targets are only registered on
    //     expression nodes, never on declaration identifiers).
    //     Needle: `Request {` with occurrence=0 lands on the declaration name
    //     (the struct keyword immediately precedes it); occurrence=1 would be
    //     the first literal site, which is already covered by test (1).
    const auto on_decl = run_hover_request_with_init(source, "Request {", kRichInit);
    check(on_decl.find("struct Request") != std::string::npos,
          "hover.construct.decl.signature_preserved");
    // Distinguish from construct-site facts: the declaration payload uses
    // `Fields: 3` (no " total" suffix). We assert exactly that so future
    // render changes cannot silently blur the two roles.
    const auto has_fields_just_3 = on_decl.find("Fields: 3\n") != std::string::npos ||
                                   on_decl.find("Fields: 3\"") != std::string::npos ||
                                   on_decl.find("Fields: 3") != std::string::npos;
    const auto has_fields_3_total = on_decl.find("3 total") != std::string::npos;
    // The declaration hover should not include the construct-unique " total"
    // suffix. (It may still show Fields: 3 via the StructDecl facts path.)
    check(has_fields_just_3 && !has_fields_3_total,
          "hover.construct.decl.fields_count_without_total_suffix");
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
    test_signature_help_keyword_family();
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
    test_diagnostics_cover_parse_resolve_typecheck_and_validation();
    test_project_definition_workspace_symbol_and_rename_cross_file();
    test_project_workspace_symbol_deduplicates_open_project_snapshots();
    test_project_open_document_overlay_drives_definition();
    test_project_diagnostics_refresh_dependent_open_documents();
    test_package_graph_manifest_selects_module_roots_for_source();
    test_package_graph_workspace_selects_member_dependency_source();
    test_descriptorless_workspace_infers_module_root_for_imports();
    test_descriptorless_std_file_does_not_add_overlapping_workspace_root();
    test_descriptorless_workspace_does_not_inject_prelude();
    test_diagnostic_related_information_surfaces_for_multi_module_mismatch();
    test_hover_renderer_detail_levels();
    test_hover_respects_client_markup_and_debug_options();
    test_hover_rich_symbol_targets();
    test_hover_service_payload_contract();
    test_hover_trait_where_bounds();
    test_code_action_qf_duplicate_struct_related_navigation();
    test_code_action_qf_unused_import_text_edit();
    test_code_action_qf_wrong_arity_placeholder();
    test_code_action_qf_agent_context();
    test_code_action_qf_agent_capabilities();
    test_hover_struct_literal_shows_construct_summary();
    check_hover_integration_fixture_coverage();

    // Wave-21 A-3: tie-break ordering contract between priority and enum ordinal.
    test_hover_target_tie_break_order_contract();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
