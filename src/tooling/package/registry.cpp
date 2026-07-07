#include "tooling/package/registry.hpp"

#include "base/json/json_value.hpp"
#include "base/support/http.hpp"
#include "base/support/sha256.hpp"
#include "tooling/package/resolver.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::package {

namespace {

namespace fs = std::filesystem;

fs::path cache_directory() {
    const char *env = std::getenv("AHFL_PACKAGE_CACHE");
    if (env != nullptr && *env != '\0')
        return fs::path(env);
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return fs::path(home) / ".ahfl" / "packages";
    }
    return fs::path("/tmp") / ".ahfl" / "packages";
}

std::string default_registry_base_url(std::string base_url) {
    if (base_url != "https://registry.ahfl.io/v1") {
        return base_url;
    }
    const char *env = std::getenv("AHFL_REGISTRY_URL");
    if (env != nullptr && *env != '\0') {
        return std::string{env};
    }
    return base_url;
}

[[nodiscard]] bool is_cache_key_passthrough(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.';
}

[[nodiscard]] std::string cache_file_name(std::string_view cache_key) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string file_name;
    file_name.reserve(cache_key.size());
    for (const unsigned char value : cache_key) {
        if (is_cache_key_passthrough(value)) {
            file_name.push_back(static_cast<char>(value));
            continue;
        }
        file_name.push_back('_');
        file_name.push_back(kHex[(value >> 4U) & 0x0FU]);
        file_name.push_back(kHex[value & 0x0FU]);
    }
    return file_name;
}

fs::path cache_file_path(std::string_view cache_key) {
    auto dir = cache_directory();
    return dir / (cache_file_name(cache_key) + ".json");
}

class HttpRegistryTransport final : public RegistryTransport {
  public:
    [[nodiscard]] RegistryHttpResponse get(std::string_view url, int timeout_seconds) override {
        return request("GET", url, {}, timeout_seconds);
    }

    [[nodiscard]] RegistryHttpResponse request(std::string_view method,
                                               std::string_view url,
                                               std::string_view body,
                                               int timeout_seconds) override {
        ahfl::support::HttpRequest request;
        request.method = std::string(method);
        request.url = std::string(url);
        request.timeout_seconds = timeout_seconds;
        if (!body.empty() || method != "GET") {
            request.body = std::string(body);
            request.headers.emplace_back("Content-Type", "application/json");
        }

        const auto response = ahfl::support::execute_http(request);
        return RegistryHttpResponse{
            .status_code = response.status_code,
            .body = response.body,
            .success = response.is_success(),
        };
    }
};

class FilesystemRegistryCache final : public RegistryCache {
  public:
    void save(std::string_view cache_key, std::string_view json_data) override {
        auto dir = cache_directory();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            return;
        }

        std::ofstream ofs(cache_file_path(cache_key), std::ios::binary | std::ios::trunc);
        if (ofs) {
            ofs << json_data;
        }
    }

    [[nodiscard]] std::optional<std::string> load(std::string_view cache_key) override {
        auto path = cache_file_path(cache_key);
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            return std::nullopt;
        }
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
};

std::optional<std::string> object_string(const ahfl::json::JsonValue &object,
                                         std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::string(*value);
}

[[nodiscard]] bool has_key(std::initializer_list<std::string_view> keys,
                           std::string_view candidate) {
    return std::find(keys.begin(), keys.end(), candidate) != keys.end();
}

void reject_unknown_fields(const ahfl::json::JsonValue &object,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view context,
                           std::vector<std::string> &diagnostics) {
    for (const auto &[key, _] : object.object_fields) {
        if (!has_key(allowed, key)) {
            diagnostics.push_back("unsupported registry index field '" + std::string{context} +
                                  key + "'");
        }
    }
}

[[nodiscard]] bool is_lower_hex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool is_sha256_digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix) || value.size() != prefix.size() + 64) {
        return false;
    }
    return std::all_of(
        value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(), is_lower_hex);
}

[[nodiscard]] bool is_semver(std::string_view value) {
    std::array<std::string_view, 3> parts{};
    std::size_t part_index = 0;
    std::size_t start = 0;
    while (start <= value.size() && part_index < parts.size()) {
        const auto separator = value.find('.', start);
        const auto end = separator == std::string_view::npos ? value.size() : separator;
        parts[part_index++] = value.substr(start, end - start);
        if (part_index == parts.size() && separator != std::string_view::npos) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    if (part_index != parts.size()) {
        return false;
    }
    return std::all_of(parts.begin(), parts.end(), [](std::string_view part) {
        return !part.empty() && std::all_of(part.begin(), part.end(), [](char item) {
            return item >= '0' && item <= '9';
        });
    });
}

[[nodiscard]] bool is_version_requirement(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    if (value.front() == '^' || value.front() == '~') {
        value.remove_prefix(1);
    }
    return is_semver(value);
}

[[nodiscard]] std::optional<std::string> required_string(const ahfl::json::JsonValue &object,
                                                         std::string_view key,
                                                         std::string_view context,
                                                         std::vector<std::string> &diagnostics) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        diagnostics.push_back("registry index is missing required field '" + std::string{context} +
                              std::string{key} + "'");
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value() || value->empty()) {
        diagnostics.push_back("registry index field '" + std::string{context} + std::string{key} +
                              "' must be a non-empty string");
        return std::nullopt;
    }
    return std::string{*value};
}

[[nodiscard]] std::optional<bool> required_bool(const ahfl::json::JsonValue &object,
                                                std::string_view key,
                                                std::string_view context,
                                                std::vector<std::string> &diagnostics) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        diagnostics.push_back("registry index is missing required field '" + std::string{context} +
                              std::string{key} + "'");
        return std::nullopt;
    }
    auto value = field->as_bool();
    if (!value.has_value()) {
        diagnostics.push_back("registry index field '" + std::string{context} + std::string{key} +
                              "' must be a boolean");
        return std::nullopt;
    }
    return *value;
}

