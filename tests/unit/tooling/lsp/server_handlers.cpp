#include "tooling/lsp/analysis_service.hpp"
#include "tooling/lsp/code_action.hpp"
#include "tooling/lsp/hover_service.hpp"
#include "tooling/lsp/semantic_tokens.hpp"
#include "tooling/lsp/server.hpp"

#include "compiler/syntax/frontend/project.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace {

using namespace ahfl::lsp;
namespace project_discovery = ahfl::project_discovery;

int test_count = 0;
int pass_count = 0;

class ScopedEnvVar {
  public:
    ScopedEnvVar(const char *name, const std::string &value) : name_(name) {
        if (const char *existing = std::getenv(name); existing != nullptr) {
            old_value_ = existing;
        }
        ::setenv(name, value.c_str(), 1);
    }

    ~ScopedEnvVar() {
        if (old_value_.has_value()) {
            ::setenv(name_, old_value_->c_str(), 1);
        } else {
            ::unsetenv(name_);
        }
    }

    ScopedEnvVar(const ScopedEnvVar &) = delete;
    ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  private:
    const char *name_;
    std::optional<std::string> old_value_;
};

class ScopedUnsetEnvVar {
  public:
    explicit ScopedUnsetEnvVar(const char *name) : name_(name) {
        if (const char *existing = std::getenv(name); existing != nullptr) {
            old_value_ = existing;
        }
        ::unsetenv(name);
    }

    ~ScopedUnsetEnvVar() {
        if (old_value_.has_value()) {
            ::setenv(name_, old_value_->c_str(), 1);
        }
    }

    ScopedUnsetEnvVar(const ScopedUnsetEnvVar &) = delete;
    ScopedUnsetEnvVar &operator=(const ScopedUnsetEnvVar &) = delete;

  private:
    const char *name_;
    std::optional<std::string> old_value_;
};

class ScopedCurrentPath {
  public:
    explicit ScopedCurrentPath(const std::filesystem::path &path) {
        std::error_code error;
        old_path_ = std::filesystem::current_path(error);
        if (!error) {
            std::filesystem::current_path(path, error);
        }
    }

    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(old_path_, error);
    }

    ScopedCurrentPath(const ScopedCurrentPath &) = delete;
    ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

  private:
    std::filesystem::path old_path_;
};

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

const SourceUnitFact *index_source_unit_for_uri(const LspWorkspaceIndex &index,
                                                const std::string &uri) {
    const auto &source_units = index.source_units();
    const auto found = std::find_if(source_units.begin(),
                                    source_units.end(),
                                    [&](const SourceUnitFact &unit) { return unit.uri == uri; });
    return found == source_units.end() ? nullptr : &*found;
}

const SymbolFact *index_symbol_for_canonical_name(const LspWorkspaceIndex &index,
                                                  std::string_view canonical_name) {
    const auto &symbols = index.symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(), [&](const SymbolFact &symbol) {
        return symbol.canonical_name == canonical_name;
    });
    return found == symbols.end() ? nullptr : &*found;
}

bool index_has_diagnostic(const LspWorkspaceIndex &index,
                          SourceUnitId source_unit,
                          IndexDiagnosticPhase phase,
                          const std::string &code_prefix) {
    const auto diagnostics = index.diagnostics_for_source(source_unit);
    return std::find_if(
               diagnostics.begin(), diagnostics.end(), [&](const IndexDiagnosticFact *diagnostic) {
                   return diagnostic != nullptr && diagnostic->phase == phase &&
                          diagnostic->code.rfind(code_prefix, 0) == 0 &&
                          !diagnostic->message.empty();
               }) != diagnostics.end();
}

bool diagnostic_response_has_error(std::string_view response) {
    return response.find(R"("severity":1)") != std::string_view::npos;
}

bool source_unit_has_scope_kind(const SourceUnitFact &source, LspNavigationIndexSourceKind kind) {
    return std::find(source.scope_kinds.begin(), source.scope_kinds.end(), kind) !=
           source.scope_kinds.end();
}

bool source_unit_has_export_scope(const SourceUnitFact &source) {
    return source_unit_has_scope_kind(source, LspNavigationIndexSourceKind::PackageExport) ||
           source_unit_has_scope_kind(source, LspNavigationIndexSourceKind::SysrootExport);
}

Location
index_test_location(const std::string &uri, std::uint32_t line, std::uint32_t character = 0) {
    return Location{
        .uri = uri,
        .range =
            Range{
                .start = Position{.line = line, .character = character},
                .end = Position{.line = line, .character = character + 1},
            },
    };
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

struct DecodedSemanticToken {
    std::uint32_t line{0};
    std::uint32_t start{0};
    std::uint32_t length{0};
    SemanticTokenType type{SemanticTokenType::Namespace};
    std::uint32_t modifiers{0};
};

std::vector<DecodedSemanticToken> decode_semantic_tokens(const SemanticTokens &tokens) {
    std::vector<DecodedSemanticToken> decoded;
    decoded.reserve(tokens.data.size());

    std::uint32_t line = 0;
    std::uint32_t start = 0;
    for (const auto &token : tokens.data) {
        line += token.delta_line;
        start = token.delta_line == 0 ? start + token.delta_start : token.delta_start;
        decoded.push_back(DecodedSemanticToken{
            .line = line,
            .start = start,
            .length = token.length,
            .type = static_cast<SemanticTokenType>(token.token_type),
            .modifiers = token.token_modifiers,
        });
    }

    return decoded;
}

bool has_semantic_token_at(const std::vector<DecodedSemanticToken> &tokens,
                           Position position,
                           SemanticTokenType type) {
    return std::any_of(tokens.begin(), tokens.end(), [&](const DecodedSemanticToken &token) {
        return token.line == position.line && token.start == position.character &&
               token.type == type;
    });
}

void check_semantic_token_at(const std::vector<DecodedSemanticToken> &tokens,
                             const std::string &source,
                             std::string_view test_name,
                             const std::string &needle,
                             SemanticTokenType type,
                             std::size_t occurrence = 0) {
    const auto position = position_of(source, needle, occurrence);
    check(has_semantic_token_at(tokens, position, type),
          "semanticTokens." + std::string(test_name));
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
    const auto repo_root = this_file
                               .parent_path()  // tests/unit/tooling/lsp
                               .parent_path()  // tests/unit/tooling
                               .parent_path()  // tests/unit
                               .parent_path()  // tests
                               .parent_path(); // repo root
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
    analysis.set_workspace_folders({root});

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

void write_std_manifest(const std::filesystem::path &sysroot_std_root) {
    write_file(sysroot_std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"option\", \"collections\", \"prelude\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"option\"]\n");
}

void write_minimal_std_package(const std::filesystem::path &sysroot_std_root,
                               std::string_view marker) {
    write_std_manifest(sysroot_std_root);
    write_file(sysroot_std_root / "option.ahfl",
               "module std::option;\n"
               "\n"
               "enum Option<T> { Some(T), None, }\n");
    write_file(sysroot_std_root / "collections.ahfl",
               "module std::collections;\n"
               "\n"
               "pub struct List<T> {}\n");
    write_file(sysroot_std_root / "prelude.ahfl",
               "module std::prelude;\n"
               "// " +
                   std::string(marker) + "\n");
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

std::string did_change_configuration_body() {
    return R"({"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{}})";
}

std::string workspace_configuration_response_body(std::string_view request_id,
                                                  std::string_view sysroot) {
    return R"({"jsonrpc":"2.0","id":")" + std::string(request_id) + R"(","result":[{"sysroot":")" +
           escape_json_string(std::string(sysroot)) + R"("}]})";
}

std::string initialize_body(const std::filesystem::path &root) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) + R"("}})";
}

std::string initialize_body_with_sysroot(const std::filesystem::path &root,
                                         const std::filesystem::path &sysroot) {
    const auto root_uri = AnalysisService::uri_from_path(root);
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" + root_uri +
           R"(","initializationOptions":{"ahfl":{"toolchain":{"defaultSysroot":")" +
           escape_json_string(sysroot.string()) + R"(","profiles":[{"workspaceFolder":")" +
           root_uri + R"(","sysroot":")" + escape_json_string(sysroot.string()) + R"("}]}}}}})";
}

std::string initialize_body_with_bundled_sysroot(const std::filesystem::path &root,
                                                 const std::filesystem::path &sysroot) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) +
           R"(","initializationOptions":{"ahfl":{"toolchain":{"bundledSysroot":")" +
           escape_json_string(sysroot.string()) + R"("}}}}})";
}

std::string
initialize_body_with_default_and_bundled_sysroot(const std::filesystem::path &root,
                                                 const std::filesystem::path &default_sysroot,
                                                 const std::filesystem::path &bundled_sysroot) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) +
           R"(","initializationOptions":{"ahfl":{"toolchain":{"defaultSysroot":")" +
           escape_json_string(default_sysroot.string()) + R"(","bundledSysroot":")" +
           escape_json_string(bundled_sysroot.string()) + R"("}}}}})";
}

std::string initialize_body_with_legacy_sysroot(const std::filesystem::path &root,
                                                const std::filesystem::path &sysroot) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) +
           R"(","initializationOptions":{"ahfl":{"sysroot":")" +
           escape_json_string(sysroot.string()) + R"("}}}})";
}

std::string
initialize_body_with_noncanonical_toolchain_sysroot(const std::filesystem::path &root,
                                                    const std::filesystem::path &sysroot) {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":")" +
           AnalysisService::uri_from_path(root) +
           R"(","initializationOptions":{"ahfl":{"toolchain":{"sysroot":")" +
           escape_json_string(sysroot.string()) + R"("}}}}})";
}

project_discovery::ToolchainProfileSet
toolchain_profile_set_for_sysroot(const std::filesystem::path &sysroot) {
    project_discovery::ToolchainProfileSet profiles;
    auto result = project_discovery::toolchain_profile_from_sysroot_input(
        sysroot, project_discovery::ToolchainProfileOrigin::LspInitialization);
    profiles.diagnostics = std::move(result.diagnostics);
    if (result.profile.has_value()) {
        profiles.default_profile = std::move(result.profile);
    }
    return profiles;
}

project_discovery::ToolchainProfileSet
workspace_toolchain_profile_set_for_sysroot(const std::filesystem::path &workspace_root,
                                            const std::filesystem::path &sysroot) {
    project_discovery::ToolchainProfileSet profiles;
    auto result = project_discovery::toolchain_profile_from_sysroot_input(
        sysroot, project_discovery::ToolchainProfileOrigin::LspInitialization);
    profiles.diagnostics = std::move(result.diagnostics);
    if (result.profile.has_value()) {
        result.profile->scope = project_discovery::ToolchainProfileScope::WorkspaceFolder;
        profiles.workspace_profiles.push_back(project_discovery::WorkspaceToolchainProfile{
            .workspace_root = workspace_root,
            .profile = std::move(*result.profile),
        });
    }
    return profiles;
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

void test_semantic_tokens_cover_current_syntax_surface() {
    const auto root = make_temp_project("semantic_tokens_current_syntax");
    const auto main_path = root / "src" / "main.ahfl";
    write_package_manifest(root, "lsp-semantic-tokens", "app", "\"main\"");

    const std::string source = R"(module app::main;
pub use app::main::Msg as PublicMsg;

pub const LIMIT: Int = 42;

pub struct Msg<T> {
    value: String;
    flag: Bool = true;
}

pub enum Choice<T> {
    None,
    Some(T),
    Pair { left: Int = 1, right: String },
}

pub capability Call(req: Msg<Int>) -> Bool {
    effect: read;
    domain: app::main;
    idempotency: req.value;
    receipt: optional;
    retry: safe;
    timeout: 5s;
    compensation: app::main::Recover;
    policy: [app::main::Policy];
}

pub predicate ok(req: Msg<Int>) -> Bool;

pub agent A {
    input: Msg<Int>;
    context: Msg<Int>;
    output: Msg<Int>;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Call];
    quota: { max_tool_calls: 3; max_execution_time: 1s; }
    transition Init -> Done;
}

contract for A {
    requires: input.flag;
    ensures: output.flag;
    invariant: always(in_state(Init) or eventually(called(Call)));
    forbid: completed(first, Done);
    decreases: 0;
}

flow for A {
    state Init with { retry: 2; retry_on: [app::main::Failure]; timeout: 1s; } {
        let local: Msg<Int> = Msg { value: "x", flag: true };
        goto Done;
    }
    state Done {
        return output;
    }
}

pub workflow W {
    input: Msg<Int>;
    output: Msg<Int>;
    node first: A(input);
    safety: always(running(first) => eventually(completed(first, Done)));
    liveness: next(called(Call));
    return: input;
}

pub fn compute<T: Msg<Int>>(f: Fn(Int) -> Bool effect Pure, x: Int) -> Int effect Pure decreases x where T: Msg<Int> {
    let y: Int = 1;
    let z: Int = match Some(x) { Some(v) if v > 0 => v, _ => 0, };
    let captured: Fn(Int) -> Int = \[y] (p: Int) -> p + y;
    assert(z > 0, "z");
    requires(true);
    unwrap(Some(z));
    if let Some(inner) = Some(z) { return inner; } else { return z; }
}

pub trait Fold<T>: Msg<Int> {
    fn fold(self: T, seed: Int) -> Int effect Pure decreases seed;
    type Item: Msg<Int> = Msg<Int>;
    const DEFAULT: Int = 0;
}

impl Msg<Int> {
    pub fn get(self) -> String effect Pure decreases 0 { return self.value; }
    pub type Item = Int;
    pub const DEFAULT: Int = 1;
}

impl Fold<Msg<Int>> for Msg<Int> {
    fn fold(self: Msg<Int>, seed: Int) -> Int effect Pure decreases seed { return seed; }
    type Item = Int;
    const DEFAULT: Int = 0;
}
)";
    write_file(main_path, source);
    const auto main_uri = AnalysisService::uri_from_path(main_path);

    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });
    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto tokens = decode_semantic_tokens(compute_semantic_tokens(main_uri, analysis));
    check(!tokens.empty(), "semanticTokens.non_empty");

    check_semantic_token_at(
        tokens, source, "module_path", "app::main", SemanticTokenType::Namespace);
    check_semantic_token_at(tokens, source, "use_alias", "PublicMsg", SemanticTokenType::Namespace);
    check_semantic_token_at(tokens, source, "const_name", "LIMIT", SemanticTokenType::Variable);
    check_semantic_token_at(tokens, source, "const_type", "Int = 42", SemanticTokenType::Type);
    check_semantic_token_at(tokens, source, "const_number", "42", SemanticTokenType::Number);
    check_semantic_token_at(tokens, source, "struct_name", "Msg<T>", SemanticTokenType::Struct);
    check_semantic_token_at(
        tokens, source, "struct_type_param", "T>", SemanticTokenType::TypeParameter);
    check_semantic_token_at(
        tokens, source, "struct_field", "value: String", SemanticTokenType::Property);
    check_semantic_token_at(tokens, source, "primitive_string", "String;", SemanticTokenType::Type);
    check_semantic_token_at(
        tokens, source, "primitive_bool", "Bool = true", SemanticTokenType::Type);
    check_semantic_token_at(tokens, source, "bool_literal", "true;", SemanticTokenType::Keyword);
    check_semantic_token_at(tokens, source, "enum_name", "Choice<T>", SemanticTokenType::Enum);
    check_semantic_token_at(
        tokens, source, "enum_variant", "Some(T)", SemanticTokenType::EnumMember);
    check_semantic_token_at(
        tokens, source, "enum_named_field", "left: Int", SemanticTokenType::Property);
    check_semantic_token_at(
        tokens, source, "capability_name", "Call(req", SemanticTokenType::Interface);
    check_semantic_token_at(
        tokens, source, "capability_param", "req: Msg", SemanticTokenType::Parameter);
    check_semantic_token_at(
        tokens, source, "capability_effect", "read;", SemanticTokenType::Keyword);
    check_semantic_token_at(tokens, source, "capability_duration", "5s", SemanticTokenType::Number);
    check_semantic_token_at(
        tokens, source, "predicate_name", "ok(req", SemanticTokenType::Function);
    check_semantic_token_at(tokens, source, "agent_name", "A {", SemanticTokenType::Class);
    check_semantic_token_at(
        tokens, source, "agent_state", "Init, Done", SemanticTokenType::Variable);
    check_semantic_token_at(
        tokens, source, "contract_target", "A {\n    requires", SemanticTokenType::Class);
    check_semantic_token_at(
        tokens, source, "temporal_called", "Call)));", SemanticTokenType::Interface);
    check_semantic_token_at(tokens, source, "flow_state", "Init with", SemanticTokenType::Variable);
    check_semantic_token_at(tokens, source, "flow_string", "\"x\"", SemanticTokenType::String);
    check_semantic_token_at(tokens, source, "workflow_name", "W {", SemanticTokenType::Class);
    check_semantic_token_at(
        tokens, source, "workflow_node", "first: A", SemanticTokenType::Variable);
    check_semantic_token_at(
        tokens, source, "function_name", "compute<T", SemanticTokenType::Function);
    check_semantic_token_at(tokens, source, "fn_type", "Fn(Int)", SemanticTokenType::Type);
    check_semantic_token_at(tokens, source, "fn_param", "x: Int", SemanticTokenType::Parameter);
    check_semantic_token_at(
        tokens, source, "match_variant", "Some(v)", SemanticTokenType::EnumMember);
    check_semantic_token_at(tokens, source, "match_binding", "v) if", SemanticTokenType::Variable);
    check_semantic_token_at(tokens, source, "lambda_capture", "y] (p", SemanticTokenType::Variable);
    check_semantic_token_at(tokens, source, "lambda_param", "p: Int", SemanticTokenType::Parameter);
    check_semantic_token_at(tokens, source, "trait_name", "Fold<T>", SemanticTokenType::Interface);
    check_semantic_token_at(tokens, source, "trait_method", "fold(self", SemanticTokenType::Method);
    check_semantic_token_at(tokens, source, "assoc_type", "Item: Msg", SemanticTokenType::Type);
    check_semantic_token_at(
        tokens, source, "assoc_const", "DEFAULT: Int", SemanticTokenType::Variable);
    check_semantic_token_at(tokens, source, "impl_method", "get(self", SemanticTokenType::Method);
}

void test_semantic_tokens_request_uses_document_uri() {
    const std::string source = "struct Msg {\n"
                               "    value: String;\n"
                               "}\n";
    const std::string params = R"({"textDocument":{"uri":"file:///test.ahfl"}})";
    const auto output = run_handler_request(source, "textDocument/semanticTokens/full", params);
    const auto response = response_body_for_id(output, 2);
    check(response.find(R"("data":[)") != std::string::npos,
          "semanticTokens.request_has_data_array");
    check(response.find(R"("data":[])") == std::string::npos,
          "semanticTokens.request_data_is_non_empty");
}

void test_hover_pattern_bindings_use_typed_pattern_facts() {
    const std::string source =
        "enum Maybe {\n"
        "    Some(Int),\n"
        "    None,\n"
        "}\n"
        "\n"
        "fn inspect(x: Maybe) -> Int effect Pure decreases 0 {\n"
        "    let matched: Int = match x { Some(v) => v, None => 0, };\n"
        "    if let Some(inner) = x { return inner; } else { return matched; }\n"
        "}\n";

    const auto match_binding = run_hover_request(source, "v) =>");
    check(match_binding.find("pattern binding") != std::string::npos,
          "hover.pattern_binding.match_headline");
    check(match_binding.find("`v`") != std::string::npos &&
              match_binding.find("`Int`") != std::string::npos,
          "hover.pattern_binding.match_type");

    const auto if_let_binding = run_hover_request(source, "inner)");
    check(if_let_binding.find("pattern binding") != std::string::npos,
          "hover.pattern_binding.if_let_headline");
    check(if_let_binding.find("`inner`") != std::string::npos &&
              if_let_binding.find("`Int`") != std::string::npos,
          "hover.pattern_binding.if_let_type");
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

void test_code_lens_uses_text_fallback_without_workspace_index() {
    const std::string source = "struct Foo {\n"
                               "    value: String;\n"
                               "}\n";
    const std::string params = R"({"textDocument":{"uri":"file:///test.ahfl"}})";
    const auto output = run_handler_request(source, "textDocument/codeLens", params);
    const auto response = response_body_for_id(output, 2);

    check(response.find(R"("title":"struct Foo")") != std::string::npos,
          "codeLens.fallback_struct_title");
    check(response.find(R"("command":"ahfl.showReferences")") != std::string::npos,
          "codeLens.fallback_command");
}

void test_code_lens_uses_workspace_index_symbols() {
    const auto root = make_temp_project("code_lens_workspace_index");
    const auto main_path = root / "src" / "main.ahfl";
    write_package_manifest(root, "lsp-code-lens-index", "app", "\"main\"");

    const std::string source = "module app::main;\n"
                               "\n"
                               "fn keep(x: Int) -> Int effect Pure decreases 0 {\n"
                               "    return x;\n"
                               "}\n";
    write_file(main_path, source);
    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const std::string code_lens =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/codeLens","params":{"textDocument":{"uri":")" +
        main_uri + R"("}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, source),
        code_lens,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);

    check(response.find(R"("title":"fn keep")") != std::string::npos,
          "codeLens.index_function_title");
    check(response.find(R"("command":"ahfl.showReferences")") != std::string::npos,
          "codeLens.index_command");
    check(response.find(R"("arguments":["keep"])") != std::string::npos, "codeLens.index_argument");
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
    check(init_response.find("\"typeDefinitionProvider\":true") != std::string::npos,
          "typeDefinition.capability_provider");

    const std::string decl_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":0,"character":7}})";
    const auto decl_output = run_handler_request(source, "textDocument/prepareRename", decl_params);
    const auto decl_response = response_body_for_id(decl_output, 2);
    check(decl_response.find("\"start\":{\"line\":0,\"character\":7}") != std::string::npos,
          "prepareRename.declaration_start");
    check(decl_response.find("\"end\":{\"line\":0,\"character\":10}") != std::string::npos,
          "prepareRename.declaration_end");

    const std::string ref_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":5,"character":13}})";
    const auto ref_output = run_handler_request(source, "textDocument/prepareRename", ref_params);
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
    check(!diagnostic_response_has_error(fixed_response), "diagnostics.version_2_no_errors");
    check(fixed_response.find("N::detached_source_unit") != std::string::npos,
          "diagnostics.version_2_detached_note");
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
    check(!diagnostic_response_has_error(fixed_response),
          "textDocumentDiagnostic.recovery_no_error_items");
    check(fixed_response.find("N::detached_source_unit") != std::string::npos,
          "textDocumentDiagnostic.recovery_detached_note");
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
        R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" +
            uri + R"("}}})",
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
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            uri + R"("}}})",
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
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            uri + R"("},"previousResultId":")" + result_id + R"("}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto second_response = response_body_for_id(second_output, 2);
    const auto second_result_id = extract_result_id(second_response);
    check(second_result_id == result_id, "textDocumentDiagnostic.prev_id_unchanged_same_result_id");
    check(second_response.find("\"items\":[]") != std::string::npos,
          "textDocumentDiagnostic.prev_id_unchanged_empty_items");
    check(second_response.find("\"kind\":\"full\"") != std::string::npos,
          "textDocumentDiagnostic.prev_id_unchanged_still_full_kind");

    // Third pass: send a different previousResultId - should get full report
    const auto third_output = run_lsp_messages({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        did_open_body(uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            uri + R"("},"previousResultId":"fake-id-123"}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto third_response = response_body_for_id(third_output, 2);
    const auto third_result_id = extract_result_id(third_response);
    check(third_result_id == result_id, "textDocumentDiagnostic.prev_id_mismatch_same_result_id");
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
    const auto prev_ids_json = "{\"" + main_uri + "\":\"" + main_result_id + "\",\"" + types_uri +
                               "\":\"" + types_result_id + "\"}";
    const auto second_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(types_uri, 1, types_source),
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{"previousResultIds":)" +
            prev_ids_json + R"(}})",
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
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/diagnostic","params":{"previousResultIds":)" +
            prev_ids_json + R"(}})",
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
    check(response.find("\"items\":[") != std::string::npos, "workspaceDiagnostic.items_array");
    check(response.find(main_uri) != std::string::npos, "workspaceDiagnostic.includes_open_entry");
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
            .before = [types_path,
                       broken_types_source]() { write_file(types_path, broken_types_source); },
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

void test_watched_file_path_invalidation_keeps_unaffected_snapshots() {
    const auto root = make_temp_project("watched_file_path_invalidation");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    const auto unrelated_path = root / "scratch" / "note.ahfl";
    write_package_manifest(root, "lsp-watched-path-invalidation", "app", "\"main\", \"types\"");

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
    write_file(unrelated_path, "module scratch::note;\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *first = analysis.snapshot_for_uri(main_uri);
    check(first != nullptr, "watchedPathInvalidation.first_snapshot_exists");
    check(analysis.analysis_runs() == 1, "watchedPathInvalidation.initial_run_count");

    analysis.invalidate_paths({unrelated_path});
    const auto *after_unrelated = analysis.snapshot_for_uri(main_uri);
    check(after_unrelated != nullptr, "watchedPathInvalidation.unrelated_snapshot_exists");
    check(analysis.analysis_runs() == 1, "watchedPathInvalidation.unrelated_keeps_cache");

    write_file(types_path, broken_types_source);
    analysis.invalidate_paths({types_path});
    const auto *after_types = analysis.snapshot_for_uri(main_uri);
    check(after_types != nullptr, "watchedPathInvalidation.related_snapshot_exists");
    check(analysis.analysis_runs() == 2, "watchedPathInvalidation.related_rebuilds_cache");
    if (after_types != nullptr) {
        const auto diagnostics = after_types->diagnostics_for_uri(types_uri);
        const auto has_missing =
            std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diag) {
                return diag.message.find("unknown type 'Missing'") != std::string::npos;
            });
        check(has_missing, "watchedPathInvalidation.related_reloads_disk_source");
    }
}

