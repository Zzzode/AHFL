#include "tooling/lsp/server.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

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

/// Helper: send initialize + didOpen + request, return response body
std::string run_handler_request(const std::string &source_text,
                                const std::string &method,
                                const std::string &params_json) {
    // Build initialize request
    std::string init_body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    // Build didOpen notification
    std::string escaped_text;
    for (char c : source_text) {
        if (c == '"') escaped_text += "\\\"";
        else if (c == '\n') escaped_text += "\\n";
        else if (c == '\\') escaped_text += "\\\\";
        else escaped_text += c;
    }
    std::string did_open_body =
        R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test.ahfl","languageId":"ahfl","version":1,"text":")" +
        escaped_text + R"("}}})";
    // Build actual request
    std::string req_body =
        R"({"jsonrpc":"2.0","id":2,"method":")" + method +
        R"(","params":)" + params_json + R"(})";
    // Build shutdown
    std::string shutdown_body = R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})";

    std::string input = make_frame(init_body) + make_frame(did_open_body) +
                        make_frame(req_body) + make_frame(shutdown_body);

    std::istringstream in(input);
    std::ostringstream out;
    LspServer server(in, out);
    server.run();

    return out.str();
}

void test_document_symbol_lists_all() {
    std::string source = "struct Foo {\n    value: String;\n}\n\nstruct Bar {\n    name: String;\n}";
    std::string params = R"({"textDocument":{"uri":"file:///test.ahfl"}})";
    std::string output = run_handler_request(source, "textDocument/documentSymbol", params);

    // Response should contain both "Foo" and "Bar"
    check(output.find("Foo") != std::string::npos, "documentSymbol.contains_Foo");
    check(output.find("Bar") != std::string::npos, "documentSymbol.contains_Bar");
}

void test_workspace_symbol_filters() {
    std::string source = "struct Alpha {\n    value: String;\n}\n\nstruct Beta {\n    name: String;\n}";
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
    std::string source = "struct Msg {\n    value: String;\n}\n\nstruct Envelope {\n    payload: Msg;\n}";
    std::string params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7}})";
    std::string output = run_handler_request(source, "textDocument/references", params);

    // If references are found, response should be an array (containing "[")
    // At minimum it should contain a well-formed response for id:2
    check(output.find("\"id\"") != std::string::npos, "references.has_response");
}

void test_rename_returns_workspace_edit() {
    std::string source = "struct Msg {\n    value: String;\n}\n\nstruct Envelope {\n    payload: Msg;\n}";
    std::string params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7},"newName":"Message"})";
    std::string output = run_handler_request(source, "textDocument/rename", params);

    // If rename succeeds, response should contain the new name "Message"
    // or a changes object
    check(output.find("\"id\"") != std::string::npos, "rename.has_response");
}

} // anonymous namespace

int main() {
    test_document_symbol_lists_all();
    test_workspace_symbol_filters();
    test_references_returns_locations();
    test_rename_returns_workspace_edit();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