[[nodiscard]] bool is_valid_package_name(std::string_view value) {
    if (value.empty() || value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    bool previous_dash = false;
    for (const char item : value) {
        const bool lower = item >= 'a' && item <= 'z';
        const bool digit = item >= '0' && item <= '9';
        if (lower || digit) {
            previous_dash = false;
            continue;
        }
        if (item == '-' && !previous_dash) {
            previous_dash = true;
            continue;
        }
        return false;
    }
    return value.back() != '-';
}

void validate_digest_field(std::string_view value,
                           std::string_view field,
                           std::vector<std::string> &diagnostics) {
    if (!is_sha256_digest(value)) {
        diagnostics.push_back("registry index field '" + std::string{field} +
                              "' must be sha256:<64 lowercase hex digits>");
    }
}

[[nodiscard]] std::optional<RegistryDependencyMetadata>
parse_registry_dependency(const ahfl::json::JsonValue &dependency,
                          std::size_t index,
                          std::vector<std::string> &diagnostics) {
    const auto context = "dependencies[" + std::to_string(index) + "].";
    if (!dependency.is_object()) {
        diagnostics.push_back("registry index field '" + context + "' must be an object");
        return std::nullopt;
    }
    reject_unknown_fields(
        dependency, {"name", "source", "registry", "version"}, context, diagnostics);

    RegistryDependencyMetadata parsed;
    if (auto name = required_string(dependency, "name", context, diagnostics); name.has_value()) {
        parsed.name = std::move(*name);
        if (!is_valid_package_name(parsed.name)) {
            diagnostics.push_back("registry dependency name must be kebab-case");
        }
    }
    if (auto source = required_string(dependency, "source", context, diagnostics);
        source.has_value()) {
        parsed.source = std::move(*source);
    }
    if (auto registry = object_string(dependency, "registry"); registry.has_value()) {
        if (registry->empty()) {
            diagnostics.push_back("registry dependency field 'registry' must not be empty");
        } else {
            parsed.registry = std::move(*registry);
        }
    }
    if (auto version = object_string(dependency, "version"); version.has_value()) {
        parsed.version_requirement = std::move(*version);
        if (!is_version_requirement(*parsed.version_requirement)) {
            diagnostics.push_back(
                "registry dependency version must be an exact, caret, or tilde semantic version "
                "requirement");
        }
    }

    if (parsed.source == "sysroot") {
        if (parsed.name != "std") {
            diagnostics.push_back("sysroot registry dependency name must be 'std'");
        }
        if (parsed.registry.has_value() || parsed.version_requirement.has_value()) {
            diagnostics.push_back(
                "sysroot registry dependency must not declare registry or version");
        }
    } else if (parsed.source == "registry") {
        if (!parsed.version_requirement.has_value()) {
            diagnostics.push_back("registry dependency is missing required field 'version'");
        }
    } else if (!parsed.source.empty()) {
        diagnostics.push_back("unsupported registry dependency source '" + parsed.source + "'");
    }

    return parsed;
}

[[nodiscard]] std::vector<RegistryDependencyMetadata>
parse_registry_dependencies(const ahfl::json::JsonValue &object,
                            std::vector<std::string> &diagnostics) {
    const auto *dependencies = object.get("dependencies");
    if (dependencies == nullptr) {
        diagnostics.push_back("registry index is missing required field 'dependencies'");
        return {};
    }
    if (!dependencies->is_array()) {
        diagnostics.push_back("registry index field 'dependencies' must be an array");
        return {};
    }

    std::vector<RegistryDependencyMetadata> parsed;
    parsed.reserve(dependencies->array_items.size());
    for (std::size_t index = 0; index < dependencies->array_items.size(); ++index) {
        auto dependency =
            parse_registry_dependency(*dependencies->array_items[index], index, diagnostics);
        if (dependency.has_value()) {
            parsed.push_back(std::move(*dependency));
        }
    }
    return parsed;
}

[[nodiscard]] bool should_try_cache_after_registry_response(const RegistryHttpResponse &response) {
    return !response.success && response.status_code != 404;
}

struct RegistryResourceFetch {
    std::optional<std::string> body;
    std::optional<RegistryError> error;
    std::string message;
};

[[nodiscard]] RegistryResourceFetch fetch_registry_resource(RegistryTransport &transport,
                                                            RegistryCache &cache,
                                                            const std::string &url,
                                                            std::string_view cache_key,
                                                            std::string_view label) {
    auto http_result = transport.get(url, 10);
    if (http_result.success && !http_result.body.empty()) {
        return RegistryResourceFetch{.body = std::move(http_result.body)};
    }
    if (http_result.success) {
        return RegistryResourceFetch{.error = RegistryError::InvalidResponse,
                                     .message = "empty registry artifact response for " +
                                                std::string{label}};
    }
    if (http_result.status_code == 404) {
        return RegistryResourceFetch{.error = RegistryError::NotFound,
                                     .message =
                                         "registry artifact not found: " + std::string{label}};
    }
    if (should_try_cache_after_registry_response(http_result)) {
        auto cached = cache.load(cache_key);
        if (cached.has_value() && !cached->empty()) {
            return RegistryResourceFetch{.body = std::move(*cached)};
        }
    }
    return RegistryResourceFetch{.error = RegistryError::NetworkError,
                                 .message = "registry artifact unavailable: " + std::string{label}};
}

[[nodiscard]] std::string version_base_url(std::string_view base_url,
                                           std::string_view package_name,
                                           std::string_view version) {
    return std::string{base_url} + "/packages/" + std::string{package_name} + "/versions/" +
           std::string{version};
}

[[nodiscard]] std::string registry_index_cache_key(std::string_view package_name,
                                                   std::string_view version) {
    return "registry-index:" + std::string{package_name} + ":" + std::string{version};
}

[[nodiscard]] std::string registry_package_index_cache_key(std::string_view package_name) {
    return "registry-package-index:" + std::string{package_name};
}

[[nodiscard]] std::string source_archive_manifest_cache_key(std::string_view digest) {
    return "source-archive-manifest:" + std::string{digest};
}

[[nodiscard]] std::string source_archive_payload_cache_key(std::string_view digest) {
    return "source-archive-payload:" + std::string{digest};
}

[[nodiscard]] std::string public_api_snapshot_cache_key(std::string_view digest) {
    return "public-api-snapshot:" + std::string{digest};
}

[[nodiscard]] RegistryPackageArtifactResult artifact_error(RegistryError error,
                                                           std::vector<std::string> diagnostics) {
    return RegistryPackageArtifactResult{.error = error, .diagnostics = std::move(diagnostics)};
}

[[nodiscard]] RegistryPackageIndexResult index_error(RegistryError error,
                                                     std::vector<std::string> diagnostics) {
    return RegistryPackageIndexResult{.error = error, .diagnostics = std::move(diagnostics)};
}

[[nodiscard]] RegistryPublicApiSnapshotResult
public_api_snapshot_error(RegistryError error, std::vector<std::string> diagnostics) {
    return RegistryPublicApiSnapshotResult{.error = error, .diagnostics = std::move(diagnostics)};
}

[[nodiscard]] std::vector<std::string>
validate_public_api_snapshot(std::string_view snapshot, std::string_view expected_digest);

[[nodiscard]] RegistryMutationResult mutation_error(RegistryError error,
                                                    std::vector<std::string> diagnostics) {
    return RegistryMutationResult{.error = error, .diagnostics = std::move(diagnostics)};
}

[[nodiscard]] RegistryError mutation_error_from_response(const RegistryHttpResponse &response) {
    if (response.status_code == 401 || response.status_code == 403) {
        return RegistryError::Unauthorized;
    }
    if (response.status_code == 404) {
        return RegistryError::NotFound;
    }
    if (response.status_code == 409) {
        return RegistryError::Conflict;
    }
    return RegistryError::NetworkError;
}

[[nodiscard]] bool same_dependencies(const std::vector<RegistryDependencyMetadata> &lhs,
                                     const std::vector<RegistryDependencyMetadata> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].name != rhs[i].name || lhs[i].source != rhs[i].source ||
            lhs[i].registry != rhs[i].registry ||
            lhs[i].version_requirement != rhs[i].version_requirement) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::unique_ptr<ahfl::json::JsonValue>>