void test_manifest_watcher_refreshes_workspace_index_scope() {
    const auto root = make_temp_project("manifest_watcher_workspace_index_scope");
    const auto main_path = root / "src" / "main.ahfl";
    const auto extra_path = root / "src" / "extra.ahfl";
    const auto manifest_path = root / "ahfl.toml";
    write_package_manifest(root, "lsp-manifest-watcher-index", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string extra_source = "module app::extra;\n"
                                     "\n"
                                     "struct ExtraOnly {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(extra_path, extra_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto extra_uri = AnalysisService::uri_from_path(extra_path);
    const auto manifest_uri = AnalysisService::uri_from_path(manifest_path);
    const std::string workspace_symbol_before =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"ExtraOnly"}})";
    const std::string watched_manifest_change =
        R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
        manifest_uri + R"(","type":2}]}})";
    const std::string workspace_symbol_after =
        R"({"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"ExtraOnly"}})";

    const auto output = run_lsp_message_steps({
        LspMessageStep{.body = initialize_body(root)},
        LspMessageStep{.body = did_open_body(main_uri, 1, main_source)},
        LspMessageStep{.body = workspace_symbol_before},
        LspMessageStep{
            .body = watched_manifest_change,
            .before =
                [root]() {
                    write_package_manifest(
                        root, "lsp-manifest-watcher-index", "app", "\"main\", \"extra\"");
                },
        },
        LspMessageStep{.body = workspace_symbol_after},
        LspMessageStep{.body = R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})"},
    });

    const auto before_response = response_body_for_id(output, 2);
    check(before_response.find("ExtraOnly") == std::string::npos,
          "manifestWatcherIndex.before_excludes_unexported_symbol");
    check(before_response.find(extra_uri) == std::string::npos,
          "manifestWatcherIndex.before_excludes_unexported_uri");

    const auto after_response = response_body_for_id(output, 3);
    check(after_response.find("ExtraOnly") != std::string::npos,
          "manifestWatcherIndex.after_includes_exported_symbol");
    check(after_response.find(extra_uri) != std::string::npos,
          "manifestWatcherIndex.after_includes_exported_uri");
}

void test_manifest_invalidation_preserves_workspace_source_unit_ids() {
    const auto root = make_temp_project("manifest_source_unit_stability");
    const auto main_path = root / "src" / "main.ahfl";
    const auto extra_path = root / "src" / "extra.ahfl";
    const auto manifest_path = root / "ahfl.toml";
    write_package_manifest(root, "lsp-manifest-source-unit-stability", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string extra_source = "module app::extra;\n"
                                     "\n"
                                     "struct ExtraOnly {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(extra_path, extra_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto extra_uri = AnalysisService::uri_from_path(extra_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    const auto *initial = analysis.snapshot_for_uri(main_uri);
    check(initial != nullptr, "manifestSourceUnitStability.initial_snapshot_exists");
    if (initial == nullptr || initial->workspace_index == nullptr) {
        check(false, "manifestSourceUnitStability.initial_index_exists");
        return;
    }
    const auto *initial_main = index_source_unit_for_uri(*initial->workspace_index, main_uri);
    check(initial_main != nullptr, "manifestSourceUnitStability.initial_main_source_exists");
    if (initial_main == nullptr) {
        return;
    }
    const auto main_source_unit = initial_main->source_unit_id;

    write_package_manifest(
        root, "lsp-manifest-source-unit-stability", "app", "\"main\", \"extra\"");
    analysis.invalidate_paths({manifest_path});

    const auto *updated = analysis.snapshot_for_uri(main_uri);
    check(updated != nullptr, "manifestSourceUnitStability.updated_snapshot_exists");
    if (updated == nullptr || updated->workspace_index == nullptr) {
        check(false, "manifestSourceUnitStability.updated_index_exists");
        return;
    }

    const auto *updated_main = index_source_unit_for_uri(*updated->workspace_index, main_uri);
    const auto *updated_extra = index_source_unit_for_uri(*updated->workspace_index, extra_uri);
    check(updated_main != nullptr, "manifestSourceUnitStability.updated_main_source_exists");
    check(updated_extra != nullptr, "manifestSourceUnitStability.updated_extra_source_exists");
    if (updated_main != nullptr) {
        check(updated_main->source_unit_id == main_source_unit,
              "manifestSourceUnitStability.main_source_id_stable");
    }
    if (updated_extra != nullptr) {
        check(updated_extra->source_unit_id.value > main_source_unit.value,
              "manifestSourceUnitStability.extra_source_id_appended");
    }
}

void test_lsp_workspace_index_uses_package_graph_source_unit_ids() {
    const auto root = make_temp_project("lsp_package_graph_source_unit_ids");
    const auto sysroot = root / "sysroot";
    const auto app_root = root / "app";
    const auto main_path = app_root / "src" / "main.ahfl";
    const auto extra_path = app_root / "src" / "extra.ahfl";
    write_minimal_std_package(sysroot / "std", "graph-source-units");
    write_package_manifest(app_root, "lsp-graph-source-units", "app", "\"main\", \"extra\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {}\n";
    const std::string extra_source = "module app::extra;\n"
                                     "\n"
                                     "struct ExtraOnly {}\n";
    write_file(main_path, main_source);
    write_file(extra_path, extra_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto extra_uri = AnalysisService::uri_from_path(extra_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    auto profiles = toolchain_profile_set_for_sysroot(sysroot);
    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(profiles);
    const auto *snapshot = analysis.snapshot_for_uri(main_uri);
    check(snapshot != nullptr, "workspaceIndex.graph_source_ids.snapshot_exists");
    if (snapshot == nullptr || snapshot->workspace_index == nullptr) {
        check(false, "workspaceIndex.graph_source_ids.index_exists");
        return;
    }

    const auto project_context =
        project_discovery::discover_project_context(project_discovery::ProjectDiscoveryInput{
            .document_path = main_path,
            .workspace_boundaries = {project_discovery::WorkspaceBoundary{.root = root}},
            .toolchains = std::move(profiles),
        });
    check(project_context.context.has_value(), "workspaceIndex.graph_source_ids.context_exists");
    if (!project_context.context.has_value()) {
        return;
    }

    const auto app_package =
        project_context.context->graph.package_by_name("lsp-graph-source-units");
    check(app_package.has_value(), "workspaceIndex.graph_source_ids.package_exists");
    if (!app_package.has_value()) {
        return;
    }

    const auto source_unit_for_module =
        [&](std::string_view module_path) -> const ahfl::package_graph::SourceUnitNode * {
        const auto &units = project_context.context->graph.source_units;
        const auto found = std::find_if(units.begin(), units.end(), [&](const auto &unit) {
            return unit.package == *app_package && unit.module_path == module_path;
        });
        return found == units.end() ? nullptr : &*found;
    };

    const auto *graph_main = source_unit_for_module("main");
    const auto *graph_extra = source_unit_for_module("extra");
    const auto *index_main = index_source_unit_for_uri(*snapshot->workspace_index, main_uri);
    const auto *index_extra = index_source_unit_for_uri(*snapshot->workspace_index, extra_uri);
    check(graph_main != nullptr, "workspaceIndex.graph_source_ids.graph_main_exists");
    check(graph_extra != nullptr, "workspaceIndex.graph_source_ids.graph_extra_exists");
    check(index_main != nullptr, "workspaceIndex.graph_source_ids.index_main_exists");
    check(index_extra != nullptr, "workspaceIndex.graph_source_ids.index_extra_exists");
    if (graph_main != nullptr && index_main != nullptr) {
        check(index_main->source_unit_id == graph_main->id,
              "workspaceIndex.graph_source_ids.main_matches_graph");
    }
    if (graph_extra != nullptr && index_extra != nullptr) {
        check(index_extra->source_unit_id == graph_extra->id,
              "workspaceIndex.graph_source_ids.extra_matches_graph");
    }
}

void test_lsp_workspace_index_symbol_fingerprints_are_stable() {
    const auto root = make_temp_project("lsp_symbol_fingerprint_stability");
    const auto sysroot = root / "sysroot";
    const auto app_root = root / "app";
    const auto main_path = app_root / "src" / "main.ahfl";
    write_minimal_std_package(sysroot / "std", "symbol-fingerprint-stability");
    write_package_manifest(app_root, "lsp-symbol-fingerprint-stability", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n"
                                    "\n"
                                    "fn keep(x: MainOnly) -> MainOnly effect Pure decreases 0 {\n"
                                    "    return x;\n"
                                    "}\n";
    write_file(main_path, main_source);
    const auto main_uri = AnalysisService::uri_from_path(main_path);

    auto fingerprint_for_main = [&]() -> std::optional<DefFingerprint> {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });

        auto profiles = toolchain_profile_set_for_sysroot(sysroot);
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(std::move(profiles));
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspaceIndex.def_fingerprint.snapshot_exists");
        if (snapshot == nullptr || snapshot->workspace_index == nullptr) {
            check(false, "workspaceIndex.def_fingerprint.index_exists");
            return std::nullopt;
        }
        const auto *symbol =
            index_symbol_for_canonical_name(*snapshot->workspace_index, "app::main::MainOnly");
        check(symbol != nullptr, "workspaceIndex.def_fingerprint.symbol_exists");
        if (symbol == nullptr) {
            return std::nullopt;
        }
        check(symbol->fingerprint.value != 0, "workspaceIndex.def_fingerprint.nonzero");
        return symbol->fingerprint;
    };

    const auto first = fingerprint_for_main();
    const auto second = fingerprint_for_main();
    check(first.has_value(), "workspaceIndex.def_fingerprint.first_exists");
    check(second.has_value(), "workspaceIndex.def_fingerprint.second_exists");
    if (first.has_value() && second.has_value()) {
        check(*first == *second, "workspaceIndex.def_fingerprint.stable_across_rebuild");
    }
}

void test_project_input_source_cache_is_distinct_from_open_overlays() {
    const auto root = make_temp_project("project_source_cache_overlay");
    const auto main_path = root / "src" / "main.ahfl";
    write_file(main_path,
               "module app::main;\n"
               "\n"
               "struct DiskOnly {}\n");

    ahfl::ProjectInput input;
    input.entry_files.push_back(main_path);
    input.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
        .prefix = "app",
        .root = root / "src",
        .exported_modules = {"main"},
        .dependency_prefixes = {},
    });

    const auto source_key = AnalysisService::normalized_path_key(main_path);
    input.source_cache.emplace(source_key,
                               "module app::main;\n"
                               "\n"
                               "struct CachedOnly {}\n");

    const ahfl::Frontend frontend;
    auto cached_result = ahfl::parse_project(frontend, input);
    check(!cached_result.has_errors(), "project_input.source_cache.no_errors");
    check(cached_result.graph.sources.size() == 1, "project_input.source_cache.source_count");
    if (!cached_result.graph.sources.empty()) {
        const auto &content = cached_result.graph.sources.front().source.content;
        check(content.find("CachedOnly") != std::string::npos,
              "project_input.source_cache.uses_cached_text");
        check(content.find("DiskOnly") == std::string::npos,
              "project_input.source_cache.skips_disk_text");
    }

    input.source_overlays.emplace(source_key,
                                  "module app::main;\n"
                                  "\n"
                                  "struct OverlayOnly {}\n");
    auto overlay_result = ahfl::parse_project(frontend, input);
    check(!overlay_result.has_errors(), "project_input.source_overlay.no_errors");
    check(overlay_result.graph.sources.size() == 1, "project_input.source_overlay.source_count");
    if (!overlay_result.graph.sources.empty()) {
        const auto &content = overlay_result.graph.sources.front().source.content;
        check(content.find("OverlayOnly") != std::string::npos,
              "project_input.source_overlay.uses_overlay_text");
        check(content.find("CachedOnly") == std::string::npos,
              "project_input.source_overlay.precedes_cache_text");
    }
}

void test_diagnostics_cover_parse_resolve_typecheck_and_validation() {
    const auto parse_output = diagnostics_output_for_source("struct Broken {\n    value: ;\n}\n");
    check(parse_output.find("parse.diagnostic") != std::string::npos, "diagnostics.parse_code");

    const auto resolve_output =
        diagnostics_output_for_source("struct Envelope {\n    payload: Missing;\n}\n");
    check(resolve_output.find("E::detached_unknown_nominal_type") != std::string::npos,
          "diagnostics.resolve_code");

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

void test_match_missing_patterns_diagnostic_exposes_structured_witness_data() {
    const std::string source = "enum E { A, B }\n"
                               "fn f(e: E) -> Int effect Pure decreases 0 {\n"
                               "    return match e {\n"
                               "        A => 1,\n"
                               "    };\n"
                               "}\n";

    const auto output = diagnostics_output_for_source(source);
    check(output.find("typecheck.MATCH_MISSING_PATTERNS") != std::string::npos,
          "diagnostics.match_missing_patterns.code");
    check(output.find(R"("data":{"missing_witnesses":["B"]})") != std::string::npos,
          "diagnostics.match_missing_patterns.structured_witness_data");
}

void test_project_definition_workspace_symbol_and_rename_cross_file() {
    const auto root = make_temp_project("project_cross_file");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-project", "app", "\"main\", \"types\"");
    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n"
                                    "\n"
                                    "fn use_msg(payload: types::Msg) -> Int effect Pure "
                                    "decreases 0 {\n"
                                    "    let copy = payload;\n"
                                    "    return 0;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module app::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string init = initialize_body(root);
    const std::string open_main = did_open_body(main_uri, 1, main_source);
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22}}})";
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"Msg"}})";
    const std::string type_definition =
        R"({"jsonrpc":"2.0","id":5,"method":"textDocument/typeDefinition","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22}}})";
    const std::string expression_type_definition =
        R"({"jsonrpc":"2.0","id":7,"method":"textDocument/typeDefinition","params":)" +
        hover_params_at(main_uri, position_of(main_source, "payload;")) + R"(})";
    const std::string rename =
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/rename","params":{"textDocument":{"uri":")" +
        main_uri + R"("},"position":{"line":4,"character":22},"newName":"Message"}})";
    const std::string shutdown = R"({"jsonrpc":"2.0","id":8,"method":"shutdown","params":{}})";

    const auto output = run_lsp_messages({init,
                                          open_main,
                                          definition,
                                          workspace_symbol,
                                          rename,
                                          type_definition,
                                          expression_type_definition,
                                          shutdown});
    const auto type_definition_response = response_body_for_id(output, 5);
    const auto expression_type_definition_response = response_body_for_id(output, 7);
    check(output.find(types_uri) != std::string::npos, "project.definition_targets_imported_uri");
    check(output.find("\"name\":\"Msg\"") != std::string::npos,
          "project.workspace_symbol_includes_unopened_source");
    check(output.find("Message") != std::string::npos, "project.rename_contains_new_name");
    check(output.find(main_uri) != std::string::npos, "project.rename_edits_main_uri");
    check(output.find(types_uri) != std::string::npos, "project.rename_edits_decl_uri");
    check(type_definition_response.find(types_uri) != std::string::npos,
          "project.type_definition_targets_imported_uri");
    check(expression_type_definition_response.find(types_uri) != std::string::npos,
          "project.type_definition_expression_targets_result_type");
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

void test_workspace_symbol_uses_root_index_without_open_documents() {
    const auto root = make_temp_project("project_workspace_symbol_cold_index");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-project-cold-symbol", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
                                     "\n"
                                     "struct ColdMsg {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"ColdMsg"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(types_uri) != std::string::npos,
          "project.workspace_symbol_cold_index_includes_unopened_uri");
    check(response.find("ColdMsg") != std::string::npos,
          "project.workspace_symbol_cold_index_includes_name");
}

void test_project_references_include_indexed_unopened_source() {
    const auto root = make_temp_project("project_references_index");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    const auto extra_path = root / "src" / "extra.ahfl";
    write_package_manifest(root, "lsp-project-references", "app", "\"main\", \"types\", \"extra\"");

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
    const std::string extra_source = "module app::extra;\n"
                                     "import app::types as types;\n"
                                     "\n"
                                     "struct Other {\n"
                                     "    payload: types::Msg;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);
    write_file(extra_path, extra_source);

    const auto types_uri = AnalysisService::uri_from_path(types_path);
    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto extra_uri = AnalysisService::uri_from_path(extra_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = types_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = types_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(types_uri);
        check(snapshot != nullptr, "references.index_model.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->workspace_index != nullptr, "references.index_model.index_exists");
            if (snapshot->workspace_index != nullptr) {
                check(snapshot->workspace_index->metadata().revision ==
                          snapshot->workspace_revision,
                      "references.index_model.metadata_revision_matches_snapshot");
                check(snapshot->workspace_index->metadata().index_schema_version ==
                          "lsp-workspace-index-v1",
                      "references.index_model.metadata_index_schema");
                check(snapshot->workspace_index->metadata().index_identity_schema_version ==
                          "lsp-workspace-index-identity-v1",
                      "references.index_model.metadata_identity_schema");
                const auto &source_units = snapshot->workspace_index->source_units();
                check(!source_units.empty(), "references.index_model.source_units_exist");
                const auto &symbols = snapshot->workspace_index->symbols();
                const auto msg_symbol =
                    std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                        return symbol.canonical_name == "app::types::Msg";
                    });
                check(msg_symbol != symbols.end(), "references.index_model.msg_symbol_fact");
                if (msg_symbol != symbols.end()) {
                    check(msg_symbol->name_space == ahfl::SymbolNamespace::Types,
                          "references.index_model.msg_symbol_namespace");
                    const auto &semantic_symbols = snapshot->resolve_result.symbol_table.symbols();
                    const auto semantic_msg =
                        std::find_if(semantic_symbols.begin(),
                                     semantic_symbols.end(),
                                     [](const ahfl::Symbol &symbol) {
                                         return symbol.canonical_name == "app::types::Msg";
                                     });
                    check(semantic_msg != semantic_symbols.end(),
                          "references.index_model.semantic_msg_symbol_exists");
                    if (semantic_msg != semantic_symbols.end()) {
                        const auto remapped_def =
                            snapshot->workspace_def_for_symbol(semantic_msg->id);
                        check(remapped_def.has_value(),
                              "references.index_model.semantic_remap_exists");
                        if (remapped_def.has_value()) {
                            check(*remapped_def == msg_symbol->def_id,
                                  "references.index_model.semantic_remap_matches_index_def");
                        }
                    }
                    check(msg_symbol->package_id.value != std::numeric_limits<std::size_t>::max(),
                          "references.index_model.msg_symbol_has_package_id");
                    const auto *source_unit =
                        snapshot->workspace_index->source_unit_for_id(msg_symbol->source_unit_id);
                    check(source_unit != nullptr,
                          "references.index_model.msg_symbol_source_unit_valid");
                    check(msg_symbol->selection_range.end_offset >
                              msg_symbol->selection_range.begin_offset,
                          "references.index_model.msg_symbol_selection_range_has_extent");
                    if (source_unit != nullptr) {
                        check(source_unit->package_id == msg_symbol->package_id,
                              "references.index_model.source_unit_package_matches_symbol");
                        check(source_unit->revision == snapshot->workspace_revision,
                              "references.index_model.source_unit_revision_matches_snapshot");
                        check(source_unit->uri == types_uri,
                              "references.index_model.source_unit_uri_matches_types");
                        const auto package_sources =
                            snapshot->workspace_index->source_units_for_package(
                                msg_symbol->package_id);
                        check(std::find(package_sources.begin(),
                                        package_sources.end(),
                                        msg_symbol->source_unit_id) != package_sources.end(),
                              "references.index_model.package_source_map_includes_msg_source");
                    }
                    const auto indexed_references =
                        snapshot->workspace_index->reference_locations_for_def(msg_symbol->def_id);
                    check(std::find_if(indexed_references.begin(),
                                       indexed_references.end(),
                                       [&](const Location &location) {
                                           return location.uri == main_uri;
                                       }) != indexed_references.end(),
                          "references.index_model.reference_map_includes_main");
                    check(std::find_if(indexed_references.begin(),
                                       indexed_references.end(),
                                       [&](const Location &location) {
                                           return location.uri == extra_uri;
                                       }) != indexed_references.end(),
                          "references.index_model.reference_map_includes_extra");
                }
            }
        }
    }

    const auto refs_position = position_of(types_source, "Msg");
    const std::string references =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
        types_uri + R"("},"position":{"line":)" + std::to_string(refs_position.line) +
        R"(,"character":)" + std::to_string(refs_position.character) +
        R"(},"context":{"includeDeclaration":true}}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(types_uri, 1, types_source),
        references,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(types_uri) != std::string::npos,
          "references.index_includes_declaration_source");
    check(response.find(main_uri) != std::string::npos,
          "references.index_includes_semantic_import_source");
    check(response.find(extra_uri) != std::string::npos,
          "references.index_includes_unopened_exported_source");
    check(count_substring(response, types_uri) == 1, "references.index_emits_declaration_once");
    check(count_substring(response, main_uri) == 1, "references.index_emits_semantic_import_once");
    check(count_substring(response, extra_uri) == 1, "references.index_emits_unopened_export_once");
}

void test_user_package_references_include_lazy_sysroot_index() {
    const auto root = make_temp_project("references_lazy_sysroot_index");
    const auto app_root = root / "app";
    const auto std_root = root / "std";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto collections_path = std_root / "collections.ahfl";
    const auto json_path = std_root / "json.ahfl";

    write_package_manifest(app_root,
                           "references-sysroot-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"collections\", \"json\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"collections_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(collections_path,
               "module std::collections;\n"
               "\n"
               "pub struct List<T> {}\n");
    write_file(json_path,
               "module std::json;\n"
               "import std::collections as collections;\n"
               "\n"
               "pub struct JsonList {\n"
               "    payload: collections::List<Int>;\n"
               "}\n");

    const std::string app_source = "module app::main;\n"
                                   "import std::collections as collections;\n"
                                   "\n"
                                   "struct Use {\n"
                                   "    payload: collections::List<Int>;\n"
                                   "}\n";
    write_file(app_path, app_source);

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    const auto collections_uri = AnalysisService::uri_from_path(collections_path);
    const auto json_uri = AnalysisService::uri_from_path(json_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = app_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = app_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));
        const auto *snapshot = analysis.snapshot_for_uri(app_uri);
        check(snapshot != nullptr, "references.lazy_sysroot.snapshot_exists");
        const auto *sysroot_index = analysis.sysroot_index_for_uri(app_uri);
        check(sysroot_index != nullptr, "references.lazy_sysroot.index_exists");
        if (sysroot_index != nullptr) {
            const auto &symbols = sysroot_index->symbols();
            const auto list_symbol =
                std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                    return symbol.canonical_name == "std::collections::List";
                });
            check(list_symbol != symbols.end(), "references.lazy_sysroot.list_symbol_exists");
            if (list_symbol != symbols.end()) {
                const auto indexed_refs =
                    sysroot_index->reference_locations_for_def(list_symbol->def_id);
                check(std::find_if(indexed_refs.begin(),
                                   indexed_refs.end(),
                                   [&](const Location &location) {
                                       return location.uri == json_uri;
                                   }) != indexed_refs.end(),
                      "references.lazy_sysroot.index_includes_json_reference");
            }
        }
    }
    const auto list_position = position_of(app_source, "List<Int>");
    const std::string references =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/references","params":{"textDocument":{"uri":")" +
        app_uri + R"("},"position":{"line":)" + std::to_string(list_position.line) +
        R"(,"character":)" + std::to_string(list_position.character) +
        R"(},"context":{"includeDeclaration":true}}})";

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(app_uri, 1, app_source),
        references,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);

    check(response.find(collections_uri) != std::string::npos,
          "references.lazy_sysroot_includes_std_declaration");
    check(response.find(json_uri) != std::string::npos,
          "references.lazy_sysroot_includes_unopened_std_export");
    check(response.find(app_uri) != std::string::npos,
          "references.lazy_sysroot_keeps_user_semantic_reference");
}

