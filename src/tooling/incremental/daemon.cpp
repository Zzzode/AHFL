#include "tooling/incremental/daemon.hpp"

#include "base/json/json_value.hpp"
#include "compiler/project_discovery/discovery.hpp"
#include "tooling/incremental/dependency_graph.hpp"
#include "tooling/incremental/incremental_compiler.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::incremental {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kFileSchemePrefix = "file://";

// Extracts a filesystem path from a file:// URI. Returns nullopt when the
// URI does not carry the file scheme. Percent-encoding and file-host forms
// (file://localhost/...) are not handled in this slice.
[[nodiscard]] std::optional<std::string> path_from_uri(std::string_view uri) {
    if (uri.size() < kFileSchemePrefix.size() ||
        uri.substr(0, kFileSchemePrefix.size()) != kFileSchemePrefix) {
        return std::nullopt;
    }
    std::string path(uri.substr(kFileSchemePrefix.size()));
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

void emit_error(std::ostream &output, std::string_view message) {
    auto object = json::JsonValue::make_object();
    object->set("type", json::JsonValue::make_string("error"));
    object->set("message", json::JsonValue::make_string(std::string{message}));
    output << json::serialize_json(*object) << '\n';
}

void emit_stats(std::ostream &output, const IncrementalStats &stats) {
    auto object = json::JsonValue::make_object();
    object->set("type", json::JsonValue::make_string("stats"));
    object->set("modules_checked",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.modules_checked)));
    object->set("modules_recompiled",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.modules_recompiled)));
    object->set("cache_hits",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.cache_hits)));
    object->set("cache_misses",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.cache_misses)));
    object->set("persistent_cache_hits",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.persistent_cache_hits)));
    object->set("fingerprint_skipped",
                json::JsonValue::make_int(static_cast<std::int64_t>(stats.fingerprint_skipped)));
    output << json::serialize_json(*object) << '\n';
}

// Manifest edits restructure the project: every cache entry is keyed to the
// old graph and must be dropped. Re-discovery of the import graph is a
// follow-up slice; for now the daemon conservatively invalidates everything.
[[nodiscard]] bool is_manifest_path(const fs::path &path) {
    const auto name = path.filename().string();
    return name == "ahfl.toml" || name == "ahfl.workspace.toml";
}

} // namespace

void run_daemon(IncrementalCompiler &compiler,
                DependencyGraph &graph,
                std::istream &input,
                std::ostream &output) {
    std::string line;
    while (std::getline(input, line)) {
        // Tolerate CRLF-terminated streams (Windows LSP clients).
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const auto parsed = json::parse_json(line);
        if (!parsed.has_value() || !parsed->get() || !(*parsed)->is_object()) {
            emit_error(output, "malformed JSON line");
            continue;
        }
        const auto &notification = *(*parsed);

        const auto *type_field = notification.get("type");
        if (type_field == nullptr) {
            emit_error(output, "missing 'type' field");
            continue;
        }
        const auto type = type_field->as_string();
        if (!type.has_value()) {
            emit_error(output, "'type' field must be a string");
            continue;
        }

        if (*type == "didClose") {
            // Closing a buffer says nothing about the file on disk; never
            // invalidate on didClose (RFC 0016 daemon contract).
            continue;
        }

        if (*type != "didChange" && *type != "didSave") {
            emit_error(output, "unknown type: " + std::string{*type});
            continue;
        }

        const auto *uri_field = notification.get("uri");
        if (uri_field == nullptr) {
            emit_error(output, "missing 'uri' field");
            continue;
        }
        const auto uri = uri_field->as_string();
        if (!uri.has_value()) {
            emit_error(output, "'uri' field must be a string");
            continue;
        }

        const auto raw_path = path_from_uri(*uri);
        if (!raw_path.has_value()) {
            emit_error(output, "unsupported uri (expected file://): " + std::string{*uri});
            continue;
        }

        // Normalize to the absolute, weakly-canonical key space used by the
        // discovered import graph and the IncrementalCompiler.
        const auto normalized =
            ahfl::project_discovery::normalize_project_path(fs::path(*raw_path));
        const auto path = normalized.string();

        if (is_manifest_path(normalized)) {
            compiler.invalidate_all();
            compiler.reset_stats();
            emit_stats(output, compiler.stats());
            continue;
        }

        // Register previously unknown modules (newly created files) so the
        // compiler's topological pass actually visits them. The compiler
        // observes the mutation because it holds the graph by reference.
        if (!graph.has_module(path)) {
            ModuleNode node;
            node.module_path = path;
            graph.add_module(std::move(node));
        }

        (void)compiler.compile_changed({path});
        emit_stats(output, compiler.stats());
    }
}

} // namespace ahfl::incremental