parse_json_object_for_envelope(std::string_view payload,
                               std::string_view label,
                               std::vector<std::string> &diagnostics) {
    auto parsed = ahfl::json::parse_json(payload);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        diagnostics.push_back(std::string{label} + " must serialize as a JSON object");
        return std::nullopt;
    }
    return std::move(*parsed);
}

[[nodiscard]] std::string
serialize_publish_request_envelope(const RegistryPublishPackageRequest &request,
                                   std::vector<std::string> &diagnostics) {
    auto registry_json = parse_json_object_for_envelope(
        serialize_registry_index_entry(request.registry), "registry index", diagnostics);
    auto archive_json = parse_json_object_for_envelope(
        serialize_source_archive_manifest(request.archive), "source archive manifest", diagnostics);
    if (!registry_json.has_value() || !archive_json.has_value()) {
        return {};
    }

    auto root = ahfl::json::JsonValue::make_object();
    root->set("format_version",
              ahfl::json::JsonValue::make_string("ahfl.registry.publish_request.v1"));
    root->set("registry_index", std::move(*registry_json));
    root->set("source_archive", std::move(*archive_json));
    root->set("source_archive_payload",
              ahfl::json::JsonValue::make_string(request.archive.payload));
    root->set("public_api_snapshot",
              ahfl::json::JsonValue::make_string(request.public_api_snapshot));
    return ahfl::json::serialize_json(*root);
}

[[nodiscard]] std::string serialize_yank_request_envelope(std::string_view package_name,
                                                          std::string_view version,
                                                          std::string_view registry_id,
                                                          std::string_view reason) {
    auto root = ahfl::json::JsonValue::make_object();
    root->set("format_version",
              ahfl::json::JsonValue::make_string("ahfl.registry.yank_request.v1"));
    root->set("registry_id", ahfl::json::JsonValue::make_string(std::string{registry_id}));
    root->set("package", ahfl::json::JsonValue::make_string(std::string{package_name}));
    root->set("version", ahfl::json::JsonValue::make_string(std::string{version}));
    root->set("reason", ahfl::json::JsonValue::make_string(std::string{reason}));
    return ahfl::json::serialize_json(*root);
}

[[nodiscard]] RegistryMutationResult
parse_mutation_registry_response(const RegistryHttpResponse &response,
                                 std::string_view label) {
    if (!response.success) {
        return mutation_error(mutation_error_from_response(response),
                              {std::string{"registry "} + std::string{label} +
                               " request failed with HTTP status " +
                               std::to_string(response.status_code)});
    }
    if (response.body.empty()) {
        return mutation_error(RegistryError::InvalidResponse,
                              {std::string{"registry "} + std::string{label} +
                               " response must contain registry index metadata"});
    }
    auto parsed = parse_registry_index_entry(response.body);
    if (parsed.has_errors() || !parsed.entry.has_value()) {
        return mutation_error(RegistryError::InvalidResponse, std::move(parsed.diagnostics));
    }
    return RegistryMutationResult{.entry = std::move(*parsed.entry)};
}