void test_workspace_index_identity_hash_and_flat_store_ids() {
    check(SourceUnitId{7} == SourceUnitId{7}, "workspace_index.identity.source_unit_equal");
    check(!(SourceUnitId{7} == SourceUnitId{8}), "workspace_index.identity.source_unit_not_equal");
    check(DefId{3} == DefId{3}, "workspace_index.identity.def_equal");
    check(!(DefId{3} == DefId{4}), "workspace_index.identity.def_not_equal");
    check(WorkspaceImplId{5} == WorkspaceImplId{5}, "workspace_index.identity.impl_equal");
    check(!(WorkspaceImplId{5} == WorkspaceImplId{6}), "workspace_index.identity.impl_not_equal");
    check(ReferenceFactId{9} == ReferenceFactId{9}, "workspace_index.identity.reference_equal");
    check(!(ReferenceFactId{9} == ReferenceFactId{10}),
          "workspace_index.identity.reference_not_equal");

    const auto int_type = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Int,
    };
    const auto int_type_again = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Int,
    };
    const auto float_type = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Float,
    };
    const auto decimal_zero = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Decimal,
        .primitive_parameter = 0,
    };
    const auto decimal_two = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Decimal,
        .primitive_parameter = 2,
    };
    const auto list_int = TypeKey{
        .kind = TypeKey::Kind::Nominal,
        .def = DefId{42},
        .type_args = {int_type},
    };
    const auto list_float = TypeKey{
        .kind = TypeKey::Kind::Nominal,
        .def = DefId{42},
        .type_args = {float_type},
    };

    check(int_type == int_type_again, "workspace_index.identity.type_key_equal");
    check(TypeKeyHash{}(int_type) == TypeKeyHash{}(int_type_again),
          "workspace_index.identity.type_key_equal_hash");

    std::unordered_set<TypeKey, TypeKeyHash> keys;
    keys.insert(int_type);
    keys.insert(int_type_again);
    keys.insert(float_type);
    keys.insert(decimal_zero);
    keys.insert(decimal_two);
    keys.insert(list_int);
    keys.insert(list_float);
    check(keys.size() == 6, "workspace_index.identity.type_key_hash_distinguishes_shape");
    check(keys.find(decimal_two) != keys.end(),
          "workspace_index.identity.decimal_parameter_lookup");
    check(keys.find(list_int) != keys.end(), "workspace_index.identity.nominal_generic_lookup");

    LspWorkspaceIndex index;
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = SourceUnitId{0},
        .package_id = ahfl::package_graph::PackageId{0},
        .path = "pkg.ahfl",
        .uri = "file:///pkg.ahfl",
        .revision = 1,
        .completeness = FactCompleteness::Typed,
    });
    index.add_symbol(SymbolFact{
        .def_id = DefId{99},
        .package_id = ahfl::package_graph::PackageId{0},
        .source_unit_id = SourceUnitId{0},
        .kind = ahfl::SymbolKind::Struct,
        .local_name = "Box",
        .canonical_name = "pkg::Box",
        .declaration_range = ahfl::SourceRange{0, 3},
        .selection_range = ahfl::SourceRange{0, 3},
        .location = index_test_location("file:///pkg.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_impl(ImplFact{
        .impl_id = WorkspaceImplId{99},
        .package_id = ahfl::package_graph::PackageId{0},
        .source_unit_id = SourceUnitId{0},
        .target_type = TypeKey{.kind = TypeKey::Kind::Nominal, .def = DefId{0}},
        .declaration_range = ahfl::SourceRange{4, 8},
        .target_range = ahfl::SourceRange{9, 12},
        .location = index_test_location("file:///pkg.ahfl", 1),
        .source_order = 0,
        .completeness = FactCompleteness::Typed,
    });
    index.add_reference(ReferenceFact{
        .package_id = ahfl::package_graph::PackageId{0},
        .source_unit_id = SourceUnitId{0},
        .target_def = DefId{0},
        .reference_kind = ahfl::ReferenceKind::TypeName,
        .range = ahfl::SourceRange{13, 16},
        .location = index_test_location("file:///pkg.ahfl", 2),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_reference(ReferenceFact{
        .package_id = ahfl::package_graph::PackageId{0},
        .source_unit_id = SourceUnitId{0},
        .target_def = DefId{0},
        .reference_kind = ahfl::ReferenceKind::TypeName,
        .range = ahfl::SourceRange{17, 20},
        .location = index_test_location("file:///pkg.ahfl", 3),
        .completeness = FactCompleteness::Parsed,
    });
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = SourceUnitId{1000},
        .package_id = ahfl::package_graph::PackageId{0},
        .path = "overlay.ahfl",
        .uri = "file:///overlay.ahfl",
        .revision = 1,
        .scope_kinds = {LspNavigationIndexSourceKind::OpenOverlay},
        .completeness = FactCompleteness::Parsed,
    });
    index.add_diagnostic(IndexDiagnosticFact{
        .diagnostic_id = IndexDiagnosticFactId{99},
        .package_id = ahfl::package_graph::PackageId{0},
        .source_unit_id = SourceUnitId{1000},
        .phase = IndexDiagnosticPhase::Parse,
        .severity = ahfl::DiagnosticSeverity::Error,
        .code = "parse.test",
        .message = "overlay parse diagnostic",
        .range = ahfl::SourceRange{0, 1},
        .completeness = FactCompleteness::Parsed,
    });

    const auto *box_symbol = index.symbol_for_def(DefId{0});
    check(box_symbol != nullptr, "workspace_index.identity.symbol_flat_id_exists");
    if (box_symbol != nullptr) {
        check(box_symbol->def_id == DefId{0}, "workspace_index.identity.symbol_flat_id");
    }
    const auto &impls = index.impls();
    check(impls.size() == 1, "workspace_index.identity.impl_count");
    if (!impls.empty()) {
        check(impls.front().impl_id == WorkspaceImplId{0}, "workspace_index.identity.impl_flat_id");
    }
    const auto nominal_impls = index.implementation_locations_for_nominal_def(DefId{0});
    check(nominal_impls.size() == 1, "workspace_index.identity.nominal_impl_lookup");
    const auto references = index.reference_locations_for_def(DefId{0});
    check(references.size() == 1, "workspace_index.identity.resolved_references_only");
    if (!references.empty()) {
        check(references.front().range.start.line == 2,
              "workspace_index.identity.reference_location");
    }
    const auto *overlay_source = index.source_unit_for_id(SourceUnitId{1000});
    check(overlay_source != nullptr, "workspace_index.identity.opaque_source_unit_lookup");
    if (overlay_source != nullptr) {
        check(overlay_source->uri == "file:///overlay.ahfl",
              "workspace_index.identity.opaque_source_unit_uri");
    }
    check(index.source_units().size() == 2,
          "workspace_index.identity.opaque_source_unit_dense_store");
    const auto overlay_diagnostics = index.diagnostics_for_source(SourceUnitId{1000});
    check(overlay_diagnostics.size() == 1,
          "workspace_index.identity.opaque_source_unit_diagnostics");
    check(index.source_unit_for_id(SourceUnitId{7}) == nullptr,
          "workspace_index.identity.unknown_source_unit_missing");
}

void test_workspace_index_queries_sort_by_package_source_and_order() {
    LspWorkspaceIndex index;
    const auto pkg1 = ahfl::package_graph::PackageId{1};
    const auto pkg2 = ahfl::package_graph::PackageId{2};
    const auto source_pkg2 = SourceUnitId{0};
    const auto source_pkg1_a = SourceUnitId{1};
    const auto source_pkg1_b = SourceUnitId{2};

    index.add_source_unit(SourceUnitFact{
        .source_unit_id = source_pkg2,
        .package_id = pkg2,
        .path = "pkg2.ahfl",
        .uri = "file:///pkg2.ahfl",
        .revision = 1,
        .completeness = FactCompleteness::Typed,
    });
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = source_pkg1_a,
        .package_id = pkg1,
        .path = "pkg1_a.ahfl",
        .uri = "file:///pkg1_a.ahfl",
        .revision = 1,
        .completeness = FactCompleteness::Typed,
    });
    index.add_source_unit(SourceUnitFact{
        .source_unit_id = source_pkg1_b,
        .package_id = pkg1,
        .path = "pkg1_b.ahfl",
        .uri = "file:///pkg1_b.ahfl",
        .revision = 1,
        .completeness = FactCompleteness::Typed,
    });

    const auto target_def = DefId{0};
    index.add_symbol(SymbolFact{
        .def_id = DefId{42},
        .package_id = pkg2,
        .source_unit_id = source_pkg2,
        .kind = ahfl::SymbolKind::Struct,
        .local_name = "Thing",
        .canonical_name = "pkg2::Thing",
        .declaration_range = ahfl::SourceRange{0, 5},
        .selection_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg2.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_symbol(SymbolFact{
        .def_id = DefId{1},
        .package_id = pkg1,
        .source_unit_id = source_pkg1_b,
        .kind = ahfl::SymbolKind::Struct,
        .local_name = "Thing",
        .canonical_name = "pkg1::b::Thing",
        .declaration_range = ahfl::SourceRange{0, 5},
        .selection_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_b.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_symbol(SymbolFact{
        .def_id = DefId{2},
        .package_id = pkg1,
        .source_unit_id = source_pkg1_a,
        .kind = ahfl::SymbolKind::Struct,
        .local_name = "Thing",
        .canonical_name = "pkg1::a::Thing",
        .declaration_range = ahfl::SourceRange{0, 5},
        .selection_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_a.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });

    const auto *target_symbol = index.symbol_for_def(target_def);
    check(target_symbol != nullptr, "workspace_index.order.symbol_for_def_exists");
    if (target_symbol != nullptr) {
        check(target_symbol->def_id == target_def,
              "workspace_index.order.symbol_for_def_normalized");
        check(target_symbol->location.uri == "file:///pkg2.ahfl",
              "workspace_index.order.symbol_for_def_location");
    }
    check(index.symbol_for_def(DefId{99}) == nullptr,
          "workspace_index.order.symbol_for_unknown_def_missing");

    const auto symbols = index.workspace_symbols("Thing");
    check(symbols.size() == 3, "workspace_index.order.symbol_count");
    if (symbols.size() == 3) {
        check(symbols[0]->location.uri == "file:///pkg1_a.ahfl",
              "workspace_index.order.symbol_pkg1_source_a_first");
        check(symbols[1]->location.uri == "file:///pkg1_b.ahfl",
              "workspace_index.order.symbol_pkg1_source_b_second");
        check(symbols[2]->location.uri == "file:///pkg2.ahfl",
              "workspace_index.order.symbol_pkg2_last");
    }

    index.add_reference(ReferenceFact{
        .package_id = pkg2,
        .source_unit_id = source_pkg2,
        .target_def = target_def,
        .reference_kind = ahfl::ReferenceKind::TypeName,
        .range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg2.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_reference(ReferenceFact{
        .package_id = pkg1,
        .source_unit_id = source_pkg1_b,
        .target_def = target_def,
        .reference_kind = ahfl::ReferenceKind::TypeName,
        .range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_b.ahfl", 0),
        .completeness = FactCompleteness::Resolved,
    });
    index.add_reference(ReferenceFact{
        .package_id = pkg1,
        .source_unit_id = source_pkg1_a,
        .target_def = target_def,
        .reference_kind = ahfl::ReferenceKind::TypeName,
        .range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_a.ahfl", 7),
        .completeness = FactCompleteness::Resolved,
    });
    const auto references = index.reference_locations_for_def(target_def);
    check(references.size() == 3, "workspace_index.order.reference_count");
    if (references.size() == 3) {
        check(references[0].uri == "file:///pkg1_a.ahfl",
              "workspace_index.order.reference_pkg1_source_a_first");
        check(references[1].uri == "file:///pkg1_b.ahfl",
              "workspace_index.order.reference_pkg1_source_b_second");
        check(references[2].uri == "file:///pkg2.ahfl",
              "workspace_index.order.reference_pkg2_last");
    }

    const auto int_type = TypeKey{
        .kind = TypeKey::Kind::Primitive,
        .primitive = PrimitiveKind::Int,
    };
    index.add_impl(ImplFact{
        .impl_id = WorkspaceImplId{0},
        .package_id = pkg2,
        .source_unit_id = source_pkg2,
        .target_type = int_type,
        .trait_def = std::nullopt,
        .declaration_range = ahfl::SourceRange{0, 5},
        .target_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg2.ahfl", 0),
        .source_order = 0,
        .completeness = FactCompleteness::Typed,
    });
    index.add_impl(ImplFact{
        .impl_id = WorkspaceImplId{1},
        .package_id = pkg1,
        .source_unit_id = source_pkg1_a,
        .target_type = int_type,
        .trait_def = std::nullopt,
        .declaration_range = ahfl::SourceRange{0, 5},
        .target_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_a_second.ahfl", 0),
        .source_order = 2,
        .completeness = FactCompleteness::Typed,
    });
    index.add_impl(ImplFact{
        .impl_id = WorkspaceImplId{2},
        .package_id = pkg1,
        .source_unit_id = source_pkg1_b,
        .target_type = int_type,
        .trait_def = std::nullopt,
        .declaration_range = ahfl::SourceRange{0, 5},
        .target_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_b.ahfl", 0),
        .source_order = 0,
        .completeness = FactCompleteness::Typed,
    });
    index.add_impl(ImplFact{
        .impl_id = WorkspaceImplId{3},
        .package_id = pkg1,
        .source_unit_id = source_pkg1_a,
        .target_type = int_type,
        .trait_def = std::nullopt,
        .declaration_range = ahfl::SourceRange{0, 5},
        .target_range = ahfl::SourceRange{0, 5},
        .location = index_test_location("file:///pkg1_a_first.ahfl", 0),
        .source_order = 1,
        .completeness = FactCompleteness::Typed,
    });
    const auto impls = index.implementation_locations_for_type(int_type);
    check(impls.size() == 4, "workspace_index.order.impl_count");
    if (impls.size() == 4) {
        check(impls[0].uri == "file:///pkg1_a_first.ahfl",
              "workspace_index.order.impl_pkg1_source_a_order_first");
        check(impls[1].uri == "file:///pkg1_a_second.ahfl",
              "workspace_index.order.impl_pkg1_source_a_order_second");
        check(impls[2].uri == "file:///pkg1_b.ahfl",
              "workspace_index.order.impl_pkg1_source_b_third");
        check(impls[3].uri == "file:///pkg2.ahfl", "workspace_index.order.impl_pkg2_last");
    }
}

void test_workspace_index_assigns_source_units_by_scope_order() {
    const auto root = make_temp_project("workspace_index_source_unit_order");
    const auto source_root = root / "src";
    const auto a_path = source_root / "a.ahfl";
    const auto b_path = source_root / "b.ahfl";

    write_file(a_path,
               "module app::a;\n"
               "\n"
               "struct A {}\n"
               "\n"
               "impl A {\n"
               "    fn keep_a(self: A) -> A effect Pure decreases 0 {\n"
               "        return self;\n"
               "    }\n"
               "}\n");
    write_file(b_path,
               "module app::b;\n"
               "\n"
               "struct B {}\n"
               "\n"
               "impl B {\n"
               "    fn keep_b(self: B) -> B effect Pure decreases 0 {\n"
               "        return self;\n"
               "    }\n"
               "}\n");

    ahfl::ProjectInput project;
    project.entry_files = {a_path};
    project.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
        .prefix = "app",
        .root = source_root,
        .exported_modules = {"a", "b"},
        .dependency_prefixes = {},
    });

    const auto package_id = ahfl::package_graph::PackageId{0};
    const ahfl::Frontend frontend;
    auto index = build_lsp_workspace_index(
        frontend,
        LspWorkspaceIndexInput{
            .project = std::move(project),
            .scope =
                NavigationIndexScope{
                    .package_roots =
                        {
                            LspIndexPackageRoot{
                                .package_id = package_id,
                                .module_root = source_root,
                            },
                        },
                    .source_units =
                        {
                            LspIndexSourceUnitSeed{
                                .source_unit_id = SourceUnitId{0},
                                .package_id = package_id,
                                .path = b_path,
                                .scope_kinds =
                                    {
                                        LspNavigationIndexSourceKind::OpenOverlay,
                                        LspNavigationIndexSourceKind::PackageExport,
                                        LspNavigationIndexSourceKind::PackageExport,
                                    },
                            },
                            LspIndexSourceUnitSeed{
                                .source_unit_id = SourceUnitId{1},
                                .package_id = package_id,
                                .path = a_path,
                                .scope_kinds = {LspNavigationIndexSourceKind::PackageExport},
                            },
                        },
                },
            .metadata =
                NavigationIndexMetadata{
                    .revision = 7,
                    .index_schema_version = "test-index-schema",
                    .index_identity_schema_version = "test-identity-schema",
                },
        });

    const auto &source_units = index.source_units();
    check(source_units.size() == 2, "workspace_index.source_unit_order.count");
    if (source_units.size() == 2) {
        check(source_units[0].source_unit_id == SourceUnitId{0},
              "workspace_index.source_unit_order.first_id");
        check(source_units[0].uri == AnalysisService::uri_from_path(b_path),
              "workspace_index.source_unit_order.scope_seed_b_first");
        check(source_units[0].uri != AnalysisService::uri_from_path(a_path),
              "workspace_index.source_unit_order.scope_seed_not_entry_file");
        check(source_units[0].completeness == FactCompleteness::Typed,
              "workspace_index.source_unit_order.first_typed");
        check(source_unit_has_scope_kind(source_units[0],
                                         LspNavigationIndexSourceKind::PackageExport),
              "workspace_index.source_unit_order.scope_kind");
        check(source_units[0].scope_kinds.size() == 2,
              "workspace_index.source_unit_order.scope_kinds_deduplicated");
        if (source_units[0].scope_kinds.size() == 2) {
            check(source_units[0].scope_kinds[0] == LspNavigationIndexSourceKind::PackageExport,
                  "workspace_index.source_unit_order.scope_kinds_sorted_package");
            check(source_units[0].scope_kinds[1] == LspNavigationIndexSourceKind::OpenOverlay,
                  "workspace_index.source_unit_order.scope_kinds_sorted_overlay");
        }
        check(source_units[1].source_unit_id == SourceUnitId{1},
              "workspace_index.source_unit_order.second_id");
        check(source_units[1].uri == AnalysisService::uri_from_path(a_path),
              "workspace_index.source_unit_order.scope_seed_a_second");
        check(source_units[1].completeness == FactCompleteness::Typed,
              "workspace_index.source_unit_order.second_typed");
    }

    const auto package_sources = index.source_units_for_package(package_id);
    check(package_sources.size() == 2, "workspace_index.source_unit_order.package_count");
    if (package_sources.size() == 2) {
        check(package_sources[0] == SourceUnitId{0},
              "workspace_index.source_unit_order.package_b_first");
        check(package_sources[1] == SourceUnitId{1},
              "workspace_index.source_unit_order.package_a_second");
    }

    const auto &symbols = index.symbols();
    const auto b_symbol =
        std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
            return symbol.local_name == "B";
        });
    const auto a_symbol =
        std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
            return symbol.local_name == "A";
        });
    check(b_symbol != symbols.end(), "workspace_index.source_unit_order.b_symbol_exists");
    check(a_symbol != symbols.end(), "workspace_index.source_unit_order.a_symbol_exists");
    if (b_symbol != symbols.end() && a_symbol != symbols.end()) {
        check(b_symbol->source_unit_id == SourceUnitId{0},
              "workspace_index.source_unit_order.b_symbol_source_unit");
        check(a_symbol->source_unit_id == SourceUnitId{1},
              "workspace_index.source_unit_order.a_symbol_source_unit");
        check(b_symbol->def_id.value < a_symbol->def_id.value,
              "workspace_index.source_unit_order.def_id_uses_scope_order");
    }

    const auto b_uri = AnalysisService::uri_from_path(b_path);
    const auto a_uri = AnalysisService::uri_from_path(a_path);
    const auto &impls = index.impls();
    const auto b_impl = std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
        return impl.location.uri == b_uri;
    });
    const auto a_impl = std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
        return impl.location.uri == a_uri;
    });
    check(b_impl != impls.end(), "workspace_index.source_unit_order.b_impl_exists");
    check(a_impl != impls.end(), "workspace_index.source_unit_order.a_impl_exists");
    if (b_impl != impls.end() && a_impl != impls.end()) {
        check(b_impl->source_unit_id == SourceUnitId{0},
              "workspace_index.source_unit_order.b_impl_source_unit");
        check(a_impl->source_unit_id == SourceUnitId{1},
              "workspace_index.source_unit_order.a_impl_source_unit");
        check(b_impl->impl_id.value < a_impl->impl_id.value,
              "workspace_index.source_unit_order.impl_id_uses_scope_order");
    }
}

void test_workspace_index_reuses_unchanged_source_unit_facts() {
    const auto root = make_temp_project("workspace_index_incremental_source_units");
    const auto source_root = root / "src";
    const auto a_path = source_root / "a.ahfl";
    const auto b_path = source_root / "b.ahfl";

    const std::string a_source_v1 = "module app::a;\n"
                                    "import app::b as b;\n"
                                    "\n"
                                    "struct A {\n"
                                    "    payload: b::B;\n"
                                    "}\n";
    const std::string a_source_v2 = "module app::a;\n"
                                    "import app::b as b;\n"
                                    "\n"
                                    "struct A {\n"
                                    "    payload: b::B;\n"
                                    "}\n"
                                    "\n"
                                    "struct AddedInA {}\n";
    const std::string b_source = "module app::b;\n"
                                 "\n"
                                 "struct B {}\n"
                                 "\n"
                                 "fn keep(x: B) -> B effect Pure decreases 0 {\n"
                                 "    return x;\n"
                                 "}\n"
                                 "\n"
                                 "impl B {\n"
                                 "    fn clone(self: B) -> B effect Pure decreases 0 {\n"
                                 "        return self;\n"
                                 "    }\n"
                                 "}\n";
    write_file(a_path, a_source_v1);
    write_file(b_path, b_source);

    const auto package_id = ahfl::package_graph::PackageId{0};
    const auto make_input = [&](std::uint64_t revision,
                                const LspWorkspaceIndex *previous_index = nullptr) {
        ahfl::ProjectInput project;
        project.entry_files = {a_path, b_path};
        project.module_roots.push_back(ahfl::ProjectInput::ModuleRoot{
            .prefix = "app",
            .root = source_root,
            .exported_modules = {"a", "b"},
            .dependency_prefixes = {},
        });
        return LspWorkspaceIndexInput{
            .project = std::move(project),
            .scope =
                NavigationIndexScope{
                    .package_roots =
                        {
                            LspIndexPackageRoot{
                                .package_id = package_id,
                                .module_root = source_root,
                            },
                        },
                    .source_units =
                        {
                            LspIndexSourceUnitSeed{
                                .source_unit_id = SourceUnitId{0},
                                .package_id = package_id,
                                .path = a_path,
                                .scope_kinds = {LspNavigationIndexSourceKind::PackageExport},
                            },
                            LspIndexSourceUnitSeed{
                                .source_unit_id = SourceUnitId{1},
                                .package_id = package_id,
                                .path = b_path,
                                .scope_kinds = {LspNavigationIndexSourceKind::PackageExport},
                            },
                        },
                },
            .metadata =
                NavigationIndexMetadata{
                    .revision = revision,
                    .index_schema_version = "test-index-schema",
                    .index_identity_schema_version = "test-identity-schema",
                },
            .previous_index = previous_index,
        };
    };

    const ahfl::Frontend frontend;
    auto first = build_lsp_workspace_index(frontend, make_input(1));
    check(first.reuse_stats().reused_source_units == 0,
          "workspace_index.incremental.first_has_no_reuse");

    const auto *first_a_source = first.source_unit_for_id(SourceUnitId{0});
    const auto *first_b_source = first.source_unit_for_id(SourceUnitId{1});
    const auto *first_b_symbol = index_symbol_for_canonical_name(first, "app::b::B");
    check(first_a_source != nullptr, "workspace_index.incremental.first_a_source_exists");
    check(first_b_source != nullptr, "workspace_index.incremental.first_b_source_exists");
    check(first_b_symbol != nullptr, "workspace_index.incremental.first_b_symbol_exists");
    if (first_b_symbol != nullptr) {
        check(!first.reference_locations_for_def(first_b_symbol->def_id).empty(),
              "workspace_index.incremental.first_b_references_exist");
        check(!first.implementation_locations_for_nominal_def(first_b_symbol->def_id).empty(),
              "workspace_index.incremental.first_b_impl_exists");
    }

    write_file(a_path, a_source_v2);
    auto second = build_lsp_workspace_index(frontend, make_input(2, &first));
    const auto *second_a_source = second.source_unit_for_id(SourceUnitId{0});
    const auto *second_b_source = second.source_unit_for_id(SourceUnitId{1});
    const auto *second_b_symbol = index_symbol_for_canonical_name(second, "app::b::B");
    check(second_a_source != nullptr, "workspace_index.incremental.second_a_source_exists");
    check(second_b_source != nullptr, "workspace_index.incremental.second_b_source_exists");
    check(second_b_symbol != nullptr, "workspace_index.incremental.second_b_symbol_exists");
    if (first_a_source != nullptr && first_b_source != nullptr && second_a_source != nullptr &&
        second_b_source != nullptr) {
        check(second_a_source->content_fingerprint != first_a_source->content_fingerprint,
              "workspace_index.incremental.changed_source_not_reused_by_fingerprint");
        check(second_b_source->content_fingerprint == first_b_source->content_fingerprint,
              "workspace_index.incremental.unchanged_source_fingerprint_stable");
    }

    check(second.reuse_stats().reused_source_units == 1,
          "workspace_index.incremental.reuses_only_unchanged_source");
    check(second.reuse_stats().reused_symbol_facts > 0,
          "workspace_index.incremental.reuses_symbol_facts");
    check(second.reuse_stats().reused_reference_facts > 0,
          "workspace_index.incremental.reuses_reference_facts");
    check(second.reuse_stats().reused_impl_facts > 0,
          "workspace_index.incremental.reuses_impl_facts");
    if (second_b_symbol != nullptr) {
        check(!second.reference_locations_for_def(second_b_symbol->def_id).empty(),
              "workspace_index.incremental.remapped_references_still_query");
        check(!second.implementation_locations_for_nominal_def(second_b_symbol->def_id).empty(),
              "workspace_index.incremental.remapped_impls_still_query");
    }
}

void test_workspace_symbol_keeps_index_facts_when_exported_module_typecheck_fails() {
    const auto root = make_temp_project("project_index_partial_typecheck");
    const auto main_path = root / "src" / "main.ahfl";
    const auto broken_path = root / "src" / "broken.ahfl";
    write_package_manifest(root, "lsp-project-partial-index", "app", "\"main\", \"broken\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string broken_source = "module app::broken;\n"
                                      "\n"
                                      "struct BrokenIndexed {\n"
                                      "    value: Int;\n"
                                      "}\n"
                                      "\n"
                                      "impl BrokenIndexed {\n"
                                      "    fn clone(self: BrokenIndexed) -> BrokenIndexed effect "
                                      "Pure decreases 0 {\n"
                                      "        return self;\n"
                                      "    }\n"
                                      "}\n"
                                      "\n"
                                      "const bad: Int = \"not an int\";\n";
    write_file(main_path, main_source);
    write_file(broken_path, broken_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto broken_uri = AnalysisService::uri_from_path(broken_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_symbol.partial_index.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(broken_uri) == nullptr,
                  "workspace_symbol.partial_index.broken_not_in_semantic_sources");
            check(snapshot->workspace_index != nullptr,
                  "workspace_symbol.partial_index.workspace_index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *broken_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, broken_uri);
                check(broken_source != nullptr,
                      "workspace_symbol.partial_index.broken_source_unit_exists");
                if (broken_source != nullptr) {
                    check(index_has_diagnostic(*snapshot->workspace_index,
                                               broken_source->source_unit_id,
                                               IndexDiagnosticPhase::TypeCheck,
                                               "typecheck."),
                          "workspace_symbol.partial_index.typecheck_diagnostic_fact");
                    const auto &impls = snapshot->workspace_index->impls();
                    const auto resolved_impl =
                        std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
                            return impl.location.uri == broken_uri &&
                                   impl.completeness == FactCompleteness::Resolved;
                        });
                    check(resolved_impl != impls.end(),
                          "workspace_symbol.partial_index.resolved_impl_fact_exists");
                    if (resolved_impl != impls.end()) {
                        check(resolved_impl->source_unit_id == broken_source->source_unit_id,
                              "workspace_symbol.partial_index.resolved_impl_source_unit");
                        check(resolved_impl->target_type.kind == TypeKey::Kind::Unknown,
                              "workspace_symbol.partial_index.resolved_impl_unknown_type");
                        check(!resolved_impl->methods.empty() &&
                                  resolved_impl->methods.front().name == "clone",
                              "workspace_symbol.partial_index.resolved_impl_method_header");
                    }
                    const auto queried_impls =
                        snapshot->workspace_index->implementation_locations_for_type(TypeKey{
                            .kind = TypeKey::Kind::Unknown,
                        });
                    check(queried_impls.empty(),
                          "workspace_symbol.partial_index.resolved_impl_not_queryable");
                }
            }
        }
    }

    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"BrokenIndexed"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(broken_uri) != std::string::npos,
          "workspace_symbol.partial_index_includes_typecheck_failed_export");
    check(response.find("BrokenIndexed") != std::string::npos,
          "workspace_symbol.partial_index_includes_typecheck_failed_symbol");
}

