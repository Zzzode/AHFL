#include "tooling/registry/registry_handler.hpp"

#include "base/json/json_value.hpp"
#include "base/support/sha256.hpp"
#include "tooling/package/registry.hpp"
#include "tooling/package/source_archive.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::registry {

namespace {

using ahfl::package::RegistryIndexEntry;
using ahfl::package::SourceArchive;

constexpr std::string_view kPublishFormat = "ahfl.registry.publish_request.v1";
constexpr std::string_view kYankFormat = "ahfl.registry.yank_request.v1";
constexpr std::string_view kPublicApiSchema = "ahfl.public_api.v1";

struct RouteMatch {
    enum class Status {
        Matched,
        NotFound,
        MethodNotAllowed,
    };
    enum class Kind {
        PackageIndex,
        VersionIndex,
        SourceArchive,
        SourceArchivePayload,
        PublicApi,
        Publish,
        Yank,
    };
    Status status{Status::NotFound};
    Kind kind{};
    std::string package;
    std::string version;
};

[[nodiscard]] std::vector<std::string_view> split_path(std::string_view path) {
    std::vector<std::string_view> segments;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto segment = slash == std::string_view::npos ? path : path.substr(0, slash);
        if (!segment.empty()) {
            segments.push_back(segment);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
    return segments;
}

[[nodiscard]] bool is_route_method(RouteMatch::Kind kind, std::string_view method) {
    switch (kind) {
    case RouteMatch::Kind::Publish:
        return method == "PUT";
    case RouteMatch::Kind::Yank:
        return method == "POST";
    default:
        return method == "GET";
    }
}

[[nodiscard]] RouteMatch match_route(std::string_view method, std::string_view path) {
    auto segments = split_path(path);

    // Accept the protocol's /v1 prefix (and the /api/v1 spelling) optionally.
    if (!segments.empty() && segments[0] == "v1") {
        segments.erase(segments.begin());
    } else if (segments.size() >= 2 && segments[0] == "api" && segments[1] == "v1") {
        segments.erase(segments.begin(), segments.begin() + 2);
    }

    RouteMatch match;
    if (segments.size() == 3 && segments[0] == "packages" && segments[2] == "registry-index") {
        match.kind = RouteMatch::Kind::PackageIndex;
        match.package = std::string(segments[1]);
    } else if (segments.size() == 5 && segments[0] == "packages" && segments[2] == "versions") {
        match.package = std::string(segments[1]);
        match.version = std::string(segments[3]);
        const std::string_view tail = segments[4];
        if (tail == "registry-index") {
            match.kind = RouteMatch::Kind::VersionIndex;
        } else if (tail == "source-archive") {
            match.kind = RouteMatch::Kind::SourceArchive;
        } else if (tail == "source-archive.payload") {
            match.kind = RouteMatch::Kind::SourceArchivePayload;
        } else if (tail == "public-api") {
            match.kind = RouteMatch::Kind::PublicApi;
        } else if (tail == "publish") {
            match.kind = RouteMatch::Kind::Publish;
        } else if (tail == "yank") {
            match.kind = RouteMatch::Kind::Yank;
        } else {
            return match; // NotFound
        }
    } else {
        return match; // NotFound
    }

    match.status = is_route_method(match.kind, method) ? RouteMatch::Status::Matched
                                                       : RouteMatch::Status::MethodNotAllowed;
    return match;
}

[[nodiscard]] HttpResponse error_response(int status, std::string_view message) {
    std::string body = "{\"error\":\"";
    for (const char value : message) {
        if (value == '"' || value == '\\') {
            body.push_back('\\');
        }
        body.push_back(value);
    }
    body.append("\"}");
    return HttpResponse::json(status, std::move(body));
}

[[nodiscard]] std::optional<std::string> require_string_field(const ahfl::json::JsonValue &object,
                                                              std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto sv = field->as_string();
    if (!sv.has_value()) {
        return std::nullopt;
    }
    return std::string{*sv};
}

[[nodiscard]] std::string digest_text(std::string_view content) {
    return "sha256:" + ahfl::support::sha256_hex(content);
}

struct PublishEnvelope {
    RegistryIndexEntry entry;
    SourceArchive archive;
    std::string payload;
    std::string public_api_snapshot;
};

/// Parses and validates a publish request envelope. Returns nullopt and fills
/// `error` on any violation.
[[nodiscard]] std::optional<PublishEnvelope>
parse_publish_envelope(std::string_view body, const RouteMatch &route, std::string &error) {
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        error = "publish request body must be a JSON object";
        return std::nullopt;
    }
    const auto &root = **parsed;

    const auto format = require_string_field(root, "format_version");
    if (!format.has_value() || *format != kPublishFormat) {
        error = "publish request format_version must be '";
        error += kPublishFormat;
        error += "'";
        return std::nullopt;
    }

    const auto *registry_json = root.get("registry_index");
    if (registry_json == nullptr || !registry_json->is_object()) {
        error = "publish request registry_index must be a JSON object";
        return std::nullopt;
    }
    auto parsed_index =
        ahfl::package::parse_registry_index_entry(ahfl::json::serialize_json(*registry_json));
    if (parsed_index.has_errors() || !parsed_index.entry.has_value()) {
        error = "publish request registry_index is invalid: ";
        error += parsed_index.diagnostics.empty() ? "unknown error"
                                                   : parsed_index.diagnostics.front();
        return std::nullopt;
    }
    if (parsed_index.entry->yanked) {
        error = "publish request must not publish a yanked version";
        return std::nullopt;
    }

    const auto *archive_json = root.get("source_archive");
    if (archive_json == nullptr || !archive_json->is_object()) {
        error = "publish request source_archive must be a JSON object";
        return std::nullopt;
    }
    auto parsed_archive =
        ahfl::package::parse_source_archive_manifest(ahfl::json::serialize_json(*archive_json));
    if (parsed_archive.has_errors() || !parsed_archive.archive.has_value()) {
        error = "publish request source_archive is invalid: ";
        error += parsed_archive.diagnostics.empty() ? "unknown error"
                                                     : parsed_archive.diagnostics.front();
        return std::nullopt;
    }

    const auto payload = require_string_field(root, "source_archive_payload");
    if (!payload.has_value() || payload->empty()) {
        error = "publish request source_archive_payload must be a non-empty string";
        return std::nullopt;
    }
    const auto snapshot = require_string_field(root, "public_api_snapshot");
    if (!snapshot.has_value() || snapshot->empty()) {
        error = "publish request public_api_snapshot must be a non-empty string";
        return std::nullopt;
    }

    const auto &entry = *parsed_index.entry;
    const auto &archive = *parsed_archive.archive;

    if (entry.package != route.package) {
        error = "publish request package does not match URL";
        return std::nullopt;
    }
    if (entry.version != route.version) {
        error = "publish request version does not match URL";
        return std::nullopt;
    }
    if (archive.package_name != entry.package) {
        error = "source archive package does not match registry index";
        return std::nullopt;
    }
    if (archive.archive_sha256 != entry.source_archive_sha256) {
        error = "source archive digest does not match registry index";
        return std::nullopt;
    }
    if (archive.manifest_sha256 != entry.manifest_sha256) {
        error = "source archive manifest digest does not match registry index";
        return std::nullopt;
    }
    if (digest_text(*payload) != entry.source_archive_sha256) {
        error = "source archive payload digest does not match registry index";
        return std::nullopt;
    }
    if (digest_text(*snapshot) != entry.public_api_sha256) {
        error = "public API snapshot digest does not match registry index";
        return std::nullopt;
    }

    auto parsed_snapshot = ahfl::json::parse_json(*snapshot);
    if (!parsed_snapshot.has_value() || !*parsed_snapshot || !(*parsed_snapshot)->is_object()) {
        error = "public API snapshot must be a JSON object";
        return std::nullopt;
    }
    const auto schema = require_string_field(**parsed_snapshot, "schema");
    if (!schema.has_value() || *schema != kPublicApiSchema) {
        error = "public API snapshot schema must be '";
        error += kPublicApiSchema;
        error += "'";
        return std::nullopt;
    }

    return PublishEnvelope{
        .entry = std::move(*parsed_index.entry),
        .archive = std::move(*parsed_archive.archive),
        .payload = std::string(*payload),
        .public_api_snapshot = std::string(*snapshot),
    };
}

[[nodiscard]] std::string
serialize_package_index(std::string_view package,
                        const std::vector<RegistryIndexEntry> &versions) {
    auto root = ahfl::json::JsonValue::make_object();
    root->set("format_version",
              ahfl::json::JsonValue::make_string("ahfl.registry.package_index.v1"));
    root->set("package", ahfl::json::JsonValue::make_string(std::string(package)));
    auto array = ahfl::json::JsonValue::make_array();
    for (const auto &entry : versions) {
        auto parsed = ahfl::json::parse_json(
            ahfl::package::serialize_registry_index_entry(entry));
        if (parsed.has_value() && *parsed) {
            array->push(std::move(*parsed));
        }
    }
    root->set("versions", std::move(array));
    return ahfl::json::serialize_json(*root);
}

} // namespace