[[nodiscard]] std::vector<std::string>
validate_publish_request(const RegistryPublishPackageRequest &request) {
    std::vector<std::string> diagnostics;
    auto parsed_registry = parse_registry_index_entry(serialize_registry_index_entry(request.registry));
    if (parsed_registry.has_errors()) {
        diagnostics.insert(diagnostics.end(),
                           parsed_registry.diagnostics.begin(),
                           parsed_registry.diagnostics.end());
    }
    if (request.registry.yanked) {
        diagnostics.push_back("package publish request must not publish a yanked version");
    }
    if (request.archive.package_name != request.registry.package) {
        diagnostics.push_back("source archive package does not match registry index package");
    }
    if (request.archive.archive_sha256 != request.registry.source_archive_sha256) {
        diagnostics.push_back("source archive digest does not match registry index");
    }
    if (request.archive.manifest_sha256 != request.registry.manifest_sha256) {
        diagnostics.push_back("source archive manifest digest does not match registry index");
    }
    auto public_api_diagnostics =
        validate_public_api_snapshot(request.public_api_snapshot, request.registry.public_api_sha256);
    diagnostics.insert(diagnostics.end(),
                       public_api_diagnostics.begin(),
                       public_api_diagnostics.end());
    return diagnostics;
}

[[nodiscard]] std::vector<std::string>
validate_public_api_snapshot(std::string_view snapshot, std::string_view expected_digest) {
    std::vector<std::string> diagnostics;
    if (!is_sha256_digest(expected_digest)) {
        diagnostics.push_back("public API snapshot expected digest is not sha256:<hex64>");
    }

    const auto actual_digest = "sha256:" + ahfl::support::sha256_hex(snapshot);
    if (actual_digest != expected_digest) {
        diagnostics.push_back("public API snapshot digest does not match public_api_sha256");
    }

    auto parsed = ahfl::json::parse_json(snapshot);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        diagnostics.push_back("public API snapshot must be a JSON object");
        return diagnostics;
    }
    const auto schema = object_string(**parsed, "schema");
    if (!schema.has_value() || *schema != "ahfl.public_api.v1") {
        diagnostics.push_back("public API snapshot schema must be 'ahfl.public_api.v1'");
    }
    return diagnostics;
}

[[nodiscard]] std::string percent_encode_query_value(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        const bool unreserved = (character >= 'A' && character <= 'Z') ||
                                (character >= 'a' && character <= 'z') ||
                                (character >= '0' && character <= '9') || character == '-' ||
                                character == '.' || character == '_' || character == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(character));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(character >> 4U) & 0x0FU]);
        encoded.push_back(kHex[character & 0x0FU]);
    }
    return encoded;
}

std::optional<PackageVersion> parse_version_object(const ahfl::json::JsonValue &version_object,
                                                   const std::string &package_name,
                                                   const std::string *forced_version = nullptr) {
    if (!version_object.is_object()) {
        return std::nullopt;
    }

    auto version = forced_version != nullptr ? std::optional<std::string>(*forced_version)
                                             : object_string(version_object, "version");
    if (!version.has_value() || version->empty()) {
        return std::nullopt;
    }

    PackageVersion parsed;
    parsed.name = package_name;
    parsed.version = *version;
    if (auto description = object_string(version_object, "description"); description.has_value()) {
        parsed.description = *description;
    }
    if (auto checksum = object_string(version_object, "checksum"); checksum.has_value()) {
        parsed.checksum = *checksum;
    } else if (auto sha256 = object_string(version_object, "sha256"); sha256.has_value()) {
        parsed.checksum = "sha256:" + *sha256;
    }

    if (const auto *authors = version_object.get("authors");
        authors != nullptr && authors->is_array()) {
        for (const auto &author : authors->array_items) {
            if (auto value = author->as_string(); value.has_value()) {
                parsed.authors.push_back(std::string(*value));
            } else {
                return std::nullopt;
            }
        }
    }
    return parsed;
}

std::optional<PackageMetadata> parse_metadata_json(const std::string &json,
                                                   const std::string &package_name) {
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }

    PackageMetadata metadata;
    metadata.name = package_name;
    if (auto latest = object_string(**parsed, "latest_version"); latest.has_value()) {
        metadata.latest_version = *latest;
    }

    const auto *versions = (*parsed)->get("versions");
    if (versions == nullptr || !versions->is_array()) {
        return std::nullopt;
    }
    for (const auto &version : versions->array_items) {
        auto parsed_version = parse_version_object(*version, package_name);
        if (!parsed_version.has_value()) {
            return std::nullopt;
        }
        metadata.versions.push_back(std::move(*parsed_version));
    }

    if (metadata.latest_version.empty() && !metadata.versions.empty()) {
        metadata.latest_version = metadata.versions.back().version;
    }
    if (metadata.versions.empty() || metadata.latest_version.empty()) {
        return std::nullopt;
    }

    return metadata;
}

std::optional<PackageVersion> parse_single_version_json(const std::string &json,
                                                        const std::string &package_name,
                                                        const std::string &version) {
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::nullopt;
    }
    auto package_version = parse_version_object(**parsed, package_name);
    if (!package_version.has_value() || package_version->version != version) {
        return std::nullopt;
    }
    return package_version;
}