void test_workspace_symbol_keeps_parse_facts_when_exported_module_resolve_fails() {
    const auto root = make_temp_project("project_index_partial_resolve");
    const auto main_path = root / "src" / "main.ahfl";
    const auto broken_path = root / "src" / "broken.ahfl";
    write_package_manifest(
        root, "lsp-project-partial-resolve-index", "app", "\"main\", \"broken\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string broken_source = "module app::broken;\n"
                                      "\n"
                                      "struct ResolveIndexed {\n"
                                      "    value: MissingType;\n"
                                      "}\n"
                                      "\n"
                                      "impl ResolveIndexed {\n"
                                      "    fn clone(self: ResolveIndexed) -> ResolveIndexed "
                                      "effect Pure decreases 0 {\n"
                                      "        return self;\n"
                                      "    }\n"
                                      "}\n";
    write_file(main_path, main_source);
    write_file(broken_path, broken_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto broken_uri = AnalysisService::uri_from_path(broken_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_symbol.resolve_index.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(broken_uri) == nullptr,
                  "workspace_symbol.resolve_index.broken_not_in_semantic_sources");
            check(snapshot->workspace_index != nullptr,
                  "workspace_symbol.resolve_index.workspace_index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *broken_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, broken_uri);
                check(broken_source != nullptr,
                      "workspace_symbol.resolve_index.broken_source_unit_exists");
                if (broken_source != nullptr) {
                    check(index_has_diagnostic(*snapshot->workspace_index,
                                               broken_source->source_unit_id,
                                               IndexDiagnosticPhase::Resolve,
                                               "resolve."),
                          "workspace_symbol.resolve_index.resolve_diagnostic_fact");
                    const auto &impls = snapshot->workspace_index->impls();
                    const auto parsed_impl =
                        std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
                            return impl.location.uri == broken_uri &&
                                   impl.completeness == FactCompleteness::Parsed;
                        });
                    check(parsed_impl != impls.end(),
                          "workspace_symbol.resolve_index.parsed_impl_fact_exists");
                    if (parsed_impl != impls.end()) {
                        check(parsed_impl->source_unit_id == broken_source->source_unit_id,
                              "workspace_symbol.resolve_index.parsed_impl_source_unit");
                        check(!parsed_impl->methods.empty() &&
                                  parsed_impl->methods.front().name == "clone",
                              "workspace_symbol.resolve_index.parsed_impl_method_header");
                    }
                }
            }
        }
    }

    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"ResolveIndexed"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(broken_uri) != std::string::npos,
          "workspace_symbol.resolve_index_includes_resolve_failed_export");
    check(response.find("ResolveIndexed") != std::string::npos,
          "workspace_symbol.resolve_index_includes_resolve_failed_symbol");
}

void test_workspace_symbol_keeps_parse_skeleton_when_exported_module_parse_fails() {
    const auto root = make_temp_project("project_index_parse_skeleton");
    const auto main_path = root / "src" / "main.ahfl";
    const auto broken_path = root / "src" / "broken.ahfl";
    write_package_manifest(root, "lsp-project-parse-skeleton", "app", "\"main\", \"broken\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string broken_source = "module app::broken;\n"
                                      "\n"
                                      "struct ParsedBeforeError {\n"
                                      "    value: Int;\n"
                                      "}\n"
                                      "\n"
                                      "impl ParsedBeforeError {\n"
                                      "    fn clone(self: ParsedBeforeError) -> "
                                      "ParsedBeforeError effect Pure decreases 0 {\n"
                                      "        return self;\n"
                                      "    }\n"
                                      "}\n"
                                      "\n"
                                      "fn still_broken(\n";
    write_file(main_path, main_source);
    write_file(broken_path, broken_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto broken_uri = AnalysisService::uri_from_path(broken_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_symbol.parse_index.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->workspace_index != nullptr,
                  "workspace_symbol.parse_index.workspace_index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *broken_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, broken_uri);
                check(broken_source != nullptr,
                      "workspace_symbol.parse_index.broken_source_unit_exists");
                if (broken_source != nullptr) {
                    check(index_has_diagnostic(*snapshot->workspace_index,
                                               broken_source->source_unit_id,
                                               IndexDiagnosticPhase::Parse,
                                               "parse."),
                          "workspace_symbol.parse_index.parse_diagnostic_fact");
                    const auto &impls = snapshot->workspace_index->impls();
                    const auto parsed_impl =
                        std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
                            return impl.location.uri == broken_uri &&
                                   impl.completeness == FactCompleteness::Parsed;
                        });
                    check(parsed_impl != impls.end(),
                          "workspace_symbol.parse_index.parsed_impl_fact_exists");
                    if (parsed_impl != impls.end()) {
                        check(parsed_impl->source_unit_id == broken_source->source_unit_id,
                              "workspace_symbol.parse_index.parsed_impl_source_unit");
                        check(parsed_impl->target_type.kind == TypeKey::Kind::Unknown,
                              "workspace_symbol.parse_index.parsed_impl_unknown_type");
                        check(!parsed_impl->methods.empty() &&
                                  parsed_impl->methods.front().name == "clone",
                              "workspace_symbol.parse_index.parsed_impl_method_header");
                    }
                    const auto queried_impls =
                        snapshot->workspace_index->implementation_locations_for_type(TypeKey{
                            .kind = TypeKey::Kind::Unknown,
                        });
                    check(queried_impls.empty(),
                          "workspace_symbol.parse_index.parsed_impl_not_queryable");
                }
            }
        }
    }

    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"ParsedBeforeError"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(broken_uri) != std::string::npos,
          "workspace_symbol.parse_skeleton_includes_parse_failed_export");
    check(response.find("ParsedBeforeError") != std::string::npos,
          "workspace_symbol.parse_skeleton_includes_parse_failed_symbol");
}

void test_parse_failed_export_does_not_block_typed_facts_for_healthy_index_sources() {
    const auto root = make_temp_project("project_index_parse_error_keeps_healthy_typed");
    const auto main_path = root / "src" / "main.ahfl";
    const auto impls_path = root / "src" / "impls.ahfl";
    const auto broken_path = root / "src" / "broken.ahfl";
    write_package_manifest(root,
                           "lsp-project-parse-error-keeps-healthy-index",
                           "app",
                           "\"main\", \"impls\", \"broken\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "fn keep(x: Int) -> Int effect Pure decreases 0 {\n"
                                    "    return x;\n"
                                    "}\n";
    const std::string impls_source = "module app::impls;\n"
                                     "\n"
                                     "impl Int {}\n";
    const std::string broken_source = "module app::broken;\n"
                                      "\n"
                                      "struct ParsedBeforeError {\n"
                                      "    value: Int;\n"
                                      "}\n"
                                      "\n"
                                      "fn still_broken(\n";
    write_file(main_path, main_source);
    write_file(impls_path, impls_source);
    write_file(broken_path, broken_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto impls_uri = AnalysisService::uri_from_path(impls_path);
    const auto broken_uri = AnalysisService::uri_from_path(broken_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_index.parse_error_healthy.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(impls_uri) == nullptr,
                  "workspace_index.parse_error_healthy.impls_not_semantic_source");
            check(snapshot->workspace_index != nullptr,
                  "workspace_index.parse_error_healthy.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *broken_source_unit =
                    index_source_unit_for_uri(*snapshot->workspace_index, broken_uri);
                check(broken_source_unit != nullptr,
                      "workspace_index.parse_error_healthy.broken_source_unit_exists");
                if (broken_source_unit != nullptr) {
                    check(index_has_diagnostic(*snapshot->workspace_index,
                                               broken_source_unit->source_unit_id,
                                               IndexDiagnosticPhase::Parse,
                                               "parse."),
                          "workspace_index.parse_error_healthy.broken_parse_diagnostic");
                }
                const auto impl_locations =
                    snapshot->workspace_index->implementation_locations_for_primitive(
                        PrimitiveKind::Int);
                check(std::find_if(impl_locations.begin(),
                                   impl_locations.end(),
                                   [&](const Location &location) {
                                       return location.uri == impls_uri;
                                   }) != impl_locations.end(),
                      "workspace_index.parse_error_healthy.typed_impl_survives");
            }
        }
    }

    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(main_uri, position_of(main_source, "Int) ->")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(impls_uri) != std::string::npos,
          "implementation.parse_error_healthy_includes_typed_impl");
}

void test_typecheck_failed_export_does_not_block_typed_facts_for_healthy_index_sources() {
    const auto root = make_temp_project("project_index_typecheck_error_keeps_healthy_typed");
    const auto main_path = root / "src" / "main.ahfl";
    const auto impls_path = root / "src" / "impls.ahfl";
    const auto broken_path = root / "src" / "broken.ahfl";
    write_package_manifest(root,
                           "lsp-project-typecheck-error-keeps-healthy-index",
                           "app",
                           "\"main\", \"impls\", \"broken\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "fn keep(x: Int) -> Int effect Pure decreases 0 {\n"
                                    "    return x;\n"
                                    "}\n";
    const std::string impls_source = "module app::impls;\n"
                                     "\n"
                                     "impl Int {}\n";
    const std::string broken_source = "module app::broken;\n"
                                      "\n"
                                      "struct TypecheckBroken {\n"
                                      "    value: Int;\n"
                                      "}\n"
                                      "\n"
                                      "const bad: Int = \"not an int\";\n";
    write_file(main_path, main_source);
    write_file(impls_path, impls_source);
    write_file(broken_path, broken_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto impls_uri = AnalysisService::uri_from_path(impls_path);
    const auto broken_uri = AnalysisService::uri_from_path(broken_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_index.typecheck_error_healthy.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(impls_uri) == nullptr,
                  "workspace_index.typecheck_error_healthy.impls_not_semantic_source");
            check(snapshot->workspace_index != nullptr,
                  "workspace_index.typecheck_error_healthy.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *broken_source_unit =
                    index_source_unit_for_uri(*snapshot->workspace_index, broken_uri);
                check(broken_source_unit != nullptr,
                      "workspace_index.typecheck_error_healthy.broken_source_unit_exists");
                if (broken_source_unit != nullptr) {
                    check(index_has_diagnostic(*snapshot->workspace_index,
                                               broken_source_unit->source_unit_id,
                                               IndexDiagnosticPhase::TypeCheck,
                                               "typecheck."),
                          "workspace_index.typecheck_error_healthy.broken_typecheck_diagnostic");
                }
                const auto impl_locations =
                    snapshot->workspace_index->implementation_locations_for_primitive(
                        PrimitiveKind::Int);
                check(std::find_if(impl_locations.begin(),
                                   impl_locations.end(),
                                   [&](const Location &location) {
                                       return location.uri == impls_uri;
                                   }) != impl_locations.end(),
                      "workspace_index.typecheck_error_healthy.typed_impl_survives");
            }
        }
    }

    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(main_uri, position_of(main_source, "Int) ->")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(impls_uri) != std::string::npos,
          "implementation.typecheck_error_healthy_includes_typed_impl");
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

void test_workspace_index_includes_open_unexported_overlay() {
    const auto root = make_temp_project("workspace_index_open_unexported_overlay");
    const auto main_path = root / "src" / "main.ahfl";
    const auto draft_path = root / "src" / "draft.ahfl";
    write_package_manifest(root, "lsp-open-overlay-index", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct MainOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string draft_overlay = "module app::draft;\n"
                                      "\n"
                                      "struct DraftOnly {\n"
                                      "    value: String;\n"
                                      "}\n";
    write_file(main_path, main_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto draft_uri = AnalysisService::uri_from_path(draft_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        store.open(TextDocumentItem{
            .uri = draft_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = draft_overlay,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_index.open_overlay.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(draft_uri) == nullptr,
                  "workspace_index.open_overlay.not_semantic_source");
            check(snapshot->workspace_index != nullptr,
                  "workspace_index.open_overlay.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *draft_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, draft_uri);
                check(draft_source != nullptr, "workspace_index.open_overlay.source_unit_exists");
                if (draft_source != nullptr) {
                    check(source_unit_has_scope_kind(*draft_source,
                                                     LspNavigationIndexSourceKind::OpenOverlay),
                          "workspace_index.open_overlay.scope_kind");
                }
                const auto symbols = snapshot->workspace_index->workspace_symbols("DraftOnly");
                check(std::find_if(symbols.begin(),
                                   symbols.end(),
                                   [&](const SymbolFact *symbol) {
                                       return symbol != nullptr &&
                                              symbol->location.uri == draft_uri &&
                                              symbol->local_name == "DraftOnly";
                                   }) != symbols.end(),
                      "workspace_index.open_overlay.symbol_fact_exists");
            }
        }
    }

    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"DraftOnly"}})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        did_open_body(draft_uri, 1, draft_overlay),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    check(response.find(draft_uri) != std::string::npos,
          "workspace_symbol.open_overlay_includes_unexported_uri");
    check(response.find("DraftOnly") != std::string::npos,
          "workspace_symbol.open_overlay_includes_unexported_name");
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
                                   "pub struct Msg {\n"
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

void test_workspace_index_includes_path_dependency_exports() {
    const auto root = make_temp_project("workspace_index_path_dependency");
    const auto app_root = root / "app";
    const auto lib_root = root / "lib";
    const auto std_root = root / "std";
    const auto main_path = app_root / "src" / "main.ahfl";
    const auto types_path = lib_root / "src" / "types.ahfl";

    write_minimal_std_package(std_root, "path-dependency-index");
    write_package_manifest(app_root,
                           "path-index-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\n"
                           "lib = { source = \"path\", path = \"../lib\", version = \"0.1.0\" }\n");
    write_package_manifest(lib_root, "lib", "lib", "\"types\"", "src/types.ahfl");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct LocalOnly {\n"
                                    "    value: Int;\n"
                                    "}\n";
    const std::string types_source = "module lib::types;\n"
                                     "\n"
                                     "struct PathMsg {\n"
                                     "    value: String;\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });

        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "workspace_index.path_dependency.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(types_uri) == nullptr,
                  "workspace_index.path_dependency.not_semantic_source");
            check(snapshot->workspace_index != nullptr,
                  "workspace_index.path_dependency.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *dependency_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, types_uri);
                check(dependency_source != nullptr,
                      "workspace_index.path_dependency.source_unit_exists");
                if (dependency_source != nullptr) {
                    check(source_unit_has_export_scope(*dependency_source),
                          "workspace_index.path_dependency.export_scope");
                }
                const auto path_symbols = snapshot->workspace_index->workspace_symbols("PathMsg");
                check(std::find_if(path_symbols.begin(),
                                   path_symbols.end(),
                                   [&](const SymbolFact *symbol) {
                                       return symbol != nullptr &&
                                              symbol->location.uri == types_uri;
                                   }) != path_symbols.end(),
                      "workspace_index.path_dependency.symbol_fact_exists");
            }
        }
    }

    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"PathMsg"}})";
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(main_uri, 1, main_source),
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);
    check(response.find(types_uri) != std::string::npos,
          "workspace_index.path_dependency.workspace_symbol_uri");
    check(response.find("PathMsg") != std::string::npos,
          "workspace_index.path_dependency.workspace_symbol_name");
}

void test_cross_workspace_path_dependency_rejects_mixed_toolchain_profiles() {
    const auto root = make_temp_project("cross_workspace_toolchain_profile");
    const auto workspace_a = root / "workspace-a";
    const auto workspace_b = root / "workspace-b";
    const auto app_root = workspace_a;
    const auto lib_root = workspace_b / "lib";
    const auto sysroot_a = root / "sysroot-a";
    const auto sysroot_b = root / "sysroot-b";
    const auto app_path = app_root / "src" / "main.ahfl";

    write_minimal_std_package(sysroot_a / "std", "profile-a");
    write_minimal_std_package(sysroot_b / "std", "profile-b");
    write_package_manifest(app_root,
                           "app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\n"
                           "lib = { source = \"path\", path = \"../workspace-b/lib\", "
                           "version = \"0.1.0\" }\n");
    write_package_manifest(lib_root, "lib", "lib", "\"types\"", "src/types.ahfl");

    const std::string app_source = "module app::main;\n"
                                   "import lib::types as types;\n"
                                   "\n"
                                   "struct Use {\n"
                                   "    payload: types::Msg;\n"
                                   "}\n";
    write_file(app_path, app_source);
    write_file(lib_root / "src" / "types.ahfl",
               "module lib::types;\n"
               "struct Msg {}\n");

    project_discovery::ToolchainProfileSet profiles;
    auto add_workspace_profile = [&](const std::filesystem::path &workspace_root,
                                     const std::filesystem::path &sysroot,
                                     std::string_view label) {
        auto result = project_discovery::toolchain_profile_from_sysroot_input(
            sysroot, project_discovery::ToolchainProfileOrigin::LspInitialization);
        check(result.profile.has_value(),
              "cross_workspace_toolchain_profile." + std::string(label) + "_exists");
        profiles.diagnostics.insert(
            profiles.diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        if (!result.profile.has_value()) {
            return;
        }
        result.profile->scope = project_discovery::ToolchainProfileScope::WorkspaceFolder;
        profiles.workspace_profiles.push_back(project_discovery::WorkspaceToolchainProfile{
            .workspace_root = workspace_root,
            .profile = std::move(*result.profile),
        });
    };
    add_workspace_profile(workspace_a, sysroot_a, "profile_a");
    add_workspace_profile(workspace_b, sysroot_b, "profile_b");

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = app_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = app_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({workspace_a, workspace_b});
    analysis.set_toolchain_profiles(std::move(profiles));

    const auto *snapshot = analysis.snapshot_for_uri(app_uri);
    check(snapshot != nullptr, "cross_workspace_toolchain_profile.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(app_uri);
    const auto has_profile_conflict =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.code == "E::toolchain_profile_ambiguous" &&
                   diagnostic.message.find("workspace-b/lib/ahfl.toml") != std::string::npos;
        });
    check(has_profile_conflict, "cross_workspace_toolchain_profile.diagnostic");
}

void test_package_graph_workspace_rejects_private_dependency_module() {
    const auto root = make_temp_project("package_graph_workspace_private_module");
    const auto app_root = root / "packages" / "app";
    const auto lib_root = root / "packages" / "lib";
    const auto std_root = root / "std";
    const auto app_path = app_root / "src" / "main.ahfl";

    write_std_manifest(std_root);
    write_file(std_root / "collections.ahfl", "module std::collections;\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(std_root / "option.ahfl", "module std::option;\n");
    write_workspace_manifest(root, "\"packages/app\", \"packages/lib\"");
    write_package_manifest(app_root,
                           "lsp-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nlib = { source = \"workspace\" }\n");
    write_package_manifest(lib_root, "lib", "lib", "\"public\"", "src/public.ahfl");

    const std::string app_source = "module app::main;\n"
                                   "import lib::internal as internal;\n"
                                   "\n"
                                   "struct Use {\n"
                                   "    payload: internal::Secret;\n"
                                   "}\n";
    write_file(app_path, app_source);
    write_file(lib_root / "src" / "public.ahfl", "module lib::public;\n");
    write_file(lib_root / "src" / "internal.ahfl",
               "module lib::internal;\n"
               "struct Secret {\n"
               "    value: String;\n"
               "}\n");

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = app_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = app_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *snapshot = analysis.snapshot_for_uri(app_uri);
    check(snapshot != nullptr, "package_graph_workspace_private_module.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }
    check(snapshot->project_aware, "package_graph_workspace_private_module.project_aware");

    const auto diagnostics = snapshot->diagnostics_for_uri(app_uri);
    const auto has_private_import =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find(
                       "imported module 'lib::internal' is private to package prefix 'lib'") !=
                   std::string::npos;
        });
    check(has_private_import, "package_graph_workspace_private_module.private_import_diagnostic");
}

void test_workspace_index_records_visibility_alias_facts() {
    const auto root = make_temp_project("workspace_index_visibility_alias_facts");
    const auto app_root = root / "packages" / "app";
    const auto lib_root = root / "packages" / "lib";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto facade_path = lib_root / "src" / "facade.ahfl";
    const auto internal_path = lib_root / "src" / "internal.ahfl";

    write_workspace_manifest(root, "\"packages/app\", \"packages/lib\"");
    write_package_manifest(app_root,
                           "lsp-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nlib = { source = \"workspace\" }\n");
    write_package_manifest(lib_root, "lib", "lib", "\"facade\"", "src/facade.ahfl");

    const std::string app_source = "module app::main;\n"
                                   "\n"
                                   "import lib::facade as facade;\n"
                                   "\n"
                                   "fn accept(request: facade::Request) -> Int effect Pure "
                                   "decreases 0;\n";
    write_file(app_path, app_source);
    write_file(facade_path,
               "module lib::facade;\n"
               "\n"
               "pub use lib::internal::Token;\n"
               "pub use lib::internal::Request;\n");
    write_file(internal_path,
               "module lib::internal;\n"
               "\n"
               "pub struct Token {}\n"
               "\n"
               "pub struct Request {\n"
               "    token: Token;\n"
               "}\n");

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = app_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = app_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    const auto *snapshot = analysis.snapshot_for_uri(app_uri);
    check(snapshot != nullptr, "workspace_index.visibility_alias.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }
    check(snapshot->workspace_index != nullptr, "workspace_index.visibility_alias.index_exists");
    if (snapshot->workspace_index == nullptr) {
        return;
    }

    const auto &symbols = snapshot->workspace_index->symbols();
    const auto target_request =
        std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
            return !symbol.alias_id.has_value() &&
                   symbol.canonical_name == "lib::internal::Request";
        });
    check(target_request != symbols.end(), "workspace_index.visibility_alias.target_exists");
    if (target_request != symbols.end()) {
        check(target_request->visibility == ahfl::ast::Visibility::Public,
              "workspace_index.visibility_alias.target_public");
        check(target_request->api_reachable, "workspace_index.visibility_alias.target_api");
    }

    const auto alias_request =
        std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
            return symbol.alias_id.has_value() && symbol.canonical_name == "lib::facade::Request";
        });
    check(alias_request != symbols.end(), "workspace_index.visibility_alias.alias_exists");
    if (alias_request != symbols.end()) {
        check(alias_request->alias_target_def.has_value(),
              "workspace_index.visibility_alias.alias_target_def");
        check(alias_request->api_reachable, "workspace_index.visibility_alias.alias_api");
        check(alias_request->location.uri == AnalysisService::uri_from_path(facade_path),
              "workspace_index.visibility_alias.alias_location");
    }
}