HttpResponse RegistryRequestHandler::handle(const HttpRequest &request) {
    const auto route = match_route(request.method, request.path());
    if (route.status == RouteMatch::Status::NotFound) {
        return error_response(404, "no such route");
    }
    if (route.status == RouteMatch::Status::MethodNotAllowed) {
        auto response = error_response(405, "method not allowed");
        response.headers.emplace_back("Allow", is_route_method(route.kind, "PUT")
                                                    ? "PUT"
                                                : is_route_method(route.kind, "POST") ? "POST"
                                                                                      : "GET");
        return response;
    }

    switch (route.kind) {
    case RouteMatch::Kind::PackageIndex: {
        auto versions = store_.list_versions(route.package);
        if (!versions.has_value()) {
            return error_response(404, "package not found");
        }
        return HttpResponse::json(200, serialize_package_index(route.package, *versions));
    }
    case RouteMatch::Kind::VersionIndex: {
        auto stored = store_.load(route.package, route.version);
        if (!stored.has_value()) {
            return stored.error() == LoadError::NotFound
                       ? error_response(404, "package version not found")
                       : error_response(500, "stored package version is corrupt");
        }
        return HttpResponse::json(
            200, ahfl::package::serialize_registry_index_entry(stored->entry));
    }
    case RouteMatch::Kind::SourceArchive: {
        auto stored = store_.load(route.package, route.version);
        if (!stored.has_value()) {
            return stored.error() == LoadError::NotFound
                       ? error_response(404, "package version not found")
                       : error_response(500, "stored package version is corrupt");
        }
        return HttpResponse::json(200, stored->source_archive_manifest);
    }
    case RouteMatch::Kind::SourceArchivePayload: {
        auto stored = store_.load(route.package, route.version);
        if (!stored.has_value()) {
            return stored.error() == LoadError::NotFound
                       ? error_response(404, "package version not found")
                       : error_response(500, "stored package version is corrupt");
        }
        return HttpResponse::text(200, "application/octet-stream", stored->source_archive_payload);
    }
    case RouteMatch::Kind::PublicApi: {
        auto stored = store_.load(route.package, route.version);
        if (!stored.has_value()) {
            return stored.error() == LoadError::NotFound
                       ? error_response(404, "package version not found")
                       : error_response(500, "stored package version is corrupt");
        }
        return HttpResponse::json(200, stored->public_api_snapshot);
    }
    case RouteMatch::Kind::Publish: {
        std::string error;
        auto envelope = parse_publish_envelope(request.body, route, error);
        if (!envelope.has_value()) {
            return error_response(400, error);
        }
        auto result = store_.publish(envelope->entry,
                                     ahfl::package::serialize_source_archive_manifest(
                                         envelope->archive),
                                     std::move(envelope->payload),
                                     std::move(envelope->public_api_snapshot));
        if (!result.has_value()) {
            return result.error() == PublishError::Conflict
                       ? error_response(
                             409, "package version already exists with different content")
                       : error_response(500, "failed to persist package version");
        }
        return HttpResponse::json(
            200, ahfl::package::serialize_registry_index_entry(*result));
    }
    case RouteMatch::Kind::Yank: {
        auto parsed = ahfl::json::parse_json(request.body);
        if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
            return error_response(400, "yank request body must be a JSON object");
        }
        const auto &root = **parsed;
        const auto format = require_string_field(root, "format_version");
        if (!format.has_value() || *format != kYankFormat) {
            return error_response(400, "yank request format_version must be 'ahfl.registry.yank_request.v1'");
        }
        const auto package = require_string_field(root, "package");
        const auto version = require_string_field(root, "version");
        if (!package.has_value() || *package != route.package || !version.has_value() ||
            *version != route.version) {
            return error_response(400, "yank request package/version does not match URL");
        }
        auto yanked = store_.yank(route.package, route.version);
        if (!yanked.has_value()) {
            return error_response(409, "package version is not published");
        }
        return HttpResponse::json(
            200, ahfl::package::serialize_registry_index_entry(*yanked));
    }
    }
    return error_response(500, "unhandled route");
}

} // namespace ahfl::registry