std::vector<std::string> parse_search_json(const std::string &json) {
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || !*parsed) {
        return {};
    }

    const ahfl::json::JsonValue *items = parsed->get();
    if (items->is_object()) {
        items = items->get("results");
    }
    if (items == nullptr || !items->is_array()) {
        return {};
    }

    std::vector<std::string> results;
    for (const auto &item : items->array_items) {
        if (auto name = item->as_string(); name.has_value()) {
            results.push_back(std::string(*name));
            continue;
        }
        if (item->is_object()) {
            auto name = object_string(*item, "name");
            if (name.has_value()) {
                results.push_back(*name);
                continue;
            }
        }
        return {};
    }
    return results;
}

} // namespace

RegistryIndexParseResult parse_registry_index_entry(std::string_view input) {
    RegistryIndexParseResult result;
    auto parsed = ahfl::json::parse_json(input);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        result.diagnostics.push_back("registry index entry must be valid JSON object");
        return result;
    }

    const auto &root = **parsed;
    reject_unknown_fields(root,
                          {"format_version",
                           "registry_id",
                           "package",
                           "version",
                           "yanked",
                           "source_archive_sha256",
                           "manifest_sha256",
                           "public_api_sha256",
                           "dependencies"},
                          "",
                          result.diagnostics);

    RegistryIndexEntry entry;
    if (auto format = required_string(root, "format_version", "", result.diagnostics);
        format.has_value()) {
        entry.format_version = std::move(*format);
        if (entry.format_version != "ahfl.registry.index.v1") {
            result.diagnostics.push_back("registry index field 'format_version' must be "
                                         "'ahfl.registry.index.v1'");
        }
    }
    if (auto registry_id = required_string(root, "registry_id", "", result.diagnostics);
        registry_id.has_value()) {
        entry.registry_id = std::move(*registry_id);
    }
    if (auto package = required_string(root, "package", "", result.diagnostics);
        package.has_value()) {
        entry.package = std::move(*package);
        if (!is_valid_package_name(entry.package)) {
            result.diagnostics.push_back("registry index field 'package' must be kebab-case");
        }
    }
    if (auto version = required_string(root, "version", "", result.diagnostics);
        version.has_value()) {
        entry.version = std::move(*version);
        if (!is_semver(entry.version)) {
            result.diagnostics.push_back(
                "registry index field 'version' must be an exact semantic version");
        }
    }
    if (auto yanked = required_bool(root, "yanked", "", result.diagnostics); yanked.has_value()) {
        entry.yanked = *yanked;
    }
    if (auto digest = required_string(root, "source_archive_sha256", "", result.diagnostics);
        digest.has_value()) {
        entry.source_archive_sha256 = std::move(*digest);
        validate_digest_field(
            entry.source_archive_sha256, "source_archive_sha256", result.diagnostics);
    }
    if (auto digest = required_string(root, "manifest_sha256", "", result.diagnostics);
        digest.has_value()) {
        entry.manifest_sha256 = std::move(*digest);
        validate_digest_field(entry.manifest_sha256, "manifest_sha256", result.diagnostics);
    }
    if (auto digest = required_string(root, "public_api_sha256", "", result.diagnostics);
        digest.has_value()) {
        entry.public_api_sha256 = std::move(*digest);
        validate_digest_field(entry.public_api_sha256, "public_api_sha256", result.diagnostics);
    }
    entry.dependencies = parse_registry_dependencies(root, result.diagnostics);

    if (!result.has_errors()) {
        result.entry = std::move(entry);
    }
    return result;
}

[[nodiscard]] RegistryPackageIndexResult parse_registry_package_index(std::string_view input) {
    RegistryPackageIndexResult result;
    auto parsed = ahfl::json::parse_json(input);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return index_error(RegistryError::InvalidResponse,
                           {"registry package index must be valid JSON object"});
    }

    const auto &root = **parsed;
    std::vector<std::string> diagnostics;
    reject_unknown_fields(root, {"format_version", "package", "versions"}, "", diagnostics);

    RegistryPackageIndex index;
    if (auto format = required_string(root, "format_version", "", diagnostics);
        format.has_value()) {
        index.format_version = std::move(*format);
        if (index.format_version != "ahfl.registry.package_index.v1") {
            diagnostics.push_back("registry package index field 'format_version' must be "
                                  "'ahfl.registry.package_index.v1'");
        }
    }
    if (auto package = required_string(root, "package", "", diagnostics); package.has_value()) {
        index.package = std::move(*package);
        if (!is_valid_package_name(index.package)) {
            diagnostics.push_back("registry package index field 'package' must be kebab-case");
        }
    }

    const auto *versions = root.get("versions");
    if (versions == nullptr) {
        diagnostics.push_back("registry package index is missing required field 'versions'");
    } else if (!versions->is_array()) {
        diagnostics.push_back("registry package index field 'versions' must be an array");
    } else {
        index.versions.reserve(versions->array_items.size());
        for (std::size_t i = 0; i < versions->array_items.size(); ++i) {
            const auto &version = versions->array_items[i];
            if (!version->is_object()) {
                diagnostics.push_back("registry package index versions[" + std::to_string(i) +
                                      "] must be an object");
                continue;
            }
            auto entry = parse_registry_index_entry(ahfl::json::serialize_json(*version));
            if (entry.has_errors() || !entry.entry.has_value()) {
                for (const auto &diagnostic : entry.diagnostics) {
                    diagnostics.push_back("versions[" + std::to_string(i) + "]: " + diagnostic);
                }
                continue;
            }
            if (!index.package.empty() && entry.entry->package != index.package) {
                diagnostics.push_back("versions[" + std::to_string(i) + "] package '" +
                                      entry.entry->package + "' does not match package index '" +
                                      index.package + "'");
                continue;
            }
            index.versions.push_back(std::move(*entry.entry));
        }
    }

    if (index.versions.empty()) {
        diagnostics.push_back("registry package index must contain at least one version");
    }
    if (!diagnostics.empty()) {
        return index_error(RegistryError::InvalidResponse, std::move(diagnostics));
    }
    result.index = std::move(index);
    return result;
}