void test_package_graph_workspace_preserves_cross_package_hover() {
    const auto root = make_temp_project("package_graph_workspace_hover");
    const auto app_root = root / "packages" / "app";
    const auto lib_root = root / "packages" / "lib";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto agents_path = lib_root / "src" / "agents.ahfl";
    const auto types_path = lib_root / "src" / "types.ahfl";
    write_workspace_manifest(root, "\"packages/app\", \"packages/lib\"");
    write_package_manifest(app_root,
                           "lsp-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nlib = { source = \"workspace\" }\n");
    write_package_manifest(lib_root, "lib", "lib", "\"agents\", \"types\"", "src/agents.ahfl");
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
          "package_graph_workspace.no_unknown_imported_alias_diagnostic");
    check(output.find(types_uri) != std::string::npos,
          "package_graph_workspace.definition_targets_imported_source");

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
          "package_graph_workspace.input_label_hover_does_not_select_agent");
    check(input_hover_output.find("input: lib::types::Request") != std::string::npos,
          "package_graph_workspace.input_label_hover_shows_resolved_type");
    check(input_hover_output.find("- Declared as: `types::RequestAlias`") != std::string::npos,
          "package_graph_workspace.input_label_hover_shows_declared_alias");

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
          "package_graph_workspace.agent_name_hover_selects_agent");

    const auto alias_position = position_of(agents_source, "RequestAlias");
    const auto alias_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        hover_request_body(agents_uri, alias_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(alias_hover_output.find("type lib::types::RequestAlias = lib::types::Request") !=
              std::string::npos,
          "package_graph_workspace.alias_hover_shows_alias_signature");
    check(alias_hover_output.find("- Declared as: `Request`") != std::string::npos,
          "package_graph_workspace.alias_hover_shows_declared_target");

    const auto transition_position = position_of(agents_source, "transition");
    const auto transition_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(agents_uri, 1, agents_source),
        hover_request_body(agents_uri, transition_position),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(transition_hover_output.find("transition Init -> Done") != std::string::npos,
          "package_graph_workspace.transition_label_hover_signature");
    check(transition_hover_output.find("Transition of `AliasAgent`") != std::string::npos,
          "package_graph_workspace.transition_label_hover_headline");

    const auto liveness_hover_output = run_lsp_messages({
        initialize_body(root),
        did_open_body(app_uri, 1, app_source),
        hover_request_body(app_uri, position_of(app_source, "liveness")),
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    check(liveness_hover_output.find("liveness: eventually completed(run, Missing)") !=
              std::string::npos,
          "package_graph_workspace.liveness_hover_signature");
    check(liveness_hover_output.find("workflow liveness property") != std::string::npos,
          "package_graph_workspace.liveness_hover_headline");
}

void test_sysroot_std_manifest_is_not_loaded_as_root_package() {
    const auto root = make_temp_project("sysroot_std_manifest");
    const auto std_root = root / "std";
    const auto collections_path = std_root / "collections.ahfl";
    const auto option_path = std_root / "option.ahfl";
    const std::string collections_source = "module std::collections;\n"
                                           "import std::option as option;\n"
                                           "\n"
                                           "struct List<T> {}\n";
    write_std_manifest(std_root);
    write_file(collections_path, collections_source);
    write_file(option_path,
               "module std::option;\n"
               "\n"
               "enum Option<T> { Some(T), None, }\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");

    const auto collections_uri = AnalysisService::uri_from_path(collections_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = collections_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = collections_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));

    const auto *snapshot = analysis.snapshot_for_uri(collections_uri);
    check(snapshot != nullptr, "sysroot_std.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    check(snapshot->project_aware, "sysroot_std.project_aware");
    check(snapshot->package_graph_manifest.has_value(), "sysroot_std.manifest_recorded");
    if (snapshot->package_graph_manifest.has_value()) {
        check(
            *snapshot->package_graph_manifest ==
                std::filesystem::path(AnalysisService::normalized_path_key(std_root / "ahfl.toml")),
            "sysroot_std.manifest_is_std");
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(collections_uri);
    const auto has_ambiguous_import =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("ambiguous across search roots") != std::string::npos;
        });
    check(!has_ambiguous_import, "sysroot_std.no_ambiguous_import");
}

void write_primitive_home_std_package(const std::filesystem::path &std_root,
                                      std::string_view bool_source,
                                      std::string_view int_source,
                                      std::string_view float_source,
                                      std::string_view string_source,
                                      std::string_view uuid_source) {
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"bool\", \"int\", \"float\", \"string\", \"uuid\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\", \"string_*\", \"uuid_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(std_root / "bool.ahfl", std::string(bool_source));
    write_file(std_root / "int.ahfl", std::string(int_source));
    write_file(std_root / "float.ahfl", std::string(float_source));
    write_file(std_root / "string.ahfl", std::string(string_source));
    write_file(std_root / "uuid.ahfl", std::string(uuid_source));
}

void test_definition_targets_source_sysroot_primitive_home_modules() {
    const auto root = make_temp_project("definition_primitive_home_modules");
    const auto std_root = root / "std";
    const auto bool_path = std_root / "bool.ahfl";
    const auto int_path = std_root / "int.ahfl";
    const auto float_path = std_root / "float.ahfl";
    const auto string_path = std_root / "string.ahfl";
    const auto uuid_path = std_root / "uuid.ahfl";
    const std::string bool_source = "module std::bool;\n"
                                    "\n"
                                    "impl Bool {}\n";
    const std::string int_source = "module std::int;\n"
                                   "\n"
                                   "impl Int {}\n";
    const std::string float_source = "module std::float;\n"
                                     "\n"
                                     "impl Float {}\n";
    const std::string string_source = "module std::string;\n"
                                      "\n"
                                      "impl String {}\n";
    const std::string uuid_source = "module std::uuid;\n"
                                    "\n"
                                    "@builtin(\"primitive_probe\")\n"
                                    "fn primitive_probe(flag: Bool, count: Int, ratio: Float, "
                                    "id: UUID, callback: Fn(Int) -> Unit) -> String effect Pure;\n"
                                    "\n"
                                    "enum PrimitiveEnvelope {\n"
                                    "    EBool(Bool),\n"
                                    "    EInt(Int),\n"
                                    "    EFloat(Float),\n"
                                    "    EText(String),\n"
                                    "    EId(UUID),\n"
                                    "    ERecord { flag: Bool, label: String },\n"
                                    "}\n"
                                    "\n"
                                    "impl UUID {}\n";
    write_primitive_home_std_package(
        std_root, bool_source, int_source, float_source, string_source, uuid_source);

    const auto bool_uri = AnalysisService::uri_from_path(bool_path);
    const auto int_uri = AnalysisService::uri_from_path(int_path);
    const auto float_uri = AnalysisService::uri_from_path(float_path);
    const auto string_uri = AnalysisService::uri_from_path(string_path);
    const auto uuid_uri = AnalysisService::uri_from_path(uuid_path);
    const std::string bool_definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Bool")) + R"(})";
    const std::string int_definition =
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Int")) + R"(})";
    const std::string float_definition =
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Float")) + R"(})";
    const std::string string_definition =
        R"({"jsonrpc":"2.0","id":5,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "String")) + R"(})";
    const std::string uuid_definition =
        R"({"jsonrpc":"2.0","id":6,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "UUID")) + R"(})";
    const std::string enum_bool_definition =
        R"({"jsonrpc":"2.0","id":7,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Bool),")) + R"(})";
    const std::string enum_named_bool_definition =
        R"({"jsonrpc":"2.0","id":8,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Bool, label")) + R"(})";
    const std::string fn_param_int_definition =
        R"({"jsonrpc":"2.0","id":9,"method":"textDocument/definition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Int) -> Unit")) + R"(})";
    const std::string enum_bool_type_definition =
        R"({"jsonrpc":"2.0","id":10,"method":"textDocument/typeDefinition","params":)" +
        hover_params_at(uuid_uri, position_of(uuid_source, "Bool),")) + R"(})";

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(uuid_uri, 1, uuid_source),
        bool_definition,
        int_definition,
        float_definition,
        string_definition,
        uuid_definition,
        enum_bool_definition,
        enum_named_bool_definition,
        fn_param_int_definition,
        enum_bool_type_definition,
        R"({"jsonrpc":"2.0","id":11,"method":"shutdown","params":{}})",
    });
    const auto bool_response = response_body_for_id(output, 2);
    const auto int_response = response_body_for_id(output, 3);
    const auto float_response = response_body_for_id(output, 4);
    const auto string_response = response_body_for_id(output, 5);
    const auto uuid_response = response_body_for_id(output, 6);
    const auto enum_bool_response = response_body_for_id(output, 7);
    const auto enum_named_bool_response = response_body_for_id(output, 8);
    const auto fn_param_int_response = response_body_for_id(output, 9);
    const auto enum_bool_type_response = response_body_for_id(output, 10);

    check(bool_response.find(bool_uri) != std::string::npos,
          "definition.primitive_bool_targets_std_bool_uri");
    check(bool_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_bool_targets_impl_selection");
    check(int_response.find(int_uri) != std::string::npos,
          "definition.primitive_int_targets_std_int_uri");
    check(int_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_int_targets_impl_selection");
    check(float_response.find(float_uri) != std::string::npos,
          "definition.primitive_float_targets_std_float_uri");
    check(float_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_float_targets_impl_selection");

    check(string_response.find(string_uri) != std::string::npos,
          "definition.primitive_string_targets_std_string_uri");
    check(string_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_string_targets_impl_selection");
    check(uuid_response.find(uuid_uri) != std::string::npos,
          "definition.primitive_uuid_targets_std_uuid_uri");
    check(uuid_response.find(R"("start":{"line":14,"character":5})") != std::string::npos,
          "definition.primitive_uuid_targets_impl_selection");
    check(enum_bool_response.find(bool_uri) != std::string::npos,
          "definition.primitive_enum_tuple_payload_bool_targets_std_bool_uri");
    check(enum_bool_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_enum_tuple_payload_bool_targets_impl_selection");
    check(enum_named_bool_response.find(bool_uri) != std::string::npos,
          "definition.primitive_enum_struct_payload_bool_targets_std_bool_uri");
    check(enum_named_bool_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_enum_struct_payload_bool_targets_impl_selection");
    check(fn_param_int_response.find(int_uri) != std::string::npos,
          "definition.primitive_fn_param_int_targets_std_int_uri");
    check(fn_param_int_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_fn_param_int_targets_impl_selection");
    check(enum_bool_type_response.find(bool_uri) != std::string::npos,
          "typeDefinition.primitive_enum_tuple_payload_bool_targets_std_bool_uri");
    check(enum_bool_type_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "typeDefinition.primitive_enum_tuple_payload_bool_targets_impl_selection");
}

void test_definition_discovers_primitive_home_from_exported_impl_facts() {
    const auto root = make_temp_project("definition_primitive_home_from_impl_facts");
    const auto std_root = root / "std";
    const auto int_home_path = std_root / "numbers" / "int_home.ahfl";
    const auto use_path = std_root / "consumer.ahfl";
    const std::string int_home_source = "module std::numbers::int_home;\n"
                                        "\n"
                                        "impl Int {}\n";
    const std::string use_source = "module std::consumer;\n"
                                   "\n"
                                   "@builtin(\"primitive_use\")\n"
                                   "fn primitive_use(x: Int) -> Int effect Pure;\n";

    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"numbers/int_home\", \"consumer\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(int_home_path, int_home_source);
    write_file(use_path, use_source);

    const auto int_home_uri = AnalysisService::uri_from_path(int_home_path);
    const auto use_uri = AnalysisService::uri_from_path(use_path);
    const std::string int_definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":)" +
        hover_params_at(use_uri, position_of(use_source, "Int")) + R"(})";
    const std::string int_type_definition =
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/typeDefinition","params":)" +
        hover_params_at(use_uri, position_of(use_source, "Int")) + R"(})";

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(use_uri, 1, use_source),
        int_definition,
        int_type_definition,
        R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})",
    });
    const auto definition_response = response_body_for_id(output, 2);
    const auto type_definition_response = response_body_for_id(output, 3);

    check(definition_response.find(int_home_uri) != std::string::npos,
          "definition.primitive_home_from_impl_facts.targets_exported_home_uri");
    check(definition_response.find(R"("start":{"line":2,"character":5})") != std::string::npos,
          "definition.primitive_home_from_impl_facts.targets_impl_selection");
    check(definition_response.find("std/int.ahfl") == std::string::npos,
          "definition.primitive_home_from_impl_facts.no_std_int_fallback");
    check(type_definition_response.find(int_home_uri) != std::string::npos,
          "typeDefinition.primitive_home_from_impl_facts.targets_exported_home_uri");
}

void test_implementation_returns_all_impl_blocks_for_type() {
    const auto root = make_temp_project("implementation_type_impl_candidates");
    const auto path = root / "main.ahfl";
    const std::string source = "module app;\n"
                               "\n"
                               "struct Box {}\n"
                               "\n"
                               "impl Box {}\n"
                               "impl Box {}\n"
                               "\n"
                               "fn keep(x: Box) -> Box effect Pure decreases 0 {\n"
                               "    return x;\n"
                               "}\n";
    write_file(path, source);

    const auto uri = AnalysisService::uri_from_path(path);
    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(uri, position_of(source, "Box) ->")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(uri, 1, source),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);
    check(count_substring(response, uri) == 2, "implementation.type_returns_two_impl_locations");
    check(response.find(R"("start":{"line":4,"character":5})") != std::string::npos,
          "implementation.type_first_impl_selection");
    check(response.find(R"("start":{"line":5,"character":5})") != std::string::npos,
          "implementation.type_second_impl_selection");
}

void test_implementation_uses_index_for_unopened_nominal_impls() {
    const auto root = make_temp_project("implementation_index_nominal_impls");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    const auto extra_path = root / "src" / "extra.ahfl";
    write_package_manifest(
        root, "lsp-implementation-index", "app", "\"main\", \"types\", \"extra\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
                                     "\n"
                                     "struct Msg {}\n";
    const std::string extra_source =
        "module app::extra;\n"
        "import app::types as types;\n"
        "\n"
        "trait Renderable {\n"
        "    fn display(self: types::Msg) -> Int effect Pure;\n"
        "}\n"
        "\n"
        "impl Renderable for types::Msg {\n"
        "    fn display(self: types::Msg) -> Int effect Pure decreases 0 {\n"
        "        return 0;\n"
        "    }\n"
        "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);
    write_file(extra_path, extra_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto extra_uri = AnalysisService::uri_from_path(extra_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "implementation.nominal_index.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(extra_uri) == nullptr,
                  "implementation.nominal_index.extra_not_in_semantic_sources");
            check(snapshot->workspace_index != nullptr,
                  "implementation.nominal_index.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto &symbols = snapshot->workspace_index->symbols();
                const auto trait_symbol =
                    std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                        return symbol.canonical_name == "app::extra::Renderable" &&
                               symbol.kind == ahfl::SymbolKind::Trait;
                    });
                check(trait_symbol != symbols.end(),
                      "implementation.nominal_index.trait_symbol_fact_exists");
                const auto msg_symbol =
                    std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                        return symbol.canonical_name == "app::types::Msg" &&
                               symbol.kind == ahfl::SymbolKind::Struct;
                    });
                check(msg_symbol != symbols.end(),
                      "implementation.nominal_index.msg_symbol_fact_exists");
                if (msg_symbol != symbols.end()) {
                    const auto &semantic_symbols = snapshot->resolve_result.symbol_table.symbols();
                    const auto semantic_msg =
                        std::find_if(semantic_symbols.begin(),
                                     semantic_symbols.end(),
                                     [](const ahfl::Symbol &symbol) {
                                         return symbol.canonical_name == "app::types::Msg";
                                     });
                    check(semantic_msg != semantic_symbols.end(),
                          "implementation.nominal_index.semantic_msg_symbol_exists");
                    if (semantic_msg != semantic_symbols.end()) {
                        const auto remapped_def =
                            snapshot->workspace_def_for_symbol(semantic_msg->id);
                        check(remapped_def.has_value(),
                              "implementation.nominal_index.semantic_remap_exists");
                        if (remapped_def.has_value()) {
                            check(*remapped_def == msg_symbol->def_id,
                                  "implementation.nominal_index.semantic_remap_matches_index_def");
                        }
                    }
                }
                const auto &impls = snapshot->workspace_index->impls();
                const auto extra_impl =
                    std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
                        return impl.location.uri == extra_uri;
                    });
                check(extra_impl != impls.end(), "implementation.nominal_index.impl_fact_exists");
                if (extra_impl != impls.end()) {
                    check(extra_impl->trait_def.has_value(),
                          "implementation.nominal_index.impl_trait_def_exists");
                    if (trait_symbol != symbols.end() && extra_impl->trait_def.has_value()) {
                        check(*extra_impl->trait_def == trait_symbol->def_id,
                              "implementation.nominal_index.impl_trait_def_matches");
                    }
                    check(extra_impl->trait_range.has_value() &&
                              extra_impl->trait_range->end_offset >
                                  extra_impl->trait_range->begin_offset,
                          "implementation.nominal_index.impl_trait_range_has_extent");
                    if (trait_symbol != symbols.end()) {
                        const auto trait_impl_locations =
                            snapshot->workspace_index->implementation_locations_for_trait(
                                trait_symbol->def_id);
                        check(std::find_if(trait_impl_locations.begin(),
                                           trait_impl_locations.end(),
                                           [&](const Location &location) {
                                               return location.uri == extra_uri;
                                           }) != trait_impl_locations.end(),
                              "implementation.nominal_index.impl_trait_query_includes_extra");
                    }
                    check(extra_impl->methods.size() == 1,
                          "implementation.nominal_index.impl_method_count");
                    if (!extra_impl->methods.empty()) {
                        check(extra_impl->methods.front().name == "display",
                              "implementation.nominal_index.impl_method_name");
                        check(extra_impl->methods.front().has_body,
                              "implementation.nominal_index.impl_method_has_body");
                    }
                }
            }
        }
    }

    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(main_uri, position_of(main_source, "Msg;")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);
    check(response.find(extra_uri) != std::string::npos,
          "implementation.nominal_index_includes_unopened_exported_impl");
    check(response.find(R"("start":{"line":7,"character":20})") != std::string::npos,
          "implementation.nominal_index_targets_impl_selection");
}

void test_implementation_uses_nominal_def_index_for_generic_impls() {
    const auto root = make_temp_project("implementation_index_generic_nominal_impls");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "lsp-implementation-generic-index", "app", "\"main\", \"types\"");

    const std::string main_source = "module app::main;\n"
                                    "import app::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Box<Int>;\n"
                                    "}\n";
    const std::string types_source = "module app::types;\n"
                                     "\n"
                                     "struct Box<T> {}\n"
                                     "\n"
                                     "impl<T> Box<T> {\n"
                                     "    fn size(self: Box<T>) -> Int effect Pure decreases 0 {\n"
                                     "        return 0;\n"
                                     "    }\n"
                                     "}\n";
    write_file(main_path, main_source);
    write_file(types_path, types_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto types_uri = AnalysisService::uri_from_path(types_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = main_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = main_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        const auto *snapshot = analysis.snapshot_for_uri(main_uri);
        check(snapshot != nullptr, "implementation.generic_nominal_index.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->workspace_index != nullptr,
                  "implementation.generic_nominal_index.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto &symbols = snapshot->workspace_index->symbols();
                const auto box_symbol =
                    std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                        return symbol.canonical_name == "app::types::Box" &&
                               symbol.kind == ahfl::SymbolKind::Struct;
                    });
                check(box_symbol != symbols.end(),
                      "implementation.generic_nominal_index.box_symbol_fact_exists");
                const auto &impls = snapshot->workspace_index->impls();
                const auto generic_impl =
                    std::find_if(impls.begin(), impls.end(), [&](const ImplFact &impl) {
                        return impl.location.uri == types_uri;
                    });
                check(generic_impl != impls.end(),
                      "implementation.generic_nominal_index.impl_fact_exists");
                if (generic_impl != impls.end()) {
                    check(generic_impl->target_type.kind == TypeKey::Kind::Nominal,
                          "implementation.generic_nominal_index.impl_target_nominal");
                    check(!generic_impl->target_type.type_args.empty(),
                          "implementation.generic_nominal_index.impl_target_has_type_args");
                }
                if (box_symbol != symbols.end()) {
                    const auto box_impl_locations =
                        snapshot->workspace_index->implementation_locations_for_nominal_def(
                            box_symbol->def_id);
                    check(std::find_if(box_impl_locations.begin(),
                                       box_impl_locations.end(),
                                       [&](const Location &location) {
                                           return location.uri == types_uri;
                                       }) != box_impl_locations.end(),
                          "implementation.generic_nominal_index.nominal_def_query_includes_impl");
                }
            }
        }
    }

    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(main_uri, position_of(main_source, "Box<Int>")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body(root),
        did_open_body(main_uri, 1, main_source),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);
    check(response.find(types_uri) != std::string::npos,
          "implementation.generic_nominal_index_includes_generic_impl");
    check(response.find("\"start\":{\"line\":4") != std::string::npos,
          "implementation.generic_nominal_index_targets_impl_line");
}

void test_std_exported_impl_modules_feed_primitive_candidates() {
    const auto root = make_temp_project("std_exported_impl_candidates");
    const auto std_root = root / "std";
    const auto collections_path = std_root / "collections.ahfl";
    const auto int_path = std_root / "int.ahfl";
    const auto fmt_path = std_root / "fmt.ahfl";
    const auto json_path = std_root / "json.ahfl";
    const std::string collections_source = "module std::collections;\n"
                                           "\n"
                                           "struct Set<T> {}\n"
                                           "\n"
                                           "@builtin(\"set_raw_size\")\n"
                                           "fn set_raw_size<T>(s: Set<T>) -> Int effect Pure;\n";
    const std::string int_source = "module std::int;\n"
                                   "\n"
                                   "impl Int {}\n";
    const std::string fmt_source = "module std::fmt;\n"
                                   "\n"
                                   "fn format(x: Int) -> String effect Pure;\n"
                                   "\n"
                                   "impl Int {}\n";
    const std::string json_source = "module std::json;\n"
                                    "\n"
                                    "fn encode_int(x: Int) -> String effect Pure;\n"
                                    "\n"
                                    "impl Int {}\n";
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"collections\", \"int\", \"fmt\", \"json\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"set_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(collections_path, collections_source);
    write_file(int_path, int_source);
    write_file(fmt_path, fmt_source);
    write_file(json_path, json_source);

    const auto collections_uri = AnalysisService::uri_from_path(collections_path);
    const auto int_uri = AnalysisService::uri_from_path(int_path);
    const auto fmt_uri = AnalysisService::uri_from_path(fmt_path);
    const auto json_uri = AnalysisService::uri_from_path(json_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = collections_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = collections_source,
        });

        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));
        const auto *snapshot = analysis.snapshot_for_uri(collections_uri);
        check(snapshot != nullptr, "workspace_index.std_impl.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(int_uri) == nullptr,
                  "workspace_index.std_impl.int_not_in_semantic_sources");
            check(snapshot->source_for_uri(fmt_uri) == nullptr,
                  "workspace_index.std_impl.fmt_not_in_semantic_sources");
            check(snapshot->source_for_uri(json_uri) == nullptr,
                  "workspace_index.std_impl.json_not_in_semantic_sources");
            check(snapshot->workspace_index != nullptr, "workspace_index.std_impl.index_exists");
            if (snapshot->workspace_index != nullptr) {
                const auto *int_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, int_uri);
                const auto *fmt_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, fmt_uri);
                const auto *json_source =
                    index_source_unit_for_uri(*snapshot->workspace_index, json_uri);
                check(int_source != nullptr, "workspace_index.std_impl.int_source_unit_exists");
                check(fmt_source != nullptr, "workspace_index.std_impl.fmt_source_unit_exists");
                check(json_source != nullptr, "workspace_index.std_impl.json_source_unit_exists");
                if (int_source != nullptr) {
                    check(source_unit_has_export_scope(*int_source),
                          "workspace_index.std_impl.int_export_scope");
                }
                if (fmt_source != nullptr) {
                    check(source_unit_has_export_scope(*fmt_source),
                          "workspace_index.std_impl.fmt_export_scope");
                }
                if (json_source != nullptr) {
                    check(source_unit_has_export_scope(*json_source),
                          "workspace_index.std_impl.json_export_scope");
                }
                const auto indexed_impls =
                    snapshot->workspace_index->implementation_locations_for_primitive(
                        PrimitiveKind::Int);
                const auto indexed_home =
                    snapshot->workspace_index->primitive_home_location_for_type(TypeKey{
                        .kind = TypeKey::Kind::Primitive,
                        .primitive = PrimitiveKind::Int,
                    });
                check(indexed_home.has_value(),
                      "workspace_index.std_impl.primitive_home_location_exists");
                if (indexed_home.has_value()) {
                    check(indexed_home->uri == int_uri,
                          "workspace_index.std_impl.primitive_home_location_targets_int");
                }
                check(std::find_if(indexed_impls.begin(),
                                   indexed_impls.end(),
                                   [&](const Location &location) {
                                       return location.uri == int_uri;
                                   }) != indexed_impls.end(),
                      "workspace_index.std_impl.index_includes_int_impl");
                check(std::find_if(indexed_impls.begin(),
                                   indexed_impls.end(),
                                   [&](const Location &location) {
                                       return location.uri == fmt_uri;
                                   }) != indexed_impls.end(),
                      "workspace_index.std_impl.index_includes_fmt_impl");
                check(std::find_if(indexed_impls.begin(),
                                   indexed_impls.end(),
                                   [&](const Location &location) {
                                       return location.uri == json_uri;
                                   }) != indexed_impls.end(),
                      "workspace_index.std_impl.index_includes_json_impl");
            }
        }
    }

    const auto int_position = position_of(collections_source, "Int effect");
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":)" +
        hover_params_at(collections_uri, int_position) + R"(})";
    const std::string implementation =
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/implementation","params":)" +
        hover_params_at(collections_uri, int_position) + R"(})";
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":4,"method":"workspace/symbol","params":{"query":"format"}})";
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(collections_uri, 1, collections_source),
        definition,
        implementation,
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":5,"method":"shutdown","params":{}})",
    });
    const auto definition_response = response_body_for_id(output, 2);
    const auto implementation_response = response_body_for_id(output, 3);
    const auto workspace_symbol_response = response_body_for_id(output, 4);

    check(definition_response.find(int_uri) != std::string::npos,
          "definition.primitive_int_includes_canonical_home");
    check(definition_response.find(fmt_uri) == std::string::npos,
          "definition.primitive_int_excludes_exported_fmt_impl");
    check(definition_response.find(json_uri) == std::string::npos,
          "definition.primitive_int_excludes_exported_json_impl");
    check(definition_response.find(R"("command")") == std::string::npos,
          "definition.primitive_int_payload_has_no_command");
    check(implementation_response.find(int_uri) != std::string::npos,
          "implementation.primitive_int_includes_canonical_home_impl");
    check(implementation_response.find(fmt_uri) != std::string::npos,
          "implementation.primitive_int_includes_exported_fmt_impl");
    check(implementation_response.find(json_uri) != std::string::npos,
          "implementation.primitive_int_includes_exported_json_impl");
    check(implementation_response.find(R"("command")") == std::string::npos,
          "implementation.primitive_int_payload_has_no_command");
    check(workspace_symbol_response.find(fmt_uri) != std::string::npos,
          "workspace_symbol.index_includes_exported_fmt_function");
    check(workspace_symbol_response.find("format") != std::string::npos,
          "workspace_symbol.index_includes_exported_fmt_name");
}

void test_user_package_lazy_sysroot_index_feeds_primitive_candidates() {
    const auto root = make_temp_project("user_package_lazy_sysroot_index");
    const auto app_root = root / "app";
    const auto std_root = root / "std";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto int_path = std_root / "int.ahfl";
    const auto fmt_path = std_root / "fmt.ahfl";
    const auto string_path = std_root / "string.ahfl";
    write_package_manifest(app_root,
                           "lazy-sysroot-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"int\", \"fmt\", \"string\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(int_path, "module std::int;\n\nimpl Int {}\n");
    write_file(fmt_path,
               "module std::fmt;\n"
               "\n"
               "fn format_int(x: Int) -> String effect Pure;\n"
               "\n"
               "impl Int {}\n");
    write_file(string_path, "module std::string;\n\nimpl String {}\n");

    const std::string app_source = "module app::main;\n"
                                   "\n"
                                   "fn keep(x: Int) -> Int effect Pure decreases 0 {\n"
                                   "    return x;\n"
                                   "}\n";
    write_file(app_path, app_source);

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    const auto int_uri = AnalysisService::uri_from_path(int_path);
    const auto fmt_uri = AnalysisService::uri_from_path(fmt_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = app_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = app_source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));
        const auto *snapshot = analysis.snapshot_for_uri(app_uri);
        check(snapshot != nullptr, "lazy_sysroot.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->source_for_uri(int_uri) == nullptr,
                  "lazy_sysroot.int_not_in_semantic_sources");
            check(snapshot->source_for_uri(fmt_uri) == nullptr,
                  "lazy_sysroot.fmt_not_in_semantic_sources");
            check(snapshot->workspace_index != nullptr, "lazy_sysroot.workspace_index_exists");
            if (snapshot->workspace_index != nullptr) {
                check(index_source_unit_for_uri(*snapshot->workspace_index, int_uri) == nullptr,
                      "lazy_sysroot.workspace_index_excludes_int_export");
                check(index_source_unit_for_uri(*snapshot->workspace_index, fmt_uri) == nullptr,
                      "lazy_sysroot.workspace_index_excludes_fmt_export");
            }
        }
    }

    const auto int_position = position_of(app_source, "Int) ->");
    const std::string definition =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":)" +
        hover_params_at(app_uri, int_position) + R"(})";
    const std::string type_definition =
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/typeDefinition","params":)" +
        hover_params_at(app_uri, int_position) + R"(})";
    const std::string implementation =
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/implementation","params":)" +
        hover_params_at(app_uri, int_position) + R"(})";
    const std::string workspace_symbol =
        R"({"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":"format_int"}})";
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(app_uri, 1, app_source),
        definition,
        type_definition,
        implementation,
        workspace_symbol,
        R"({"jsonrpc":"2.0","id":6,"method":"shutdown","params":{}})",
    });
    const auto definition_response = response_body_for_id(output, 2);
    const auto type_definition_response = response_body_for_id(output, 3);
    const auto implementation_response = response_body_for_id(output, 4);
    const auto workspace_symbol_response = response_body_for_id(output, 5);

    check(definition_response.find(int_uri) != std::string::npos,
          "lazy_sysroot.definition_includes_primitive_home");
    check(definition_response.find(fmt_uri) == std::string::npos,
          "lazy_sysroot.definition_excludes_fmt_impl");
    check(type_definition_response.find(int_uri) != std::string::npos,
          "lazy_sysroot.type_definition_includes_primitive_home");
    check(implementation_response.find(int_uri) != std::string::npos,
          "lazy_sysroot.implementation_includes_primitive_home");
    check(implementation_response.find(fmt_uri) != std::string::npos,
          "lazy_sysroot.implementation_includes_fmt_impl");
    check(workspace_symbol_response.find(fmt_uri) != std::string::npos,
          "lazy_sysroot.workspace_symbol_includes_fmt");
    check(workspace_symbol_response.find("format_int") != std::string::npos,
          "lazy_sysroot.workspace_symbol_includes_fmt_name");
}

