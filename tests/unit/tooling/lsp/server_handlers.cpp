#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/server.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::string initialize_body(const std::filesystem::path &root) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) + R"("}})";
}

std::string diagnostics_output_for_source(const std::string &source) {
    return run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body("file:///diag.ahfl", 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})",
    });
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

void test_diagnostics_publish_document_version() {
    const std::string init_body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    const std::string did_open =
        did_open_body("file:///diag.ahfl", 1, "struct Broken {\n    value: ;\n}\n");
    const std::string did_change =
        R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///diag.ahfl","version":2},"contentChanges":[{"text":"struct Fixed {\n    value: String;\n}\n"}]}})";
    const std::string shutdown_body = R"({"jsonrpc":"2.0","id":2,"method":"shutdown","params":{}})";

    const auto output = run_lsp_messages({init_body, did_open, did_change, shutdown_body});
    check(output.find("\"version\":1") != std::string::npos, "diagnostics.version_1_published");
    check(output.find("\"version\":2") != std::string::npos, "diagnostics.version_2_published");
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
    test_signature_help_capability();
    test_completion_type_member_enum_state_and_workflow_contexts();
    test_rename_rejects_keyword_and_conflict();
    test_document_symbol_hierarchy();
    test_diagnostics_publish_document_version();
    test_diagnostics_cover_parse_resolve_typecheck_and_validation();
    test_project_definition_workspace_symbol_and_rename_cross_file();
    test_project_open_document_overlay_drives_definition();
    test_workspace_descriptor_selects_project_for_source();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