std::string serialize_registry_index_entry(const RegistryIndexEntry &entry) {
    auto root = ahfl::json::JsonValue::make_object();
    root->set("format_version", ahfl::json::JsonValue::make_string(entry.format_version));
    root->set("registry_id", ahfl::json::JsonValue::make_string(entry.registry_id));
    root->set("package", ahfl::json::JsonValue::make_string(entry.package));
    root->set("version", ahfl::json::JsonValue::make_string(entry.version));
    root->set("yanked", ahfl::json::JsonValue::make_bool(entry.yanked));
    root->set("source_archive_sha256",
              ahfl::json::JsonValue::make_string(entry.source_archive_sha256));
    root->set("manifest_sha256", ahfl::json::JsonValue::make_string(entry.manifest_sha256));
    root->set("public_api_sha256", ahfl::json::JsonValue::make_string(entry.public_api_sha256));

    auto dependencies = ahfl::json::JsonValue::make_array();
    for (const auto &dependency : entry.dependencies) {
        auto item = ahfl::json::JsonValue::make_object();
        item->set("name", ahfl::json::JsonValue::make_string(dependency.name));
        item->set("source", ahfl::json::JsonValue::make_string(dependency.source));
        if (dependency.registry.has_value()) {
            item->set("registry", ahfl::json::JsonValue::make_string(*dependency.registry));
        }
        if (dependency.version_requirement.has_value()) {
            item->set("version",
                      ahfl::json::JsonValue::make_string(*dependency.version_requirement));
        }
        dependencies->push(std::move(item));
    }
    root->set("dependencies", std::move(dependencies));
    return ahfl::json::serialize_json(*root);
}

RegistryCandidateSelection
select_registry_candidate(std::string_view package_name,
                          const std::vector<RegistryIndexEntry> &candidates,
                          std::string_view version_requirement,
                          std::optional<std::string_view> locked_version) {
    if (!is_valid_package_name(package_name)) {
        return RegistryCandidateSelection{.error_message =
                                              "registry package name must be kebab-case"};
    }
    if (!is_version_requirement(version_requirement)) {
        return RegistryCandidateSelection{
            .error_message =
                "registry version requirement must be exact, caret, or tilde semantic version"};
    }

    const RegistryIndexEntry *best = nullptr;
    for (const auto &candidate : candidates) {
        if (candidate.package != package_name) {
            continue;
        }
        if (!Resolver::satisfies(Resolver::parse_version(candidate.version),
                                 std::string{version_requirement})) {
            continue;
        }
        if (locked_version.has_value()) {
            if (candidate.version == *locked_version) {
                return RegistryCandidateSelection{.entry = candidate};
            }
            continue;
        }
        if (candidate.yanked) {
            continue;
        }
        if (best == nullptr || Resolver::compare(Resolver::parse_version(candidate.version),
                                                 Resolver::parse_version(best->version)) > 0) {
            best = &candidate;
        }
    }

    if (best != nullptr) {
        return RegistryCandidateSelection{.entry = *best};
    }
    if (locked_version.has_value()) {
        return RegistryCandidateSelection{
            .error_message = "locked registry version '" + std::string{*locked_version} +
                             "' is not available for package '" + std::string{package_name} + "'"};
    }
    return RegistryCandidateSelection{.error_message =
                                          "no registry version satisfies requirement '" +
                                          std::string{version_requirement} + "' for package '" +
                                          std::string{package_name} + "'"};
}

Registry::Registry(std::string base_url)
    : Registry(default_registry_base_url(std::move(base_url)),
               std::make_shared<HttpRegistryTransport>(),
               std::make_shared<FilesystemRegistryCache>()) {}

Registry::Registry(std::string base_url,
                   std::shared_ptr<RegistryTransport> transport,
                   std::shared_ptr<RegistryCache> cache)
    : base_url_(std::move(base_url)), transport_(std::move(transport)), cache_(std::move(cache)) {
    if (transport_ == nullptr) {
        transport_ = std::make_shared<HttpRegistryTransport>();
    }
    if (cache_ == nullptr) {
        cache_ = std::make_shared<FilesystemRegistryCache>();
    }
}

RegistryResult Registry::fetch_metadata(const std::string &package_name) const {
    auto url = base_url_ + "/packages/" + package_name;
    auto http_result = transport_->get(url, 10);

    bool invalid_registry_response = false;
    if (http_result.success && !http_result.body.empty()) {
        auto metadata = parse_metadata_json(http_result.body, package_name);
        if (metadata.has_value()) {
            cache_->save(package_name, http_result.body);
            return RegistryResult{true, std::move(*metadata), std::nullopt, ""};
        }
        invalid_registry_response = true;
    } else if (http_result.success) {
        invalid_registry_response = true;
    }

    if (invalid_registry_response) {
        return RegistryResult{false,
                              std::nullopt,
                              RegistryError::InvalidResponse,
                              "invalid registry response for package: " + package_name};
    }

    if (should_try_cache_after_registry_response(http_result)) {
        auto cached_json = cache_->load(package_name);
        if (cached_json.has_value() && !cached_json->empty()) {
            auto metadata = parse_metadata_json(*cached_json, package_name);
            if (metadata.has_value()) {
                return RegistryResult{true, std::move(*metadata), std::nullopt, ""};
            }
        }
    } else if (http_result.status_code == 404) {
        return RegistryResult{
            false, std::nullopt, RegistryError::NotFound, "package not found: " + package_name};
    }
    return RegistryResult{false,
                          std::nullopt,
                          RegistryError::NetworkError,
                          "package metadata unavailable: " + package_name};
}