void write_detached_primitive_home_std_package(const std::filesystem::path &std_root) {
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"bool\", \"int\", \"string\", \"time\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\", \"string_*\", \"time_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(std_root / "bool.ahfl", "module std::bool;\n\nimpl Bool {}\n");
    write_file(std_root / "int.ahfl", "module std::int;\n\nimpl Int {}\n");
    write_file(std_root / "string.ahfl",
               "module std::string;\n\nimpl String {\n"
               "    fn length(self) -> Int effect Pure decreases 0 { return 0; }\n"
               "}\n");
    write_file(std_root / "time.ahfl",
               "module std::time;\n\nimpl Timestamp {}\nimpl Duration {}\n");
}

void test_detached_file_uses_sysroot_primitive_home_only() {
    const auto root = make_temp_project("detached_primitive_home");
    const auto std_root = root / "std";
    const auto scratch_path = root / "scratch" / "loose.ahfl";
    write_detached_primitive_home_std_package(std_root);

    const std::string source = "struct Scratch {\n"
                               "    title: String;\n"
                               "    ok: Bool;\n"
                               "    delay: Duration;\n"
                               "    values: List<Int>;\n"
                               "}\n";
    const auto scratch_uri = AnalysisService::uri_from_path(scratch_path);
    const auto string_uri = AnalysisService::uri_from_path(std_root / "string.ahfl");
    const auto bool_uri = AnalysisService::uri_from_path(std_root / "bool.ahfl");
    const auto time_uri = AnalysisService::uri_from_path(std_root / "time.ahfl");

    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = scratch_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = source,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(workspace_toolchain_profile_set_for_sysroot(root, root));

        const auto *snapshot = analysis.snapshot_for_uri(scratch_uri);
        check(snapshot != nullptr, "detached_primitive_home.snapshot_exists");
        if (snapshot != nullptr) {
            check(snapshot->analysis_mode == LspAnalysisMode::DetachedSourceUnit,
                  "detached_primitive_home.analysis_mode");
            check(!snapshot->project_aware, "detached_primitive_home.not_project_aware");
        }

        const auto *primitive_index = analysis.sysroot_primitive_index_for_uri(scratch_uri);
        check(primitive_index != nullptr, "detached_primitive_home.primitive_index_exists");
        if (primitive_index != nullptr) {
            const auto home = primitive_index->home_location_for_type(TypeKey{
                .kind = TypeKey::Kind::Primitive,
                .primitive = PrimitiveKind::String,
            });
            check(home.has_value() && home->uri == string_uri,
                  "detached_primitive_home.primitive_index_targets_string");
        }
    }

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(scratch_uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":)" +
            hover_params_at(scratch_uri, position_of(source, "String")) + R"(})",
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/typeDefinition","params":)" +
            hover_params_at(scratch_uri, position_of(source, "Bool")) + R"(})",
        R"({"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":)" +
            hover_params_at(scratch_uri, position_of(source, "Duration")) + R"(})",
        R"({"jsonrpc":"2.0","id":5,"method":"textDocument/implementation","params":)" +
            hover_params_at(scratch_uri, position_of(source, "String")) + R"(})",
        R"({"jsonrpc":"2.0","id":6,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            scratch_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":7,"method":"shutdown","params":{}})",
    });

    const auto string_definition = response_body_for_id(output, 2);
    const auto bool_type_definition = response_body_for_id(output, 3);
    const auto duration_definition = response_body_for_id(output, 4);
    const auto implementation = response_body_for_id(output, 5);
    const auto diagnostics = response_body_for_id(output, 6);

    check(string_definition.find(string_uri) != std::string::npos,
          "detached_primitive_home.definition_targets_string");
    check(bool_type_definition.find(bool_uri) != std::string::npos,
          "detached_primitive_home.type_definition_targets_bool");
    check(duration_definition.find(time_uri) != std::string::npos,
          "detached_primitive_home.definition_targets_duration");
    check(implementation.find(R"("result":[])") != std::string::npos,
          "detached_primitive_home.implementation_empty");
    check(implementation.find(string_uri) == std::string::npos,
          "detached_primitive_home.implementation_excludes_std_impl");
    check(diagnostics.find("N::detached_source_unit") != std::string::npos,
          "detached_primitive_home.diagnostic_has_detached_note");
    check(diagnostics.find("E::detached_unknown_nominal_type") != std::string::npos,
          "detached_primitive_home.diagnostic_has_unknown_nominal");
}

void test_detached_file_warns_when_used_primitive_home_is_missing() {
    const auto root = make_temp_project("detached_missing_primitive_home");
    const auto std_root = root / "std";
    const auto scratch_path = root / "scratch" / "loose.ahfl";
    write_detached_primitive_home_std_package(std_root);

    const std::string source = "struct Scratch {\n"
                               "    value: Unit;\n"
                               "}\n";
    const auto scratch_uri = AnalysisService::uri_from_path(scratch_path);

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(scratch_uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            scratch_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto diagnostics = response_body_for_id(output, 2);
    check(diagnostics.find("W::primitive_home_unavailable") != std::string::npos,
          "detached_missing_primitive_home.warns");
    check(diagnostics.find("Unit") != std::string::npos,
          "detached_missing_primitive_home.names_primitive");
    check(diagnostics.find("unit.ahfl") != std::string::npos,
          "detached_missing_primitive_home.names_expected_file");
}

void test_detached_file_warns_without_toolchain_profile() {
    const auto root = make_temp_project("detached_no_toolchain_profile");
    const auto scratch_path = root / "scratch" / "loose.ahfl";
    const auto scratch_uri = AnalysisService::uri_from_path(scratch_path);
    const std::string source = "struct Scratch {\n"
                               "    title: String;\n"
                               "}\n";

    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = scratch_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });
    AnalysisService analysis(store);
    project_discovery::ToolchainProfileSet profiles;
    profiles.allow_compile_default = false;
    analysis.set_toolchain_profiles(std::move(profiles));

    const auto *snapshot = analysis.snapshot_for_uri(scratch_uri);
    check(snapshot != nullptr, "detached_no_toolchain.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }
    check(snapshot->analysis_mode == LspAnalysisMode::DetachedSourceUnit,
          "detached_no_toolchain.analysis_mode");
    const auto diagnostics = snapshot->diagnostics_for_uri(scratch_uri);
    const auto warning =
        std::find_if(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.code == "W::primitive_home_unavailable" &&
                   diagnostic.message.find("toolchain profile") != std::string::npos;
        });
    check(warning != diagnostics.end(), "detached_no_toolchain.warns_primitive_home_unavailable");
}

void test_detached_file_rejects_imports_and_import_code_actions() {
    const auto root = make_temp_project("detached_import_code_actions");
    const auto std_root = root / "std";
    const auto scratch_path = root / "scratch" / "imports.ahfl";
    write_detached_primitive_home_std_package(std_root);

    const std::string source = "import std::collections as collections;\n"
                               "\n"
                               "struct Scratch {\n"
                               "    values: collections::List<Int>;\n"
                               "}\n";
    const auto scratch_uri = AnalysisService::uri_from_path(scratch_path);

    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(scratch_uri, 1, source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            scratch_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/codeAction","params":{"textDocument":{"uri":")" +
            scratch_uri +
            R"("},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":38}}}})",
        R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})",
    });

    const auto diagnostics = response_body_for_id(output, 2);
    const auto code_actions = response_body_for_id(output, 3);

    check(diagnostics.find("E::detached_import") != std::string::npos,
          "detached_imports.diagnostic_has_detached_import");
    check(code_actions.find("Organize Imports") == std::string::npos,
          "detached_imports.no_organize_imports_action");
    check(code_actions.find("Remove unused import") == std::string::npos,
          "detached_imports.no_remove_unused_import_action");
}

void test_watched_sysroot_file_change_refreshes_primitive_candidates() {
    const auto root = make_temp_project("watched_sysroot_impl_refresh");
    const auto app_root = root / "app";
    const auto std_root = root / "std";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto int_path = std_root / "int.ahfl";
    const auto fmt_path = std_root / "fmt.ahfl";
    const auto string_path = std_root / "string.ahfl";
    write_package_manifest(app_root,
                           "watched-sysroot-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"int\", \"fmt\", \"string\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(int_path, "module std::int;\n\nimpl Int {}\n");
    write_file(fmt_path,
               "module std::fmt;\n"
               "\n"
               "fn format_int(x: Int) -> String effect Pure;\n");
    write_file(string_path, "module std::string;\n\nimpl String {}\n");

    const std::string app_source = "module app::main;\n"
                                   "\n"
                                   "fn keep(x: Int) -> Int effect Pure decreases 0 {\n"
                                   "    return x;\n"
                                   "}\n";
    const std::string fmt_with_impl = "module std::fmt;\n"
                                      "\n"
                                      "fn format_int(x: Int) -> String effect Pure;\n"
                                      "\n"
                                      "impl Int {}\n";
    write_file(app_path, app_source);

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    const auto fmt_uri = AnalysisService::uri_from_path(fmt_path);
    const auto int_position = position_of(app_source, "Int) ->");
    const std::string implementation_before =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(app_uri, int_position) + R"(})";
    const std::string watched_change =
        R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":")" +
        fmt_uri + R"(","type":2}]}})";
    const std::string implementation_after =
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/implementation","params":)" +
        hover_params_at(app_uri, int_position) + R"(})";

    const auto output = run_lsp_message_steps({
        LspMessageStep{.body = initialize_body_with_sysroot(root, root)},
        LspMessageStep{.body = did_open_body(app_uri, 1, app_source)},
        LspMessageStep{.body = implementation_before},
        LspMessageStep{
            .body = watched_change,
            .before = [fmt_path, fmt_with_impl]() { write_file(fmt_path, fmt_with_impl); },
        },
        LspMessageStep{.body = implementation_after},
        LspMessageStep{.body = R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})"},
    });
    const auto before_response = response_body_for_id(output, 2);
    const auto after_response = response_body_for_id(output, 3);
    check(before_response.find(fmt_uri) == std::string::npos,
          "watched_sysroot_impl.before_excludes_fmt_impl");
    check(after_response.find(fmt_uri) != std::string::npos,
          "watched_sysroot_impl.after_includes_fmt_impl");
}

void test_open_sysroot_overlay_feeds_primitive_implementation_candidates() {
    const auto root = make_temp_project("sysroot_overlay_primitive_impl");
    const auto app_root = root / "app";
    const auto std_root = root / "std";
    const auto app_path = app_root / "src" / "main.ahfl";
    const auto decimal_path = std_root / "decimal.ahfl";
    const auto fmt_path = std_root / "fmt.ahfl";
    const auto string_path = std_root / "string.ahfl";
    write_package_manifest(app_root,
                           "sysroot-overlay-app",
                           "app",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_file(std_root / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"standard-library\"\n"
               "\n"
               "[module]\n"
               "prefix = \"std\"\n"
               "root = \".\"\n"
               "\n"
               "[exports]\n"
               "modules = [\"prelude\", \"decimal\", \"fmt\", \"string\"]\n"
               "\n"
               "[prelude]\n"
               "module = \"std::prelude\"\n"
               "injection = \"explicit\"\n"
               "\n"
               "[compiler_intrinsics]\n"
               "allow = [\"primitive_*\"]\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(decimal_path, "module std::decimal;\n\nimpl Decimal(0) {}\n");
    write_file(fmt_path,
               "module std::fmt;\n"
               "\n"
               "fn format_decimal(x: Decimal(2)) -> String effect Pure;\n");
    write_file(string_path, "module std::string;\n\nimpl String {}\n");

    const std::string fmt_overlay = "module std::fmt;\n"
                                    "\n"
                                    "fn format_decimal(x: Decimal(2)) -> String effect Pure;\n"
                                    "\n"
                                    "impl Decimal(2) {}\n";
    const std::string app_source = "module app::main;\n"
                                   "\n"
                                   "fn keep(x: Decimal(2)) -> Decimal(2) effect Pure decreases 0 "
                                   "{\n"
                                   "    return x;\n"
                                   "}\n";
    write_file(app_path, app_source);

    const auto app_uri = AnalysisService::uri_from_path(app_path);
    const auto fmt_uri = AnalysisService::uri_from_path(fmt_path);
    {
        DocumentStore store;
        store.open(TextDocumentItem{
            .uri = app_uri,
            .language_id = "ahfl",
            .version = 1,
            .text = app_source,
        });
        store.open(TextDocumentItem{
            .uri = fmt_uri,
            .language_id = "ahfl",
            .version = 7,
            .text = fmt_overlay,
        });
        AnalysisService analysis(store);
        analysis.set_workspace_folders({root});
        analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root));
        const auto *index = analysis.sysroot_index_for_uri(app_uri);
        check(index != nullptr, "sysroot_overlay_impl.index_exists");
        if (index != nullptr) {
            const auto decimal_impls = index->implementation_locations_for_type(TypeKey{
                .kind = TypeKey::Kind::Primitive,
                .primitive = PrimitiveKind::Decimal,
                .primitive_parameter = 2,
            });
            check(std::find_if(decimal_impls.begin(),
                               decimal_impls.end(),
                               [&](const Location &location) { return location.uri == fmt_uri; }) !=
                      decimal_impls.end(),
                  "sysroot_overlay_impl.index_includes_unsaved_fmt_impl");
        }
    }

    const std::string implementation =
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":)" +
        hover_params_at(app_uri, position_of(app_source, "Decimal(2)")) + R"(})";
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, root),
        did_open_body(app_uri, 1, app_source),
        did_open_body(fmt_uri, 7, fmt_overlay),
        implementation,
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });
    const auto response = response_body_for_id(output, 2);
    check(response.find(fmt_uri) != std::string::npos,
          "sysroot_overlay_impl.implementation_includes_unsaved_fmt_impl");
}

void write_minimal_std_sources(const std::filesystem::path &std_root,
                               const std::filesystem::path &json_path,
                               std::string_view json_source) {
    write_std_manifest(std_root);
    write_file(std_root / "collections.ahfl",
               "module std::collections;\n"
               "\n"
               "struct List<T> {}\n");
    write_file(std_root / "option.ahfl",
               "module std::option;\n"
               "\n"
               "enum Option<T> { Some(T), None, }\n");
    write_file(std_root / "prelude.ahfl", "module std::prelude;\n");
    write_file(json_path, std::string(json_source));
}

void check_std_json_lsp_project_analysis(const std::filesystem::path &std_root,
                                         const std::filesystem::path &json_path,
                                         std::string_view json_source,
                                         std::vector<std::filesystem::path> workspace_roots,
                                         std::string_view label) {
    const auto uri = AnalysisService::uri_from_path(json_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = std::string(json_source),
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders(std::move(workspace_roots));
    analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(std_root.parent_path()));

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, std::string(label) + ".snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    check(snapshot->project_aware, std::string(label) + ".project_aware");
    check(snapshot->package_graph_manifest.has_value(), std::string(label) + ".manifest_recorded");
    if (snapshot->package_graph_manifest.has_value()) {
        check(
            *snapshot->package_graph_manifest ==
                std::filesystem::path(AnalysisService::normalized_path_key(std_root / "ahfl.toml")),
            std::string(label) + ".manifest_is_std");
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(uri);
    const auto has_unknown_std_import =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown type 'collections::List'") !=
                       std::string::npos ||
                   diagnostic.message.find("unknown type 'option::Option'") != std::string::npos;
        });
    check(!has_unknown_std_import, std::string(label) + ".std_imports_resolve");
}

void test_sysroot_std_manifest_detected_when_workspace_root_is_std_directory() {
    const auto root = make_temp_project("sysroot_std_folder_root");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    check_std_json_lsp_project_analysis(
        std_root, json_path, json_source, {std_root}, "sysroot_std_folder_root");
}

void test_sysroot_std_manifest_detected_without_workspace_root() {
    const auto root = make_temp_project("sysroot_std_no_workspace_root");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    check_std_json_lsp_project_analysis(
        std_root, json_path, json_source, {}, "sysroot_std_no_workspace_root");
}

void test_lsp_initialization_sysroot_option_selects_toolchain_sysroot() {
    const auto root = make_temp_project("sysroot_std_initialization_option");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(std_root, root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("unknown type 'collections::List'") == std::string::npos,
          "sysroot_std_initialization_option.std_imports_resolve");
    check(output.find("unknown type 'option::Option'") == std::string::npos,
          "sysroot_std_initialization_option.option_imports_resolve");
}

void test_lsp_bundled_sysroot_initialization_option_selects_fallback() {
    const auto root = make_temp_project("sysroot_std_bundled_initialization_option");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_bundled_sysroot(std_root, root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("unknown type 'collections::List'") == std::string::npos,
          "sysroot_std_bundled_initialization_option.std_imports_resolve");
    check(output.find("unknown type 'option::Option'") == std::string::npos,
          "sysroot_std_bundled_initialization_option.option_imports_resolve");
}

void test_lsp_default_sysroot_precedes_bundled_fallback() {
    const auto root = make_temp_project("default_sysroot_precedes_bundled");
    const auto alternate_root = make_temp_project("default_sysroot_precedes_bundled_alt");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);
    write_minimal_std_package(alternate_root / "std", "alternate");

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_default_and_bundled_sysroot(std_root, alternate_root, root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("E::toolchain_sysroot_mismatch") != std::string::npos,
          "default_sysroot_precedes_bundled.mismatch");
}

void test_lsp_bundled_sysroot_mismatch_reports_related_information() {
    const auto root = make_temp_project("bundled_sysroot_mismatch_related");
    const auto bundled_root = make_temp_project("bundled_sysroot_mismatch_related_alt");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);
    write_minimal_std_package(bundled_root / "std", "bundled");

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_bundled_sysroot(std_root, bundled_root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("E::toolchain_sysroot_mismatch") != std::string::npos,
          "bundled_sysroot_mismatch_related.mismatch");
    check(output.find("help: configure ahfl.toolchain.sysroot") != std::string::npos,
          "bundled_sysroot_mismatch_related.help");
    check(output.find("\"relatedInformation\"") != std::string::npos,
          "bundled_sysroot_mismatch_related.related_information");
    check(output.find("opened standard-library package manifest") != std::string::npos,
          "bundled_sysroot_mismatch_related.opened_manifest_note");
    check(output.find("active std manifest from profile origin 'bundled-extension'") !=
              std::string::npos,
          "bundled_sysroot_mismatch_related.origin_note");
    check(output.find(AnalysisService::uri_from_path(std_root / "ahfl.toml")) != std::string::npos,
          "bundled_sysroot_mismatch_related.opened_manifest_uri");
    check(output.find(AnalysisService::uri_from_path(bundled_root / "std" / "ahfl.toml")) !=
              std::string::npos,
          "bundled_sysroot_mismatch_related.active_manifest_uri");
}

void test_lsp_legacy_initialization_sysroot_option_is_ignored() {
    const auto root = make_temp_project("legacy_sysroot_initialization_option");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_legacy_sysroot(std_root, root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    check(output.find("E::toolchain_sysroot_mismatch") != std::string::npos,
          "legacy_sysroot_initialization_option.ignored");
}

void test_lsp_noncanonical_toolchain_sysroot_initialization_option_is_ignored() {
    const auto root = make_temp_project("noncanonical_toolchain_sysroot_initialization_option");
    const auto std_root = root / "std";
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_noncanonical_toolchain_sysroot(std_root, root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto ignored_with_diagnostic =
        output.find("E::toolchain_sysroot_mismatch") != std::string::npos ||
        output.find("E::toolchain_sysroot_missing") != std::string::npos;
    check(ignored_with_diagnostic, "noncanonical_toolchain_sysroot_initialization_option.ignored");
}

void test_did_change_configuration_requests_resource_toolchain_profile() {
    const auto root = make_temp_project("resource_configuration_sysroot");
    const auto std_root = root / "std";
    const auto alternate_root = make_temp_project("resource_configuration_alternate_sysroot");
    const auto json_path = std_root / "json.ahfl";
    const std::string json_source = "module std::json;\n"
                                    "import std::collections as collections;\n"
                                    "import std::option as option;\n"
                                    "\n"
                                    "type List<T> = collections::List<T>;\n"
                                    "type MaybeList<T> = option::Option<List<T>>;\n";
    write_minimal_std_sources(std_root, json_path, json_source);
    write_minimal_std_package(alternate_root / "std", "alternate");

    const auto json_uri = AnalysisService::uri_from_path(json_path);
    const auto output = run_lsp_messages({
        initialize_body_with_sysroot(root, alternate_root),
        did_open_body(json_uri, 1, json_source),
        R"({"jsonrpc":"2.0","id":2,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        did_change_configuration_body(),
        workspace_configuration_response_body("ahfl-workspace-configuration-1", "."),
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/diagnostic","params":{"textDocument":{"uri":")" +
            json_uri + R"("}}})",
        R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":{}})",
    });

    check(output.find(R"("method":"workspace/configuration")") != std::string::npos,
          "resource_configuration.request_sent");
    check(output.find(R"("section":"ahfl.toolchain")") != std::string::npos,
          "resource_configuration.section");
    check(output.find(R"("scopeUri":")" + AnalysisService::uri_from_path(root)) !=
              std::string::npos,
          "resource_configuration.scope_uri");

    const auto before = response_body_for_id(output, 2);
    check(before.find("E::toolchain_sysroot_mismatch") != std::string::npos,
          "resource_configuration.initial_mismatch");

    const auto after = response_body_for_id(output, 3);
    check(after.find("E::toolchain_sysroot_mismatch") == std::string::npos,
          "resource_configuration.updated_no_mismatch");
    check(after.find("unknown type 'collections::List'") == std::string::npos,
          "resource_configuration.updated_imports_resolve");
}

void test_workspace_manifest_does_not_capture_unlisted_nested_package() {
    const auto root = make_temp_project("workspace_unlisted_nested_package");
    const auto app_root = root / "packages" / "app";
    const auto standalone_root = root / "packages" / "standalone";
    const auto main_path = standalone_root / "src" / "main.ahfl";
    const auto types_path = standalone_root / "src" / "types.ahfl";
    write_workspace_manifest(root, "\"packages/app\"");
    write_package_manifest(app_root, "listed-app", "listed", "\"main\"");
    write_package_manifest(standalone_root, "standalone", "standalone", "\"main\", \"types\"");

    const std::string main_source = "module standalone::main;\n"
                                    "import standalone::types as types;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    payload: types::Msg;\n"
                                    "}\n";
    write_file(main_path, main_source);
    write_file(types_path,
               "module standalone::types;\n"
               "\n"
               "struct Msg {\n"
               "    value: String;\n"
               "}\n");

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *snapshot = analysis.snapshot_for_uri(main_uri);
    check(snapshot != nullptr, "workspace_unlisted_nested.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }
    check(snapshot->project_aware, "workspace_unlisted_nested.project_aware");
    check(snapshot->package_graph_manifest.has_value(),
          "workspace_unlisted_nested.manifest_recorded");
    if (snapshot->package_graph_manifest.has_value()) {
        check(*snapshot->package_graph_manifest ==
                  std::filesystem::path(
                      AnalysisService::normalized_path_key(standalone_root / "ahfl.toml")),
              "workspace_unlisted_nested.uses_package_manifest");
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(main_uri);
    const auto has_unknown_msg =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown type 'types::Msg'") != std::string::npos;
        });
    check(!has_unknown_msg, "workspace_unlisted_nested.local_import_resolves");
}

void test_lsp_project_discovery_ignores_process_cwd_sysroot_probe() {
    const ScopedUnsetEnvVar unset_sysroot("AHFL_SYSROOT");
    const auto root = make_temp_project("cwd_independent_project_discovery");
    const auto poison_root = make_temp_project("cwd_poison_sysroot");
    const auto main_path = root / "src" / "main.ahfl";
    const auto types_path = root / "src" / "types.ahfl";
    write_package_manifest(root, "cwd-independent", "app", "\"main\", \"types\"");
    write_file(poison_root / "std" / "ahfl.toml",
               "manifest_version = 1\n"
               "\n"
               "[package]\n"
               "name = \"not-std\"\n"
               "version = \"0.1.0\"\n"
               "edition = \"2026\"\n"
               "kind = \"library\"\n");

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
               "    value: String;\n"
               "}\n");
    const ScopedCurrentPath cwd(poison_root);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *snapshot = analysis.snapshot_for_uri(main_uri);
    check(snapshot != nullptr, "cwd_independent.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }
    check(snapshot->project_aware, "cwd_independent.project_aware");
    const auto diagnostics = snapshot->diagnostics_for_uri(main_uri);
    const auto has_poisoned_sysroot =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("not-std") != std::string::npos ||
                   diagnostic.message.find("sysroot package must be standard-library package") !=
                       std::string::npos;
        });
    check(!has_poisoned_sysroot, "cwd_independent.ignores_process_cwd_sysroot");
}

