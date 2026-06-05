#include "tooling/package/registry.hpp"

#include "base/json/json_value.hpp"
#include "base/support/http.hpp"

#include <algorithm>
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

fs::path cache_file_path(const std::string &package_name) {
    auto dir = cache_directory();
    return dir / (package_name + ".json");
}

class HttpRegistryTransport final : public RegistryTransport {
  public:
    [[nodiscard]] RegistryHttpResponse get(std::string_view url, int timeout_seconds) override {
        ahfl::support::HttpRequest request;
        request.method = "GET";
        request.url = std::string(url);
        request.timeout_seconds = timeout_seconds;

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
    void save(std::string_view package_name, std::string_view json_data) override {
        auto dir = cache_directory();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            return;
        }

        std::ofstream ofs(cache_file_path(std::string(package_name)));
        if (ofs) {
            ofs << json_data;
        }
    }

    [[nodiscard]] std::optional<std::string> load(std::string_view package_name) override {
        auto path = cache_file_path(std::string(package_name));
        std::ifstream ifs(path);
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
    return parse_version_object(**parsed, package_name, &version);
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

Registry::Registry(std::string base_url)
    : Registry(std::move(base_url),
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

    auto cached_json = cache_->load(package_name);
    if (cached_json.has_value() && !cached_json->empty()) {
        auto metadata = parse_metadata_json(*cached_json, package_name);
        if (metadata.has_value()) {
            return RegistryResult{true, std::move(*metadata), std::nullopt, ""};
        }
    }

    if (http_result.status_code == 404) {
        return RegistryResult{
            false, std::nullopt, RegistryError::NotFound, "package not found: " + package_name};
    }
    if (invalid_registry_response) {
        return RegistryResult{false,
                              std::nullopt,
                              RegistryError::InvalidResponse,
                              "invalid registry response for package: " + package_name};
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
    if (invalid_registry_response) {
        return RegistryResult{false,
                              std::nullopt,
                              RegistryError::InvalidResponse,
                              "invalid registry response for package version: " + package_name +
                                  "@" + version};
    }
    return RegistryResult{false,
                          std::nullopt,
                          RegistryError::NotFound,
                          "version " + version + " not found for " + package_name};
}

std::vector<std::string> Registry::search(const std::string &query) const {
    auto url = base_url_ + "/search?q=" + query;
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