RegistryResult Registry::fetch_version(const std::string &package_name,
                                       const std::string &version) const {
    auto url = base_url_ + "/packages/" + package_name + "/versions/" + version;
    auto http_result = transport_->get(url, 10);

    bool invalid_registry_response = false;
    const bool direct_version_not_found = http_result.status_code == 404;
    if (http_result.success && !http_result.body.empty()) {
        PackageMetadata metadata;
        metadata.name = package_name;
        metadata.latest_version = version;

        auto parsed_version = parse_single_version_json(http_result.body, package_name, version);
        if (parsed_version.has_value()) {
            metadata.versions.push_back(std::move(*parsed_version));
            return RegistryResult{true, std::move(metadata), std::nullopt, ""};
        }
        invalid_registry_response = true;
    } else if (http_result.success) {
        invalid_registry_response = true;
    }

    if (invalid_registry_response) {
        return RegistryResult{false,
                              std::nullopt,
                              RegistryError::InvalidResponse,
                              "invalid registry response for package version: " + package_name +
                                  "@" + version};
    }

    auto full = fetch_metadata(package_name);
    if (full.success && full.metadata.has_value()) {
        for (auto &v : full.metadata->versions) {
            if (v.version == version) {
                PackageMetadata single;
                single.name = package_name;
                single.latest_version = version;
                single.versions.push_back(std::move(v));
                return RegistryResult{true, std::move(single), std::nullopt, ""};
            }
        }
    }

    if (!full.success && !direct_version_not_found && full.error.has_value()) {
        return RegistryResult{false,
                              std::nullopt,
                              full.error,
                              "package version unavailable: " + package_name + "@" + version};
    }
    return RegistryResult{false,
                          std::nullopt,
                          RegistryError::NotFound,
                          "version " + version + " not found for " + package_name};
}

RegistryPackageIndexResult Registry::fetch_package_index(const std::string &package_name) const {
    const auto url = base_url_ + "/packages/" + package_name + "/registry-index";
    const auto cache_key = registry_package_index_cache_key(package_name);
    auto index_fetch =
        fetch_registry_resource(*transport_, *cache_, url, cache_key, "registry package index");
    if (!index_fetch.body.has_value()) {
        return index_error(*index_fetch.error, {std::move(index_fetch.message)});
    }

    auto parsed_index = parse_registry_package_index(*index_fetch.body);
    if (!parsed_index.success() || !parsed_index.index.has_value()) {
        return parsed_index;
    }
    if (parsed_index.index->package != package_name) {
        return index_error(RegistryError::InvalidResponse,
                           {"registry package index package '" + parsed_index.index->package +
                            "' does not match requested package '" + package_name + "'"});
    }

    cache_->save(cache_key, *index_fetch.body);
    return parsed_index;
}

RegistryPackageArtifactResult Registry::fetch_package_artifacts(const std::string &package_name,
                                                                const std::string &version) const {
    const auto base = version_base_url(base_url_, package_name, version);
    const auto index_url = base + "/registry-index";
    const auto archive_manifest_url = base + "/source-archive";
    const auto archive_payload_url = base + "/source-archive.payload";
    const auto index_key = registry_index_cache_key(package_name, version);

    auto index_fetch =
        fetch_registry_resource(*transport_, *cache_, index_url, index_key, "registry index");
    if (!index_fetch.body.has_value()) {
        return artifact_error(*index_fetch.error, {std::move(index_fetch.message)});
    }

    auto parsed_index = parse_registry_index_entry(*index_fetch.body);
    if (parsed_index.has_errors() || !parsed_index.entry.has_value()) {
        return artifact_error(RegistryError::InvalidResponse, std::move(parsed_index.diagnostics));
    }
    if (parsed_index.entry->package != package_name) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"registry index package '" + parsed_index.entry->package +
                               "' does not match requested package '" + package_name + "'"});
    }
    if (parsed_index.entry->version != version) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"registry index version '" + parsed_index.entry->version +
                               "' does not match requested version '" + version + "'"});
    }

    const auto archive_manifest_key =
        source_archive_manifest_cache_key(parsed_index.entry->source_archive_sha256);
    auto archive_manifest_fetch = fetch_registry_resource(*transport_,
                                                          *cache_,
                                                          archive_manifest_url,
                                                          archive_manifest_key,
                                                          "source archive manifest");
    if (!archive_manifest_fetch.body.has_value()) {
        return artifact_error(*archive_manifest_fetch.error,
                              {std::move(archive_manifest_fetch.message)});
    }

    auto parsed_archive = parse_source_archive_manifest(*archive_manifest_fetch.body);
    if (parsed_archive.has_errors() || !parsed_archive.archive.has_value()) {
        return artifact_error(RegistryError::InvalidResponse,
                              std::move(parsed_archive.diagnostics));
    }
    if (parsed_archive.archive->package_name != package_name) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"source archive package '" + parsed_archive.archive->package_name +
                               "' does not match requested package '" + package_name + "'"});
    }
    if (parsed_archive.archive->archive_sha256 != parsed_index.entry->source_archive_sha256) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"source archive digest does not match registry index digest"});
    }
    if (parsed_archive.archive->manifest_sha256 != parsed_index.entry->manifest_sha256) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"source archive manifest digest does not match registry index"});
    }

    const auto archive_payload_key =
        source_archive_payload_cache_key(parsed_index.entry->source_archive_sha256);
    auto archive_payload_fetch = fetch_registry_resource(
        *transport_, *cache_, archive_payload_url, archive_payload_key, "source archive payload");
    if (!archive_payload_fetch.body.has_value()) {
        return artifact_error(*archive_payload_fetch.error,
                              {std::move(archive_payload_fetch.message)});
    }

    const auto payload_digest = "sha256:" + ahfl::support::sha256_hex(*archive_payload_fetch.body);
    if (payload_digest != parsed_archive.archive->archive_sha256) {
        return artifact_error(RegistryError::InvalidResponse,
                              {"source archive payload digest does not match archive_sha256"});
    }

    parsed_archive.archive->payload = *archive_payload_fetch.body;
    cache_->save(index_key, *index_fetch.body);
    cache_->save(archive_manifest_key, *archive_manifest_fetch.body);
    cache_->save(archive_payload_key, *archive_payload_fetch.body);

    return RegistryPackageArtifactResult{
        .artifacts =
            RegistryPackageArtifacts{
                .registry = std::move(*parsed_index.entry),
                .archive = std::move(*parsed_archive.archive),
                .payload = std::move(*archive_payload_fetch.body),
            },
    };
}