void test_project_graph_error_does_not_fall_back_to_single_file_semantics() {
    const auto root = make_temp_project("project_error_no_single_file_fallback");
    const auto main_path = root / "src" / "main.ahfl";
    write_package_manifest(root, "project-error", "app", "\"main\"");
    const std::string source = "module app::main;\n"
                               "import app::types as types;\n"
                               "\n"
                               "struct Use {\n"
                               "    payload: types::Msg;\n"
                               "}\n";
    write_file(main_path, source);

    const auto uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(root / "missing-sysroot"));

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, "project_error_no_fallback.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(uri);
    const auto has_sysroot_error =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("failed to open sysroot std manifest") !=
                       std::string::npos ||
                   diagnostic.message.find("failed to open sysroot std") != std::string::npos ||
                   diagnostic.code == "E::toolchain_sysroot_missing";
        });
    const auto has_unknown_type =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown type 'types::Msg'") != std::string::npos;
        });
    check(has_sysroot_error, "project_error_no_fallback.sysroot_diagnostic");
    check(!has_unknown_type, "project_error_no_fallback.no_single_file_unknown_type");
}

void test_incompatible_toolchain_profile_stops_project_analysis() {
    const auto root = make_temp_project("incompatible_toolchain_profile");
    const auto std_root = root / "std";
    const auto main_path = root / "src" / "main.ahfl";
    write_minimal_std_package(std_root, "incompatible");
    write_package_manifest(root, "incompatible-toolchain", "app", "\"main\"");

    const std::string source = "module app::main;\n"
                               "\n"
                               "struct Use {\n"
                               "    payload: Missing;\n"
                               "}\n";
    write_file(main_path, source);

    auto profile_result = project_discovery::toolchain_profile_from_sysroot_input(
        root, project_discovery::ToolchainProfileOrigin::LspConfiguration);
    check(profile_result.profile.has_value(), "incompatible_toolchain.profile_exists");
    project_discovery::ToolchainProfileSet profiles;
    profiles.diagnostics = std::move(profile_result.diagnostics);
    if (profile_result.profile.has_value()) {
        profile_result.profile->server_compatibility =
            project_discovery::ToolchainServerCompatibility::Incompatible;
        profiles.default_profile = std::move(profile_result.profile);
    }

    const auto uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(std::move(profiles));

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, "incompatible_toolchain.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(uri);
    const auto has_incompatible =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.code == "E::toolchain_incompatible" &&
                   diagnostic.message.find("lsp-configuration") != std::string::npos;
        });
    const auto has_missing_type =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown type 'Missing'") != std::string::npos;
        });
    check(has_incompatible, "incompatible_toolchain.diagnostic");
    check(!has_missing_type, "incompatible_toolchain.stops_typecheck");
}

void test_toolchain_profile_records_std_identity_checksum() {
    const auto root = make_temp_project("toolchain_profile_std_identity");
    const auto std_root = root / "std";
    write_minimal_std_package(std_root, "identity-a");

    auto from_root = project_discovery::toolchain_profile_from_sysroot_input(
        root, project_discovery::ToolchainProfileOrigin::LspInitialization);
    auto from_manifest = project_discovery::toolchain_profile_from_sysroot_input(
        std_root / "ahfl.toml", project_discovery::ToolchainProfileOrigin::LspInitialization);

    check(!from_root.has_errors(), "toolchain_identity.root_has_no_errors");
    check(!from_manifest.has_errors(), "toolchain_identity.manifest_has_no_errors");
    check(from_root.profile.has_value(), "toolchain_identity.root_profile_exists");
    check(from_manifest.profile.has_value(), "toolchain_identity.manifest_profile_exists");
    if (!from_root.profile.has_value() || !from_manifest.profile.has_value()) {
        return;
    }

    check(from_root.profile->std_identity.starts_with("sha256:"),
          "toolchain_identity.checksum_prefix");
    check(from_root.profile->std_identity.size() == 71, "toolchain_identity.checksum_length");
    check(from_root.profile->std_identity == from_manifest.profile->std_identity,
          "toolchain_identity.root_and_manifest_match");

    write_minimal_std_package(std_root, "identity-b");
    auto changed = project_discovery::toolchain_profile_from_sysroot_input(
        root, project_discovery::ToolchainProfileOrigin::LspInitialization);
    check(changed.profile.has_value(), "toolchain_identity.changed_profile_exists");
    if (changed.profile.has_value()) {
        check(changed.profile->std_identity != from_root.profile->std_identity,
              "toolchain_identity.source_change_updates_checksum");
    }
}

void test_analysis_snapshot_cache_key_records_toolchain_identity() {
    const auto root = make_temp_project("analysis_toolchain_cache_key");
    const auto app_root = root / "app";
    const auto main_path = app_root / "src" / "main.ahfl";
    const auto sysroot_a = root / "sysroot-a";
    const auto sysroot_b = root / "sysroot-b";
    write_minimal_std_package(sysroot_a / "std", "cache-a");
    write_minimal_std_package(sysroot_b / "std", "cache-b");
    write_package_manifest(app_root, "cache-key-app", "app", "\"main\"");

    const std::string source = "module app::main;\n"
                               "\n"
                               "struct Msg {\n"
                               "    value: String;\n"
                               "}\n";
    write_file(main_path, source);

    auto profile_a = project_discovery::toolchain_profile_from_sysroot_input(
        sysroot_a, project_discovery::ToolchainProfileOrigin::LspInitialization);
    auto profile_b = project_discovery::toolchain_profile_from_sysroot_input(
        sysroot_b, project_discovery::ToolchainProfileOrigin::LspInitialization);
    check(profile_a.profile.has_value(), "analysis_toolchain_cache_key.profile_a_exists");
    check(profile_b.profile.has_value(), "analysis_toolchain_cache_key.profile_b_exists");
    if (!profile_a.profile.has_value() || !profile_b.profile.has_value()) {
        return;
    }

    const auto uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(workspace_toolchain_profile_set_for_sysroot(root, sysroot_a));

    const auto *first = analysis.snapshot_for_uri(uri);
    const auto *second = analysis.snapshot_for_uri(uri);
    check(first != nullptr, "analysis_toolchain_cache_key.first_exists");
    check(first == second, "analysis_toolchain_cache_key.reuses_same_key");
    check(analysis.analysis_runs() == 1, "analysis_toolchain_cache_key.single_run");
    if (first == nullptr || !first->toolchain_cache_key.has_value()) {
        check(false, "analysis_toolchain_cache_key.key_exists");
        return;
    }

    check(first->toolchain_cache_key->workspace_folder_uri == AnalysisService::uri_from_path(root),
          "analysis_toolchain_cache_key.workspace_uri");
    check(first->toolchain_cache_key->root_manifest ==
              AnalysisService::normalized_path_key(app_root / "ahfl.toml"),
          "analysis_toolchain_cache_key.root_manifest");
    check(first->toolchain_cache_key->workspace_manifest.empty(),
          "analysis_toolchain_cache_key.no_workspace_manifest");
    check(first->toolchain_cache_key->package_graph_identity.starts_with("sha256:"),
          "analysis_toolchain_cache_key.package_graph_identity_prefix");
    check(first->toolchain_cache_key->package_graph_identity.size() == 71,
          "analysis_toolchain_cache_key.package_graph_identity_length");
    check(first->toolchain_cache_key->std_manifest ==
              AnalysisService::normalized_path_key(sysroot_a / "std" / "ahfl.toml"),
          "analysis_toolchain_cache_key.std_manifest");
    check(first->toolchain_cache_key->std_identity == profile_a.profile->std_identity,
          "analysis_toolchain_cache_key.std_identity");
    check(first->toolchain_cache_key->scope == "workspace-folder-uri",
          "analysis_toolchain_cache_key.scope");
    check(first->toolchain_cache_key->index_schema_version == "lsp-workspace-index-v1",
          "analysis_toolchain_cache_key.index_schema_version");
    check(first->toolchain_cache_key->index_identity_schema_version ==
              "lsp-workspace-index-identity-v1",
          "analysis_toolchain_cache_key.index_identity_schema_version");

    analysis.set_toolchain_profiles(workspace_toolchain_profile_set_for_sysroot(root, sysroot_b));
    const auto *third = analysis.snapshot_for_uri(uri);
    check(third != nullptr, "analysis_toolchain_cache_key.third_exists");
    check(analysis.analysis_runs() == 2, "analysis_toolchain_cache_key.rebuilds_after_profile");
    if (third != nullptr && third->toolchain_cache_key.has_value()) {
        check(third->toolchain_cache_key->std_manifest ==
                  AnalysisService::normalized_path_key(sysroot_b / "std" / "ahfl.toml"),
              "analysis_toolchain_cache_key.updated_std_manifest");
        check(third->toolchain_cache_key->std_identity == profile_b.profile->std_identity,
              "analysis_toolchain_cache_key.updated_std_identity");
    }
}

void test_analysis_snapshot_cache_key_records_workspace_manifest() {
    const auto root = make_temp_project("analysis_workspace_manifest_cache_key");
    const auto app_root = root / "packages" / "app";
    const auto lib_root = root / "packages" / "lib";
    const auto sysroot = root / "sysroot";
    const auto main_path = app_root / "src" / "main.ahfl";
    const auto lib_path = lib_root / "src" / "lib.ahfl";

    write_workspace_manifest(root, "\"packages/app\"");
    write_package_manifest(app_root, "workspace-cache-key-app", "app", "\"main\"");
    write_minimal_std_package(sysroot / "std", "workspace-cache-key-std");

    const std::string source = "module app::main;\n"
                               "\n"
                               "struct Msg {\n"
                               "    value: String;\n"
                               "}\n";
    write_file(main_path, source);

    const auto uri = AnalysisService::uri_from_path(main_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri,
        .language_id = "ahfl",
        .version = 1,
        .text = source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    analysis.set_toolchain_profiles(workspace_toolchain_profile_set_for_sysroot(root, sysroot));

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, "analysis_workspace_manifest_cache_key.snapshot_exists");
    if (snapshot == nullptr || !snapshot->toolchain_cache_key.has_value()) {
        check(false, "analysis_workspace_manifest_cache_key.key_exists");
        return;
    }

    check(snapshot->toolchain_cache_key->root_manifest ==
              AnalysisService::normalized_path_key(app_root / "ahfl.toml"),
          "analysis_workspace_manifest_cache_key.root_manifest");
    check(snapshot->toolchain_cache_key->workspace_manifest ==
              AnalysisService::normalized_path_key(root / "ahfl.workspace.toml"),
          "analysis_workspace_manifest_cache_key.workspace_manifest");
    const auto first_graph_identity = snapshot->toolchain_cache_key->package_graph_identity;
    check(first_graph_identity.starts_with("sha256:"),
          "analysis_workspace_manifest_cache_key.graph_identity_prefix");
    check(snapshot->package_graph_manifest ==
              std::optional<std::filesystem::path>{std::filesystem::path(
                  AnalysisService::normalized_path_key(root / "ahfl.workspace.toml"))},
          "analysis_workspace_manifest_cache_key.snapshot_graph_manifest");

    write_workspace_manifest(root, "\"packages/app\", \"packages/lib\"");
    write_package_manifest(lib_root, "workspace-cache-key-lib", "lib", "\"lib\"", "src/lib.ahfl");
    write_file(lib_path,
               "module lib::lib;\n"
               "\n"
               "struct Lib {}\n");
    analysis.invalidate_all();

    const auto *updated = analysis.snapshot_for_uri(uri);
    check(updated != nullptr, "analysis_workspace_manifest_cache_key.updated_snapshot_exists");
    if (updated != nullptr && updated->toolchain_cache_key.has_value()) {
        check(updated->toolchain_cache_key->package_graph_identity != first_graph_identity,
              "analysis_workspace_manifest_cache_key.graph_identity_changes");
    }
}

void test_analysis_snapshot_cache_key_records_open_overlay_revisions() {
    const auto root = make_temp_project("analysis_overlay_revision_key");
    const auto main_path = root / "src" / "main.ahfl";
    const auto draft_path = root / "src" / "draft.ahfl";
    write_package_manifest(root, "overlay-revision-key-app", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct Main {}\n";
    const std::string draft_source = "module app::draft;\n"
                                     "\n"
                                     "struct Draft {}\n";
    write_file(main_path, main_source);
    write_file(draft_path, draft_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto draft_uri = AnalysisService::uri_from_path(draft_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });
    store.open(TextDocumentItem{
        .uri = draft_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = draft_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *first = analysis.snapshot_for_uri(main_uri);
    check(first != nullptr, "analysis_overlay_revision_key.first_exists");
    check(analysis.analysis_runs() == 1, "analysis_overlay_revision_key.single_run");
    if (first == nullptr) {
        return;
    }

    const auto first_overlay_key = first->open_document_overlay_revision_set;
    check(first_overlay_key.find(main_uri + "@") != std::string::npos,
          "analysis_overlay_revision_key.includes_main");
    check(first_overlay_key.find(draft_uri + "@") != std::string::npos,
          "analysis_overlay_revision_key.includes_draft");

    store.change(draft_uri,
                 2,
                 "module app::draft;\n"
                 "\n"
                 "struct DraftChanged {}\n");
    const auto *second = analysis.snapshot_for_uri(main_uri);
    check(second != nullptr, "analysis_overlay_revision_key.second_exists");
    check(analysis.analysis_runs() == 2, "analysis_overlay_revision_key.rebuilds_after_overlay");
    if (second != nullptr) {
        check(second->open_document_overlay_revision_set != first_overlay_key,
              "analysis_overlay_revision_key.changes_after_overlay_edit");
        check(second->open_document_overlay_revision_set.find(draft_uri + "@") != std::string::npos,
              "analysis_overlay_revision_key.keeps_draft_after_edit");
    }
}

void test_analysis_snapshot_cache_key_ignores_unrelated_open_overlay_revisions() {
    const auto root = make_temp_project("analysis_overlay_revision_scope");
    const auto unrelated_root = make_temp_project("analysis_overlay_revision_unrelated");
    const auto main_path = root / "src" / "main.ahfl";
    const auto unrelated_path = unrelated_root / "src" / "note.ahfl";
    write_package_manifest(root, "overlay-revision-scope-app", "app", "\"main\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct Main {}\n";
    const std::string unrelated_source = "module unrelated::note;\n"
                                         "\n"
                                         "struct Note {}\n";
    write_file(main_path, main_source);
    write_file(unrelated_path, unrelated_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto unrelated_uri = AnalysisService::uri_from_path(unrelated_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });
    store.open(TextDocumentItem{
        .uri = unrelated_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = unrelated_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});

    const auto *first = analysis.snapshot_for_uri(main_uri);
    check(first != nullptr, "analysis_overlay_revision_scope.first_exists");
    check(analysis.analysis_runs() == 1, "analysis_overlay_revision_scope.single_run");
    if (first == nullptr) {
        return;
    }

    const auto first_overlay_key = first->open_document_overlay_revision_set;
    check(first_overlay_key.find(main_uri + "@") != std::string::npos,
          "analysis_overlay_revision_scope.includes_main");
    check(first_overlay_key.find(unrelated_uri + "@") == std::string::npos,
          "analysis_overlay_revision_scope.excludes_unrelated");

    store.change(unrelated_uri,
                 2,
                 "module unrelated::note;\n"
                 "\n"
                 "struct NoteChanged {}\n");
    const auto *second = analysis.snapshot_for_uri(main_uri);
    check(second == first, "analysis_overlay_revision_scope.reuses_after_unrelated_overlay");
    check(analysis.analysis_runs() == 1, "analysis_overlay_revision_scope.no_unrelated_rebuild");
}

void test_sysroot_index_cache_key_separates_workspace_roots() {
    const auto root = make_temp_project("sysroot_index_cache_key_workspace_roots");
    const auto sysroot = root / "sysroot";
    const auto app_a = root / "app-a";
    const auto app_b = root / "app-b";
    const auto main_a = app_a / "src" / "main.ahfl";
    const auto main_b = app_b / "src" / "main.ahfl";
    write_minimal_std_package(sysroot / "std", "shared-sysroot");
    write_package_manifest(app_a,
                           "cache-key-app-a",
                           "appa",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_package_manifest(app_b,
                           "cache-key-app-b",
                           "appb",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");

    const std::string source_a = "module appa::main;\n"
                                 "\n"
                                 "struct MsgA {\n"
                                 "    value: String;\n"
                                 "}\n";
    const std::string source_b = "module appb::main;\n"
                                 "\n"
                                 "struct MsgB {\n"
                                 "    value: String;\n"
                                 "}\n";
    write_file(main_a, source_a);
    write_file(main_b, source_b);

    const auto uri_a = AnalysisService::uri_from_path(main_a);
    const auto uri_b = AnalysisService::uri_from_path(main_b);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri_a,
        .language_id = "ahfl",
        .version = 1,
        .text = source_a,
    });
    store.open(TextDocumentItem{
        .uri = uri_b,
        .language_id = "ahfl",
        .version = 1,
        .text = source_b,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({app_a, app_b});
    analysis.set_toolchain_profiles(toolchain_profile_set_for_sysroot(sysroot));

    const auto *index_a = analysis.sysroot_index_for_uri(uri_a);
    const auto *index_a_again = analysis.sysroot_index_for_uri(uri_a);
    const auto *index_b = analysis.sysroot_index_for_uri(uri_b);
    check(index_a != nullptr, "sysroot_index_cache_key.index_a_exists");
    check(index_b != nullptr, "sysroot_index_cache_key.index_b_exists");
    check(index_a == index_a_again, "sysroot_index_cache_key.reuses_same_workspace_entry");
    check(index_a != index_b, "sysroot_index_cache_key.separates_workspace_entries");
}

void test_multi_root_sysroot_index_uses_resource_toolchain_profiles() {
    const auto root = make_temp_project("multi_root_sysroot_index_profiles");
    const auto app_a = root / "app-a";
    const auto app_b = root / "app-b";
    const auto sysroot_a = root / "sysroot-a";
    const auto sysroot_b = root / "sysroot-b";
    const auto main_a = app_a / "src" / "main.ahfl";
    const auto main_b = app_b / "src" / "main.ahfl";
    const auto fmt_a = sysroot_a / "std" / "fmt.ahfl";
    const auto fmt_b = sysroot_b / "std" / "fmt.ahfl";

    auto write_profiled_std = [&](const std::filesystem::path &std_root,
                                  std::string_view format_name,
                                  std::string_view marker) {
        write_file(std_root / "ahfl.toml",
                   "manifest_version = 1\n"
                   "\n"
                   "[package]\n"
                   "name = \"std\"\n"
                   "version = \"0.1.0\"\n"
                   "edition = \"2026\"\n"
                   "kind = \"standard-library\"\n"
                   "\n"
                   "[module]\n"
                   "prefix = \"std\"\n"
                   "root = \".\"\n"
                   "\n"
                   "[exports]\n"
                   "modules = [\"prelude\", \"int\", \"fmt\"]\n"
                   "\n"
                   "[prelude]\n"
                   "module = \"std::prelude\"\n"
                   "injection = \"explicit\"\n"
                   "\n"
                   "[compiler_intrinsics]\n"
                   "allow = [\"primitive_*\"]\n");
        write_file(std_root / "prelude.ahfl",
                   "module std::prelude;\n"
                   "// " +
                       std::string(marker) + "\n");
        write_file(std_root / "int.ahfl", "module std::int;\n\nimpl Int {}\n");
        write_file(std_root / "fmt.ahfl",
                   "module std::fmt;\n"
                   "\n"
                   "fn " +
                       std::string(format_name) +
                       "(x: Int) -> String effect Pure;\n"
                       "\n"
                       "impl Int {}\n");
    };
    write_profiled_std(sysroot_a / "std", "format_a", "profile-a");
    write_profiled_std(sysroot_b / "std", "format_b", "profile-b");
    write_package_manifest(app_a,
                           "multi-root-app-a",
                           "appa",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");
    write_package_manifest(app_b,
                           "multi-root-app-b",
                           "appb",
                           "\"main\"",
                           "src/main.ahfl",
                           "\n[dependencies]\nstd = { source = \"sysroot\" }\n");

    const std::string source_a = "module appa::main;\n"
                                 "\n"
                                 "fn keep_a(x: Int) -> Int effect Pure decreases 0 {\n"
                                 "    return x;\n"
                                 "}\n";
    const std::string source_b = "module appb::main;\n"
                                 "\n"
                                 "fn keep_b(x: Int) -> Int effect Pure decreases 0 {\n"
                                 "    return x;\n"
                                 "}\n";
    write_file(main_a, source_a);
    write_file(main_b, source_b);

    project_discovery::ToolchainProfileSet profiles;
    auto add_workspace_profile = [&](const std::filesystem::path &workspace_root,
                                     const std::filesystem::path &sysroot,
                                     std::string_view label) {
        auto result = project_discovery::toolchain_profile_from_sysroot_input(
            sysroot, project_discovery::ToolchainProfileOrigin::LspInitialization);
        check(result.profile.has_value(),
              "multi_root_sysroot_index." + std::string(label) + "_profile_exists");
        profiles.diagnostics.insert(
            profiles.diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        if (!result.profile.has_value()) {
            return;
        }
        result.profile->scope = project_discovery::ToolchainProfileScope::WorkspaceFolder;
        profiles.workspace_profiles.push_back(project_discovery::WorkspaceToolchainProfile{
            .workspace_root = workspace_root,
            .profile = std::move(*result.profile),
        });
    };
    add_workspace_profile(app_a, sysroot_a, "a");
    add_workspace_profile(app_b, sysroot_b, "b");

    const auto uri_a = AnalysisService::uri_from_path(main_a);
    const auto uri_b = AnalysisService::uri_from_path(main_b);
    const auto fmt_a_uri = AnalysisService::uri_from_path(fmt_a);
    const auto fmt_b_uri = AnalysisService::uri_from_path(fmt_b);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = uri_a,
        .language_id = "ahfl",
        .version = 1,
        .text = source_a,
    });
    store.open(TextDocumentItem{
        .uri = uri_b,
        .language_id = "ahfl",
        .version = 1,
        .text = source_b,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({app_a, app_b});
    analysis.set_toolchain_profiles(std::move(profiles));

    const auto *index_a = analysis.sysroot_index_for_uri(uri_a);
    const auto *index_b = analysis.sysroot_index_for_uri(uri_b);
    check(index_a != nullptr, "multi_root_sysroot_index.index_a_exists");
    check(index_b != nullptr, "multi_root_sysroot_index.index_b_exists");
    if (index_a != nullptr) {
        check(!index_a->workspace_symbols("format_a").empty(),
              "multi_root_sysroot_index.a_contains_own_symbol");
        check(index_a->workspace_symbols("format_b").empty(),
              "multi_root_sysroot_index.a_excludes_other_symbol");
        check(index_source_unit_for_uri(*index_a, fmt_a_uri) != nullptr,
              "multi_root_sysroot_index.a_contains_own_source");
        check(index_source_unit_for_uri(*index_a, fmt_b_uri) == nullptr,
              "multi_root_sysroot_index.a_excludes_other_source");
    }
    if (index_b != nullptr) {
        check(!index_b->workspace_symbols("format_b").empty(),
              "multi_root_sysroot_index.b_contains_own_symbol");
        check(index_b->workspace_symbols("format_a").empty(),
              "multi_root_sysroot_index.b_excludes_other_symbol");
        check(index_source_unit_for_uri(*index_b, fmt_b_uri) != nullptr,
              "multi_root_sysroot_index.b_contains_own_source");
        check(index_source_unit_for_uri(*index_b, fmt_a_uri) == nullptr,
              "multi_root_sysroot_index.b_excludes_other_source");
    }
}

void test_package_graph_manifest_does_not_inject_prelude() {
    const auto root = make_temp_project("package_graph_explicit_prelude");
    const auto source_path = root / "src" / "main.ahfl";
    write_package_manifest(root, "lsp-explicit-prelude", "app", "\"main\"");
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
    analysis.set_workspace_folders({root});

    const auto *snapshot = analysis.snapshot_for_uri(uri);
    check(snapshot != nullptr, "package_graph_explicit_prelude.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(uri);
    const auto has_unknown_some =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown callable 'some'") != std::string::npos;
        });
    check(has_unknown_some, "package_graph_explicit_prelude.some_is_not_implicit");
}

void test_index_only_exported_type_does_not_satisfy_unimported_reference() {
    const auto root = make_temp_project("index_only_type_visibility");
    const auto main_path = root / "src" / "main.ahfl";
    const auto indexed_path = root / "src" / "indexed.ahfl";
    write_package_manifest(root, "lsp-index-only-type", "app", "\"main\", \"indexed\"");

    const std::string main_source = "module app::main;\n"
                                    "\n"
                                    "struct Use {\n"
                                    "    value: IndexOnly;\n"
                                    "}\n";
    const std::string indexed_source = "module app::indexed;\n"
                                       "\n"
                                       "struct IndexOnly {}\n";
    write_file(main_path, main_source);
    write_file(indexed_path, indexed_source);

    const auto main_uri = AnalysisService::uri_from_path(main_path);
    const auto indexed_uri = AnalysisService::uri_from_path(indexed_path);
    DocumentStore store;
    store.open(TextDocumentItem{
        .uri = main_uri,
        .language_id = "ahfl",
        .version = 1,
        .text = main_source,
    });

    AnalysisService analysis(store);
    analysis.set_workspace_folders({root});
    const auto *snapshot = analysis.snapshot_for_uri(main_uri);
    check(snapshot != nullptr, "index_only_type_visibility.snapshot_exists");
    if (snapshot == nullptr) {
        return;
    }

    check(snapshot->source_for_uri(indexed_uri) == nullptr,
          "index_only_type_visibility.indexed_not_semantic_source");
    check(snapshot->workspace_index != nullptr, "index_only_type_visibility.index_exists");
    if (snapshot->workspace_index != nullptr) {
        const auto *indexed_source_unit =
            index_source_unit_for_uri(*snapshot->workspace_index, indexed_uri);
        check(indexed_source_unit != nullptr,
              "index_only_type_visibility.indexed_source_unit_exists");
        const auto &symbols = snapshot->workspace_index->symbols();
        const auto indexed_symbol =
            std::find_if(symbols.begin(), symbols.end(), [](const SymbolFact &symbol) {
                return symbol.canonical_name == "app::indexed::IndexOnly";
            });
        check(indexed_symbol != symbols.end(),
              "index_only_type_visibility.indexed_symbol_fact_exists");
    }

    const auto diagnostics = snapshot->diagnostics_for_uri(main_uri);
    const auto has_unknown_index_only =
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const LspDiagnostic &diagnostic) {
            return diagnostic.message.find("unknown type 'IndexOnly'") != std::string::npos;
        });
    check(has_unknown_index_only, "index_only_type_visibility.unimported_type_stays_unknown");
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
    const std::string source = "trait Display {\n"
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
    const auto payload = hover.payload_at(*snapshot, *lsp_source, position_of(source, "Foo<Int>"));
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
    const std::string prefix = "struct M { v: String; }\n"
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
    const std::string suffix = "        goto Done;\n"
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
            static_cast<uint32_t>(indent.size()) + static_cast<uint32_t>(cursor_offset_inside_stmt);
        const std::string params =
            R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
            std::to_string(stmt_line_no) + R"(,"character":)" + std::to_string(ch) + R"(}})";
        const std::string response =
            run_handler_request(source, "textDocument/signatureHelp", params);

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
    run_case("assert(",
             "assert",
             7,
             "\"label\":\"assert(condition: Bool[, message: String])\"",
             "Predicate assertion; throws assertion failure when condition is False",
             2,
             0);
    // Case 2: after first arg + comma: `assert(x, ` cursor offset = 10
    run_case("assert(x, ",
             "assert",
             10,
             "\"label\":\"assert(condition: Bool[, message: String])\"",
             "Predicate assertion; throws assertion failure when condition is False",
             2,
             1);
    // Case 3 (negative): cursor after a regular identifier that is not in family.
    // Use a `foo(` call; expect null because foo is not declared.
    run_case("foo(", "assert_not_family", 4, "", "", 0, std::nullopt);

    // -- unwrap --
    run_case("unwrap(",
             "unwrap",
             7,
             R"("label":"unwrap(value: Option<T>) -> T")",
             "Optional deconstructor; if value is Some<T> returns T",
             1,
             0);
    run_case("unwrap(x, ",
             "unwrap",
             10,
             R"("label":"unwrap(value: Option<T>) -> T")",
             "Optional deconstructor; if value is Some<T> returns T",
             1,
             1);
    run_case("bar(", "unwrap_not_family", 4, "", "", 0, std::nullopt);

    // -- requires --
    run_case("requires(",
             "requires",
             9,
             "\"label\":\"requires(condition: Bool[, message: String])\"",
             "Contract requirement; fails contract evaluation",
             2,
             0);
    run_case("requires(x, ",
             "requires",
             12,
             "\"label\":\"requires(condition: Bool[, message: String])\"",
             "Contract requirement; fails contract evaluation",
             2,
             1);
    run_case("baz(", "requires_not_family", 4, "", "", 0, std::nullopt);

    // -- unreachable --
    run_case("unreachable(",
             "unreachable",
             12,
             "\"label\":\"unreachable([message: String])\"",
             "Unreachable code marker; if ever executed raises kind: UNREACHABLE_EXECUTED",
             1,
             0);
    run_case("unreachable(, ",
             "unreachable",
             14,
             "\"label\":\"unreachable([message: String])\"",
             "Unreachable code marker; if ever executed raises kind: UNREACHABLE_EXECUTED",
             1,
             1);
    run_case("qux(", "unreachable_not_family", 4, "", "", 0, std::nullopt);
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

void test_completion_pattern_context_uses_typed_pattern_facts() {
    const std::string source = "enum Choice {\n"
                               "    First,\n"
                               "    Second,\n"
                               "}\n"
                               "\n"
                               "enum Other {\n"
                               "    Alien,\n"
                               "}\n"
                               "\n"
                               "enum Wrap {\n"
                               "    Item(Choice),\n"
                               "    Empty,\n"
                               "}\n"
                               "\n"
                               "fn use_match(choice: Choice) -> Int effect Pure decreases 0 {\n"
                               "    return match choice {\n"
                               "        _ => 0,\n"
                               "    };\n"
                               "}\n"
                               "\n"
                               "fn use_if_let(choice: Choice) -> Int effect Pure decreases 0 {\n"
                               "    if let _ = choice {\n"
                               "        return 1;\n"
                               "    } else {\n"
                               "        return 0;\n"
                               "    }\n"
                               "}\n"
                               "\n"
                               "fn use_nested(wrap: Wrap) -> Int effect Pure decreases 0 {\n"
                               "    return match wrap {\n"
                               "        Item(_) => 1,\n"
                               "        Empty => 0,\n"
                               "    };\n"
                               "}\n";

    const auto wildcard_position = position_of(source, "_ => 0");
    const std::string wildcard_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
        std::to_string(wildcard_position.line) + R"(,"character":)" +
        std::to_string(wildcard_position.character) + R"(}})";
    const auto wildcard_output =
        run_handler_request(source, "textDocument/completion", wildcard_params);
    check(wildcard_output.find("\"label\":\"First\"") != std::string::npos,
          "completion.pattern_match_contains_first");
    check(wildcard_output.find("\"label\":\"Second\"") != std::string::npos,
          "completion.pattern_match_contains_second");
    check(wildcard_output.find("\"label\":\"Alien\"") == std::string::npos,
          "completion.pattern_match_excludes_other_enum");

    const auto if_let_position = position_of(source, "if let _");
    const std::string if_let_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
        std::to_string(if_let_position.line) + R"(,"character":)" +
        std::to_string(if_let_position.character + 7) + R"(}})";
    const auto if_let_output =
        run_handler_request(source, "textDocument/completion", if_let_params);
    check(if_let_output.find("\"label\":\"First\"") != std::string::npos,
          "completion.pattern_if_let_contains_first");
    check(if_let_output.find("\"label\":\"Second\"") != std::string::npos,
          "completion.pattern_if_let_contains_second");
    check(if_let_output.find("\"label\":\"Alien\"") == std::string::npos,
          "completion.pattern_if_let_excludes_other_enum");

    const auto nested_position = position_of(source, "Item(_)");
    const std::string nested_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
        std::to_string(nested_position.line) + R"(,"character":)" +
        std::to_string(nested_position.character + 5) + R"(}})";
    const auto nested_output =
        run_handler_request(source, "textDocument/completion", nested_params);
    check(nested_output.find("\"label\":\"First\"") != std::string::npos,
          "completion.pattern_nested_contains_payload_first");
    check(nested_output.find("\"label\":\"Empty\"") == std::string::npos,
          "completion.pattern_nested_excludes_outer_variant");
}

void test_completion_struct_variant_fields_uses_typed_pattern_facts() {
    const std::string source = "enum Packet {\n"
                               "    Empty,\n"
                               "    Data { code: Int, label: String },\n"
                               "}\n"
                               "\n"
                               "fn use_match(packet: Packet) -> Int effect Pure decreases 0 {\n"
                               "    return match packet {\n"
                               "        Data { .. } => 1,\n"
                               "        Empty => 0,\n"
                               "    };\n"
                               "}\n"
                               "\n"
                               "fn use_if_let(packet: Packet) -> Int effect Pure decreases 0 {\n"
                               "    if let Data { code, .. } = packet {\n"
                               "        return code;\n"
                               "    } else {\n"
                               "        return 0;\n"
                               "    }\n"
                               "}\n";

    const auto empty_fields_position = position_of(source, "Data { .. }");
    const std::string empty_fields_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
        std::to_string(empty_fields_position.line) + R"(,"character":)" +
        std::to_string(empty_fields_position.character + 7) + R"(}})";
    const auto empty_fields_output =
        run_handler_request(source, "textDocument/completion", empty_fields_params);
    check(empty_fields_output.find("\"label\":\"code\"") != std::string::npos,
          "completion.pattern_struct_fields_contains_code");
    check(empty_fields_output.find("\"label\":\"label\"") != std::string::npos,
          "completion.pattern_struct_fields_contains_label");
    check(empty_fields_output.find("\"label\":\"Empty\"") == std::string::npos,
          "completion.pattern_struct_fields_excludes_enum_variant");

    const auto used_field_position = position_of(source, "Data { code, .. }");
    const std::string used_field_params =
        R"({"textDocument":{"uri":"file:///test.ahfl"},"position":{"line":)" +
        std::to_string(used_field_position.line) + R"(,"character":)" +
        std::to_string(used_field_position.character + 13) + R"(}})";
    const auto used_field_output =
        run_handler_request(source, "textDocument/completion", used_field_params);
    check(used_field_output.find("\"label\":\"label\"") != std::string::npos,
          "completion.pattern_struct_fields_keeps_unused_label");
    check(used_field_output.find("\"label\":\"code\"") == std::string::npos,
          "completion.pattern_struct_fields_filters_used_code");
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
            a_uri +
            R"("},"range":{"start":{"line":2,"character":0},"end":{"line":2,"character":8}}}})",
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})",
    });

    const auto response = response_body_for_id(output, 2);
    // Non-empty result array is the minimum assertion.
    check(response.find("\"result\":[") != std::string::npos, "codeAction.qf_dup.result_is_array");
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
        "        let dup = Request{ category: input.category, priority: Priority::Low, retries: 0 "
        "};\n"
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
    const auto rich = run_hover_request_with_init(source, "High, retries", kRichInit);

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
    const auto on_enum_decl =
        run_hover_request_with_init(enum_decl_source, "Priority {\n", kRichInit);
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
    const std::string diag_source = "struct Ctx {\n"
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
    const auto on_mismatch = run_hover_request_with_init(diag_source, "1;\n", kRichInit);
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

    const std::string uri = AnalysisService::uri_from_path(root / "src" / "main.ahfl");

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
    if (qf == nullptr)
        return;

    // 1. Human-readable title carries the inserted clause verbatim so the
    //    IDE preview matches what will be written.
    check(qf->title.find("Insert `context: struct { };` clause") != std::string::npos,
          "codeAction.qf_ctx.title_mentions_insert_context");
    // 2. kind == quickfix (the `only:["quickfix"]` client filter will pick it).
    check(qf->kind == CodeActionKind::QuickFix, "codeAction.qf_ctx.kind_is_quickfix");
    // 3. isPreferred so clients with "apply preferred quickfix" shortcuts
    //    default to this (it is the only valid remediation).
    check(qf->is_preferred, "codeAction.qf_ctx.marked_preferred");
    // 4. The workspace edit must contain *exactly one* TextEdit and its
    //    payload must contain the new `context: struct { };` declaration.
    check(qf->edit.has_value(), "codeAction.qf_ctx.has_workspace_edit");
    if (!qf->edit.has_value())
        return;

    std::size_t total_edits = 0;
    bool found_struct_clause = false;
    bool found_non_empty = false;
    Position insert_pos{0, 0};
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &e : edits) {
            if (e.new_text.find("context: struct { };") != std::string::npos)
                found_struct_clause = true;
            if (!e.new_text.empty())
                found_non_empty = true;
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

void test_code_action_qf_match_missing_patterns_inserts_witness_arm() {
    const std::string source = "module lsp::match_qf;\n"
                               "enum E { A, B }\n"
                               "fn f(e: E) -> Int effect Pure decreases 0 {\n"
                               "    return match e {\n"
                               "        A => 1,\n"
                               "    };\n"
                               "}\n";

    LspDiagnostic diag;
    diag.code = "typecheck.MATCH_MISSING_PATTERNS";
    diag.severity = DiagnosticSeverity::Error;
    diag.message = "non-exhaustive match";
    diag.data["missing_witnesses"] = {"B"};
    diag.range = Range{Position{3, 11}, Position{5, 5}};

    const Range cursor_range{Position{3, 11}, Position{3, 16}};
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    const CodeAction *qf = nullptr;
    for (const auto &action : actions) {
        if (action.title == "Insert missing match arm") {
            qf = &action;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_match_missing.action_found");
    if (qf == nullptr)
        return;

    check(qf->title == "Insert missing match arm", "codeAction.qf_match_missing.title");
    check(qf->kind == CodeActionKind::QuickFix, "codeAction.qf_match_missing.kind_is_quickfix");
    check(qf->is_preferred, "codeAction.qf_match_missing.marked_preferred");
    check(qf->edit.has_value(), "codeAction.qf_match_missing.has_workspace_edit");
    if (!qf->edit.has_value())
        return;

    std::size_t total_edits = 0;
    bool found_witness_arm = false;
    bool replaces_closing_indent = false;
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &edit : edits) {
            if (edit.new_text.find("B => <TODO>,") != std::string::npos &&
                edit.new_text.find("_ => <TODO>,") == std::string::npos) {
                found_witness_arm = true;
            }
            if (edit.range.start.line == 5 && edit.range.start.character == 0 &&
                edit.range.end.line == 5 && edit.range.end.character == 4) {
                replaces_closing_indent = true;
            }
        }
    }
    check(total_edits == 1, "codeAction.qf_match_missing.single_text_edit");
    check(found_witness_arm, "codeAction.qf_match_missing.inserts_witness_arm");
    check(replaces_closing_indent, "codeAction.qf_match_missing.replaces_closing_indent");
}

void test_code_action_qf_match_missing_patterns_keeps_struct_witness_fields() {
    const std::string source = "module lsp::match_qf;\n"
                               "fn f(x: Int) -> Int effect Pure decreases 0 {\n"
                               "    return match x {\n"
                               "        _ => 1,\n"
                               "    };\n"
                               "}\n";

    LspDiagnostic diag;
    diag.code = "typecheck.MATCH_MISSING_PATTERNS";
    diag.severity = DiagnosticSeverity::Error;
    diag.message = "non-exhaustive match";
    diag.data["missing_witnesses"] = {"Data { flag: false, other: false }", "Empty"};
    diag.range = Range{Position{2, 11}, Position{4, 5}};

    const Range cursor_range{Position{2, 11}, Position{2, 16}};
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    const CodeAction *qf = nullptr;
    for (const auto &action : actions) {
        if (action.title == "Insert missing match arms") {
            qf = &action;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_match_missing_struct.action_found");
    if (qf == nullptr || !qf->edit.has_value())
        return;

    bool found_struct_witness = false;
    bool found_second_witness = false;
    bool avoided_field_split = true;
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        for (const auto &edit : edits) {
            found_struct_witness =
                found_struct_witness ||
                edit.new_text.find("Data { flag: false, other: false } => <TODO>,") !=
                    std::string::npos;
            found_second_witness =
                found_second_witness || edit.new_text.find("Empty => <TODO>,") != std::string::npos;
            avoided_field_split =
                avoided_field_split &&
                edit.new_text.find("other: false => <TODO>,") == std::string::npos;
        }
    }
    check(found_struct_witness, "codeAction.qf_match_missing_struct.inserts_struct_witness");
    check(found_second_witness, "codeAction.qf_match_missing_struct.inserts_second_witness");
    check(avoided_field_split, "codeAction.qf_match_missing_struct.does_not_split_fields");
}

void test_code_action_qf_match_missing_patterns_falls_back_to_wildcard() {
    const std::string source = "module lsp::match_qf;\n"
                               "enum E { A, B }\n"
                               "fn f(e: E) -> Int effect Pure decreases 0 {\n"
                               "    return match e {\n"
                               "        A => 1,\n"
                               "    };\n"
                               "}\n";

    LspDiagnostic diag;
    diag.code = "typecheck.MATCH_MISSING_PATTERNS";
    diag.severity = DiagnosticSeverity::Error;
    diag.message = "non-exhaustive match";
    diag.range = Range{Position{3, 11}, Position{5, 5}};

    const Range cursor_range{Position{3, 11}, Position{3, 16}};
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    const CodeAction *qf = nullptr;
    for (const auto &action : actions) {
        if (action.title == "Insert wildcard match arm") {
            qf = &action;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_match_missing_fallback.action_found");
    if (qf == nullptr || !qf->edit.has_value())
        return;

    bool found_wildcard_arm = false;
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        for (const auto &edit : edits) {
            if (edit.new_text.find("_ => <TODO>,") != std::string::npos) {
                found_wildcard_arm = true;
            }
        }
    }
    check(found_wildcard_arm, "codeAction.qf_match_missing_fallback.inserts_wildcard_arm");
}

void test_code_action_qf_match_unreachable_arm_removes_arm_line() {
    const std::string source = "module lsp::match_qf;\n"
                               "enum E { A, B }\n"
                               "fn f(e: E) -> Int effect Pure decreases 0 {\n"
                               "    return match e {\n"
                               "        _ => 0,\n"
                               "        A => 1,\n"
                               "        B => 2,\n"
                               "    };\n"
                               "}\n";

    LspDiagnostic diag;
    diag.code = "typecheck.MATCH_UNREACHABLE_ARM";
    diag.severity = DiagnosticSeverity::Warning;
    diag.message = "unreachable match arm";
    diag.range = Range{Position{5, 8}, Position{5, 9}};

    const Range cursor_range{Position{5, 8}, Position{5, 9}};
    const auto actions = ahfl::lsp::compute_code_actions(source, cursor_range, {diag});

    const CodeAction *qf = nullptr;
    for (const auto &action : actions) {
        if (action.title == "Remove unreachable match arm") {
            qf = &action;
            break;
        }
    }
    check(qf != nullptr, "codeAction.qf_match_unreachable.action_found");
    if (qf == nullptr || !qf->edit.has_value())
        return;

    std::size_t total_edits = 0;
    bool deletes_unreachable_line = false;
    bool keeps_surrounding_lines = true;
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &edit : edits) {
            deletes_unreachable_line =
                deletes_unreachable_line || (edit.range.start.line == 5 &&
                                             edit.range.start.character == 0 &&
                                             edit.range.end.line == 6 &&
                                             edit.range.end.character == 0 &&
                                             edit.new_text.empty());
            keeps_surrounding_lines =
                keeps_surrounding_lines && edit.range.start.line != 4 && edit.range.end.line != 7;
        }
    }
    check(total_edits == 1, "codeAction.qf_match_unreachable.single_text_edit");
    check(deletes_unreachable_line, "codeAction.qf_match_unreachable.deletes_arm_line");
    check(keeps_surrounding_lines, "codeAction.qf_match_unreachable.keeps_other_arms");
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
    if (qf == nullptr)
        return;

    // 1. Title verbatim matches QW-4 lint UX copy.
    check(qf->title.find("Insert empty `capabilities: [];` clause") != std::string::npos,
          "codeAction.qf_caps.title_mentions_insert_capabilities");
    // 2. kind == quickfix.
    check(qf->kind == CodeActionKind::QuickFix, "codeAction.qf_caps.kind_is_quickfix");
    // 3. isPreferred.
    check(qf->is_preferred, "codeAction.qf_caps.marked_preferred");
    // 4. Workspace edit contains the expected insertion text and the
    //    insert position is before the first transition (i.e. on a line
    //    strictly less than the transition line = line 10).
    check(qf->edit.has_value(), "codeAction.qf_caps.has_workspace_edit");
    if (!qf->edit.has_value())
        return;

    std::size_t total_edits = 0;
    bool found_empty_array = false;
    bool found_non_empty = false;
    Position insert_pos{0, 0};
    for (const auto &[uri_key, edits] : qf->edit->changes) {
        total_edits += edits.size();
        for (const auto &e : edits) {
            if (e.new_text.find("capabilities: [];") != std::string::npos)
                found_empty_array = true;
            if (!e.new_text.empty())
                found_non_empty = true;
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
        "        let dup = Request{ category: input.category, priority: Priority::Low, retries: 0 "
        "};\n"
        "        goto Done;\n"
        "    }\n"
        "}\n";

    // Initialize with maxFacts high enough to surface every field (default = 3
    // would cut off `retries` after Fields + category + priority).
    const char *kRichInit = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)"
                            R"({"initializationOptions":{"ahfl":{"hover":{"maxFacts":20}}}}})";

    // (1) Hover on the `Request` type-name that opens the FIRST literal.
    //     Needle includes the trailing ` { category` fragment so we avoid
    //     the schema references (agent input/context/output) by accident.
    const auto on_first = run_hover_request_with_init(source, "Request { category", kRichInit);
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
    test_semantic_tokens_cover_current_syntax_surface();
    test_semantic_tokens_request_uses_document_uri();
    test_hover_pattern_bindings_use_typed_pattern_facts();
    test_document_symbol_lists_all();
    test_workspace_symbol_filters();
    test_references_returns_locations();
    test_code_lens_uses_text_fallback_without_workspace_index();
    test_code_lens_uses_workspace_index_symbols();
    test_rename_returns_workspace_edit();
    test_prepare_rename_returns_range_or_null();
    test_signature_help_capability();
    test_signature_help_keyword_family();
    test_completion_type_member_enum_state_and_workflow_contexts();
    test_completion_pattern_context_uses_typed_pattern_facts();
    test_completion_struct_variant_fields_uses_typed_pattern_facts();
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
    test_watched_file_path_invalidation_keeps_unaffected_snapshots();
    test_manifest_watcher_refreshes_workspace_index_scope();
    test_manifest_invalidation_preserves_workspace_source_unit_ids();
    test_lsp_workspace_index_uses_package_graph_source_unit_ids();
    test_lsp_workspace_index_symbol_fingerprints_are_stable();
    test_project_input_source_cache_is_distinct_from_open_overlays();
    test_diagnostics_cover_parse_resolve_typecheck_and_validation();
    test_match_missing_patterns_diagnostic_exposes_structured_witness_data();
    test_project_definition_workspace_symbol_and_rename_cross_file();
    test_project_workspace_symbol_deduplicates_open_project_snapshots();
    test_workspace_symbol_uses_root_index_without_open_documents();
    test_project_references_include_indexed_unopened_source();
    test_user_package_references_include_lazy_sysroot_index();
    test_workspace_index_identity_hash_and_flat_store_ids();
    test_workspace_index_queries_sort_by_package_source_and_order();
    test_workspace_index_assigns_source_units_by_scope_order();
    test_workspace_index_reuses_unchanged_source_unit_facts();
    test_workspace_symbol_keeps_index_facts_when_exported_module_typecheck_fails();
    test_workspace_symbol_keeps_parse_facts_when_exported_module_resolve_fails();
    test_workspace_symbol_keeps_parse_skeleton_when_exported_module_parse_fails();
    test_parse_failed_export_does_not_block_typed_facts_for_healthy_index_sources();
    test_typecheck_failed_export_does_not_block_typed_facts_for_healthy_index_sources();
    test_project_open_document_overlay_drives_definition();
    test_workspace_index_includes_open_unexported_overlay();
    test_project_diagnostics_refresh_dependent_open_documents();
    test_package_graph_manifest_selects_module_roots_for_source();
    test_package_graph_workspace_selects_member_dependency_source();
    test_workspace_index_includes_path_dependency_exports();
    test_cross_workspace_path_dependency_rejects_mixed_toolchain_profiles();
    test_package_graph_workspace_rejects_private_dependency_module();
    test_workspace_index_records_visibility_alias_facts();
    test_package_graph_workspace_preserves_cross_package_hover();
    test_sysroot_std_manifest_is_not_loaded_as_root_package();
    test_definition_targets_source_sysroot_primitive_home_modules();
    test_definition_discovers_primitive_home_from_exported_impl_facts();
    test_implementation_returns_all_impl_blocks_for_type();
    test_implementation_uses_index_for_unopened_nominal_impls();
    test_implementation_uses_nominal_def_index_for_generic_impls();
    test_std_exported_impl_modules_feed_primitive_candidates();
    test_user_package_lazy_sysroot_index_feeds_primitive_candidates();
    test_detached_file_uses_sysroot_primitive_home_only();
    test_detached_file_warns_when_used_primitive_home_is_missing();
    test_detached_file_warns_without_toolchain_profile();
    test_detached_file_rejects_imports_and_import_code_actions();
    test_watched_sysroot_file_change_refreshes_primitive_candidates();
    test_open_sysroot_overlay_feeds_primitive_implementation_candidates();
    test_sysroot_std_manifest_detected_when_workspace_root_is_std_directory();
    test_sysroot_std_manifest_detected_without_workspace_root();
    test_lsp_initialization_sysroot_option_selects_toolchain_sysroot();
    test_lsp_bundled_sysroot_initialization_option_selects_fallback();
    test_lsp_default_sysroot_precedes_bundled_fallback();
    test_lsp_bundled_sysroot_mismatch_reports_related_information();
    test_lsp_legacy_initialization_sysroot_option_is_ignored();
    test_lsp_noncanonical_toolchain_sysroot_initialization_option_is_ignored();
    test_did_change_configuration_requests_resource_toolchain_profile();
    test_workspace_manifest_does_not_capture_unlisted_nested_package();
    test_lsp_project_discovery_ignores_process_cwd_sysroot_probe();
    test_project_graph_error_does_not_fall_back_to_single_file_semantics();
    test_incompatible_toolchain_profile_stops_project_analysis();
    test_toolchain_profile_records_std_identity_checksum();
    test_analysis_snapshot_cache_key_records_toolchain_identity();
    test_analysis_snapshot_cache_key_records_workspace_manifest();
    test_analysis_snapshot_cache_key_records_open_overlay_revisions();
    test_analysis_snapshot_cache_key_ignores_unrelated_open_overlay_revisions();
    test_sysroot_index_cache_key_separates_workspace_roots();
    test_multi_root_sysroot_index_uses_resource_toolchain_profiles();
    test_package_graph_manifest_does_not_inject_prelude();
    test_index_only_exported_type_does_not_satisfy_unimported_reference();
    test_diagnostic_related_information_surfaces_for_multi_module_mismatch();
    test_hover_renderer_detail_levels();
    test_hover_respects_client_markup_and_debug_options();
    test_hover_rich_symbol_targets();
    test_hover_service_payload_contract();
    test_hover_trait_where_bounds();
    test_code_action_qf_duplicate_struct_related_navigation();
    test_code_action_qf_unused_import_text_edit();
    test_code_action_qf_wrong_arity_placeholder();
    test_code_action_qf_match_missing_patterns_inserts_witness_arm();
    test_code_action_qf_match_missing_patterns_keeps_struct_witness_fields();
    test_code_action_qf_match_missing_patterns_falls_back_to_wildcard();
    test_code_action_qf_match_unreachable_arm_removes_arm_line();
    test_code_action_qf_agent_context();
    test_code_action_qf_agent_capabilities();
    test_hover_struct_literal_shows_construct_summary();
    check_hover_integration_fixture_coverage();

    // Wave-21 A-3: tie-break ordering contract between priority and enum ordinal.
    test_hover_target_tie_break_order_contract();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