RegistryPublicApiSnapshotResult
Registry::fetch_public_api_snapshot(const std::string &package_name,
                                    const std::string &version,
                                    const std::string &public_api_sha256) const {
    const auto url = version_base_url(base_url_, package_name, version) + "/public-api";
    const auto cache_key = public_api_snapshot_cache_key(public_api_sha256);
    auto snapshot_fetch =
        fetch_registry_resource(*transport_, *cache_, url, cache_key, "public API snapshot");
    if (!snapshot_fetch.body.has_value()) {
        return public_api_snapshot_error(*snapshot_fetch.error,
                                         {std::move(snapshot_fetch.message)});
    }

    auto diagnostics = validate_public_api_snapshot(*snapshot_fetch.body, public_api_sha256);
    if (!diagnostics.empty()) {
        return public_api_snapshot_error(RegistryError::InvalidResponse, std::move(diagnostics));
    }

    cache_->save(cache_key, *snapshot_fetch.body);
    return RegistryPublicApiSnapshotResult{
        .snapshot =
            RegistryPublicApiSnapshot{
                .snapshot = std::move(*snapshot_fetch.body),
                .public_api_sha256 = public_api_sha256,
        },
    };
}

RegistryMutationResult
Registry::publish_package(const RegistryPublishPackageRequest &request) const {
    auto diagnostics = validate_publish_request(request);
    std::string body = serialize_publish_request_envelope(request, diagnostics);
    if (!diagnostics.empty()) {
        return mutation_error(RegistryError::InvalidResponse, std::move(diagnostics));
    }

    const auto url =
        version_base_url(base_url_, request.registry.package, request.registry.version) + "/publish";
    auto response = transport_->request("PUT", url, body, 30);
    auto result = parse_mutation_registry_response(response, "publish");
    if (!result.success() || !result.entry.has_value()) {
        return result;
    }

    const auto &entry = *result.entry;
    std::vector<std::string> response_diagnostics;
    if (entry.registry_id != request.registry.registry_id) {
        response_diagnostics.push_back("published registry id does not match request");
    }
    if (entry.package != request.registry.package) {
        response_diagnostics.push_back("published package does not match request");
    }
    if (entry.version != request.registry.version) {
        response_diagnostics.push_back("published version does not match request");
    }
    if (entry.yanked) {
        response_diagnostics.push_back("published package version must not be returned as yanked");
    }
    if (entry.source_archive_sha256 != request.registry.source_archive_sha256) {
        response_diagnostics.push_back("published source archive digest does not match request");
    }
    if (entry.manifest_sha256 != request.registry.manifest_sha256) {
        response_diagnostics.push_back("published manifest digest does not match request");
    }
    if (entry.public_api_sha256 != request.registry.public_api_sha256) {
        response_diagnostics.push_back("published public API digest does not match request");
    }
    if (!same_dependencies(entry.dependencies, request.registry.dependencies)) {
        response_diagnostics.push_back("published dependency metadata does not match request");
    }
    if (!response_diagnostics.empty()) {
        return mutation_error(RegistryError::InvalidResponse, std::move(response_diagnostics));
    }
    return result;
}

RegistryMutationResult Registry::yank_package(const std::string &package_name,
                                              const std::string &version,
                                              const std::string &registry_id,
                                              const std::string &reason) const {
    const auto body = serialize_yank_request_envelope(package_name, version, registry_id, reason);
    const auto url = version_base_url(base_url_, package_name, version) + "/yank";
    auto response = transport_->request("POST", url, body, 30);
    auto result = parse_mutation_registry_response(response, "yank");
    if (!result.success() || !result.entry.has_value()) {
        return result;
    }

    const auto &entry = *result.entry;
    std::vector<std::string> diagnostics;
    if (entry.registry_id != registry_id) {
        diagnostics.push_back("yanked registry id does not match request");
    }
    if (entry.package != package_name) {
        diagnostics.push_back("yanked package does not match request");
    }
    if (entry.version != version) {
        diagnostics.push_back("yanked version does not match request");
    }
    if (!entry.yanked) {
        diagnostics.push_back("registry yank response must return yanked=true");
    }
    if (!diagnostics.empty()) {
        return mutation_error(RegistryError::InvalidResponse, std::move(diagnostics));
    }
    return result;
}

std::vector<std::string> Registry::search(const std::string &query) const {
    auto url = base_url_ + "/search?q=" + percent_encode_query_value(query);
    auto http_result = transport_->get(url, 10);

    if (http_result.success && !http_result.body.empty()) {
        return parse_search_json(http_result.body);
    }
    return {};
}

const std::string &Registry::base_url() const {
    return base_url_;
}

} // namespace ahfl::package
